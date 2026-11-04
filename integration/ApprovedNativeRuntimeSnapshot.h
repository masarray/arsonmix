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
    const actualFalse=[&](const juce::var& source,const char* key) {
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
    // Reading a PCM graph is not a mutating operation: Main.cpp still has a
    // G0 reject handler. Until ENGINE/INTEGRATOR jointly install a real
    // dispatcher and matching readback, advertise ZERO supported controls.
    // In particular, projectApprovedNativeV8's generic WIRE structure alone
    // must not enable WIRE intent before native transaction dispatch exists.
    const auto graph=snapshot.getProperty("graph",{});
    const auto caps=graph.getProperty("capabilities",{});
    if (auto* props=caps.getDynamicObject())
        props->setProperty("supportedOps",juce::var(juce::Array<juce::var>{}));
    // Full vendor/Show/physical FAT not done. Never promote product LIVE.
    return true;
}
} // namespace armix::console
