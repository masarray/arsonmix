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
    // Missing capture source is distinguishable from confirmed output xrun:
    // this core reports its own exact priming/underflow/overflow frames only.
    armix::device::AdaptiveClockBridge empty;
    check(empty.prepare(48000,48000,2,1024,12.));
    std::array<float,128> lo {}, ro {};
    float* outs[2]={lo.data(),ro.data()};
    empty.pull(outs,2,128);
    for(int i=0;i<128;++i)check(lo[i]==0.f && ro[i]==0.f);
    check(empty.status().primingFrames==128);

    // Long-running starvation/overrun recovery. Explicitly test that FIFO
    // resumes after the capture clock stalls and drops excess source frames.
    armix::device::AdaptiveClockBridge recovering;
    check(recovering.prepare(48000,48000,2,1024,12.));
    std::array<float,512> srcL {},srcR {};
    for(int i=0;i<512;++i)srcL[i]=srcR[i]=0.25f;
    const float* src[2]={srcL.data(),srcR.data()};
    for(int i=0;i<4;++i)recovering.push(src,2,512);
    check(recovering.status().overflowFrames>0);
    for(int i=0;i<64;++i)
    {
        recovering.pull(outs,2,128);
        for(int n=0;n<128;++n)
        {
            check(std::isfinite(lo[n]) && std::isfinite(ro[n]));
            check(std::abs(lo[n])<=0.3f && std::abs(ro[n])<=0.3f);
        }
    }
    check(recovering.status().underflowFrames>0);
    // Simulate a fresh producer clock after a full starvation interval.
    for(int cycle=0;cycle<100;++cycle)
    {
        recovering.push(src,2,128);
        recovering.pull(outs,2,128);
    }
    auto after=recovering.status();
    check(after.pushedFrames>0 && after.renderedFrames>0);
    check(after.fillFrames<=1024);
    return 0;
}
