// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ApprovedNativeV8Projection.h"
#include <juce_core/juce_core.h>
#include <cstdint>

// Sole JUCE MESSAGE-thread production readback for approved /preview/.
// Never creates a ChannelId bus, audio callback, synthetic device, or
// browser-owned PCM. ENGINE #266 reports nativeChannelIdPcmActive ONLY after
// its installed realtime callback owns the accepted publisher/epoch.
// G0/M4 and any forged nativeBus without that predicate MUST stay unadmitted.
namespace armix::console {
// Legacy M2 counted only device START callbacks; the actual routing epoch
// also advances on STOP. After ASIO/WASAPI re-open they can differ (e.g.,
// G0 start=2 while accepted routing epoch=3). Always use the installed
// LiveDryRoutingLab epoch for both G0 and native accepted snapshots so a
// disconnected physical device cannot leave a stale accepted GUI LIVE.
// This remains a CONTROL-only metadata correction; never creates a graph.
inline bool synchronizeApprovedRouteDeviceEpoch(
    juce::var& snapshot,const juce::var& live)
{
    if (!snapshot.isObject() || !live.isObject()) return false;
    const auto device=snapshot.getProperty("device",{});
    const auto route=live.getProperty("dryRoutingLab",{});
    if (!device.isObject() || !route.isObject()) return false;
    const auto generation=device.getProperty("epoch",{});
    const auto epoch=route.getProperty("epoch",{});
    if (!generation.isString() ||
        !(generation.toString()=="0" || nativeV8Decimal(generation)) ||
        !(epoch.isInt() || epoch.isInt64())) return false;
    const auto actual=static_cast<juce::int64>(epoch);
    if(actual<0 || actual<generation.toString().getLargeIntValue())
        return false;
    if(auto* d=device.getDynamicObject())
        d->setProperty("epoch",juce::String(actual));
    return true;
}

// First Sound is a REAL legacy M4 physical route before ChannelId migration.
// The approved native More-menu needs its actual epoch/revision to PATCH CH1.
// Prior G0 snapshots omitted legacyM4 entirely, permanently disabling PATCH
// and ARM even when JUCE had a valid prepared mixer. This CONTROL-only
// metadata does not admit ChannelId operations, synthesize PCM or arm output.
inline bool publishVerifiedFirstSoundState(juce::var& snapshot,
                                            const juce::var& live)
{
    if(!snapshot.isObject() || !live.isObject()) return false;
    const auto device=snapshot.getProperty("device",{});
    const auto route=live.getProperty("dryRoutingLab",{});
    if(!device.isObject() || !route.isObject()) return false;
    const auto boolIs=[&](const juce::var& parent,const char* key,bool expected) {
        const auto actual=parent.getProperty(key,{});
        return actual.isBool() && static_cast<bool>(actual)==expected;
    };
    const auto epoch=device.getProperty("epoch",{});
    const auto routeEpoch=route.getProperty("epoch",{});
    const auto revision=route.getProperty("revision",{});
    const auto inputs=device.getProperty("enabledInputs",{});
    const auto outputs=device.getProperty("enabledOutputs",{});
    double rate=0.,frames=0.;
    const bool physicalReady=
        boolIs(live,"connected",true) &&
        boolIs(live,"directRouteAuthorized",true) &&
        boolIs(live,"adaptiveClockActive",false) &&
        boolIs(route,"active",true) &&
        boolIs(route,"ready",true) &&
        boolIs(route,"topologyFault",false) &&
        nativeV8Decimal(epoch) &&
        (routeEpoch.isInt() || routeEpoch.isInt64()) &&
        (revision.isInt() || revision.isInt64()) &&
        static_cast<juce::int64>(routeEpoch)>0 &&
        static_cast<juce::int64>(revision)>0 &&
        juce::String(static_cast<juce::int64>(routeEpoch))==epoch.toString() &&
        inputs.isArray() && !inputs.getArray()->isEmpty() &&
        outputs.isArray() && outputs.getArray()->size()>=2 &&
        nativeV8Number(device.getProperty("sampleRate",{}),rate) &&
        nativeV8Number(device.getProperty("bufferFrames",{}),frames) &&
        rate>=8000. && frames>=16.;
    auto firstSound=std::make_unique<juce::DynamicObject>();
    firstSound->setProperty("active",physicalReady);
    firstSound->setProperty("routeAccepted",physicalReady &&
        boolIs(route,"hasAcceptedRoute",true));
    firstSound->setProperty("routingRevision",physicalReady
        ?juce::String(static_cast<juce::int64>(revision)):juce::String("0"));
    // Direct physical-source readback for EVERY existing 10-channel M4 strip.
    // Never infer source routes from demo UI, or mark stale controls accepted.
    juce::Array<juce::var> sources;
    if(physicalReady) {
        const bool patched=boolIs(route,"hasAcceptedRoute",true);
        const auto accepted=route.getProperty(
            patched?"acceptedRoute":"preparedM4Route",{});
        const auto rows=accepted.getProperty("channels",{});
        const auto* list=rows.getArray();
        if(list && list->size()==10) {
            for(const auto& row:*list) {
                const auto mode=row.getProperty("mode",{}).toString();
                const auto left=row.getProperty("left",{});
                const auto right=row.getProperty("right",{});
                const bool mono=mode=="MONO", stereo=mode=="STEREO";
                const bool valid=mode=="NONE" ||
                    ((mono||stereo) && (left.isInt()||left.isInt64()) &&
                     (right.isInt()||right.isInt64()) &&
                     inputs.getArray()->contains(left) &&
                     (!stereo || (inputs.getArray()->contains(right) && left!=right)));
                if(!valid) {sources.clear();break;}
                auto src=std::make_unique<juce::DynamicObject>();
                src->setProperty("mode",mode);
                src->setProperty("left",mono||stereo?left:juce::var(-1));
                src->setProperty("right",stereo?right:juce::var(-1));
                sources.add(juce::var(src.release()));
            }
        }
    }
    firstSound->setProperty("channelSources",juce::var(sources));
    // Do not write the route to the graph or change native capability flags.
    snapshot.getDynamicObject()->setProperty("legacyM4",juce::var(firstSound.release()));
    return physicalReady;
}

inline bool projectInstalledNativeChannelIdGraph(
    juce::var& snapshot, const juce::var& live)
{
    if (!snapshot.isObject() || !live.isObject()) return false;
    const auto route=live.getProperty("dryRoutingLab",{});
    const auto device=snapshot.getProperty("device",{});
    if (!route.isObject() || !device.isObject()) return false;
    const auto actualFlag=[&](const juce::var& source,const char* key) {
        const auto value=source.getProperty(key,{});
        return value.isBool() && static_cast<bool>(value);
    };
    const auto actualFalse=[&](const juce::var& source,const char* key) {
        const auto value=source.getProperty(key,{});
        return value.isBool() && !static_cast<bool>(value);
    };
    if (!actualFlag(live,"connected") ||
        !actualFlag(live,"directRouteAuthorized") ||
        !actualFalse(live,"adaptiveClockActive") ||
        !actualFlag(route,"ready") ||
        !actualFlag(route,"hasAcceptedRoute") ||
        !actualFlag(route,"nativeChannelIdPcmActive") ||
        !actualFalse(route,"topologyFault"))
        return false;
    const auto epoch=device.getProperty("epoch",{});
    const auto nativeEpoch=route.getProperty("epoch",{});
    if (!nativeV8Decimal(epoch) || epoch.toString()=="0" ||
        !(nativeEpoch.isInt() || nativeEpoch.isInt64()) ||
        static_cast<juce::int64>(nativeEpoch)<=0 ||
        juce::String(static_cast<juce::int64>(nativeEpoch))!=epoch.toString())
        return false;
    const auto outputMask=device.getProperty("enabledOutputs",{});
    const auto inputMask=device.getProperty("enabledInputs",{});
    const auto rate=device.getProperty("sampleRate",{});
    const auto frames=device.getProperty("bufferFrames",{});
    double actualRate=0.,actualFrames=0.;
    if (!outputMask.isArray() || !inputMask.isArray() ||
        outputMask.getArray()->size()<2 || inputMask.getArray()->isEmpty() ||
        !nativeV8Number(rate,actualRate) ||
        !nativeV8Number(frames,actualFrames) ||
        actualRate<8000. || actualRate>384000. ||
        actualFrames<16. || actualFrames>32768.)
        return false;
    const auto bus=route.getProperty("nativeBus",{});
    if (!bus.isObject()) return false;
    // This function has no independent "true" argument. Callback admission
    // comes from the verified installed ENGINE status above, not the caller.
    if (!projectApprovedNativeV8(snapshot,bus,true))
        return false;
    // Advertise only controls provided by the currently installed and
    // positively admitted native callback (never inferred from a JSON shape
    // or the browser). Both the native and public capability are required by
    // the stateless control translator. WIRE remains explicitly disabled
    // until the native structural dispatcher is independently admitted.
    juce::Array<juce::var> available;
    const auto nativeOps=bus.getProperty("supportedOps",{});
    const auto* actual=nativeOps.getArray();
    const auto has=[&](const char* op) {
        return actual && actual->contains(juce::String(op));
    };
    if(has("NATIVE_SET_CHANNEL_MIX")) {
        available.add(juce::String("SET_FADER"));
        available.add(juce::String("SET_PAN"));
    }
    if(has("NATIVE_SET_CHANNEL_MUTE_SOLO")) {
        available.add(juce::String("SET_MUTE"));
        available.add(juce::String("SET_SOLO"));
    }
    const auto graph=snapshot.getProperty("graph",{});
    const auto caps=graph.getProperty("capabilities",{});
    if (auto* props=caps.getDynamicObject())
        props->setProperty("supportedOps",juce::var(available));
    // Product LIVE stays false until independent vendor/Show/hardware FAT.
    return true;
}
} // namespace armix::console
