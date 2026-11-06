// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/DynamicAcceptedSession.h"
#include "../engine/core/RealtimeSlew64.h"
#include <array>
#if defined(NDEBUG)
#undef NDEBUG // Native Release CTest must actually run assertions.
#endif
#include <cassert>
#include <cmath>
#include <limits>
#include <cstdio>
#include <vector>

using namespace armix::mixer;
static bool near(float a,float b,float eps=3e-5f) {
    return std::abs(a-b)<eps;
}
static ChannelEdit add(std::size_t position,ChannelRole role,const char* name,
                       ProcessingFormat format=ProcessingFormat::mono) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::add;e.position=position;
    e.role=role;e.format=format;e.text=name;return e;
}
static ChannelEdit output(ChannelId from,ChannelId to) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::setPrimaryOutput;
    e.id=from;e.target=to;return e;
}
static ChannelEdit send(ChannelId from,ChannelId to,float gain,bool pre=false) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::addSendReference;
    e.id=from;e.target=to;e.value=gain;e.preFader=pre;return e;
}

static void verifyRealtimeSlewAndNativeChannelIdContinuousControls() {
    using armix::dsp::RealtimeSlew64;
    // Sub-64 variable callbacks must not finish a 64-sample gesture in 4
    // frames. Coalescing multiple UI writes before a read keeps latest goal.
    RealtimeSlew64 s;
    s.reset(1.f);
    s.retarget(.2f);
    s.retarget(.5f); // replaces the old unrendered target; no queue buildup
    for(int i=0;i<4;++i)s.next();
    assert(near(s.current,1.f-(.5f*4.f/64.f)));
    for(int i=0;i<16;++i)s.next();
    assert(near(s.current,1.f-(.5f*20.f/64.f)));
    for(int i=0;i<32;++i)s.next();
    assert(near(s.current,1.f-(.5f*52.f/64.f)));
    for(int i=0;i<12;++i)s.next();
    assert(s.remaining==0 && s.current==.5f);
    s.retarget(0.f);
    for(int i=0;i<20;++i)s.next();
    const float applied=s.current;
    s.retarget(.75f); // reverse while in flight; no jump on target update
    assert(s.current==applied);
    assert(s.next()>applied);
    for(int i=1;i<64;++i)s.next();
    assert(s.current==.75f && s.remaining==0);

    // The approved GUI sends ChannelId-keyed realtime moves. Verify actual
    // PCM through GROUP-compatible Main, PRE wet Return and POST Monitor,
    // not a disconnected smoothing helper or browser-only fixture.
    DynamicAcceptedSession session;
    constexpr std::uint64_t epoch=9401;
    DynamicGraphRequest route;
    route.physicalInputs=1;
    route.maxFrames=64;
    std::array<ChannelEdit,4> creates{
        add(0,ChannelRole::input,"VOICE"),
        add(1,ChannelRole::fxReturn,"WET",ProcessingFormat::stereo),
        add(2,ChannelRole::main,"MAIN",ProcessingFormat::stereo),
        add(3,ChannelRole::monitor,"CUE",ProcessingFormat::stereo)
    };
    auto a=session.edit(0,0,creates,route,epoch,epoch,true);
    assert(a.ok());
    const auto voice=a.edit.createdIds[0];
    const auto wet=a.edit.createdIds[1];
    const auto main=a.edit.createdIds[2];
    const auto cue=a.edit.createdIds[3];
    std::array<ChannelEdit,4> links{
        output(voice,main),
        output(wet,main),
        send(voice,wet,.5f,true),
        send(voice,cue,.5f,false)
    };
    route.sources={{voice,0,-1,false}};
    a=session.edit(1,1,links,route,epoch,epoch,true);
    assert(a.ok()&&session.acceptedGraphRevision()==2);
    std::array<float,64> input{},outL{},outR{},cueL{},cueR{};
    input.fill(.5f);
    const float* pins[]{input.data()};
    const auto fx=+[](ChannelId,float*,float*,int) noexcept {};
    const auto render=[&](int n) {
        return session.render(epoch,pins,1,outL.data(),outR.data(),n,
                              false,fx,cueL.data(),cueR.data());
    };
    assert(render(64));
    const float initialMain=outL[63];
    const float initialCue=cueL[63];
    assert(initialMain>.1f && initialCue>.01f);
    assert(session.setLiveMix(epoch,2,voice,0.f,0.f));
    assert(session.setLiveParallelSend(epoch,2,voice,wet,true,0.f));
    assert(session.setLiveParallelSend(epoch,2,voice,cue,false,0.f));
    // PRE send, POST send and channel fader must all advance across
    // 4+16+32+12 (one complete ramp), not finish on the FIRST 4 samples.
    assert(render(4));
    const float after4=outL[3],cue4=cueL[3];
    assert(after4>0.f && after4<initialMain);
    assert(cue4>0.f && cue4<initialCue);
    assert(render(16));
    const float after20=outL[15],cue20=cueL[15];
    assert(after20>0.f && after20<after4);
    assert(cue20>0.f && cue20<cue4);
    assert(render(32));
    assert(outL[31]<after20 && cueL[31]<cue20);
    assert(render(12));
    assert(near(outL[11],0.f,0.0001f));
    assert(near(outR[11],0.f,0.0001f));
    assert(near(cueL[11],0.f,0.0001f));
    assert(near(cueR[11],0.f,0.0001f));
    assert(session.acceptedGraphRevision()==2);
    // Rapid fader drags are naturally last-target-wins: no event queue,
    // structural graph recompile, callback heap or retained stale controls.
    // 4096 high-rate gestures all collapse into ONE latest atomic target.
    // This is a CPU/heap-bounded CONTROL lookup, not a 4096-event RT FIFO.
    for(int i=0;i<4096;++i)
        assert(session.setLiveMix(epoch,2,voice,(i&1)?.8f:.2f,0.f));
    assert(!session.setLiveMix(epoch,2,ChannelId{999999},1.f,0.f));
    assert(!session.setLiveParallelSend(epoch,2,ChannelId{999999},wet,true,.5f));
    assert(render(4));
    assert(outL[3]>0.f);
    assert(render(60));
    constexpr float center=.7071067811865475f;
    assert(near(outL[59],.5f*.8f*center,0.0001f));
    assert(!session.setLiveMix(epoch,1,voice,1.f,0.f));
    assert(!session.setLiveParallelSend(epoch-1,2,voice,wet,true,1.f));
    assert(session.acceptedGraphRevision()==2);
    session.unpublish();
    session.pollRetirement();
}

// Native WIRE PCM regression: source OUT → Group → MAIN must be an
// exclusive serial route, not two dry feeds. Rejected feedback preserves the
// previous audible graph. Disconnect really removes primary audio.
static void verifyConsoleWirePrimaryOutputPcm() {
    constexpr std::uint64_t epoch=9751;
    constexpr float center=.7071067811865475f;
    DynamicAcceptedSession session;
    DynamicGraphRequest request;
    request.physicalInputs=1;request.maxFrames=64;
    const std::array<ChannelEdit,3> created{
        add(0,ChannelRole::input,"CH11"),
        add(1,ChannelRole::group,"VOICE GROUP",ProcessingFormat::stereo),
        add(2,ChannelRole::main,"MAIN",ProcessingFormat::stereo)
    };
    auto result=session.edit(0,0,created,request,epoch,epoch,true);
    assert(result.ok()&&result.edit.createdIds.size()==3);
    const auto ch=result.edit.createdIds[0];
    const auto group=result.edit.createdIds[1];
    const auto main=result.edit.createdIds[2];
    const std::array<ChannelEdit,2> first{
        output(ch,main),output(group,main)
    };
    request.sources={{ch,0,-1,false}};
    result=session.edit(1,1,first,request,epoch,epoch,true);
    assert(result.ok()&&result.graphRevision==2);
    std::array<float,64> audio{},left{},right{};
    audio.fill(.35f);
    const float* inputs[]{audio.data()};
    const auto noFx=+[](ChannelId,float*,float*,int) noexcept {};
    const auto render=[&]{
        return session.render(epoch,inputs,1,left.data(),right.data(),64,false,noFx);
    };
    assert(render()&&near(left[63],.35f*center,1e-4f));
    const std::array<ChannelEdit,1> routeGroup{output(ch,group)};
    result=session.edit(session.acceptedRosterRevision(),2,routeGroup,
                        request,epoch,epoch,true);
    assert(result.ok()&&session.acceptedGraphRevision()==3);
    const auto* graph=session.acceptedRouting(epoch,3);
    assert(graph&&graph->find(ch)->primaryOutput==group);
    assert(graph->find(group)->primaryOutput==main);
    assert(render()&&near(left[63],.35f*center,1e-4f));

    // Group -> CH11 while CH11 -> Group is cyclic. A rejected native
    // publication must preserve CH11 -> Group -> MAIN and real PCM.
    const std::array<ChannelEdit,1> feedback{output(group,ch)};
    const auto rejected=session.edit(session.acceptedRosterRevision(),3,
                                     feedback,request,epoch,epoch,true);
    assert(!rejected.ok()&&session.acceptedGraphRevision()==3);
    assert(session.acceptedRouting(epoch,3)->find(group)->primaryOutput==main);
    assert(render()&&near(left[63],.35f*center,1e-4f));
    const std::array<ChannelEdit,1> disconnected{output(ch,ChannelId{})};
    result=session.edit(session.acceptedRosterRevision(),3,disconnected,
                        request,epoch,epoch,true);
    assert(result.ok()&&session.acceptedGraphRevision()==4);
    assert(!session.acceptedRouting(epoch,4)->find(ch)->primaryOutput.value);
    assert(render()&&near(left[63],0.f,1e-5f));
    // Reconnect to the same real Group, with exactly one primary route.
    result=session.edit(session.acceptedRosterRevision(),4,routeGroup,
                        request,epoch,epoch,true);
    assert(result.ok()&&session.acceptedGraphRevision()==5);
    assert(render()&&near(left[63],.35f*center,1e-4f));
    session.unpublish();
    session.pollRetirement();
}


// Native WIRE-first acceptance: exactly ONE immutable PCM graph owner.
// Direct, nested and reconvergent paths process within the SAME sample block;
// exact edgeId detach only affects its source, rejected DAG stays audible.
static void verifyAtomicWireTransactionAndZeroSampleHops() {
    constexpr std::uint64_t epoch=9909;
    constexpr float center=.7071067811865475f;
    DynamicAcceptedSession session;
    DynamicGraphRequest physical;
    physical.physicalInputs=1;physical.maxFrames=64;
    const std::array<ChannelEdit,4> create{
        add(0,ChannelRole::input,"INPUT"),
        add(1,ChannelRole::input,"SUM 1",ProcessingFormat::stereo),
        add(2,ChannelRole::input,"SUM 2",ProcessingFormat::stereo),
        add(3,ChannelRole::main,"MAIN",ProcessingFormat::stereo)
    };
    auto a=session.edit(0,0,create,physical,epoch,epoch,true);
    assert(a.ok() && a.edit.createdIds.size()==4);
    const auto one=a.edit.createdIds[0],two=a.edit.createdIds[1];
    const auto three=a.edit.createdIds[2],main=a.edit.createdIds[3];
    physical.sources={{one,0,-1,false}};
    auto connect=[](ChannelId from,ChannelId to,float gain=1.f) {
        WireChange w;w.kind=WireChange::Kind::connect;w.from=from;
        w.to=to;w.gain=gain;return w;
    };
    auto routeOnly=[](ChannelId from,ChannelId to) {
        WireChange w;w.kind=WireChange::Kind::routeOnly;
        w.from=from;w.to=to;return w;
    };
    auto remove=[](ChannelId from,std::uint64_t id) {
        WireChange w;w.kind=WireChange::Kind::remove;
        w.from=from;w.edgeId=id;return w;
    };
    const std::array<WireChange,3> initial{
        connect(one,main),connect(two,main),connect(three,main)
    };
    a=session.wireTransaction(1,1,initial,physical,epoch,epoch,true);
    assert(a.ok()&&a.graphRevision==2&&a.edit.createdEdgeIds.size()==3);
    const auto* graph=session.acceptedRouting(epoch,2);
    assert(graph&&graph->edges.size()==3);
    const auto findEdge=[&](ChannelId from,ChannelId to,
                            std::uint64_t revision){
        const auto* current=session.acceptedRouting(epoch,revision);
        assert(current);
        const auto it=std::find_if(current->edges.begin(),current->edges.end(),
            [&](const auto& edge){return edge.from==from && edge.to==to;});
        return it==current->edges.end()?std::uint64_t{0}:it->edgeId;
    };
    const auto originalMain=findEdge(one,main,2);
    const auto otherMain=findEdge(three,main,2);
    assert(originalMain!=0 && otherMain!=0 && originalMain!=otherMain);
    std::array<float,64> impulse{},left{},right{};
    impulse[11]=.3f;
    const float* inputs[]{impulse.data()};
    const auto noFx=+[](ChannelId,float*,float*,int) noexcept {};
    const auto render=[&]{
        return session.render(epoch,inputs,1,left.data(),right.data(),64,
                              false,noFx);
    };
    assert(render() && near(left[11],.3f*center));
    for(int i=0;i<64;++i)
        if(i!=11)assert(near(left[i],0.f));

    const std::array<WireChange,1> hop1{routeOnly(one,two)};
    a=session.wireTransaction(2,2,hop1,physical,epoch,epoch,false); // live PCM routing while armed
    assert(a.ok() && a.graphRevision==3);
    assert(!findEdge(one,main,3) && findEdge(one,two,3));
    assert(render() && near(left[11],.3f*center));

    const std::array<WireChange,1> hop2{routeOnly(two,three)};
    a=session.wireTransaction(3,3,hop2,physical,epoch,epoch,true);
    assert(a.ok() && a.graphRevision==4);
    const auto chainId=findEdge(one,two,4);
    assert(chainId && !findEdge(two,main,4));
    assert(findEdge(three,main,4)==otherMain);
    assert(render() && near(left[11],.3f*center));
    for(int i=0;i<64;++i)
        if(i!=11)assert(near(left[i],0.f));

    // A second real path doubles PCM only by explicit legal fan-out.
    const std::array<WireChange,1> parallel{connect(one,main)};
    a=session.wireTransaction(4,4,parallel,physical,epoch,epoch,true);
    assert(a.ok() && a.graphRevision==5);
    const auto directId=findEdge(one,main,5);
    assert(directId && directId!=originalMain);
    assert(render() && near(left[11],2.f*.3f*center,1e-4f));
    const std::array<WireChange,1> unplug{remove(one,directId)};
    a=session.wireTransaction(5,5,unplug,physical,epoch,epoch,true);
    assert(a.ok() && a.graphRevision==6);
    assert(!findEdge(one,main,6) && findEdge(three,main,6)==otherMain);
    assert(render() && near(left[11],.3f*center));

    // Wrong edge identity cannot disconnect another source's MAIN.
    const std::array<WireChange,1> wrongEdge{remove(one,otherMain)};
    auto rejected=session.wireTransaction(6,6,wrongEdge,physical,epoch,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::editRejected);
    assert(session.acceptedGraphRevision()==6);
    assert(render() && near(left[11],.3f*center));
    const std::array<WireChange,1> duplicate{connect(one,two)};
    rejected=session.wireTransaction(6,6,duplicate,physical,epoch,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::editRejected);
    assert(session.acceptedGraphRevision()==6);

    // Mixed existing route cycle: whole native DAG compile must roll back.
    const std::array<WireChange,1> feedback{connect(three,one)};
    rejected=session.wireTransaction(6,6,feedback,physical,epoch,epoch,true);
    if(rejected.fault!=DynamicAcceptedSession::Fault::graphRejected)
        std::fprintf(stderr,"WIRE feedback rejection diagnostic: acceptedFault=%u rosterFault=%u nativeFault=%u graphFault=%u revision=%llu\\n",
            static_cast<unsigned>(rejected.fault),
            static_cast<unsigned>(rejected.edit.fault),
            static_cast<unsigned>(rejected.native.fault),
            static_cast<unsigned>(rejected.native.graphError.fault),
            static_cast<unsigned long long>(rejected.graphRevision));
    assert(rejected.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(rejected.edit.createdEdgeIds.empty()); // no phantom rejected ID
    assert(session.acceptedGraphRevision()==6);
    assert(render() && near(left[11],.3f*center));
    // Preserve output safety: the same WIRE edit with a changed hardware
    // block budget is rejected BEFORE the native PCM graph is published.
    DynamicGraphRequest changedPhysical=physical;
    changedPhysical.maxFrames=128;
    const std::array<WireChange,1> validTopology{connect(two,main)};
    rejected=session.wireTransaction(6,6,validTopology,changedPhysical,
                                     epoch,epoch,false);
    assert(rejected.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(rejected.native.fault==DynamicNativeSessionPublisher::Fault::outputArmed);
    assert(rejected.edit.createdEdgeIds.empty());
    assert(session.acceptedGraphRevision()==6);
    assert(render() && near(left[11],.3f*center));
    rejected=session.wireTransaction(5,5,unplug,physical,epoch,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::staleRoster);
    assert(session.acceptedGraphRevision()==6);
    rejected=session.wireTransaction(6,6,unplug,physical,epoch+1,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(rejected.native.fault==DynamicNativeSessionPublisher::Fault::staleDevice);
    assert(rejected.edit.createdEdgeIds.empty());
    // A genuinely valid edge must also reject the wrong epoch before
    // publication, irrespective of the edit's own validity.
    const std::array<WireChange,1> validButStale{connect(one,main)};
    rejected=session.wireTransaction(6,6,validButStale,physical,epoch+1,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(rejected.native.fault==DynamicNativeSessionPublisher::Fault::staleDevice);
    assert(session.acceptedGraphRevision()==6);
    assert(render() && near(left[11],.3f*center));

    // RETARGET keeps identity and gain, not a new/ghost wire.
    WireChange retarget;
    retarget.kind=WireChange::Kind::retarget;
    retarget.from=one;retarget.to=main;retarget.edgeId=chainId;
    const std::array<WireChange,1> reroute{retarget};
    a=session.wireTransaction(6,6,reroute,physical,epoch,epoch,true);
    assert(a.ok() && a.graphRevision==7);
    assert(findEdge(one,main,7)==chainId && !findEdge(one,two,7));
    assert(render() && near(left[11],.3f*center));
    session.unpublish();
    session.pollRetirement();
}


// New native channel = exactly one CH->MAIN edge in ONE accepted revision,
// even when MAIN appears later in the same initial transaction. Restoring
// an intentionally disconnected older channel never auto-repairs its route.
static void verifyDefaultNativeMainEdgeAndDisconnectedRestore() {
    constexpr std::uint64_t epoch=9910;
    DynamicAcceptedSession session;
    DynamicGraphRequest physical;
    physical.maxFrames=64;
    auto source=add(0,ChannelRole::input,"NEW WIRE");
    source.connectMainOnCreate=true;
    const auto main=add(1,ChannelRole::main,"MAIN",ProcessingFormat::stereo);
    const std::array<ChannelEdit,2> first{source,main};
    auto a=session.edit(0,0,first,physical,epoch,epoch,true);
    assert(a.ok() && a.edit.createdIds.size()==2);
    const auto ch=a.edit.createdIds[0],master=a.edit.createdIds[1];
    const auto* routing=session.acceptedRouting(epoch,1);
    assert(routing && routing->edges.size()==1);
    const auto id=routing->edges.front().edgeId;
    assert(id && routing->edges.front().from==ch &&
           routing->edges.front().to==master &&
           routing->edges.front().primaryOutput &&
           routing->edges.front().gainLinear==1.f);
    WireChange unplug;unplug.kind=WireChange::Kind::remove;
    unplug.from=ch;unplug.edgeId=id;
    const std::array<WireChange,1> batch{unplug};
    a=session.wireTransaction(1,1,batch,physical,epoch,epoch,true);
    assert(a.ok() && a.graphRevision==2);
    assert(session.acceptedRouting(epoch,2)->edges.empty());
    auto newer=add(1,ChannelRole::input,"NEWER WIRE");
    newer.connectMainOnCreate=true;
    const std::array<ChannelEdit,1> append{newer};
    a=session.edit(2,2,append,physical,epoch,epoch,true);
    assert(a.ok() && a.edit.createdIds.size()==1);
    routing=session.acceptedRouting(epoch,3);
    assert(routing && routing->edges.size()==1);
    assert(routing->edges.front().from==a.edit.createdIds.front());
    assert(routing->edges.front().to==master);
    assert(routing->edges.front().edgeId!=id);
    session.unpublish();session.pollRetirement();
}

static void verifyPrimaryOutKeepsCanonicalEdgeIdentity() {
    constexpr std::uint64_t epoch=9931;
    DynamicAcceptedSession session;
    DynamicGraphRequest physical;
    physical.physicalInputs=1;physical.maxFrames=64;
    const std::array<ChannelEdit,3> first{
        add(0,ChannelRole::input,"SOURCE"),
        add(1,ChannelRole::input,"INTERMEDIATE",ProcessingFormat::stereo),
        add(2,ChannelRole::main,"MAIN",ProcessingFormat::stereo)
    };
    auto a=session.edit(0,0,first,physical,epoch,epoch,true);
    assert(a.ok());
    const auto src=a.edit.createdIds[0],via=a.edit.createdIds[1];
    const auto main=a.edit.createdIds[2];
    const std::array<ChannelEdit,2> initial{output(src,main),output(via,main)};
    physical.sources={{src,0,-1,false}};
    a=session.edit(1,1,initial,physical,epoch,epoch,true);
    assert(a.ok());
    const auto* accepted=session.acceptedRouting(epoch,2);
    assert(accepted&&accepted->edges.size()==2);
    const auto edgeId=accepted->edges[0].edgeId;
    const auto viaId=accepted->edges[1].edgeId;
    assert(edgeId!=0&&viaId!=0&&edgeId!=viaId);
    std::array<float,64> impulse{},left{},right{};
    impulse[19]=.25f;
    const float* pins[]{impulse.data()};
    const auto noFx=+[](ChannelId,float*,float*,int) noexcept {};
    const auto render=[&] {
        return session.render(epoch,pins,1,left.data(),right.data(),64,false,noFx);
    };
    assert(render() && near(left[19],.25f*.7071067811865475f));
    const std::array<ChannelEdit,1> same{output(src,main)};
    a=session.edit(2,2,same,physical,epoch,epoch,true);
    assert(a.ok());
    assert(session.acceptedRouting(epoch,3)->edges[0].edgeId==edgeId);
    assert(render() && near(left[19],.25f*.7071067811865475f));
    const std::array<ChannelEdit,1> retarget{output(src,via)};
    a=session.edit(3,3,retarget,physical,epoch,epoch,true);
    assert(a.ok());
    accepted=session.acceptedRouting(epoch,4);
    assert(accepted->edges[0].edgeId==edgeId);
    assert(accepted->edges[0].to==via);
    assert(accepted->edges[1].edgeId==viaId);
    assert(render() && near(left[19],.25f*.7071067811865475f));
    session.unpublish();
    session.pollRetirement();
}

// Real PCM golden regression for a HELD operator fader, not a mouse-up-only
// scalar. Between each input move, the installed native DSP renders another
// callback block and must audibly change the waveform. Structural revision
// stays constant; there is no second callback or graph compilation.
static void verifyHeldDragIntermediatePcmOnOriginalChannels() {
    constexpr std::uint64_t epoch=94021;
    constexpr float center=.7071067811865475f;
    DynamicAcceptedSession pcm;
    DynamicGraphRequest physical;
    physical.physicalInputs=4;
    physical.maxFrames=64;
    std::vector<ChannelEdit> initial;
    for(int i=0;i<10;++i)
        initial.push_back(add(static_cast<std::size_t>(i),
                              ChannelRole::input,"HELD ORIGINAL CH"));
    initial.push_back(add(10,ChannelRole::main,"MAIN",
                          ProcessingFormat::stereo));
    const auto accepted=pcm.edit(0,0,initial,physical,epoch,epoch,true);
    assert(accepted.ok() && accepted.edit.createdIds.size()==11);
    const auto& ids=accepted.edit.createdIds;
    const auto main=ids[10];
    const std::array<ChannelId,4> channels{ids[0],ids[1],ids[4],ids[9]};
    const std::array<ChannelEdit,4> links{
        output(channels[0],main),output(channels[1],main),
        output(channels[2],main),output(channels[3],main)};
    for(std::size_t i=0;i<channels.size();++i)
        physical.sources.push_back(
            {channels[i],static_cast<int>(i),-1,false});
    const auto patched=pcm.edit(1,1,links,physical,epoch,epoch,true);
    assert(patched.ok() && pcm.acceptedGraphRevision()==2);
    std::array<float,64> a{},b{},c{},d{},left{},right{};
    a.fill(.1f);b.fill(.2f);c.fill(.3f);d.fill(.4f);
    const float* pins[]{a.data(),b.data(),c.data(),d.data()};
    const std::array<float,4> signal{.1f,.2f,.3f,.4f};
    std::array<float,4> levels{1.f,1.f,1.f,1.f};
    const auto noFx=+[](ChannelId,float*,float*,int) noexcept {};
    const auto renderAndCheck=[&] {
        assert(pcm.render(epoch,pins,4,left.data(),right.data(),64,
                          false,noFx));
        float sum=0.f;
        for(std::size_t i=0;i<levels.size();++i)
            sum+=signal[i]*levels[i];
        assert(near(left[63],sum*center));
        assert(near(right[63],sum*center));
        return left[63];
    };
    assert(near(renderAndCheck(),center));
    // These are successive pointermove gain targets with NO pointerup.
    // Assert actual PCM changes on EACH intermediate value, for every
    // original input strip whose stable ChannelId is independent of GUI order.
    for(std::size_t i=0;i<channels.size();++i) {
        float previous=center;
        for(const float db:{-6.f,-12.f,-24.f}) {
            const float linear=std::pow(10.f,db/20.f);
            assert(pcm.setLiveMix(epoch,2,channels[i],linear,0.f));
            levels[i]=linear;
            const float audible=renderAndCheck();
            assert(audible<previous);
            previous=audible;
            assert(near(pcm.acceptedRoster().find(channels[i])->faderLinear,
                        linear));
            assert(pcm.acceptedGraphRevision()==2);
        }
        // Release is merely the last target: no structural commit and no
        // newly armed audio path is required to restore the baseline.
        assert(pcm.setLiveMix(epoch,2,channels[i],1.f,0.f));
        levels[i]=1.f;
        assert(near(renderAndCheck(),center));
    }
    // A stale epoch/revision cannot mutate an otherwise live fader or PCM.
    assert(!pcm.setLiveMix(epoch-1,2,channels[2],0.f,0.f));
    assert(!pcm.setLiveMix(epoch,1,channels[2],0.f,0.f));
    assert(near(renderAndCheck(),center));
    pcm.unpublish();
    pcm.pollRetirement();
}

// Original CH1-10 do not get a different fader/solo DSP owner from CH11.
// This is an actual accepted native publisher PCM test, not a mock GUI reducer.
static void verifyOriginalAndAddedInputsShareNativePcmControls() {
    constexpr std::uint64_t epoch=94011;
    constexpr float center=.7071067811865475f;
    DynamicAcceptedSession pcm;
    DynamicGraphRequest physical;
    physical.physicalInputs=3;
    physical.maxFrames=64;
    std::vector<ChannelEdit> initial;
    for(int i=0;i<10;++i)
        initial.push_back(add(static_cast<std::size_t>(i),
                              ChannelRole::input,"ORIGINAL CH"));
    initial.push_back(add(10,ChannelRole::main,"MAIN",
                          ProcessingFormat::stereo));
    auto applied=pcm.edit(0,0,initial,physical,epoch,epoch,true);
    assert(applied.ok() && applied.edit.createdIds.size()==11);
    const auto ch1=applied.edit.createdIds[0];
    const auto ch2=applied.edit.createdIds[1];
    const auto main=applied.edit.createdIds[10];
    const std::array<ChannelEdit,2> originals{
        output(ch1,main),output(ch2,main)};
    physical.sources={{ch1,0,-1,false},{ch2,1,-1,false}};
    applied=pcm.edit(1,1,originals,physical,epoch,epoch,true);
    assert(applied.ok() && pcm.acceptedGraphRevision()==2);
    // CH11 is added to the SAME accepted native graph later, not indexed
    // as CH1 and not assigned a second VST/PCM process callback.
    const std::array<ChannelEdit,1> added{
        add(10,ChannelRole::input,"DYNAMIC CH11")};
    applied=pcm.edit(2,2,added,physical,epoch,epoch,true);
    assert(applied.ok() && applied.edit.createdIds.size()==1);
    const auto ch11=applied.edit.createdIds[0];
    assert(ch11!=ch1 && ch11!=ch2);
    physical.sources.push_back({ch11,2,-1,false});
    const std::array<ChannelEdit,1> connect{output(ch11,main)};
    applied=pcm.edit(3,3,connect,physical,epoch,epoch,true);
    assert(applied.ok() && pcm.acceptedGraphRevision()==4);
    std::array<float,64> a{},b{},c{},l{},rr{};
    a.fill(.2f);b.fill(.3f);c.fill(.4f);
    const float* inputs[]{a.data(),b.data(),c.data()};
    const auto noFx=+[](ChannelId,float*,float*,int) noexcept {};
    const auto render=[&] {
        return pcm.render(epoch,inputs,3,l.data(),rr.data(),64,false,noFx);
    };
    assert(render() && near(l[63],(.2f+.3f+.4f)*center));
    const float minus12=std::pow(10.f,-12.f/20.f);
    assert(pcm.setLiveMix(epoch,4,ch1,minus12,0.f));
    assert(render() && near(l[63],(.2f*minus12+.3f+.4f)*center));
    assert(pcm.setLiveMix(epoch,4,ch2,1.f,-1.f));
    assert(render() && near(l[63],(.2f*minus12+.4f)*center+.3f));
    assert(near(rr[63],(.2f*minus12+.4f)*center));
    assert(pcm.setLiveMuteSolo(epoch,4,ch1,true,false));
    assert(render() && near(l[63],.3f+.4f*center));
    assert(pcm.setLiveMuteSolo(epoch,4,ch11,false,true));
    assert(render() && near(l[63],.4f*center));
    assert(pcm.setLiveMix(epoch,4,ch11,.5f,1.f));
    assert(render() && near(l[63],0.f) && near(rr[63],.2f));
    // No old device, old graph revision or invented ChannelId can change
    // accepted native PCM, even while a solo/fader edit is in flight.
    assert(!pcm.setLiveMix(epoch-1,4,ch1,1.f,0.f));
    assert(!pcm.setLiveMix(epoch,3,ch2,1.f,0.f));
    assert(!pcm.setLiveMuteSolo(epoch,3,ch11,false,false));
    assert(!pcm.setLiveMuteSolo(epoch,4,ChannelId{987654},true,false));
    assert(render() && near(l[63],0.f) && near(rr[63],.2f));
    assert(pcm.acceptedGraphRevision()==4);
    assert(near(pcm.acceptedRoster().find(ch1)->faderLinear,minus12));
    assert(near(pcm.acceptedRoster().find(ch11)->faderLinear,.5f));
    assert(pcm.acceptedRoster().find(ch1)->mute);
    assert(pcm.acceptedRoster().find(ch11)->solo);
    pcm.unpublish();
    pcm.pollRetirement();
}

int main() {
    verifyHeldDragIntermediatePcmOnOriginalChannels();
    verifyOriginalAndAddedInputsShareNativePcmControls();
    verifyPrimaryOutKeepsCanonicalEdgeIdentity();
    verifyDefaultNativeMainEdgeAndDisconnectedRestore();
    verifyAtomicWireTransactionAndZeroSampleHops();

    // WIRE-first regression: PRE and POST to the SAME destination are
    // NOT two valid edges. Reject before accepting duplicate dry PCM.
    // A failed compile must preserve the previous prepared graph.
    {
        DynamicChannelRoster roster;
        const std::array<ChannelEdit,2> create{
            add(0,ChannelRole::input,"WIRE SRC"),
            add(1,ChannelRole::main,"MAIN",ProcessingFormat::stereo)
        };
        const auto created=roster.edit(0,create);
        assert(created.ok() && created.createdIds.size()==2);
        const auto src=created.createdIds[0], dst=created.createdIds[1];
        const std::array<ChannelEdit,1> first{output(src,dst)};
        assert(roster.edit(1,first).ok());
        DynamicGraphRequest request;
        request.maxFrames=64;
        DynamicPreparedMixerBlock block;
        DynamicGraphError fault;
        assert(DynamicPreparedMixerBlock::compile(roster,request,block,fault));
        assert(block.channelCount()==2);
        const std::array<ChannelEdit,1> duplicate{send(src,dst,.5f,true)};
        assert(roster.edit(2,duplicate).ok()); // legacy draft may still hold it
        assert(!DynamicPreparedMixerBlock::compile(roster,request,block,fault));
        assert(fault.fault==DynamicGraphFault::duplicateSend);
        assert(block.channelCount()==2); // previous prepared PCM untouched
    }

    verifyConsoleWirePrimaryOutputPcm();
    verifyRealtimeSlewAndNativeChannelIdContinuousControls();
    constexpr std::uint64_t epoch=77;
    constexpr float center=.7071067811865475f;
    DynamicAcceptedSession session;
    DynamicGraphRequest route;route.physicalInputs=2;route.maxFrames=64;
    std::array<ChannelEdit,4> create{
        add(0,ChannelRole::input,"LEAD"),
        add(1,ChannelRole::input,"BACKING"),
        add(2,ChannelRole::main,"MAIN",ProcessingFormat::stereo),
        add(3,ChannelRole::monitor,"MONITOR",ProcessingFormat::stereo)};
    auto applied=session.edit(0,0,create,route,epoch,epoch,true);
    assert(applied.ok()&&session.acceptedRosterRevision()==1);
    assert(session.acceptedGraphRevision()==1);
    const auto micA=applied.edit.createdIds[0],micB=applied.edit.createdIds[1];
    const auto main=applied.edit.createdIds[2],monitor=applied.edit.createdIds[3];
    assert(session.acceptedRoster().size()==4);
    std::array<ChannelEdit,4> links{
        output(micA,main),output(micB,main),
        send(micA,monitor,.2f),send(micB,monitor,.25f)};
    route.sources={{micA,0,-1,false},{micB,1,-1,false}};
    applied=session.edit(1,1,links,route,epoch,epoch,true);
    assert(applied.ok()&&applied.graphRevision==2);
    std::array<float,64> a{},b{},l{},r{},ml{},mr{};
    a.fill(.2f);b.fill(.4f);
    const float* inputs[]{a.data(),b.data()};
    ChannelId group{},fx{};
    const auto processFx=[&](ChannelId id,float* left,float* right,int frames) noexcept {
        const float gain=id==group?2.f:id==fx?.5f:1.f;
        for(int i=0;i<frames;++i){left[i]*=gain;right[i]*=gain;}
    };
    const auto render=[&](int n=64) {
        return session.render(epoch,inputs,2,l.data(),r.data(),n,
                              false,processFx,ml.data(),mr.data());
    };
    assert(render());
    assert(near(l[63],(.2f+.4f)*center));
    assert(near(ml[63],(.2f*.2f+.4f*.25f)*center));
    // M5.3 native SOURCE changes physical pins but not VST3, ChannelId,
    // send graph, or output ownership. Stage no callback memory and ramp
    // over 4+16+32+12 real samples with independent MONITOR taps.
    assert(!session.setLivePhysicalSource(epoch-1,2,micA,1,-1,false));
    assert(!session.setLivePhysicalSource(epoch,1,micA,1,-1,false));
    assert(!session.setLivePhysicalSource(epoch,2,micA,2,-1,false));
    assert(!session.setLivePhysicalSource(epoch,2,micA,1,0,true));
    assert(session.setLivePhysicalSource(epoch,2,micA,1,-1,false));
    assert(session.acceptedPhysicalRequest().sources[0].inputL==1);
    assert(render(4));
    assert(l[3]>(.2f+.4f)*center && l[3]<(.4f+.4f)*center);
    assert(render(16)&&render(32)&&render(12));
    assert(near(l[11],(.4f+.4f)*center));
    assert(near(ml[11],(.4f*.2f+.4f*.25f)*center));
    // A last-target-wins reverse retains the same PCM and plugin graph.
    assert(session.setLivePhysicalSource(epoch,2,micA,0,-1,false));
    assert(render());
    assert(near(l[63],(.2f+.4f)*center));
    assert(near(ml[63],(.2f*.2f+.4f*.25f)*center));
    assert(session.acceptedGraphRevision()==2);
    // E1: structural draft pins are preflighted BEFORE accepted SOURCE
    // reconciliation. Out-of-range or same-pin stereo must not be hidden by
    // an earlier valid MONO source, falsely ACKed, or silence existing PCM.
    ChannelEdit renameAttempt;
    renameAttempt.kind=ChannelEdit::Kind::rename;
    renameAttempt.id=micA;
    renameAttempt.text="MUST_NOT_APPEAR_AFTER_REJECTION";
    const std::array<ChannelEdit,1> structuralAttempt{renameAttempt};
    auto invalidSource=route;
    invalidSource.sources[0].inputL=2; // exactly physicalInputs
    auto deniedSource=session.edit(2,2,structuralAttempt,invalidSource,epoch,epoch,true);
    assert(!deniedSource.ok());
    assert(deniedSource.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(deniedSource.native.fault==DynamicNativeSessionPublisher::Fault::invalidGraph);
    assert(deniedSource.native.graphError.fault==DynamicGraphFault::invalidSource);
    assert(deniedSource.edit.createdIds.empty());
    assert(session.acceptedGraphRevision()==2);
    assert(session.acceptedRosterRevision()==2);
    assert(session.acceptedRoster().find(micA)->name=="LEAD");
    assert(session.acceptedPhysicalRequest().sources[0].inputL==0);
    invalidSource=route;
    invalidSource.sources[0].stereo=true;
    invalidSource.sources[0].inputR=0;
    deniedSource=session.edit(2,2,structuralAttempt,invalidSource,epoch,epoch,true);
    assert(!deniedSource.ok() &&
           deniedSource.native.graphError.fault==DynamicGraphFault::invalidSource);
    assert(render()&&near(l[63],(.2f+.4f)*center));
    // A physically valid but different pin is NOT an implicit IN request
    // during RENAME/GROUP/OUT. Previously the old SOURCE was overlaid and
    // an unfulfilled new pin was ACKed as successful (GUI/PCM disagreement).
    auto validButUnacceptedSource=route;
    validButUnacceptedSource.sources[0].inputL=1;
    auto deniedValidSource=session.edit(2,2,structuralAttempt,
        validButUnacceptedSource,epoch,epoch,true);
    assert(!deniedValidSource.ok() &&
           deniedValidSource.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(deniedValidSource.native.graphError.fault==
           DynamicGraphFault::invalidSource);
    assert(deniedValidSource.native.graphError.id==micA);
    assert(session.acceptedGraphRevision()==2 &&
           session.acceptedPhysicalRequest().sources[0].inputL==0);
    assert(session.acceptedRoster().find(micA)->name=="LEAD");
    assert(render()&&near(l[63],(.2f+.4f)*center));
    // One accepted ChannelId graph: realtime MUTE/SOLO and PRE/POST follow
    // the same callback, not a recompilation or an extra FX instance.
    assert(session.setLiveMuteSolo(epoch,2,micA,true,false));
    assert(session.acceptedRoster().find(micA)->mute);
    assert(render(4));
    assert(l[3]>.4f*center && l[3]<.6f*center);
    assert(render(16)&&render(32)&&render(12));
    assert(near(l[11],.4f*center));
    assert(near(ml[11],(.4f*.25f)*center));
    assert(session.setLiveMuteSolo(epoch,2,micA,false,true));
    assert(render());
    assert(near(l[63],.2f*center));
    assert(near(ml[63],(.2f*.2f)*center));
    assert(session.setLiveMuteSolo(epoch,2,main,true,false));
    assert(render());
    assert(near(l[63],0.f)&&near(r[63],0.f));
    assert(near(ml[63],(.2f*.2f)*center)); // independent MONITOR
    assert(session.setLiveMuteSolo(epoch,2,main,false,false));
    assert(session.setLiveMuteSolo(epoch,2,micA,false,false));
    assert(render());
    assert(near(l[63],(.2f+.4f)*center));
    assert(near(ml[63],(.2f*.2f+.4f*.25f)*center));
    assert(!session.setLiveMuteSolo(epoch-1,2,micA,true,false));
    assert(!session.setLiveMuteSolo(epoch,1,micA,true,false));
    assert(!session.setLiveMuteSolo(epoch,2,ChannelId{987654},true,false));
    assert(!session.acceptedRoster().find(micA)->mute&&
           !session.acceptedRoster().find(micA)->solo&&
           session.acceptedGraphRevision()==2);
    // Control update is a real RT atomic, but is ALSO preserved by roster so
    // subsequent BUS compilation cannot silently reset fader/send gain.
    assert(session.setLiveMix(epoch,2,micA,.5f,0.f));
    assert(session.setLiveParallelSend(epoch,2,micA,monitor,false,.3f));
    assert(session.acceptedGraphRevision()==2);
    assert(near(session.acceptedRoster().find(micA)->faderLinear,.5f));
    assert(near(session.acceptedRoster().find(micA)->sends[1].gain,.3f));
    assert(render());
    assert(near(l[63],(.1f+.4f)*center));
    assert(near(ml[63],(.1f*.3f+.4f*.25f)*center));
    assert(!session.setLiveParallelSend(epoch,2,micA,main,false,0.f));
    assert(!session.setLiveMix(epoch,1,micA,1.f,0.f));
    assert(!session.setLiveMix(epoch-1,2,micA,1.f,0.f));
    assert(!session.setLiveMix(epoch,2,micA,
                        std::numeric_limits<float>::quiet_NaN(),0.f));
    assert(!session.setLiveParallelSend(epoch,2,micA,monitor,false,-1.f));
    assert(near(session.acceptedRoster().find(micA)->faderLinear,.5f));

    const std::array<ChannelId,2> vocals{micA,micB};
    // Unsafe creation is rejected even if the editor could build a roster.
    auto rejected=session.createBusForSelected(2,2,vocals,"VOCAL BUS",
        ChannelRole::group,route,epoch,epoch,false);
    assert(rejected.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(rejected.native.fault==DynamicNativeSessionPublisher::Fault::outputArmed);
    assert(rejected.edit.createdIds.empty());
    assert(session.acceptedRoster().size()==4 && session.acceptedGraphRevision()==2);
    assert(render()&&near(l[63],(.1f+.4f)*center));
    // In one native ACK: allocate independent ChannelId + replace every
    // selected primary OUT. Never borrow CH3/CH10 or leave double-dry.
    applied=session.createBusForSelected(2,2,vocals,"VOCAL BUS",
        ChannelRole::group,route,epoch,epoch,true);
    assert(applied.ok()&&applied.edit.createdIds.size()==1);
    group=applied.edit.createdIds.front();
    assert(group.value==5 && session.acceptedRoster().size()==5);
    assert(session.acceptedGraphRevision()==3);
    const auto* view=session.acceptedRouting(epoch,3);
    assert(view&&view->find(group)->primarySources.size()==2);
    assert(view->find(group)->sourceCount()==2);
    assert(view->find(micA)->primaryOutput==group);
    assert(view->find(micB)->primaryOutput==group);
    assert(view->find(micA)->parallelSends.size()==1);
    assert(!session.acceptedRouting(epoch,2));
    assert(render());
    assert(near(l[63],(.1f+.4f)*center*2.f));
    assert(near(ml[63],(.1f*.3f+.4f*.25f)*center));
    assert(near(session.acceptedRoster().find(micA)->faderLinear,.5f));

    // FX Return is separate from subgroup; mic primary OUT stays to group.
    const std::array<ChannelId,1> single{micA};
    applied=session.createBusForSelected(3,3,single,"REVERB",
        ChannelRole::fxReturn,route,epoch,epoch,true,.25f);
    assert(applied.ok()&&applied.edit.createdIds.size()==1);
    fx=applied.edit.createdIds.front();
    assert(fx.value==6 && session.acceptedRoster().size()==6);
    assert(session.acceptedRouting(epoch,4)->find(micA)->primaryOutput==group);
    assert(session.acceptedRouting(epoch,4)->find(fx)->parallelSendSources.size()==1);
    assert(render());
    assert(near(l[63],(.1f+.4f)*center*2.f + .1f*center*.25f*.5f));
    // Live group gain retained across further successful FX BUS recompiles.
    assert(session.setLiveMix(epoch,4,group,.8f,0.f));
    assert(render());
    assert(near(l[63],(.1f+.4f)*center*2.f*.8f+.1f*center*.25f*.5f));

    // Invalid edits NEVER publish phantom channels or reset previous audio.
    const auto size=session.acceptedRoster().size();
    const auto revision=session.acceptedRosterRevision();
    rejected=session.createBusForSelected(revision,4,vocals,"DUPLICATE",
        ChannelRole::group,route,epoch-1,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(rejected.native.fault==DynamicNativeSessionPublisher::Fault::staleDevice);
    assert(session.acceptedRoster().size()==size);
    rejected=session.createBusForSelected(revision,4,vocals,"DUPLICATE",
        ChannelRole::group,route,epoch,epoch,true);
    assert(rejected.ok()); // separate nested GROUP is valid, not a feedback loop
    const auto newest=rejected.edit.createdIds.front();
    assert(newest.value==7 && session.acceptedGraphRevision()==5);

    ChannelEdit cycle=send(main,micA,1.f);
    const std::array<ChannelEdit,1> invalid{cycle};
    rejected=session.edit(session.acceptedRosterRevision(),5,invalid,
                          route,epoch,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(rejected.native.graphError.fault==DynamicGraphFault::mainFeedback);
    assert(session.acceptedGraphRevision()==5);
    assert(session.acceptedRoster().find(main)->sends.empty());
    assert(render());
    const float before=l[63];
    assert(before>0.f);
    // A stale session writer cannot delete or reroute accepted PCM.
    ChannelEdit remove;remove.kind=ChannelEdit::Kind::remove;remove.id=micA;
    const std::array<ChannelEdit,1> removeOne{remove};
    rejected=session.edit(session.acceptedRosterRevision()-1,5,removeOne,
                          route,epoch,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::staleRoster);
    rejected=session.edit(session.acceptedRosterRevision(),4,removeOne,
                          route,epoch,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::staleGraph);
    rejected=session.edit(session.acceptedRosterRevision(),5,removeOne,
                          route,epoch,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::graphRejected);
    assert(rejected.native.graphError.fault==DynamicGraphFault::missingSource);
    assert(session.acceptedRoster().size()==size+1);
    assert(render()&&near(l[63],before));

    // Real OUT NONE must keep independent send to monitor/FX available.
    ChannelEdit none=output(micB,ChannelId{});
    const std::array<ChannelEdit,1> onlyNone{none};
    applied=session.edit(session.acceptedRosterRevision(),5,onlyNone,
                         route,epoch,epoch,true);
    assert(applied.ok()&&session.acceptedGraphRevision()==6);
    const auto* updated=session.acceptedRouting(epoch,6);
    assert(updated && !updated->find(micB)->primaryOutput.value);
    assert(updated->find(micB)->parallelSends.size()==1);
    assert(render());
    assert(near(ml[63],(.1f*.3f+.4f*.25f)*center));
    // Multiple RT updates are still safe after structural revision changes.
    assert(!session.setLiveMix(epoch,5,micB,1.f,0.f));
    assert(session.setLiveMix(epoch,6,micB,.25f,0.f));
    assert(render());
    assert(near(ml[63],(.1f*.3f+.1f*.25f)*center));
    const float persisted=ml[63];
    assert(!session.render(epoch,inputs,2,l.data(),r.data(),64,
                           true,processFx,ml.data(),mr.data()));
    assert(l[63]==0.f && ml[63]==0.f);
    assert(render()&&near(ml[63],persisted));
    // Studio Console Add Bus without selection: distinct non-input ID,
    // empty source count, default OUT -> MAIN, no phantom audible signal.
    applied=session.createBus(session.acceptedRosterRevision(),6,
        "EMPTY MUSIC BUS",ChannelRole::group,route,epoch,epoch,true);
    assert(applied.ok()&&applied.edit.createdIds.size()==1);
    const auto empty=applied.edit.createdIds.front();
    assert(empty.value==8 && session.acceptedRoster().size()==8);
    const auto* emptyView=session.acceptedRouting(epoch,7);
    assert(emptyView && emptyView->find(empty)->sourceCount()==0);
    assert(emptyView->find(empty)->primaryOutput==main);
    assert(render() && near(ml[63],persisted));

    // Studio Remove Bus reconnects the original primary source to MAIN but
    // cannot redirect an OUT NONE channel, or destroy its independent sends.
    rejected=session.removeBus(session.acceptedRosterRevision(),7,
        main,route,epoch,epoch,true);
    assert(rejected.fault==DynamicAcceptedSession::Fault::editRejected);
    assert(session.acceptedGraphRevision()==7);
    applied=session.removeBus(session.acceptedRosterRevision(),7,
        newest,route,epoch,epoch,true);
    assert(applied.ok() && !session.acceptedRoster().find(newest));
    const auto* reconnected=session.acceptedRouting(epoch,8);
    assert(reconnected && reconnected->find(micA)->primaryOutput==main);
    assert(!reconnected->find(micB)->primaryOutput.value);
    assert(reconnected->find(micA)->parallelSends.size()==2);
    assert(render() && near(l[63],.1f*center*(1.f+.25f*.5f)));
    assert(near(ml[63],persisted));
    applied=session.removeBus(session.acceptedRosterRevision(),8,
        empty,route,epoch,epoch,true);
    assert(applied.ok() && session.acceptedRoster().size()==6);
    assert(session.acceptedGraphRevision()==9);
    assert(render() && near(ml[63],persisted));
    session.unpublish();
    session.pollRetirement();
    assert(!session.render(epoch,inputs,2,l.data(),r.data(),64,
                           false,processFx,ml.data(),mr.data()));
    assert(l[63]==0.f && ml[63]==0.f);
}
