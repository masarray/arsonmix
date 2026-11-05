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
    // Exactly ten hardware INPUT readbacks from the existing accepted M4
    // compiler, not the public Pages demo. A new unpatched session reports
    // an empty array: we must not fabricate a default CH1 microphone.
    juce::Array<juce::var> verifiedChannels;
    if(physicalReady && boolIs(route,"hasAcceptedRoute",true)) {
        const auto accepted=route.getProperty("acceptedRoute",{});
        const auto channels=accepted.getProperty("channels",{});
        if(auto* list=channels.getArray();list!=nullptr &&
           list->size()==10) {
            bool valid=true;
            for(const auto& item:*list) {
                if(!item.isObject()) {valid=false;break;}
                const auto mode=item.getProperty("mode",{}).toString();
                const auto left=item.getProperty("left",juce::var());
                const auto right=item.getProperty("right",juce::var());
                const auto main=item.getProperty("main",{});
                // BUS is a real accepted, internally-fed M4 lane. Discarding
                // all 10 readbacks because ONE lane is BUS would hide live
                // physical microphone inputs on otherwise verified channels.
                // Preserve BUS identity without claiming a hardware pin.
                if((mode!="NONE" && mode!="MONO" &&
                    mode!="STEREO" && mode!="BUS") ||
                   !(left.isInt()||left.isInt64()) ||
                   !(right.isInt()||right.isInt64()) || !main.isBool()) {
                    valid=false;break;
                }
                const int l=static_cast<int>(left),r=static_cast<int>(right);
                if(((mode=="NONE"||mode=="BUS")&&(l!=-1||r!=-1)) ||
                   (mode=="MONO"&&(r!=-1||l<0)) ||
                   (mode=="STEREO"&&(l<0||r<0||l==r))) {
                    valid=false;break;
                }
                if((mode=="MONO"||mode=="STEREO")&&
                   (!inputs.getArray()->contains(l)||
                   (mode=="STEREO"&&!inputs.getArray()->contains(r)))) {
                    valid=false;break;
                }
                auto verified=std::make_unique<juce::DynamicObject>();
                verified->setProperty("mode",mode);
                verified->setProperty("left",l);
                verified->setProperty("right",r);
                verified->setProperty("main",main);
                verifiedChannels.add(juce::var(verified.release()));
            }
            if(!valid)verifiedChannels.clear();
        }
    }
    firstSound->setProperty("channels",juce::var(verifiedChannels));
    // Native M4 remains the real 10-channel callback before ChannelId
    // admission, including a stable source patch under armed monitoring.
    // Publish only verified PCM controls and measured per-channel peaks.
    // Never infer controls from the Pages reference model.
    if(physicalReady && boolIs(route,"hasAcceptedRoute",true) &&
       !boolIs(route,"nativeBusStructural",true) &&
       verifiedChannels.size()==10) {
        const auto controls=route.getProperty("channelControls",{});
        const auto levelsL=route.getProperty("channelPeaksL",{});
        const auto levelsR=route.getProperty("channelPeaksR",{});
        const auto* ctl=controls.getArray();
        const auto* l=levelsL.getArray();
        const auto* r=levelsR.getArray();
        bool valid=ctl && l && r && ctl->size()==10 &&
                   l->size()==10 && r->size()==10;
        for(int i=0;valid && i<10;++i) {
            const auto row=ctl->getReference(i);
            double db=0.,pan=0.,peakL=0.,peakR=0.;
            valid=row.isObject() &&
                  nativeV8Number(row.getProperty("db",{}),db) &&
                  nativeV8Number(row.getProperty("pan",{}),pan) &&
                  nativeV8Number(l->getReference(i),peakL) &&
                  nativeV8Number(r->getReference(i),peakR) &&
                  db>=-60.01 && db<=10.01 &&
                  pan>=-100.01 && pan<=100.01 &&
                  peakL>=0. && peakR>=0. &&
                  row.getProperty("mute",{}).isBool() &&
                  row.getProperty("solo",{}).isBool();
        }
        if(valid) {
            firstSound->setProperty("channelControls",controls);
            firstSound->setProperty("channelPeaksL",levelsL);
            firstSound->setProperty("channelPeaksR",levelsR);
            double masterDb=0.;
            if(nativeV8Number(live.getProperty("masterDb",{}),masterDb) &&
               masterDb>=-60.01 && masterDb<=10.01)
                firstSound->setProperty("masterDb",masterDb);
            const auto masterMute=live.getProperty("masterMute",{});
            if(masterMute.isBool())firstSound->setProperty("masterMute",masterMute);
        }
    }
    // Real routed FX instance IDs and catalog keys are read only from the
    // installed publisher. A browser cannot insert forged plugin identities.
    if(physicalReady && boolIs(route,"hasAcceptedRoute",true)) {
        const auto fx=route.getProperty("fxInstances",{});
        if(auto* rows=fx.getArray();rows && rows->size()==14)
            firstSound->setProperty("fxInstances",fx);
    }
    firstSound->setProperty("nativeBusStructural",
        route.getProperty("nativeBusStructural",false));
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
    // ChannelId owns channel PCM, but the physical MAIN gain/mute is
    // applied by the single LiveAudioEngine host after that render. The
    // generic ChannelId roster's default MAIN fader is not this physical
    // gain. Publish the real host readback, or the visible master will
    // snap back to 0 dB after every accepted native scalar snapshot.
    double physicalMasterDb=0.;
    const auto physicalMasterMute=live.getProperty("masterMute",{});
    if(nativeV8Number(live.getProperty("masterDb",{}),physicalMasterDb) &&
       physicalMasterDb>=-60.01 && physicalMasterDb<=10.01 &&
       physicalMasterMute.isBool()) {
        const auto installedGraph=snapshot.getProperty("graph",{});
        const auto channels=installedGraph.getProperty("channels",{});
        if(auto* rows=channels.getArray())
            for(auto& row:*rows)
                if(row.getProperty("role",{}).toString()=="MAIN") {
                    const auto controls=row.getProperty("controls",{});
                    if(auto* obj=controls.getDynamicObject()) {
                        obj->setProperty("faderDb",physicalMasterDb);
                        obj->setProperty("mute",physicalMasterMute);
                    }
                }
    }
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
