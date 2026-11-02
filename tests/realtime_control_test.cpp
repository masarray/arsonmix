// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/RealtimeControl.h"
#include "../engine/core/PreparedMixerBlock.h"
#include "../engine/core/RealtimeRoutePublisher.h"
#include "../engine/core/NativeRenderAuthority.h"
#include <memory>
#include <atomic>
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include "../src/StereoPan.h"
#include "../src/LiveGraphHandoff.h"
#include <cmath>
#include <iostream>
#include <thread>
using namespace armix::rt;
int main(){
    // Production cannot fall through to a second stereo/VST mixer. The
    // only old stereo path is explicitly test/diagnostic and disconnected
    // from an accepted native M4 route.
    using Owner=armix::mixer::NativeRenderOwner;
    using Authority=armix::mixer::NativeRenderAuthority;
    assert(Authority::select(true,false)==Owner::failSilentUnpatched);
    assert(Authority::select(true,true)==Owner::acceptedM4Session);
    assert(Authority::select(false,true)==Owner::acceptedM4Session);
    assert(Authority::select(false,false)==Owner::isolatedDiagnostic);
    assert(!Authority::mayRunDiagnostic(
        Authority::select(false,true)));

    // M5 LIVE (the installed M4 callback, not only prepared ChannelId):
    // all 4 controls change in ONE atomic, while 2 threads run 200k
    // opposing GUI gestures. RT must see either complete gesture A or B;
    // no left from A with right/MUTE/SOLO from B.
    using Controls=armix::mixer::RealtimeStripControls;
    Controls atomicControls;
    const auto u=atomicControls.read(2);
    assert(std::fabs(u.left-1.f)<1e-6f && std::fabs(u.right-1.f)<1e-6f);
    assert(!u.mute && !u.solo);
    assert(atomicControls.set(2,.5f,-1.f,false,false,true));
    const auto a=atomicControls.read(2);
    assert(std::fabs(a.left-.5f)<1e-6f && std::fabs(a.right)<1e-6f);
    assert(!a.mute && a.solo);
    assert(atomicControls.set(2,.8f,1.f,false,true,false));
    const auto b=atomicControls.read(2);
    assert(std::fabs(b.left)<1e-6f && std::fabs(b.right-.8f)<1e-6f);
    assert(b.mute && !b.solo);
    std::atomic<bool> badMixed{false};
    constexpr int gestures=200000;
    std::thread ctl([&]{
        for(int k=0;k<gestures;++k)
            if(!atomicControls.set(2,(k&1)?.8f:.5f,(k&1)?1.f:-1.f,
                                   false,(k&1)!=0,(k&1)==0))
                badMixed.store(true,std::memory_order_relaxed);
    });
    std::thread audioControlRead([&] {
        for(int k=0;k<gestures;++k) {
            const auto view=atomicControls.read(2);
            const bool matchesA=std::fabs(view.left-a.left)<1e-6f &&
                std::fabs(view.right-a.right)<1e-6f &&
                view.mute==a.mute && view.solo==a.solo;
            const bool matchesB=std::fabs(view.left-b.left)<1e-6f &&
                std::fabs(view.right-b.right)<1e-6f &&
                view.mute==b.mute && view.solo==b.solo;
            if(!matchesA&&!matchesB)
                badMixed.store(true,std::memory_order_relaxed);
        }
    });
    ctl.join();audioControlRead.join();
    assert(!badMixed);
    assert(!atomicControls.set(2,-1.f,0.f,false,false,false));
    assert(!atomicControls.set(2,1.f,2.f,false,false,false));
    assert(!atomicControls.set(99,1.f,0.f,false,false,false));
    std::cout<<"PASS: 200000 concurrent native M4 GUI gestures read as atomic stereo/mute/solo blocks\\n";

    SpscQueue<int,8> q;
    int value=-1;
    assert(!q.tryPop(value));
    for(int i=0;i<7;i++) assert(q.tryPush(i));
    assert(!q.tryPush(999));
    for(int i=0;i<7;i++){assert(q.tryPop(value));assert(value==i);}
    assert(!q.tryPop(value));
    for(int i=0;i<120000;i++){assert(q.tryPush(i));assert(q.tryPop(value));assert(value==i);}

    ParameterBank<32> params;
    float read=0;std::uint32_t revision=0;
    assert(!params.readChanged(5,revision,read));
    params.publish(5,0.1f);params.publish(5,0.3f);params.publish(5,0.7f);
    assert(params.readChanged(5,revision,read)&&std::fabs(read-0.7f)<0.00001f);
    assert(!params.readChanged(5,revision,read));
    // Concurrent ordered SPSC sequence verifies visibility across 2 native threads.
    SpscQueue<std::uint32_t,1024> concurrent;
    constexpr std::uint32_t count=250000;
    std::atomic<bool> bad{false};
    std::thread producer([&]{for(std::uint32_t i=0;i<count;){if(concurrent.tryPush(i))++i;else std::this_thread::yield();}});
    std::thread consumer([&]{for(std::uint32_t i=0;i<count;){std::uint32_t v=0;if(concurrent.tryPop(v)){if(v!=i)bad=true;++i;}else std::this_thread::yield();}});
    producer.join();consumer.join();assert(!bad);
    // Test the exact pan matrix used by the production audio callback.
    auto checkPan=[](float p,float left,float right,float expectedL,float expectedR)
    {
        const auto m=armix::dsp::StereoPanMatrix::fromNormalized (p);
        const float l=m.ll*left+m.lr*right, r=m.rl*left+m.rr*right;
        assert(std::fabs(l-expectedL)<0.00001f);
        assert(std::fabs(r-expectedR)<0.00001f);
    };
    checkPan(0.f,.8f,.25f,.8f,.25f);  // transparent stereo centre
    checkPan(-1.f,1.f,1.f,1.f,0.f);    // dual mono at hard left = unity
    checkPan(1.f,1.f,1.f,0.f,1.f);     // dual mono at hard right = unity
    checkPan(-1.f,.8f,.2f,.5f,0.f);   // stereo fold
    checkPan(1.f,.8f,.2f,0.f,.5f);
    checkPan(.5f,.8f,.2f,.4f,.35f);
    checkPan(0.f,0.f,1.f,0.f,1.f);
    checkPan(0.f,1.f,0.f,1.f,0.f);
    std::cout<<"PASS: native stereo pan centre, extremes, half-pan and unit-gain dual-mono\n";

    // M1: retired FX/PCM snapshots remain owned until callbacks release
    // their leases; a full bounded queue rejects instead of growing memory.
    struct RetireProbe {
        std::atomic<int>* counter;
        explicit RetireProbe(std::atomic<int>& c):counter(&c){}
        ~RetireProbe(){counter->fetch_add(1,std::memory_order_relaxed);}
    };
    std::atomic<int> destroyed{0};
    RealtimeRoutePublisher<RetireProbe,2> publisher;
    const auto publish=[&](std::uint64_t revision) {
        auto next=std::make_unique<RealtimeRoutePublisher<RetireProbe,2>::Snapshot>();
        next->deviceEpoch=7;next->graphRevision=revision;
        next->prepared=std::make_unique<RetireProbe>(destroyed);
        return publisher.publish(std::move(next),revision-1,7);
    };
    using Result=RealtimeRoutePublisher<RetireProbe,2>::Result;
    assert(publish(1)==Result::applied);
    {
        auto lease=publisher.acquire();
        assert(lease.get(7)&&lease.get(7)->graphRevision==1);
        assert(publish(2)==Result::applied);
        assert(publish(3)==Result::applied);
        assert(publisher.pendingRetirements()==2);
        assert(publish(4)==Result::retirementFull);
        assert(destroyed.load(std::memory_order_relaxed)==1);
        assert(lease.get(7)->graphRevision==1);
    }
    assert(publisher.pollRetirement());
    assert(publisher.pendingRetirements()==0);
    assert(destroyed.load(std::memory_order_relaxed)==3);
    assert(publish(4)==Result::applied);
    publisher.unpublish();
    assert(publisher.pollRetirement());
    // Three accepted snapshots + one rejected admission + final fourth
    // accepted snapshot: all five owners are reclaimed (no leak/double free).
    assert(destroyed.load(std::memory_order_relaxed)==5);
    // Runtime contract for the exact RT graph publication helper.
    struct FakeGraph { int id; };
    FakeGraph old{1}, next{2}, third{3};
    armix::rt::LiveGraphHandoff<FakeGraph> handoff;
    handoff.initialize (&old);
    assert(!handoff.busy() && handoff.current()==&old);
    assert(handoff.stage(&next) && !handoff.stage(&third));
    auto first=handoff.beginCycle();
    assert(first.graph==&old && first.fadeOut && !first.fadeIn);
    assert(armix::rt::linearBufferRamp(0,128,true,false)==1.f);
    assert(armix::rt::linearBufferRamp(127,128,true,false)==0.f);
    handoff.endCycle(first);
    assert(handoff.current()==&next && handoff.busy());
    assert(!handoff.stage(&third));
    assert(handoff.takeRetired()==&old);
    // Retirement acknowledgement stays pending until old owners are actually
    // destroyed by control; no premature second stage in the handoff gap.
    assert(!handoff.stage(&third));
    handoff.completeRetirement();
    assert(handoff.stage(&third));
    auto second=handoff.beginCycle();
    assert(second.graph==&next && second.fadeOut && second.fadeIn);
    handoff.endCycle(second);
    assert(handoff.takeRetired()==&next);
    handoff.completeRetirement();
    assert(handoff.current()==&third);
    auto last=handoff.beginCycle();
    assert(!last.fadeOut && last.fadeIn);
    assert(armix::rt::linearBufferRamp(0,128,false,true)==0.f);
    assert(armix::rt::linearBufferRamp(127,128,false,true)==1.f);
    handoff.endCycle(last);
    // Repeat thousands of publications on another callback thread and
    // consume the acknowledgement only from the control thread.
    armix::rt::LiveGraphHandoff<FakeGraph> threaded;
    threaded.initialize (&old);
    constexpr int rounds=12000;
    std::atomic<bool> ended{false};
    std::thread audio([&] {
        while(!ended.load(std::memory_order_acquire)) {
            auto cycle=threaded.beginCycle();
            if(cycle.fadeOut)threaded.endCycle(cycle);
            else std::this_thread::yield();
        }
    });
    for(int i=0;i<rounds;++i){
        FakeGraph* candidate=(i%2)==0?&next:&old;
        while(!threaded.stage(candidate))std::this_thread::yield();
        FakeGraph* retired=nullptr;
        while(!(retired=threaded.takeRetired()))std::this_thread::yield();
        assert(retired!=candidate);
        threaded.completeRetirement();
    }
    ended.store(true,std::memory_order_release);
    audio.join();
    assert(!threaded.busy());
    std::cout<<"PASS: live graph 12000 async handoffs, bounded ramps, retire-before-reclaim\n";
    std::cout<<"PASS: SPSC capacity, wrap, 250000 ordered interthread events, parameter coalescing\n";
}
