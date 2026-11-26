// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/RealtimeStereoControlWord.h"
#include "../engine/device/ClockBridgeSizing.h"
#include "../engine/device/AdaptiveClockBridge.h"
#include "../engine/builtin_fx/FxSpsc.h"
#include "../engine/core/StereoLatencyDelay.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>

static void check(bool ok) { if (!ok) std::abort(); }

static void hostileNumbers() {
    using W=armix::dsp::RealtimeStereoControlWord;
    constexpr auto inf=std::numeric_limits<float>::infinity();
    const float nan=std::numeric_limits<float>::quiet_NaN();
    for(float x:{nan,inf,-inf,-1.f,-std::numeric_limits<float>::max(),0.f})
        check(W::quantize(x)==0);
    for(float x:{4.f,5.f,std::numeric_limits<float>::max()})
        check(W::quantize(x)==W::gainMask);
    const auto state=W::decode(W::encode(nan,100000.f,false,true));
    check(state.left==0.f && state.right==4.f && !state.mute && state.solo);
    for(int i=0;i<=4000;++i) {
        const float x=i/1000.f;
        const auto s=W::decode(W::encode(x,x,true,false));
        check(std::abs(s.left-x)<1e-6f && s.left==s.right && s.mute && !s.solo);
    }
    using S=armix::device::ClockBridgeSizing;
    const auto normal=S::choose(48000,48000,480,480,48,32768,24.);
    check(normal.valid && normal.minFrames==1060);
    constexpr auto imax=std::numeric_limits<int>::max();
    constexpr auto huge=std::numeric_limits<double>::max();
    check(!S::choose(48000,48000,480,480,imax,32768,24.).valid);
    check(!S::choose(huge,huge,480,480,48,32768,24.).valid);
    check(!S::choose(48000,48000,imax,imax,48,32768,24.).valid);
    check(!S::choose(48000,48000,480,480,48,imax,24.).valid);
    for(int i=0;i<5000;++i) {
        const int period=1+i*97;
        const auto s=S::choose(44100,48000,period,period,48,32768,24.);
        if(s.valid)check(s.minFrames<16384 && std::isfinite(s.targetMs));
    }
}

static void queueStress() {
    struct Packet { std::uint32_t sequence=0, guard=0; };
    armix::builtin_fx::FxSpsc<Packet,32> q;
    Packet p;
    check(!q.pop(p));
    for(std::uint32_t i=0;i<32;++i)check(q.push({i,~i}));
    check(!q.push({32,~32u}));
    for(std::uint32_t i=0;i<32;++i)check(q.pop(p)&&p.sequence==i&&p.guard==~i);
    check(q.empty());
    // Actual concurrent SPSC producer/consumer; spin only on TEST threads.
    constexpr std::uint32_t count=200000;
    std::thread producer([&]{
        for(std::uint32_t i=0;i<count;++i)
            while(!q.push({i,~i}))std::this_thread::yield();
    });
    for(std::uint32_t i=0;i<count;++i) {
        while(!q.pop(p))std::this_thread::yield();
        check(p.sequence==i&&p.guard==~i);
    }
    producer.join();
    check(q.empty());
}

static void bufferBoundaries() {
    armix::device::AdaptiveClockBridge bridge;
    std::array<float,128> left{},right{},in{};
    in.fill(.125f);
    const float* inputs[]={in.data()};
    float* outputs[]={left.data(),right.data()};
    left.fill(1.f);right.fill(1.f);
    bridge.pull(outputs,2,128);
    for(int i=0;i<128;++i)check(left[i]==0.f&&right[i]==0.f);
    check(bridge.prepare(48000,48000,1,2048,12.));
    check(!bridge.prepare(48000,48000,17,2048,12.));
    check(!bridge.prepare(48000,48000,1,2049,12.));
    // Failed control preparation leaves prior valid storage and format intact.
    for(int block=0;block<100;++block) {
        check(bridge.push(inputs,1,128)==128);
        bridge.pull(outputs,2,128);
        for(int i=0;i<128;++i)check(std::isfinite(left[i])&&left[i]==right[i]);
    }
    check(bridge.prepare(44100,48000,2,4096,24.));
    check(bridge.status().pushedFrames==0);
    check(bridge.push(nullptr,2,128)==0);
    check(bridge.push(inputs,0,128)==0);
    check(bridge.push(inputs,1,-1)==0);
    armix::dsp::StereoLatencyDelay delay;
    check(!delay.prepare(-1));
    check(!delay.prepare(std::numeric_limits<int>::max()));
    check(delay.prepare(1));
    float l=.25f,r=-.25f;
    delay.process(l,r);check(l==0.f&&r==0.f);
    l=.5f;r=-.5f;delay.process(l,r);check(l==.25f&&r==-.25f);
    check(delay.prepare(0));
    delay.process(l,r);check(l==.25f&&r==-.25f);
}

int main() {
    hostileNumbers();
    queueStress();
    bufferBoundaries();
    std::cout<<"ZBSH07_NATIVE_BOUNDARY_PASS\n";
}
