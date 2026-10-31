// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/DynamicAcceptedSession.h"
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
int main() {
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
    session.unpublish();
    session.pollRetirement();
    assert(!session.render(epoch,inputs,2,l.data(),r.data(),64,
                           false,processFx,ml.data(),mr.data()));
    assert(l[63]==0.f && ml[63]==0.f);
}
