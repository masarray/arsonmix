// SPDX-License-Identifier: AGPL-3.0-only
// IO-2B: real native ChannelId PCM graph, virtual A/B pin readback and
// BUS FX once-only. Deterministic no-hardware evidence, NOT dual-ASIO FAT.
#pragma once
#include "../../engine/device/QualifiedInputGraphBridge.h"
#include "../../engine/core/DynamicNativeSessionPublisher.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace armix::io2_graph_test {
namespace d=armix::device;
namespace m=armix::mixer;
static void require(bool ok,const char* reason) {
    if(!ok){std::fprintf(stderr,"IO2_NATIVE_GRAPH_FAIL %s\n",reason);std::abort();}
}
static bool near(float x,float y){return std::abs(x-y)<0.0002f;}
static m::ChannelEdit add(std::size_t index,m::ChannelRole role,
                          m::ProcessingFormat fmt) {
    m::ChannelEdit e;e.kind=m::ChannelEdit::Kind::add;e.position=index;
    e.role=role;e.format=fmt;return e;
}
static m::ChannelEdit out(m::ChannelId from,m::ChannelId to) {
    m::ChannelEdit e;e.kind=m::ChannelEdit::Kind::setPrimaryOutput;
    e.id=from;e.target=to;return e;
}
inline void run() {
    constexpr std::uint64_t epoch=1200;
    constexpr int frames=128;
    m::DynamicChannelRoster roster;
    const std::array<m::ChannelEdit,4> create{{
        add(0,m::ChannelRole::input,m::ProcessingFormat::mono),
        add(1,m::ChannelRole::input,m::ProcessingFormat::stereo),
        add(2,m::ChannelRole::group,m::ProcessingFormat::stereo),
        add(3,m::ChannelRole::main,m::ProcessingFormat::stereo)
    }};
    const auto added=roster.edit(0,create);
    require(added.ok()&&added.createdIds.size()==4,"create real ChannelIds");
    const auto a=added.createdIds[0],b=added.createdIds[1],
               bus=added.createdIds[2],main=added.createdIds[3];
    const std::array<m::ChannelEdit,3> routes{{out(a,bus),out(b,bus),out(bus,main)}};
    require(roster.edit(roster.revision(),routes).ok(),"one WIRE topology");
    const std::array<int,2> aPins{0,2},bPins{0,7};
    const std::array<d::CaptureEndpointView,2> devices{{
        {"vendor-A/native-verified",epoch,true,aPins,101},
        {"vendor-B/native-verified",epoch,false,bPins,202}
    }};
    const std::array<d::QualifiedChannelSource,2> bindings{{
        {a,{"vendor-A/native-verified",2},{},false},
        {b,{"vendor-B/native-verified",0},{"vendor-B/native-verified",7},true}
    }};
    d::MultiDeviceInputPlan plan;
    d::MultiDeviceInputFault mapFault;
    require(d::MultiDeviceInputPlan::prepare(epoch,devices,bindings,
        roster.channels(),plan,mapFault),"qualified CONTROL input plan");
    require(plan.virtualInputCount()==4&&plan.sourceCount()==2,
            "two identical physical Input1 IDs become disjoint ordinals");

    // Existing physical A is preserved; B is added. Player/deck sources and
    // legacy routing are NOT deleted by a device-only topology transition.
    m::DynamicGraphRequest old;old.physicalInputs=2;old.maxFrames=frames;
    old.sources={{a,1,-1,false}};
    m::DynamicGraphRequest candidate;
    d::QualifiedInputGraphBridge::Fault fault;
    require(d::QualifiedInputGraphBridge::stage(plan,old,candidate,fault),
            "all-or-nothing source map -> one existing graph request");
    require(candidate.physicalInputs==4 && candidate.sources.size()==2 &&
            candidate.sources[0].id==a&&candidate.sources[0].inputL==1 &&
            candidate.sources[1].id==b&&candidate.sources[1].inputL==2 &&
            candidate.sources[1].inputR==3,
            "channelId-independent physical ordinal mapping");
    const auto accepted=candidate;
    auto draft=plan; // purely CONTROL; never publish a copied plan on AUDIO

    // A missing previously accepted source must not silently erase A.
    const std::array<d::QualifiedChannelSource,1> onlyB{bindings[1]};
    require(d::MultiDeviceInputPlan::prepare(epoch,devices,onlyB,
        roster.channels(),draft,mapFault),"alternate detached plan");
    require(!d::QualifiedInputGraphBridge::stage(draft,accepted,candidate,fault)
            && fault==d::QualifiedInputGraphBridge::Fault::unqualifiedExistingSource,
            "failed update retains exact accepted graph request");
    require(candidate.physicalInputs==4&&candidate.sources.size()==2,
            "negative update cannot mutate prior candidate");
    m::DynamicGraphRequest playerCollision=old;
    playerCollision.sources.push_back({b,-1,-1,true,
        m::DynamicSourceKind::playerDeck,77});
    require(!d::QualifiedInputGraphBridge::stage(plan,playerCollision,candidate,fault)
            && fault==d::QualifiedInputGraphBridge::Fault::playerSourceCollision,
            "never steal a virtual Player source's ChannelId");
    m::DynamicGraphRequest duplicate=old;
    duplicate.sources.push_back(old.sources.front());
    require(!d::QualifiedInputGraphBridge::stage(plan,duplicate,candidate,fault)
            && fault==d::QualifiedInputGraphBridge::Fault::duplicateSource,
            "duplicate physical patch rejects without mutation");

    m::DynamicNativeSessionPublisher native;
    const auto committed=native.commit(roster,accepted,0,epoch,epoch,true,
                                       {},false,false,&plan);
    require(committed.ok()&&committed.revision==1,"one native graph publication");
    std::array<float,frames> a0{},a2{},b0{},b7{},l{},r{};
    a0.fill(.83f);a2.fill(.2f);b0.fill(.4f);b7.fill(.6f);
    const float* primary[]{a0.data(),a2.data()};
    const float* secondary[]{b0.data(),b7.data()};
    std::array<d::MultiDeviceInputPlan::CaptureBlock,2> blocks{{
        {primary,2,frames,epoch,true,101},
        {secondary,2,frames,epoch,true,202}
    }};
    d::MultiDeviceInputPlan::VirtualFrame virtualFrame;
    const auto render=[&](int& busFxCalls) {
        require(plan.assemble(epoch,frames,blocks,virtualFrame),
                "same render-clock virtual frame");
        auto fx=[&](m::ChannelId id,float* ll,float* rr,int n) noexcept {
            if(id==bus){
                ++busFxCalls;
                for(int i=0;i<n;++i){ll[i]*=2.f;rr[i]*=2.f;}
            }
        };
        return native.render(epoch,nullptr,0,l.data(),r.data(),frames,false,fx,
            nullptr,nullptr,{},{},{},nullptr,0,blocks);
    };
    int fxCalls=0;
    require(render(fxCalls)&&fxCalls==1,"A+B -> BUS FX once -> MAIN");
    // A source-map change cannot mutate a published graph unless the
    // qualified map and its exact virtual sources are admitted together.
    const auto rejected=native.commit(roster,accepted,1,epoch,epoch,true,
                                      {},false,false,&draft);
    require(rejected.fault==m::DynamicNativeSessionPublisher::Fault::invalidCandidate
            &&native.revision()==1,
            "rejected remap retains old immutable PCM + FX lease");
    constexpr float center=0.7071067811865475f;
    require(near(l[frames-1],2.f*(.2f*center+.4f))&&
            near(r[frames-1],2.f*(.2f*center+.6f)),
            "real PCM from device A and B reaches MAIN separately");
    // Device B unplug/fault is isolated; never substitute A Input1 for B.
    blocks[1].admitted=false;
    require(render(fxCalls)&&fxCalls==2,"B dropout never stalls A FX");
    require(near(l[frames-1],2.f*.2f*center)&&
            near(r[frames-1],2.f*.2f*center),
            "A remains audible while B silent");
    blocks[1].admitted=true;blocks[1].deviceLease=101;
    require(render(fxCalls)&&fxCalls==3,"equal epoch but swapped B lease renders");
    require(near(l[frames-1],2.f*.2f*center),
            "swapped device lease cannot leak B to MAIN");
    blocks[1].deviceLease=202;
    require(!plan.assemble(epoch+1,frames,blocks,virtualFrame) &&
            virtualFrame.count==0,"changed primary render epoch invalidates all");
    std::puts("IO2_NATIVE_GRAPH_PASS: A+B PCM -> ChannelId -> BUS FX once -> MAIN; stale B isolated");
}
} // namespace armix::io2_graph_test
