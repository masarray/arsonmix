// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/PreparedMixerBlock.h"
#include "../engine/core/RealtimeRoutePublisher.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <tuple>
#include <utility>
using namespace armix::mixer;
static std::atomic<unsigned long> allocations{0};
void* operator new(std::size_t n) {
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(auto* p=std::malloc(n==0?1:n))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
static bool near(float a,float b) {return std::abs(a-b)<.0002f;}
int main() {
    constexpr std::array physical{2,5,9};
    ChannelRoutingConfig cfg;
    cfg.stripsAndMaster.strips[0]={SourceMode::mono,2,-1,1.f,0.f,false,false};
    cfg.stripsAndMaster.strips[1]={SourceMode::disconnected,-1,-1,1.f,0.f,false,false};
    cfg.stripsAndMaster.strips[2]={SourceMode::stereo,5,9,1.f,0.f,false,false};
    cfg.formats[1]=ChannelFormat::stereo;
    SendRequest send;
    send.toMaster[0]=false;
    send.count=1;
    send.sends[0]={0,1,SendTap::preFader,.5f,0.f,true};
    PreparedRenderGraph plan;RenderError error;
    assert(PreparedRenderGraph::compile(cfg,physical,send,plan,error));
    assert(plan.scheduled[0]&&plan.scheduled[1]&&plan.scheduled[2]);
    PreparedMixerBlock block(plan,128);
    RealtimeStripControls controls;
    assert(controls.set(0,1.f,0.f,true,false,false));
    assert(controls.set(1,1.f,0.f,false,false,false));
    assert(controls.set(2,1.f,0.f,false,false,false));
    assert(!controls.set(10,1.f,0.f,false,false,false));
    assert(!controls.set(1,1.f,20.f,false,false,false));
    const std::array<float,128> inA=[] {std::array<float,128>a{};a.fill(.8f);return a;}();
    const std::array<float,128> inB=[] {std::array<float,128>a{};a.fill(.2f);return a;}();
    const std::array<float,128> inC=[] {std::array<float,128>a{};a.fill(.3f);return a;}();
    const float* inputs[]={inA.data(),inB.data(),inC.data()};
    std::array<float,128> left{},right{};
    const auto noFx=[](int,float*,float*,int) noexcept {};
    auto run=[&] {
        const auto before=allocations.load(std::memory_order_relaxed);
        assert(block.process(inputs,3,left.data(),right.data(),128,noFx,nullptr,&controls));
        assert(allocations.load(std::memory_order_relaxed)==before);
        return std::pair{left[127],right[127]};
    };
    // Mono PRE uses equal-power center pan (-3 dB per side).
    // CH1 .8 * send .5 * sqrt(.5) plus CH3 stereo .2/.3.
    constexpr float monoPre=.8f*.5f*std::sqrt(.5f);
    auto [l,r]=run();
    assert(near(l,.2f+monoPre)&&near(r,.3f+monoPre));
    assert(controls.set(0,0.f,0.f,true,false,false));
    std::tie(l,r)=run();
    assert(near(l,.2f+monoPre)&&near(r,.3f+monoPre)); // fader 0 does NOT kill PRE
    assert(controls.set(0,0.f,0.f,true,true,false));
    std::tie(l,r)=run();
    assert(near(l,.2f)&&near(r,.3f)); // MUTE does kill PRE
    assert(controls.set(0,0.f,0.f,true,false,true));
    std::tie(l,r)=run();
    assert(near(l,monoPre)&&near(r,monoPre)); // SOLO keeps CH1 and BUS descendant
    assert(controls.set(0,0.f,0.f,true,false,false));
    assert(controls.set(2,1.f,-1.f,false,false,false));
    std::tie(l,r)=run();
    assert(near(l,.2f+monoPre)&&near(r,monoPre)); // stereo left balance only
    assert(controls.set(1,0.f,0.f,false,false,false));
    std::tie(l,r)=run();
    assert(near(l,.2f)&&near(r,0.f)); // bus fader updates without graph build
    assert(controls.set(1,1.f,0.f,false,false,false));
    std::tie(l,r)=run();
    assert(near(l,.2f+monoPre)&&near(r,monoPre));

    // Revision/epoch-fenced control editing leaves unchanged topology and
    // the same instance intact (one writer, no plugin publication/recreate).
    struct LiveNode {RealtimeStripControls params;int identity=42;};
    using Pub=armix::rt::RealtimeRoutePublisher<LiveNode>;
    Pub publisher;
    auto snap=std::make_unique<Pub::Snapshot>();
    snap->deviceEpoch=4;snap->graphRevision=1;
    snap->prepared=std::make_unique<LiveNode>();
    assert(publisher.publish(std::move(snap),0,4)==Pub::Result::applied);
    const auto update=[](LiveNode& x) noexcept {
        return x.params.set(3,.5f,.2f,false,false,false);
    };
    assert(!publisher.updateActiveControl(3,1,update)); // old device
    assert(!publisher.updateActiveControl(4,0,update)); // old revision
    assert(publisher.updateActiveControl(4,1,update));
    {
        auto lease=publisher.acquire();
        assert(lease.get(4)&&lease.get(4)->prepared->identity==42);
        assert(near(lease.get(4)->prepared->params.at(3).left.load(),.4f)); // stereo pan +20% reduces LEFT
        assert(near(lease.get(4)->prepared->params.at(3).right.load(),.5f));
    }
    publisher.unpublish();
    assert(!publisher.updateActiveControl(4,1,update));
    assert(publisher.pollRetirement());
}
