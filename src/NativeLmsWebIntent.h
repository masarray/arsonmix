// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Browser -> CONTROL only. Translate a bounded offline speaker DESIGN,
// never device identity, PCM ownership, hardware arm, or protection authority.
#include "../engine/lms/NativeSpeakerDeviceBinder.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace armix::lms::web {
struct OfflineIntent final {
    std::string requestId;
    std::uint64_t expectedDesignRevision=0;
    SpeakerSystemRequest proposed;
};
inline bool uint64Token(const juce::var& token,std::uint64_t& result,
                        bool allowZero=true) noexcept {
    if(!token.isString())return false;
    const auto str=token.toString();
    if(str.isEmpty()||str.length()>20||(str.length()>1&&str[0]=='0'))
        return false;
    std::uint64_t value=0;
    for(int i=0;i<str.length();++i){
        const auto ch=str[i];
        if(ch<'0'||ch>'9')return false;
        const auto digit=static_cast<std::uint64_t>(ch-'0');
        if(value>(std::numeric_limits<std::uint64_t>::max()-digit)/10)
            return false;
        value=value*10+digit;
    }
    if(!allowZero&&!value)return false;
    result=value;
    return true;
}
inline bool finiteNumber(const juce::var& value,double& out) noexcept {
    if(!value.isInt() && !value.isInt64() && !value.isDouble())return false;
    out=static_cast<double>(value);
    return std::isfinite(out);
}
inline bool integerNumber(const juce::var& value,int& out) noexcept {
    double amount=0.;
    if(!finiteNumber(value,amount)||
       amount<std::numeric_limits<int>::min() ||
       amount>std::numeric_limits<int>::max() ||
       std::trunc(amount)!=amount)return false;
    out=static_cast<int>(amount);return true;
}
inline bool boolean(const juce::var& source,bool& value) noexcept {
    if(!source.isBool())return false;
    value=static_cast<bool>(source);return true;
}
inline bool decode(const juce::var& args,OfflineIntent& out) {
    if(!args.isObject() ||
       args.getProperty("schema","").toString()!="arsonmix.lms.native-design-intent.v1")
        return false;
    const auto requestId=args.getProperty("requestId",{});
    if(!requestId.isString() || requestId.toString().isEmpty() ||
       requestId.toString().length()>120)return false;
    // Log-safe fixed-size request token (no whitespace, markup or control).
    for(const auto c:requestId.toString())
        if(c<0x21||c>0x7e)return false;
    OfflineIntent candidate;
    candidate.requestId=requestId.toString().toStdString();
    if(!uint64Token(args.getProperty("deviceEpoch",{}),
                    candidate.proposed.deviceEpoch,false) ||
       !uint64Token(args.getProperty("graphRevision",{}),
                    candidate.proposed.graphRevision,false) ||
       !uint64Token(args.getProperty("expectedDesignRevision",{}),
                    candidate.expectedDesignRevision,true))
        return false;
    const auto p=args.getProperty("design",{});
    if(!p.isObject()||p.getProperty("schema","").toString()!="arsonmix.lms.draft.v1" ||
       p.getProperty("applied",{})!=juce::var(false) ||
       !integerNumber(p.getProperty("ways",{}),candidate.proposed.ways) ||
       candidate.proposed.ways<1||candidate.proposed.ways>16 ||
       !boolean(p.getProperty("stereo",{}),candidate.proposed.stereo) ||
       !finiteNumber(args.getProperty("sampleRate",{}),
                     candidate.proposed.sampleRate) ||
       !integerNumber(args.getProperty("maxFrames",{}),
                      candidate.proposed.maxFrames) ||
       !integerNumber(args.getProperty("activeOutputChannels",{}),
                      candidate.proposed.activeOutputChannels))
        return false;
    if(candidate.proposed.maxFrames<1||candidate.proposed.maxFrames>8192 ||
       candidate.proposed.activeOutputChannels<2||
       candidate.proposed.activeOutputChannels>256)return false;
    const auto cutoffVar=p.getProperty("cutoffsHz",{});
    const auto* cutoffs=cutoffVar.getArray();
    if(!cutoffs || cutoffs->size()!=candidate.proposed.ways-1)return false;
    for(const auto& c:*cutoffs){
        double value=0.;
        if(!finiteNumber(c,value))return false;
        candidate.proposed.crossoverHz.push_back(value);
    }
    const auto outputs=p.getProperty("outputs",{});
    const auto* rows=outputs.getArray();
    if(!rows||rows->size()>64)return false;
    std::unordered_set<std::string> seenIds,seenPorts;
    for(const auto& raw:*rows) {
        if(!raw.isObject())return false;
        OutputPatch row;
        const auto id=raw.getProperty("outputId",{});
        const auto pin=raw.getProperty("portIdentity",{});
        if(!id.isString()||!pin.isString()||
           id.toString().isEmpty()||pin.toString().isEmpty()||
           id.toString().length()>128||pin.toString().length()>1024)
            return false;
        row.outputId=id.toString().toStdString();
        row.portIdentity=pin.toString().toStdString();
        if(!seenIds.insert(row.outputId).second||
           !seenPorts.insert(row.portIdentity).second)return false;
        if(!integerNumber(raw.getProperty("sourceSide",{}),row.sourceSide) ||
           !integerNumber(raw.getProperty("section",{}),row.section) ||
           !finiteNumber(raw.getProperty("gainDb",{}),row.gainDb) ||
           !finiteNumber(raw.getProperty("delayMs",{}),row.delayMs) ||
           !boolean(raw.getProperty("muted",{}),row.mute) ||
           !boolean(raw.getProperty("polarityInverted",{}),row.polarityInverted))
            return false;
        // These UI booleans are inert design observations only. They are
        // NEVER interpreted as calibrated speaker protection authorization.
        bool intendsLimiter=false;
        if(!boolean(raw.getProperty("limiterEnabled",{}),intendsLimiter))
            return false;
        row.limiterEnabled=intendsLimiter;
        row.confirmedSpeakerProfile=false;
        row.ceilingDbfs=-6.; // calibration NOT supplied by the browser
        const auto eq=raw.getProperty("peq",{});
        const auto* bands=eq.getArray();
        if(!bands||bands->size()>8)return false;
        for(const auto& v:*bands){
            if(!v.isObject())return false;
            ParametricBand band;
            if(!finiteNumber(v.getProperty("frequencyHz",{}),band.frequencyHz) ||
               !finiteNumber(v.getProperty("q",{}),band.q) ||
               !finiteNumber(v.getProperty("gainDb",{}),band.gainDb) ||
               !boolean(v.getProperty("enabled",{}),band.enabled))
                return false;
            row.peq.push_back(band);
        }
        candidate.proposed.outputs.push_back(std::move(row));
    }
    out=std::move(candidate);
    return true;
}
inline juce::var inspection(const TrustedSpeakerDeviceView& device,
                             const armix::mixer::DynamicNativeSessionPublisher::
                             SpeakerDraftReadback* readback) {
    auto root=std::make_unique<juce::DynamicObject>();
    root->setProperty("schema","arsonmix.lms.native-inspection.v1");
    root->setProperty("ready",true);
    root->setProperty("deviceEpoch",juce::String(std::to_string(device.deviceEpoch)));
    root->setProperty("graphRevision",juce::String(std::to_string(device.graphRevision)));
    root->setProperty("mainChannelId",juce::String(std::to_string(device.mainChannelId)));
    root->setProperty("sampleRate",device.sampleRate);
    root->setProperty("maxFrames",device.maxFrames);
    root->setProperty("activeOutputChannels",device.callbackOutputs);
    root->setProperty("outputDisarmed",device.allOutputsDisarmed);
    root->setProperty("hardwareApplied",false);
    root->setProperty("designRevision",juce::String(std::to_string(
        readback?readback->designRevision:0)));
    juce::Array<juce::var> ports;
    for(const auto& pin:device.actualEnabledPorts) {
        auto row=std::make_unique<juce::DynamicObject>();
        row->setProperty("identity",juce::String::fromUTF8(pin.identity.c_str()));
        row->setProperty("callbackOrdinal",pin.callbackOrdinal);
        const bool occupied=pin.callbackOrdinal==device.mainL ||
            pin.callbackOrdinal==device.mainR ||
            pin.callbackOrdinal==device.monitorL ||
            pin.callbackOrdinal==device.monitorR ||
            std::find(device.otherOccupiedOrdinals.begin(),
                      device.otherOccupiedOrdinals.end(),
                      pin.callbackOrdinal)!=device.otherOccupiedOrdinals.end();
        row->setProperty("occupied",occupied);
        ports.add(juce::var(row.release()));
    }
    root->setProperty("ports",juce::var(ports));
    if(readback){
        auto design=std::make_unique<juce::DynamicObject>();
        design->setProperty("ways",readback->design.ways);
        design->setProperty("stereo",readback->design.stereo);
        juce::Array<juce::var> cuts;
        for(double hz:readback->design.crossoverHz)cuts.add(hz);
        design->setProperty("cutoffsHz",juce::var(cuts));
        juce::Array<juce::var> routes;
        for(const auto& p:readback->design.outputs){
            auto route=std::make_unique<juce::DynamicObject>();
            route->setProperty("outputId",juce::String::fromUTF8(p.outputId.c_str()));
            route->setProperty("portIdentity",juce::String::fromUTF8(p.portIdentity.c_str()));
            route->setProperty("sourceSide",p.sourceSide);
            route->setProperty("section",p.section);
            route->setProperty("muted",p.mute);
            routes.add(juce::var(route.release()));
        }
        design->setProperty("outputs",juce::var(routes));
        root->setProperty("validatedDesign",juce::var(design.release()));
    }
    return juce::var(root.release());
}
} // namespace armix::lms::web
