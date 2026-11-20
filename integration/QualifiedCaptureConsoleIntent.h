// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "ApprovedNativeV8Projection.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// IO-3B native CONTROL-plane decoder for the *existing* approved console
// requestId/epoch/revision ABI. No audio device opens, allocations in audio,
// source preview ownership, optimistic ACK, secondary renderer or JS DSP.
// Only the message-thread device coordinator may use the validated proposal.
namespace armix::console {
struct QualifiedCaptureSourceIntent final {
    std::uint64_t channelId=0;
    bool secondary=false,stereo=false;
    int left=-1,right=-1; // PHYSICAL indices, not JUCE callback ordinals
};
struct QualifiedCaptureConsoleIntent final {
    juce::String requestId,secondaryDriver;
    juce::BigInteger secondaryPins;
    double captureRate=0.;
    int capturePeriod=0;
    std::uint64_t deviceEpoch=0,graphRevision=0,rosterRevision=0;
    std::uint64_t targetChannelId=0; // exact approved v8 INPUT target
    std::vector<QualifiedCaptureSourceIntent> sources;
};
struct QualifiedCaptureConsoleDecode final {
    QualifiedCaptureConsoleIntent command;
    juce::String error;
    bool ok() const noexcept {return error.isEmpty();}
};

inline bool qualifiedCaptureDecimal(const juce::var& value,
                                    std::uint64_t& result) noexcept {
    if(!nativeV8Decimal(value))return false;
    const std::string s=value.toString().toStdString();
    std::uint64_t parsed=0;
    const auto [end,code]=std::from_chars(s.data(),s.data()+s.size(),parsed);
    if(code!=std::errc{}||end!=s.data()+s.size()||!parsed)return false;
    result=parsed;
    return true;
}
inline bool qualifiedCaptureInt(const juce::var& value,int lo,int hi,
                                int& result) noexcept {
    if(!value.isInt())return false;
    const int v=static_cast<int>(value);
    if(v<lo||v>hi)return false;
    result=v;
    return true;
}
inline bool qualifiedCaptureHasPin(const juce::var& pins,int pin) noexcept {
    if(const auto* a=pins.getArray())
        for(const auto& item:*a)
            if(item.isInt()&&static_cast<int>(item)==pin)return true;
    return false;
}

// Pure, side-effect-free parse: exact installed graph is still checked again
// by the native engine immediately before any B driver open or publication.
inline QualifiedCaptureConsoleDecode decodeQualifiedCaptureConsoleIntent(
    const juce::var& request,const juce::var& nativeSnapshot,
    const juce::var& installedBus) {
    const auto reject=[](const char* reason) {
        QualifiedCaptureConsoleDecode failed;
        failed.error=reason;
        return failed;
    };
    if(!request.isObject()||!nativeSnapshot.isObject()||
       !installedBus.isObject()||
       request.getProperty("operation","").toString()!="INSTALL_SECONDARY_CAPTURE")
        return reject("INVALID_SECONDARY_CAPTURE_INTENT");
    const auto device=nativeSnapshot.getProperty("device",{});
    const auto graph=nativeSnapshot.getProperty("graph",{});
    const auto caps=graph.getProperty("capabilities",{});
    const auto payload=request.getProperty("payload",{});
    const auto graphRows=graph.getProperty("channels",{});
    const auto busRows=installedBus.getProperty("channels",{});
    const auto requestId=request.getProperty("requestId",{});
    const auto epochRaw=request.getProperty("deviceEpoch",{});
    const auto revisionRaw=request.getProperty("baseGraphRevision",{});
    const auto rosterRaw=request.getProperty("baseRosterRevision",{});
    if(!device.isObject()||!graph.isObject()||!caps.isObject()||
       !payload.isObject()||!graphRows.isArray()||!busRows.isArray()||
       !static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",false))||
       device.getProperty("backend","").toString()!="ASIO"||
       !requestId.isString()||requestId.toString().isEmpty()||
       requestId.toString().length()>128||
       payload.getProperty("confirmed",{})!=juce::var(true))
        return reject("SECONDARY_CAPTURE_OPERATOR_OR_GRAPH_NOT_ADMITTED");
    // An arbitrary WebView event may not bypass the capability that the
    // installed native callback actually advertised for this exact epoch.
    // The UI's own supportedOps check is necessary but never sufficient.
    const auto offered=caps.getProperty("supportedOps",{});
    if(!offered.isArray()||
       !offered.getArray()->contains(juce::String("INSTALL_SECONDARY_CAPTURE")))
        return reject("SECONDARY_CAPTURE_CAPABILITY_UNAVAILABLE");
    if(!nativeV8Decimal(epochRaw)||!nativeV8Decimal(revisionRaw)||
       !nativeV8Decimal(rosterRaw)||
       epochRaw.toString()!=device.getProperty("epoch",{}).toString()||
       revisionRaw.toString()!=graph.getProperty("revision",{}).toString()||
       rosterRaw.toString()!=graph.getProperty("rosterRevision",{}).toString()||
       revisionRaw.toString()!=installedBus.getProperty("graphRevision",{}).toString()||
       rosterRaw.toString()!=installedBus.getProperty("rosterRevision",{}).toString())
        return reject("STALE_SECONDARY_CAPTURE_GRAPH");
    QualifiedCaptureConsoleIntent command;
    if(!qualifiedCaptureDecimal(epochRaw,command.deviceEpoch)||
       !qualifiedCaptureDecimal(revisionRaw,command.graphRevision)||
       !qualifiedCaptureDecimal(rosterRaw,command.rosterRevision)||
       !qualifiedCaptureDecimal(request.getProperty("targetChannelId",{}),
                                command.targetChannelId))
        return reject("INVALID_SECONDARY_CAPTURE_REVISION");
    command.requestId=requestId.toString();
    const auto driver=payload.getProperty("secondaryDriver",{});
    if(!driver.isString()||driver.toString().isEmpty()||
       driver.toString().length()>128||
       driver.toString().containsAnyOf("\r\n"))
        return reject("INVALID_SECONDARY_DRIVER");
    command.secondaryDriver=driver.toString();
    const auto rate=payload.getProperty("captureRate",{});
    if(!nativeV8Number(rate,command.captureRate)||
       command.captureRate<8000.||command.captureRate>192000.||
       !qualifiedCaptureInt(payload.getProperty("capturePeriod",{}),
                            32,8192,command.capturePeriod))
        return reject("INVALID_SECONDARY_CAPTURE_FORMAT");
    const auto physical=payload.getProperty("secondaryInputPins",{});
    const auto* physicalPins=physical.getArray();
    if(!physicalPins||physicalPins->isEmpty()||physicalPins->size()>16)
        return reject("INVALID_SECONDARY_CAPTURE_PINS");
    int previous=-1;
    for(const auto& raw:*physicalPins) {
        int index=-1;
        if(!qualifiedCaptureInt(raw,0,63,index)||index<=previous)
            return reject("DUPLICATE_OR_UNSORTED_SECONDARY_CAPTURE_PIN");
        command.secondaryPins.setBit(index);
        previous=index;
    }
    const auto rows=payload.getProperty("sources",{});
    const auto* sourceRows=rows.getArray();
    if(!sourceRows||sourceRows->isEmpty()||sourceRows->size()>64)
        return reject("SECONDARY_CAPTURE_SOURCE_COUNT_INVALID");
    const auto primaryPins=device.getProperty("enabledInputs",{});
    if(!primaryPins.isArray()||!primaryPins.getArray()||
       primaryPins.getArray()->isEmpty())
        return reject("PRIMARY_CAPTURE_PINS_UNVERIFIED");
    bool usesSecondary=false;
    for(const auto& row:*sourceRows) {
        if(!row.isObject())return reject("INVALID_SECONDARY_SOURCE_ROW");
        QualifiedCaptureSourceIntent source;
        if(!qualifiedCaptureDecimal(row.getProperty("channelId",{}),
                                    source.channelId))
            return reject("INVALID_SECONDARY_SOURCE_CHANNEL_ID");
        const auto role=row.getProperty("deviceRole",{});
        const auto stereo=row.getProperty("stereo",{});
        if(!role.isString()||
           (role.toString()!="PRIMARY"&&role.toString()!="SECONDARY")||
           !stereo.isBool())
            return reject("INVALID_SECONDARY_SOURCE_ROLE");
        source.secondary=role.toString()=="SECONDARY";
        source.stereo=static_cast<bool>(stereo);
        if(!qualifiedCaptureInt(row.getProperty("leftPhysical",{}),0,63,
                                source.left))
            return reject("INVALID_SECONDARY_SOURCE_LEFT");
        const auto right=row.getProperty("rightPhysical",{});
        if(source.stereo) {
            if(!qualifiedCaptureInt(right,0,63,source.right)||
               source.left==source.right)
                return reject("INVALID_SECONDARY_SOURCE_STEREO");
        }else if(!qualifiedCaptureInt(right,-1,-1,source.right))
            return reject("INVALID_SECONDARY_SOURCE_MONO");
        const auto permitted=[&](int pin) {
            return source.secondary?command.secondaryPins[pin]:
                   qualifiedCaptureHasPin(primaryPins,pin);
        };
        if(!permitted(source.left)||
           (source.stereo&&!permitted(source.right)))
            return reject("SECONDARY_SOURCE_PIN_NOT_VERIFIED");
        bool input=false,installedInput=false;
        for(const auto& installed:*graphRows.getArray()) {
            if(installed.getProperty("id","").toString()==
                   row.getProperty("channelId",{}).toString() &&
               installed.getProperty("role","").toString()=="INPUT")
                input=true;
        }
        for(const auto& native:*busRows.getArray()) {
            if(native.getProperty("id","").toString()==
                   row.getProperty("channelId",{}).toString() &&
               native.getProperty("role","").toString()=="INPUT")
                installedInput=true;
        }
        if(!input||!installedInput)
            return reject("SECONDARY_CAPTURE_CHANNEL_NOT_INSTALLED");
        for(const auto& prior:command.sources)
            if(prior.channelId==source.channelId)
                return reject("DUPLICATE_SECONDARY_SOURCE_CHANNEL");
        usesSecondary=usesSecondary||source.secondary;
        command.sources.push_back(source);
    }
    if(!usesSecondary)return reject("NO_SECONDARY_SOURCE_SELECTED");
    // The existing v8 controller's one ChannelId target must be the
    // actually selected B source. Do not allow a misleading main/primary
    // target to acquire ACK for a different channel's physical binding.
    bool targetIsSecondary=false;
    for(const auto& source:command.sources)
        if(source.channelId==command.targetChannelId && source.secondary)
            targetIsSecondary=true;
    if(!targetIsSecondary)return reject("SECONDARY_CAPTURE_TARGET_MISMATCH");
    return {std::move(command),{}};
}
} // namespace armix::console
