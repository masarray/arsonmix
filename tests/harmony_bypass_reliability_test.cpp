// SPDX-License-Identifier: AGPL-3.0-only
// Real JUCE FX instance and native ChannelId PCM lease, not just envelope math.
// The enabled reference must equal the twice-bypassed Harmony AFTER re-entry.
// Old code failed because a fully bypassed Harmony stopped reading live PCM.
#include "../engine/core/DynamicChannelFxLeaseBank.h"
#include "../engine/builtin_fx/ChannelHarmonyProcessor.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <algorithm>
#include <cmath>
#include <memory>
#include <iostream>

using namespace armix::mixer;
using armix::builtin_fx::ChannelHarmonyProcessor;

static float singer(int n) noexcept {
    const double t=static_cast<double>(n)/48000.;
    return static_cast<float>(.16*std::sin(6.283185307179586*220.*t)+
                              .045*std::sin(6.283185307179586*440.*t));
}
static LiveChannelFxGraph::InstanceLease makeHarmony() {
    auto p=std::make_unique<ChannelHarmonyProcessor>();
    p->prepareToPlay(48000.,64);
    assert(p->isReady() && p->getLatencySamples()==1536);
    armix::harmony::Settings rich;
    rich.voices=5;rich.wet=1.f;rich.space=1.f;
    assert(p->queueAccepted(rich,9001));
    return std::make_shared<LiveChannelFxGraph::FxInstance>(
        std::move(p),ChannelHarmonyProcessor::catalogIdentity,
        "ArSon Harmony",64);
}
int main() {
    constexpr ChannelId vocal{7301};
    auto uninterrupted=makeHarmony(), switching=makeHarmony(),legacy=makeHarmony();
    assert(switching->continuousBypassProcessing);
    // Isolated offline reference of PRE-FIX host behavior; production never
    // disables the warm-processing contract. This must diverge on reentry.
    legacy->continuousBypassProcessing=false;
    assert(uninterrupted->latencySamples==1536);
    assert(switching->latencySamples==uninterrupted->latencySamples);
    DynamicChannelFxLeaseBank::Assignment referenceRow,switchingRow,legacyRow;
    referenceRow.id=vocal;referenceRow.format=ProcessingFormat::stereo;
    referenceRow.inserts.push_back(uninterrupted);
    switchingRow.id=vocal;switchingRow.format=ProcessingFormat::stereo;
    switchingRow.inserts.push_back(switching);
    legacyRow.id=vocal;legacyRow.format=ProcessingFormat::stereo;
    legacyRow.inserts.push_back(legacy);
    DynamicChannelFxLeaseBank alwaysOn,switched,oldBehavior;
    const std::array<DynamicChannelFxLeaseBank::Assignment,1> referenceRows{referenceRow};
    const std::array<DynamicChannelFxLeaseBank::Assignment,1> switchingRows{switchingRow};
    const std::array<DynamicChannelFxLeaseBank::Assignment,1> legacyRows{legacyRow};
    assert(DynamicChannelFxLeaseBank::prepare(1,1,legacyRows,oldBehavior)==
           DynamicChannelFxLeaseBank::Fault::none);
    assert(DynamicChannelFxLeaseBank::prepare(1,1,referenceRows,alwaysOn)==
           DynamicChannelFxLeaseBank::Fault::none);
    assert(DynamicChannelFxLeaseBank::prepare(1,1,switchingRows,switched)==
           DynamicChannelFxLeaseBank::Fault::none);
    bool priorSlot=false,priorRack=false;
    float maxWetDifference=0.f,maxDryDifference=0.f;
    float maxOldBypassDifference=0.f,maxClickSlopeExcess=0.f;
    int samplesCheckedWet=0,samplesCheckedDry=0;
    float previousWetL=0.f,previousWetR=0.f;
    float previousProbeL=0.f,previousProbeR=0.f,previousDry=0.f;
    // Fixed 64-frame hardware-shaped partitions, with slot and rack OFF.
    // The last pair is a quick reverse-toggle (no graph or callback restart).
    for(int block=0;block<1300;++block) {
        const bool slotOff=(block>=320&&block<580)||
                           (block>=700&&block<780)||
                           (block==1110||block==1112);
        const bool rackOff=(block>=850&&block<1000)||
                           (block==1111||block==1113);
        if(slotOff!=priorSlot) {
            assert(switched.setInsertBypass(1,1,vocal,0,switching->instanceId,slotOff));
            assert(oldBehavior.setInsertBypass(1,1,vocal,0,legacy->instanceId,slotOff));
            priorSlot=slotOff;
        }
        if(rackOff!=priorRack) {
            assert(switched.setRackBypass(1,1,vocal,rackOff));
            assert(oldBehavior.setRackBypass(1,1,vocal,rackOff));
            priorRack=rackOff;
        }
        std::array<float,64> baseL{},baseR{},probeL{},probeR{},oldL{},oldR{};
        for(int i=0;i<64;++i) {
            const int n=block*64+i;
            baseL[i]=probeL[i]=oldL[i]=singer(n);
            baseR[i]=probeR[i]=oldR[i]=singer(n);
        }
        alwaysOn(vocal,baseL.data(),baseR.data(),64);
        switched(vocal,probeL.data(),probeR.data(),64);
        oldBehavior(vocal,oldL.data(),oldR.data(),64);
        const bool quietBypass=slotOff||rackOff;
        const bool steadyWet=!quietBypass &&
            (block>=590&&block<700 || block>=790&&block<850 ||
             block>=1010&&block<1110 || block>=1125);
        const bool steadyDry=quietBypass&&
            (block>=330&&block<580 || block>=710&&block<780 ||
             block>=860&&block<1000);
        for(int i=0;i<64;++i) {
            const int n=block*64+i;
            const float delayed=n>=1536?singer(n-1536):0.f;
            if(n>=1537){
                // Actual post-bypass sample slope may only exceed either
                // continuous wet or exact aligned dry slope by the 8ms ramp
                // contribution. No block-edge impulse is allowed.
                const float wetSlope=std::max(std::abs(baseL[i]-previousWetL),
                                               std::abs(baseR[i]-previousWetR));
                const float drySlope=std::abs(delayed-previousDry);
                const float actualSlope=std::max(std::abs(probeL[i]-previousProbeL),
                                                 std::abs(probeR[i]-previousProbeR));
                const float excess=actualSlope-std::max(wetSlope,drySlope);
                maxClickSlopeExcess=std::max(maxClickSlopeExcess,excess);
                assert(excess<.035f);
            }
            previousWetL=baseL[i];previousWetR=baseR[i];
            previousProbeL=probeL[i];previousProbeR=probeR[i];
            previousDry=delayed;
            assert(std::isfinite(probeL[i])&&std::isfinite(probeR[i]));
            assert(std::abs(probeL[i])<=4.f&&std::abs(probeR[i])<=4.f);
            if(steadyWet) {
                const float d=std::max(std::abs(probeL[i]-baseL[i]),
                                       std::abs(probeR[i]-baseR[i]));
                maxWetDifference=std::max(maxWetDifference,d);
                assert(d<.00002f); // same exact history as never-bypassed DSP
                ++samplesCheckedWet;
                if(block>=590&&block<615 || block>=790&&block<815)
                    maxOldBypassDifference=std::max(maxOldBypassDifference,
                      std::max(std::abs(oldL[i]-baseL[i]),
                               std::abs(oldR[i]-baseR[i])));
            }
            if(steadyDry) {
                const int n=block*64+i-1536;
                const float expect=n>=0?singer(n):0.f;
                const float d=std::max(std::abs(probeL[i]-expect),
                                       std::abs(probeR[i]-expect));
                maxDryDifference=std::max(maxDryDifference,d);
                assert(d<.00002f); // never switch PDC when OFF
                ++samplesCheckedDry;
            }
        }
    }
    assert(samplesCheckedWet>10000&&samplesCheckedDry>10000);
    assert(maxOldBypassDifference>.003f); // old skipped DSP history visibly diverges
    assert(!switched.rackBypassed(vocal));
    std::cout<<"Harmony native ChannelId warm bypass, dry PDC and state parity PASS; "
             <<"wet_samples="<<samplesCheckedWet<<" dry_samples="<<samplesCheckedDry
             <<" max_wet_difference="<<maxWetDifference
             <<" max_dry_difference="<<maxDryDifference
             <<" max_old_bypass_difference="<<maxOldBypassDifference
             <<" max_click_slope_excess="<<maxClickSlopeExcess<<"\\n";
}
