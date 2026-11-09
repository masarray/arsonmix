// SPDX-License-Identifier: AGPL-3.0-only
// ArSon Player M0: actual ChannelId/PCM source proof; NOT browser sound.
#include "../engine/core/DynamicAcceptedSession.h"
#ifdef NDEBUG
#undef NDEBUG // Ensure assertions run under Release CTest.
#endif
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <new>
#include <span>
using namespace armix::mixer;

static std::size_t allocations=0;
void* operator new(std::size_t n) {
    ++allocations;
    if(auto* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}

static bool near(float a,float b,float tolerance=1e-5f) {
    return std::abs(a-b)<tolerance;
}
static ChannelEdit create(std::size_t position,ChannelRole role,
                          const char* name,ProcessingFormat format) {
    ChannelEdit edit;
    edit.kind=ChannelEdit::Kind::add;
    edit.position=position;
    edit.role=role;edit.format=format;edit.text=name;
    return edit;
}
static ChannelEdit connect(ChannelId from,ChannelId to) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::setPrimaryOutput;
    e.id=from;e.target=to;return e;
}
static void verifyFirstSoundOwnedRouteAndIdempotence() {
    constexpr std::uint64_t deviceEpoch=714;
    DynamicAcceptedSession session;
    DynamicGraphRequest route;
    route.maxFrames=64;
    route.physicalInputs=1; // production device may have existing microphones
    auto mainEdit=create(0,ChannelRole::main,"MAIN",ProcessingFormat::stereo);
    auto first=session.edit(0,0,std::span<const ChannelEdit>(&mainEdit,1),
                            route,deviceEpoch,deviceEpoch,true);
    assert(first.ok() && first.graphRevision==1);
    const auto main=first.edit.createdIds.front();
    // The user presses PLAY. No manual channel creation, PATCH, or WIRE.
    auto activated=session.ensurePlayerFirstSound(
        1,1,route,deviceEpoch,deviceEpoch,false,true);
    assert(activated.ok() && activated.edit.createdIds.size()==1);
    const auto music=activated.edit.createdIds.front();
    assert(session.acceptedRoster().size()==2);
    assert(session.acceptedGraphRevision()==2);
    const auto* row=session.acceptedRoster().find(music);
    assert(row && row->role==ChannelRole::input &&
           row->format==ProcessingFormat::stereo && row->name=="MUSIC");
    assert(row->sends.size()==1 && row->sends.front().destination==main &&
           row->sends.front().primaryOutput);
    const auto& sources=session.acceptedPhysicalRequest().sources;
    assert(sources.size()==1 && sources.front().id==music);
    assert(sources.front().kind==DynamicSourceKind::playerDeck);
    assert(sources.front().deckId==1 && sources.front().inputL==-1);
    for(int i=0;i<100;++i) {
        // Duplicate UI Play messages neither create channels nor duplicate
        // MAIN feeds. The accepted graph/roster revisions stay unchanged.
        auto repeat=session.ensurePlayerFirstSound(
            2,2,route,deviceEpoch,deviceEpoch,false,true);
        assert(repeat.ok() && repeat.edit.createdIds.empty());
        assert(session.acceptedGraphRevision()==2 &&
               session.acceptedRoster().size()==2);
    }
    std::array<float,64> physical{},left{},right{},mainL{},mainR{};
    std::array<float,2> peaksL{},peaksR{};
    left.fill(.375f);right.fill(.125f);
    const float* pins[]{physical.data()};
    const std::array<DynamicPlayerPcmBlock,1> musicBlock{{{1,left.data(),right.data()}}};
    const auto fx=[](ChannelId,float*,float*,int) noexcept {};
    const auto before=allocations;
    assert(session.render(deviceEpoch,pins,1,mainL.data(),mainR.data(),64,
                          false,fx,nullptr,nullptr,peaksL,peaksR,musicBlock));
    assert(allocations==before); // absolutely no callback heap activity
    assert(near(mainL[63],.375f) && near(mainR[63],.125f));
    assert(near(peaksL[0],.375f) && near(peaksR[0],.125f));
    // Deliberately unplugged SOURCE ONLY must retain source processing and
    // meter, but MAIN must be silent even after subsequent PLAY requests.
    auto unplug=connect(music,{});
    auto dis=session.edit(2,2,std::span<const ChannelEdit>(&unplug,1),
                          session.acceptedPhysicalRequest(),
                          deviceEpoch,deviceEpoch,true);
    assert(dis.ok() && dis.graphRevision==3);
    assert(session.acceptedRoster().find(music)->sends.empty());
    const auto before2=allocations;
    assert(session.render(deviceEpoch,pins,1,mainL.data(),mainR.data(),64,
                          false,fx,nullptr,nullptr,peaksL,peaksR,musicBlock));
    assert(allocations==before2);
    assert(near(mainL[63],0.f) && near(mainR[63],0.f));
    assert(near(peaksL[0],.375f) && near(peaksR[0],.125f));
    auto retry=session.ensurePlayerFirstSound(3,3,
        session.acceptedPhysicalRequest(),deviceEpoch,deviceEpoch,false,true,
        music);
    assert(retry.ok() && session.acceptedRoster().find(music)->sends.empty());
    // Explicit user WIRE to MAIN recovers through the same graph without
    // discarding the player source or requiring another decoder.
    auto plug=connect(music,main);
    auto reconnected=session.edit(3,3,std::span<const ChannelEdit>(&plug,1),
                                  session.acceptedPhysicalRequest(),
                                  deviceEpoch,deviceEpoch,true);
    assert(reconnected.ok());
    assert(session.render(deviceEpoch,pins,1,mainL.data(),mainR.data(),64,
                          false,fx,nullptr,nullptr,peaksL,peaksR,musicBlock));
    assert(near(mainL[63],.375f) && near(mainR[63],.125f));
    // A different physical output must NEVER be silently authorized.
    DynamicAcceptedSession denied;
    auto base=denied.edit(0,0,std::span<const ChannelEdit>(&mainEdit,1),
                         route,deviceEpoch,deviceEpoch,true);
    assert(base.ok());
    auto rejected=denied.ensurePlayerFirstSound(
        1,1,route,deviceEpoch,deviceEpoch,false,false);
    assert(!rejected.ok() && denied.acceptedRoster().size()==1);
}
static void verifyPhysicalAndVirtualSharedDeckWithNoPhysicalPinSpoofing() {
    DynamicChannelRoster roster;
    std::array<ChannelEdit,4> edits{
        create(0,ChannelRole::input,"MIC",ProcessingFormat::mono),
        create(1,ChannelRole::input,"PLAYER-1",ProcessingFormat::stereo),
        create(2,ChannelRole::input,"PLAYER-2",ProcessingFormat::stereo),
        create(3,ChannelRole::main,"MAIN",ProcessingFormat::stereo)
    };
    auto added=roster.edit(0,edits);
    assert(added.ok() && added.createdIds.size()==4);
    const auto mic=added.createdIds[0],a=added.createdIds[1],
               b=added.createdIds[2],main=added.createdIds[3];
    std::array<ChannelEdit,3> wiring{
        connect(mic,main),connect(a,main),connect(b,main)};
    assert(roster.edit(1,wiring).ok());
    DynamicGraphRequest route;
    route.physicalInputs=1;route.maxFrames=32;
    route.sources={
        {mic,0,-1,false},
        {a,-1,-1,true,DynamicSourceKind::playerDeck,77},
        {b,-1,-1,true,DynamicSourceKind::playerDeck,77}
    };
    DynamicPreparedMixerBlock graph;
    DynamicGraphError err;
    assert(DynamicPreparedMixerBlock::compile(roster,route,graph,err));
    assert(graph.prepareLatency([](ChannelId) noexcept{return 0;},
                                route.maxScratchBytes,err));
    assert(graph.channelScheduled(mic)&&graph.channelScheduled(a)&&
           graph.channelScheduled(b));
    // Same deck is consumed once externally. Both channels receive the same
    // frame; intentional double routing sums twice (not a double FIFO pop).
    std::array<float,32> microphone{},musicL{},musicR{},outL{},outR{};
    microphone.fill(.2f);musicL.fill(.3f);musicR.fill(-.1f);
    const float* physical[]{microphone.data()};
    const std::array<DynamicPlayerPcmBlock,1> decks{{{77,musicL.data(),musicR.data()}}};
    const auto fx=[](ChannelId,float*,float*,int) noexcept {};
    const auto before=allocations;
    assert(graph.process(physical,1,outL.data(),outR.data(),32,fx,
                         nullptr,nullptr,{}, {},decks));
    assert(allocations==before);
    const float micSide=.2f*std::sqrt(.5f);
    assert(near(outL[31],micSide+.6f));
    assert(near(outR[31],micSide-.2f));
    // A missing starved deck contributes SILENCE without damaging MIC.
    assert(graph.process(physical,1,outL.data(),outR.data(),32,fx));
    assert(near(outL[31],micSide) && near(outR[31],micSide));
    assert(!graph.setPhysicalSource(a,0,-1,false));
    assert(graph.setPhysicalSource(mic,0,-1,false));
    // Invalid source variants may not reach accepted PCM.
    auto invalid=route;
    invalid.sources[1].deckId=0;
    DynamicPreparedMixerBlock rejected;
    assert(!DynamicPreparedMixerBlock::compile(roster,invalid,rejected,err));
    assert(err.fault==DynamicGraphFault::invalidSource);
    invalid=route;
    invalid.sources[1].inputL=0; // illegal fake physical ordinal on deck
    assert(!DynamicPreparedMixerBlock::compile(roster,invalid,rejected,err));
    invalid=route;
    invalid.sources[1].kind=DynamicSourceKind::physical;
    assert(!DynamicPreparedMixerBlock::compile(roster,invalid,rejected,err));
}
int main() {
    verifyFirstSoundOwnedRouteAndIdempotence();
    verifyPhysicalAndVirtualSharedDeckWithNoPhysicalPinSpoofing();
}
