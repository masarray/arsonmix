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
    assert(session.setLiveMix(epoch,2,voice,.2f,0.f));
    assert(session.setLiveMix(epoch,2,voice,.8f,0.f));
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

int main() {
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
    const auto render=[&]() {
        return session.render(epoch,inputs,2,l.data(),r.data(),64,
                              false,processFx,ml.data(),mr.data());
    };
    assert(render());
    assert(near(l[63],(.2f+.4f)*center));
    assert(near(ml[63],(.2f*.2f+.4f*.25f)*center));
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
