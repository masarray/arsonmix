// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <memory>

// JUCE MESSAGE-thread evidence projection. Raw values come ONLY from the
// installed LiveAudioEngine callback atomics; this does not produce PCM,
// read the audio device, invent a signal, or claim audible speaker output.
namespace armix::console {
inline bool evidenceFlag(const juce::var& o, const char* name, bool expected) {
    const auto v=o.getProperty(name,{});
    return v.isBool() && static_cast<bool>(v)==expected;
}
inline bool evidenceInteger(const juce::var& v, double& n) {
    if(!(v.isInt()||v.isInt64()||v.isDouble()))return false;
    n=static_cast<double>(v);
    return std::isfinite(n) && n>=0.0 && n<=9007199254740991.0
        && std::floor(n)==n;
}
inline bool evidencePeak(const juce::var& v, double& peak) {
    if(!(v.isInt()||v.isInt64()||v.isDouble()))return false;
    peak=static_cast<double>(v);
    return std::isfinite(peak) && peak>=0.0 && peak<=1.0;
}
inline juce::var projectFirstSoundSignalEvidence(
    const juce::var& live, bool verifiedDirectOpen,
    bool primaryOutputArmed, int activeInputCount, int activeOutputCount)
{
    auto t=std::make_unique<juce::DynamicObject>();
    // Empty/never-called capture must be UNKNOWN, not a synthetic zero
    // that misleadingly suggests a working microphone or driver.
    t->setProperty("captureObserved",false);
    t->setProperty("rawCapturePresent",false);
    t->setProperty("rawCapturePeak",juce::var());
    t->setProperty("captureCallbackPeaks",juce::var(juce::Array<juce::var>{}));
    t->setProperty("mainPcmObserved",false);
    t->setProperty("mainPcmPresent",false);
    t->setProperty("mainPcmPeak",juce::var());
    t->setProperty("monitorPcmPresent",false);
    if(!live.isObject() || !verifiedDirectOpen ||
       activeInputCount<=0 || activeInputCount>64 ||
       activeOutputCount<2 || activeOutputCount>64)
        return juce::var(t.release());
    const auto route=live.getProperty("dryRoutingLab",{});
    if(!route.isObject() ||
       !evidenceFlag(live,"connected",true) ||
       !evidenceFlag(live,"directRouteAuthorized",true) ||
       !evidenceFlag(live,"adaptiveClockActive",false) ||
       !evidenceFlag(route,"active",true) ||
       !evidenceFlag(route,"ready",true) ||
       !evidenceFlag(route,"topologyFault",false))
        return juce::var(t.release());
    double observations=0.,inCount=0.,outCount=0.,inputPeak=0.;
    if(!evidenceInteger(live.getProperty("captureObservationBlocks",{}),observations) ||
       observations<1. ||
       !evidenceInteger(live.getProperty("callbackInputChannels",{}),inCount) ||
       !evidenceInteger(live.getProperty("callbackOutputChannels",{}),outCount) ||
       inCount!=activeInputCount || outCount!=activeOutputCount ||
       !evidencePeak(live.getProperty("inputPeak",{}),inputPeak))
        return juce::var(t.release());
    const auto peaks=live.getProperty("captureCallbackPeaks",{});
    const auto* list=peaks.getArray();
    if(!list || list->size()!=activeInputCount)
        return juce::var(t.release());
    juce::Array<juce::var> verifiedPeaks;
    for(const auto& value:*list) {
        double peak=0.;
        if(!evidencePeak(value,peak))
            return juce::var(t.release());
        verifiedPeaks.add(peak);
    }
    t->setProperty("captureObserved",true);
    t->setProperty("rawCapturePeak",inputPeak);
    t->setProperty("rawCapturePresent",inputPeak>0.0005);
    t->setProperty("captureCallbackPeaks",juce::var(verifiedPeaks));
    // MAIN telemetry is measured *post* fader/FX/output gate, but remains
    // PCM evidence only: it can never certify actual DAC/earphone playback.
    if(primaryOutputArmed &&
       evidenceFlag(route,"hasAcceptedRoute",true) &&
       evidenceFlag(live,"monitor",true)) {
        double left=0.,right=0.;
        if(evidencePeak(live.getProperty("peakLeft",{}),left) &&
           evidencePeak(live.getProperty("peakRight",{}),right)) {
            const auto peak=juce::jmax(left,right);
            t->setProperty("mainPcmObserved",true);
            t->setProperty("mainPcmPeak",peak);
            t->setProperty("mainPcmPresent",peak>0.0005);
        }
    }
    return juce::var(t.release());
}
} // namespace armix::console
