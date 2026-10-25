// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AdaptiveClockBridge.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <vector>
static void check(bool ok){if(!ok)std::abort();}

static void testRate(double captureRate,double renderRate,double driftPpm)
{
    armix::device::AdaptiveClockBridge bridge;
    check(bridge.prepare(captureRate,renderRate,2,8192,50.));
    std::array<float, 512> inputL {}, inputR {}, outputL {}, outputR {};
    const float* inputs[2]={inputL.data(),inputR.data()};
    float* outputs[2]={outputL.data(),outputR.data()};
    std::uint64_t generated=0, presented=0;
    double energy=0, peak=0;
    std::uint64_t validSamples=0;
    // Independent rational clocks, without wall-clock sleep or nondeterminism.
    // Drive capture and render at 1 ms virtual-timeline boundaries.
    for(int tick=1;tick<=6000;++tick)
    {
        const auto cTotal=static_cast<std::uint64_t>(
            std::floor(captureRate*(1.+driftPpm*1e-6)*tick/1000.));
        int toPush=static_cast<int>(cTotal-generated);
        check(toPush>=0 && toPush<=512);
        for(int i=0;i<toPush;++i)
        {
            const float x=0.6f*std::sin(6.283185307179586*440.0
                                     *(generated+static_cast<std::uint64_t>(i))/captureRate);
            inputL[static_cast<std::size_t>(i)]=x;
            inputR[static_cast<std::size_t>(i)]=x;
        }
        check(bridge.push(inputs,2,toPush)==toPush);
        generated=cTotal;
        const auto outTotal=static_cast<std::uint64_t>(std::floor(renderRate*tick/1000.));
        const int toPull=static_cast<int>(outTotal-presented);
        check(toPull>=0 && toPull<=512);
        bridge.pull(outputs,2,toPull);
        presented=outTotal;
        for(int i=0;i<toPull;++i)
        {
            const float v=outputL[static_cast<std::size_t>(i)];
            check(std::isfinite(v) && std::isfinite(outputR[static_cast<std::size_t>(i)]));
            check(std::abs(v-outputR[static_cast<std::size_t>(i)])<1e-6f);
            check(std::abs(v)<0.75f);
            if(tick>1000)
            {
                energy+=static_cast<double>(v)*v;
                peak=std::max(peak,static_cast<double>(std::abs(v)));
                ++validSamples;
            }
        }
    }
    const auto st=bridge.status();
    check(st.overflowFrames==0 && st.underflowFrames==0);
    check(st.primingFrames>0 && st.renderedFrames==presented);
    check(peak>0.50 && peak<0.7);
    check(energy/validSamples>0.12 && energy/validSamples<0.23);
    check(st.fillFrames<8192);
    check(std::abs(st.correctionPpm)<=2000.0);
}

// 30 minutes of virtual capture/render clocks. Pure deterministic automation;
// no hardware dependency, sleeping, COM objects or real-time wall clock.
// Includes -300ppm and brief 10ms capture scheduling gaps with catch-up.
static void testThirtyMinuteDualClock()
{
    armix::device::AdaptiveClockBridge bridge;
    check(bridge.prepare(8000.,8000.,2,8192,20.));
    std::array<float,256> left{},right{},outL{},outR{};
    left.fill(.125f); right.fill(.125f);
    const float* input[2]={left.data(),right.data()};
    float* output[2]={outL.data(),outR.data()};
    std::uint64_t written=0, consumed=0;
    const double inputRate=8000.*(1.-300.e-6);
    for(int i=1;i<=180000;++i)
    {
        const auto expected=static_cast<std::uint64_t>(
            std::floor(inputRate*i/100.)); // 10ms timestep
        if(i%300!=100) // brief capture scheduler stall, next tick catches up
        {
            int n=static_cast<int>(expected-written);
            check(n>=0&&n<=256);
            check(bridge.push(input,2,n)==n);
            written=expected;
        }
        const auto outExpected=static_cast<std::uint64_t>(i*80);
        const int need=static_cast<int>(outExpected-consumed);
        bridge.pull(output,2,need);
        consumed=outExpected;
        for(int j=0;j<need;++j)
        {
            check(std::isfinite(outL[j])&&std::isfinite(outR[j]));
            check(std::abs(outL[j])<=.2f&&std::abs(outR[j])<=.2f);
        }
    }
    const auto stat=bridge.status();
    check(stat.pushedFrames==written);
    check(stat.renderedFrames==consumed);
    check(stat.overflowFrames==0);
    check(stat.underflowFrames==0);
    check(stat.fillFrames<8192);
    check(std::abs(stat.correctionPpm)<2000);
}

int main()
{
    armix::device::AdaptiveClockBridge bad;
    check(!bad.prepare(0,48000,2));
    check(!bad.prepare(48000,48000,0));
    check(!bad.prepare(48000,48000,2,1000));
    testRate(44100,48000,0);
    testRate(44100,48000,300);
    testRate(48000,44100,-300);
    testRate(48000,48000,100);
    testThirtyMinuteDualClock();
    // Missing capture source is distinguishable from confirmed output xrun:
    // this core reports its own exact priming/underflow/overflow frames only.
    armix::device::AdaptiveClockBridge empty;
    check(empty.prepare(48000,48000,2,1024,12.));
    std::array<float,128> lo {}, ro {};
    float* outs[2]={lo.data(),ro.data()};
    empty.pull(outs,2,128);
    for(int i=0;i<128;++i)check(lo[i]==0.f && ro[i]==0.f);
    check(empty.status().primingFrames==128);
    return 0;
}
