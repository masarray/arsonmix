// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ApprovedNativeV8Projection.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

// INTEGRATOR M3-B: bridge the approved /preview/ control ABI to the ONE
// native ChannelId PCM owner from ENGINE #266. Translation is CONTROL-thread
// only and has NO side effects. It does not install, drive or claim audio.
// Real JUCE nativeBus/callback admission and genuine mutating operation
// support must be independently established by the producer before use.
//
// The public UI submits SET_FADER/PAN/MUTE/SOLO; native PCM currently uses
// NATIVE_SET_CHANNEL_MIX / NATIVE_SET_CHANNEL_MUTE_SOLO / NATIVE_SET_BUS_MIX.
// This translator NEVER fabricates capability support or stable ChannelIds.
namespace armix::console {
struct ApprovedNativeControlIntent {
    juce::var translated;
    juce::String error;
    bool ok() const noexcept { return translated.isObject() && error.isEmpty(); }
};

inline bool approvedHasOperation(const juce::var& list,const char* name) {
    const auto* values=list.getArray();
    return values && values->contains(juce::var(juce::String(name)));
}

inline ApprovedNativeControlIntent translateApprovedNativeControl(
    const juce::var& publicIntent,
    const juce::var& acceptedSnapshot,
    const juce::var& installedBus) {
    const auto reject=[](const char* code)->ApprovedNativeControlIntent {
        return {{},juce::String(code)};
    };
    if(!publicIntent.isObject() || !acceptedSnapshot.isObject() ||
       !installedBus.isObject())return reject("NATIVE_READBACK_UNAVAILABLE");
    const auto device=acceptedSnapshot.getProperty("device",{});
    const auto graph=acceptedSnapshot.getProperty("graph",{});
    const auto caps=graph.getProperty("capabilities",{});
    const auto rows=graph.getProperty("channels",{});
    const auto nativeRows=installedBus.getProperty("channels",{});
    const auto& ids=publicIntent.getProperty("targetChannelId",{});
    const auto& request=publicIntent.getProperty("requestId",{});
    const auto op=publicIntent.getProperty("operation",{}).toString();
    const auto epoch=device.getProperty("epoch",{});
    const auto revision=graph.getProperty("revision",{});
    const auto roster=graph.getProperty("rosterRevision",{});
    if(!device.isObject() || !graph.isObject() || !caps.isObject() ||
       !rows.isArray() || !nativeRows.isArray() ||
       !static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",false)) ||
       !nativeV8Decimal(epoch) || epoch.toString()=="0" ||
       !nativeV8Decimal(revision) || revision.toString()=="0" ||
       !nativeV8Decimal(roster) || roster.toString()=="0" ||
       !nativeV8Decimal(ids) || ids.toString()=="0")
        return reject("CHANNELID_CALLBACK_NOT_ADMITTED");
    // Reject cross-device / cross-graph commands BEFORE any conversion.
    if(publicIntent.getProperty("deviceEpoch",{}).toString()!=epoch.toString() ||
       publicIntent.getProperty("baseGraphRevision",{}).toString()!=revision.toString() ||
       publicIntent.getProperty("baseRosterRevision",{}).toString()!=roster.toString() ||
       installedBus.getProperty("graphRevision",{}).toString()!=revision.toString() ||
       installedBus.getProperty("rosterRevision",{}).toString()!=roster.toString())
        return reject("STALE_NATIVE_CONTROL_REVISION");
    if(!request.isString() || request.toString().isEmpty() ||
       request.toString().length()>128)return reject("INVALID_REQUEST_ID");
    if(op!="SET_FADER" && op!="SET_PAN" && op!="SET_MUTE" &&
       op!="SET_SOLO")return reject("UNSUPPORTED_APPROVED_CONTROL");
    // Both native *and* GUI-side capabilities must be explicitly advertised
    // from real installed PCM. Native ops alone do not prove that legacy M4
    // ChannelIds (especially original CH1-10) support these controls.
    if(!approvedHasOperation(caps.getProperty("supportedOps",{}),
                             op.toRawUTF8()))
        return reject("PUBLIC_CONTROL_NOT_ADMITTED");
    juce::var channel,producer;
    for(const auto& item:*rows.getArray())
        if(item.getProperty("id","").toString()==ids.toString()) {
            if(channel.isObject())return reject("DUPLICATE_APPROVED_CHANNEL");
            channel=item;
        }
    for(const auto& item:*nativeRows.getArray())
        if(item.getProperty("id","").toString()==ids.toString()) {
            if(producer.isObject())return reject("DUPLICATE_NATIVE_CHANNEL");
            producer=item;
        }
    if(!channel.isObject() || !producer.isObject())
        return reject("UNKNOWN_OR_STALE_CHANNELID");
    const auto role=channel.getProperty("role","").toString();
    if(role!=producer.getProperty("role","").toString())
        return reject("NATIVE_ROLE_MISMATCH");
    const bool input=role=="INPUT";
    const bool bus=role=="GROUP" || role=="FX_RETURN";
    if(!input && !bus)return reject("CHANNEL_ROLE_NOT_MUTABLE");
    if(bus && (op=="SET_MUTE" || op=="SET_SOLO"))
        return reject("BUS_MUTE_SOLO_NOT_ADMITTED");
    const auto controls=channel.getProperty("controls",{});
    if(!controls.isObject())return reject("INVALID_NATIVE_CONTROLS");
    const auto fader=controls.getProperty("faderDb",{});
    const auto pan=controls.getProperty("pan",{});
    const auto mute=controls.getProperty("mute",{});
    const auto solo=controls.getProperty("solo",{});
    double acceptedDb=0.,acceptedPan=0.,nativeGain=0.,nativePan=0.;
    if(!nativeV8Number(fader,acceptedDb) ||
       !nativeV8Number(pan,acceptedPan) ||
       !nativeV8Number(producer.getProperty("fader",{}),nativeGain) ||
       !nativeV8Number(producer.getProperty("pan",{}),nativePan) ||
       !mute.isBool() || !solo.isBool() ||
       !producer.getProperty("mute",{}).isBool() ||
       !producer.getProperty("solo",{}).isBool() ||
       nativeGain<0. || nativeGain>4. ||
       std::abs(nativePan-acceptedPan)>0.015 ||
       static_cast<bool>(mute)!=static_cast<bool>(
           producer.getProperty("mute",{})) ||
       static_cast<bool>(solo)!=static_cast<bool>(
           producer.getProperty("solo",{})))
        return reject("NATIVE_CONTROL_READBACK_MISMATCH");
    const double nativeDb=nativeGain>0.?20.*std::log10(nativeGain):-60.;
    if(std::abs(nativeDb-acceptedDb)>0.11)
        return reject("STALE_NATIVE_CONTROL_READBACK");
    const auto payload=publicIntent.getProperty("payload",{});
    if(!payload.isObject())return reject("INVALID_CONTROL_PAYLOAD");
    const auto value=payload.getProperty(
        op=="SET_FADER"?"valueDb":"value",{});
    const char* nativeOperation=nullptr;
    auto translatedPayload=std::make_unique<juce::DynamicObject>();
    if(op=="SET_FADER" || op=="SET_PAN") {
        if(op=="SET_FADER") {
            double requested=0.;
            if(!nativeV8Number(value,requested) ||
               requested< -60. || requested>(bus?12.:10.))
                return reject("INVALID_FADER_VALUE");
            acceptedDb=requested;
        } else {
            double requested=0.;
            if(!nativeV8Number(value,requested) ||
               requested< -1. || requested>1.)
                return reject("INVALID_PAN_VALUE");
            acceptedPan=requested;
        }
        // PAN must not round-trip the companion fader through a clipped dB
        // projection: an accepted -60 dB can represent 0.001 linear, not
        // silence, and an upper-clamped +12 dB can represent gain 4.0.
        // Keep the verified native linear gain byte-for-byte on PAN edits.
        // Only an explicit SET_FADER request converts dB into new gain.
        const double gain=op=="SET_PAN"?nativeGain:
            (acceptedDb<= -60.?0.:std::pow(10.,acceptedDb/20.));
        if(!std::isfinite(gain) || gain>4.)
            return reject("UNREPRESENTABLE_GAIN");
        nativeOperation=bus?"NATIVE_SET_BUS_MIX":"NATIVE_SET_CHANNEL_MIX";
        translatedPayload->setProperty("gain",gain);
        translatedPayload->setProperty("pan",acceptedPan);
    } else {
        if(!value.isBool())return reject("INVALID_MUTE_SOLO_VALUE");
        nativeOperation="NATIVE_SET_CHANNEL_MUTE_SOLO";
        translatedPayload->setProperty("mute",op=="SET_MUTE"?value:mute);
        translatedPayload->setProperty("solo",op=="SET_SOLO"?value:solo);
    }
    if(!approvedHasOperation(installedBus.getProperty("supportedOps",{}),
                             nativeOperation))
        return reject("NATIVE_PCM_OPERATION_NOT_ADMITTED");
    auto converted=std::make_unique<juce::DynamicObject>();
    converted->setProperty("requestId",request);
    converted->setProperty("deviceEpoch",epoch);
    converted->setProperty("baseGraphRevision",revision);
    converted->setProperty("baseRosterRevision",roster);
    converted->setProperty("targetChannelId",ids);
    converted->setProperty("operation",nativeOperation);
    converted->setProperty("payload",juce::var(translatedPayload.release()));
    return {juce::var(converted.release()),{}};
}
} // namespace armix::console
