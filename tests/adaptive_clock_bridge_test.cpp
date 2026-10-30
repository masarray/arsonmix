// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AdaptiveClockBridge.h"
#include "../engine/device/ClockBridgeSizing.h"
#include "../engine/core/LiveStreamTapPolicy.h"
#include "../engine/device/BridgeContinuityGate.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
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

static void testLargeRenderQuantum()
{
    using namespace armix::device;
    const auto sizing = ClockBridgeSizing::choose(48000.,48000.,480,1440,
        AdaptiveClockBridge::taps,32768,24.);
    check(sizing.valid && sizing.targetMs > 60.);
    AdaptiveClockBridge bridge;
    check(bridge.prepare(48000,48000,2,32768,sizing.targetMs));
    std::array<float,480> captureL {},captureR {};
    std::array<float,1440> renderL {},renderR {};
    const float* capture[2]={captureL.data(),captureR.data()};
    float* render[2]={renderL.data(),renderR.data()};
    std::uint64_t silentAfterPrime=0;
    for(int block=0;block<200;++block)
    {
        for(int j=0;j<3;++j)
        {
            for(int i=0;i<480;++i)
                captureL[i]=captureR[i]=0.25f;
            check(bridge.push(capture,2,480)==480);
        }
        bridge.pull(render,2,1440);
        if(block>15)
            for(float sample:renderL) if(std::abs(sample)<0.05f)++silentAfterPrime;
    }
    const auto st=bridge.status();
    check(st.underflowFrames==0);
    check(st.overflowFrames==0);
    check(silentAfterPrime==0);
    const auto rejected = ClockBridgeSizing::choose(48000.,48000.,480,20000,
        AdaptiveClockBridge::taps,32768,24.);
    check(!rejected.valid);
}

// Independent virtual 48 kHz capture -> 32 kHz render FIR fidelity test.
// A 1 kHz source is passed; 19 kHz is above the render Nyquist and rejected.
static double spectralRms (double frequency)
{
    armix::device::AdaptiveClockBridge bridge;
    check (bridge.prepare (48000,32000,2,8192,50.));
    std::array<float,48> inL{},inR{};
    std::array<float,32> outL{},outR{};
    const float* inputs[]={inL.data(),inR.data()};
    float* outputs[]={outL.data(),outR.data()};
    double power=0.;
    std::uint64_t count=0;
    for(int ms=0;ms<2500;++ms)
    {
        for(int k=0;k<48;++k)
        {
            const auto sample=static_cast<std::uint64_t>(ms)*48+k;
            inL[k]=inR[k]=static_cast<float>(0.45*std::sin(
                6.283185307179586*frequency*static_cast<double>(sample)/48000.));
        }
        check(bridge.push(inputs,2,48)==48);
        bridge.pull(outputs,2,32);
        for(int k=0;k<32;++k)
        {
            check(std::isfinite(outL[k]) && std::isfinite(outR[k]));
            if(ms>500)
            {
                power+=static_cast<double>(outL[k])*outL[k];
                ++count;
            }
        }
    }
    const auto status=bridge.status();
    check(status.overflowFrames==0 && status.underflowFrames==0);
    check(status.nonFiniteInputSamples==0 && count>1000);
    return std::sqrt(power/static_cast<double>(count));
}

static void testNonFiniteCapture()
{
    using armix::device::AdaptiveClockBridge;
    AdaptiveClockBridge bridge;
    check(!bridge.prepare(48000,48000,2,8192,
        std::numeric_limits<double>::quiet_NaN()));
    check(!bridge.prepare(48000,48000,2,8192,
        std::numeric_limits<double>::infinity()));
    check(!bridge.prepare(48000,48000,2,(1<<19),12.));
    check(bridge.prepare(48000,48000,2,2048,12.));
    std::array<float,128> inL{},inR{},outL{},outR{};
    const float* inputs[]={inL.data(),inR.data()};
    float* outputs[]={outL.data(),outR.data()};
    inL.fill(.2f); inR.fill(.2f);
    inL[3]=std::numeric_limits<float>::quiet_NaN();
    inR[4]=std::numeric_limits<float>::infinity();
    for(int block=0;block<120;++block)
    {
        check(bridge.push(inputs,2,128)==128);
        bridge.pull(outputs,2,128);
        for(int k=0;k<128;++k)
            check(std::isfinite(outL[k]) && std::isfinite(outR[k]));
        if(block==0) { inL[3]=inR[4]=.2f; }
    }
    const auto status=bridge.status();
    check(status.nonFiniteInputSamples==2);
    check(status.overflowFrames==0 && status.underflowFrames==0);
    check(status.renderedFrames==120*128);
}

// End-to-end virtual-clock reproduction of an ACTUAL FIFO starvation:
// render is continuously scheduled but capture returns no useful frames
// for 60ms every 500ms. First missing sample is "underflow"; the rest
// are counted as "priming" by AdaptiveClockBridge. Policy must combine
// those counters rather than misclassify the long re-prime as clean audio.
// No hardware, wall-clock sleeps, mocks of the resampler or allocation in RT.
static void testLiveBridgeContinuityFailback()
{
    using armix::device::AdaptiveClockBridge;
    using armix::device::BridgeContinuityGate;
    AdaptiveClockBridge bridge;
    check(bridge.prepare(48000.,48000.,2,8192,50.));
    BridgeContinuityGate gate;
    std::array<float,48> l{},r{},outL{},outR{};
    l.fill(.15f);r.fill(.15f);
    const float* inputs[]={l.data(),r.data()};
    float* outputs[]={outL.data(),outR.data()};
    bool tripped=false;
    for (std::uint64_t ms=0;ms<12000;++ms)
    {
        // Physical callback may still be called with zero usable capture.
        // Do not stop the render clock or manufacture output xrun counts.
        if (ms%500>=60) check(bridge.push(inputs,2,48)==48);
        else check(bridge.push(inputs,2,0)==0);
        bridge.pull(outputs,2,48);
        const auto stats=bridge.status();
        const auto fault=gate.observe(ms,{
            stats.renderedFrames,stats.underflowFrames,
            stats.primingFrames,stats.overflowFrames});
        for(int i=0;i<48;++i)
            check(std::isfinite(outL[i]) && std::isfinite(outR[i]));
        if(fault!=BridgeContinuityGate::Fault::none)
        {
            tripped=true;
            check(ms>=BridgeContinuityGate::warmupMs
                + BridgeContinuityGate::windowMs);
            check(gate.snapshot().consecutiveBadWindows>=3);
            break;
        }
    }
    const auto stats=bridge.status();
    check(stats.primingFrames>0);
    check(stats.underflowFrames>0);
    check(tripped); // proven real FIR/FIFO starvation, not synthetic counters
}


static void testLiveStreamGateAndObsClock()
{
    using P=armix::mixer::LiveStreamTapPolicy;
    const auto onlyOBS=P::evaluate(true,true,true,true,true,false,true);
    check(onlyOBS.renderMain && onlyOBS.stream && !onlyOBS.hardware);
    const auto onlyHardware=P::evaluate(true,true,true,true,true,true,false);
    check(onlyHardware.renderMain && onlyHardware.hardware && !onlyHardware.stream);
    const auto both=P::evaluate(true,true,true,true,true,true,true);
    check(both.renderMain && both.hardware && both.stream);
    for(int bit=0;bit<5;++bit){
        bool gates[5]={true,true,true,true,true};
        gates[bit]=false;
        auto g=P::evaluate(gates[0],gates[1],gates[2],gates[3],gates[4],
                           true,true);
        check(!g.renderMain && !g.hardware && !g.stream);
    }
    const auto muted=P::evaluate(true,true,true,true,true,false,false);
    check(!muted.renderMain && !muted.hardware && !muted.stream);
    // A separate OBS playback endpoint requires real ASRC buffering, NOT a
    // 0ms claim. Existing FIR testRate() covers 44.1/48k and ±300ppm clocks.
    using namespace armix::device;
    const auto cfg=ClockBridgeSizing::choose(48000.,44100.,128,256,
                                             AdaptiveClockBridge::taps,
                                             32768,24.);
    check(cfg.valid && cfg.targetMs>=24.0);
    AdaptiveClockBridge asrc;
    check(asrc.prepare(48000.,44100.,2,32768,cfg.targetMs));
    std::array<float,128> mainL{},mainR{};
    const float* main[2]={mainL.data(),mainR.data()};
    mainL.fill(.15f);mainR.fill(-.12f);
    for(int n=0;n<200;++n)
        check(asrc.push(main,2,128)==128);
    const auto stats=asrc.status();
    check(stats.pushedFrames==25600);
    check(stats.overflowFrames==0);
}

int main()
{
    testLiveStreamGateAndObsClock();
    testLiveBridgeContinuityFailback();
    testNonFiniteCapture();
    const double passband = spectralRms (1000.);
    const double stopband = spectralRms (19000.);
    check(passband > 0.27 && passband < 0.36);
    check(stopband < 0.025);
    armix::device::AdaptiveClockBridge bad;
    check(!bad.prepare(0,48000,2));
    check(!bad.prepare(48000,48000,0));
    check(!bad.prepare(48000,48000,2,1000));
    testLargeRenderQuantum();
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
    return 0;
}
