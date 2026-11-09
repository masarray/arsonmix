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
    auto uninterrupted=makeHarmony(), switching=makeHarmony();
    assert(switching->continuousBypassProcessing);
    assert(uninterrupted->latencySamples==1536);
    assert(switching->latencySamples==uninterrupted->latencySamples);
    DynamicChannelFxLeaseBank::Assignment referenceRow,switchingRow;
    referenceRow.id=vocal;referenceRow.format=ProcessingFormat::stereo;
    referenceRow.inserts.push_back(uninterrupted);
    switchingRow.id=vocal;switchingRow.format=ProcessingFormat::stereo;
    switchingRow.inserts.push_back(switching);
    DynamicChannelFxLeaseBank alwaysOn,switched;
    const std::array<DynamicChannelFxLeaseBank::Assignment,1> referenceRows{referenceRow};
    const std::array<DynamicChannelFxLeaseBank::Assignment,1> switchingRows{switchingRow};
    assert(DynamicChannelFxLeaseBank::prepare(1,1,referenceRows,alwaysOn)==
           DynamicChannelFxLeaseBank::Fault::none);
    assert(DynamicChannelFxLeaseBank::prepare(1,1,switchingRows,switched)==
           DynamicChannelFxLeaseBank::Fault::none);
    bool priorSlot=false,priorRack=false;
    float maxWetDifference=0.f,maxDryDifference=0.f;
    int samplesCheckedWet=0,samplesCheckedDry=0;
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
            priorSlot=slotOff;
        }
        if(rackOff!=priorRack) {
            assert(switched.setRackBypass(1,1,vocal,rackOff));
            priorRack=rackOff;
        }
        std::array<float,64> baseL{},baseR{},probeL{},probeR{};
        for(int i=0;i<64;++i) {
            const int n=block*64+i;
            baseL[i]=probeL[i]=singer(n);
            baseR[i]=probeR[i]=singer(n);
        }
        alwaysOn(vocal,baseL.data(),baseR.data(),64);
        switched(vocal,probeL.data(),probeR.data(),64);
        const bool quietBypass=slotOff||rackOff;
        const bool steadyWet=!quietBypass &&
            (block>=590&&block<700 || block>=790&&block<850 ||
             block>=1010&&block<1110 || block>=1125);
        const bool steadyDry=quietBypass&&
            (block>=330&&block<580 || block>=710&&block<780 ||
             block>=860&&block<1000);
        for(int i=0;i<64;++i) {
            assert(std::isfinite(probeL[i])&&std::isfinite(probeR[i]));
            assert(std::abs(probeL[i])<=4.f&&std::abs(probeR[i])<=4.f);
            if(steadyWet) {
                const float d=std::max(std::abs(probeL[i]-baseL[i]),
                                       std::abs(probeR[i]-baseR[i]));
                maxWetDifference=std::max(maxWetDifference,d);
                assert(d<.00002f); // same exact history as never-bypassed DSP
                ++samplesCheckedWet;
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
    assert(!switched.rackBypassed(vocal));
    std::cout<<"Harmony native ChannelId warm bypass, dry PDC and state parity PASS; "
             <<"wet_samples="<<samplesCheckedWet<<" dry_samples="<<samplesCheckedDry
             <<" max_wet_difference="<<maxWetDifference
             <<" max_dry_difference="<<maxDryDifference<<"\\n";
}
