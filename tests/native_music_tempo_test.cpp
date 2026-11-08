// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/NativeMusicTempoMonitor.h"
#include "../engine/core/DynamicNativeSessionPublisher.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#define check(x) do {if(!(x)){std::fprintf(stderr,"FAIL:%s:%d %s\n",__FILE__,__LINE__,#x);std::abort();}}while(false)
static std::atomic<bool> noHeap{false};
void* operator new(std::size_t n) {
    if(noHeap.load(std::memory_order_relaxed))std::abort();
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    if(noHeap.load(std::memory_order_relaxed))std::abort();
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using armix::mixer::ChannelId;
using armix::mixer::NativeMusicTempoMonitor;
static void renderWindow(NativeMusicTempoMonitor& tap,int index,int period,
                         float beat=.7f,bool weakOffbeats=false,
                         std::uint64_t epoch=7,std::uint64_t revision=13) {
    std::array<float,128> l{},r{};
    const int pos=index%period;
    const float level=pos==0?beat:
        weakOffbeats&&pos==period/2?.07f:0.f;
    l.fill(level);r.fill(level);
    // A 10ms bucket is split across 64+36 ASIO callback frames at 10kHz.
    noHeap.store(true,std::memory_order_release);
    (void)tap.beginAudioBlock(epoch,64);
    tap.capture(ChannelId{301},l.data(),r.data(),64,epoch,revision);
    (void)tap.beginAudioBlock(epoch,36);
    tap.capture(ChannelId{301},l.data()+64,r.data()+64,36,epoch,revision);
    noHeap.store(false,std::memory_order_release);
    if(index%8==0)(void)tap.poll(epoch,revision); // host timer, not RT
}
static void lockTempo(int bpm,bool hats=false) {
    NativeMusicTempoMonitor tap;
    check(!tap.select(ChannelId{301},7,13,0));
    check(tap.select(ChannelId{301},7,13,10000));
    auto state=tap.poll(7,13);
    check(state.active&&state.source.value==301&&!state.tempo.locked);
    const int period=static_cast<int>(std::lround(6000.0/bpm));
    for(int n=0;n<1100;++n)renderWindow(tap,n,period,.75f,hats);
    state=tap.poll(7,13);
    check(state.tempo.locked);
    check(std::abs(state.tempo.bpm-bpm)<=3.1f);
    check(state.tempo.confidence>=.28f);
    check(state.tempo.beatAnchorTick>0);
    check(state.tempo.beatAnchorFrame>0);
    check(state.tempo.observationFrame>state.tempo.beatAnchorFrame);
    check(state.dropped==0);
    // One second of no music must NOT produce spurious half/double BPM.
    for(int n=1100;n<1200;++n)renderWindow(tap,n,6000);
    state=tap.poll(7,13);
    check(state.tempo.locked);
    // The user must see manual/TAP fallback after >3.2s of silence.
    for(int n=1200;n<1580;++n)renderWindow(tap,n,6000);
    state=tap.poll(7,13);
    check(!state.tempo.locked);
}
static void failClosed() {
    NativeMusicTempoMonitor tap;
    check(tap.select(ChannelId{301},7,13,48000));
    std::array<float,128> pcm{};pcm.fill(.8f);
    noHeap.store(true);
    for(int i=0;i<240;++i) {
        // Wrong channel and wrong graph revision must not populate the FIFO.
        tap.capture(ChannelId{302},pcm.data(),pcm.data(),128,7,13);
        tap.capture(ChannelId{301},pcm.data(),pcm.data(),128,7,14);
    }
    noHeap.store(false);
    auto stale=tap.poll(7,14);
    check(!stale.active&&!stale.tempo.locked);
    auto active=tap.poll(7,13);
    check(active.active&&!active.tempo.locked);
    check(tap.select(ChannelId{},7,13,0));
    check(!tap.poll(7,13).active);
}
static void actualGraphSourcePcm() {
    using namespace armix::mixer;
    DynamicChannelRoster roster;
    ChannelEdit source,main;
    source.kind=ChannelEdit::Kind::add;source.position=0;
    source.role=ChannelRole::input;source.text="BACKING";
    main.kind=ChannelEdit::Kind::add;main.position=1;
    main.role=ChannelRole::main;main.text="MAIN";
    main.format=ProcessingFormat::stereo;
    const std::array<ChannelEdit,2> adds{source,main};
    const auto created=roster.edit(0,adds);
    check(created.ok()&&created.createdIds.size()==2);
    const auto music=created.createdIds[0],master=created.createdIds[1];
    ChannelEdit output;
    output.kind=ChannelEdit::Kind::setPrimaryOutput;
    output.id=music;output.target=master;
    const std::array<ChannelEdit,1> links{output};
    check(roster.edit(1,links).ok());
    DynamicGraphRequest request;
    request.physicalInputs=1;request.maxFrames=128;
    request.sources={{music,0,-1,false}};
    DynamicNativeSessionPublisher graph;
    constexpr std::uint64_t epoch=7;
    const auto applied=graph.commit(roster,request,0,epoch,epoch,true);
    check(applied.ok()&&applied.revision==1);
    check(!graph.setMusicTempoSource(epoch,1,master,10000)); // MAIN forbidden
    check(!graph.setMusicTempoSource(epoch-1,1,music,10000));
    check(graph.setMusicTempoSource(epoch,1,music,10000));
    std::array<float,128> input{},outL{},outR{};
    const float* physical[]{input.data()};
    int expectedTick=0;
    const auto dryFx=[&](ChannelId,float*,float*,int n) noexcept {
        const auto current=NativeBeatFrameScope::current();
        check(current.valid && current.epoch==epoch &&
              current.revision==1 && n==100);
        check(current.startFrame==static_cast<std::uint64_t>(expectedTick)*100);
    };
    for(int tick=0;tick<1000;++tick) {
        input.fill((tick%50)==0?.65f:0.f); // 120 BPM from real graph
        noHeap.store(true,std::memory_order_release);
        expectedTick=tick;
        const bool rendered=graph.render(epoch,physical,1,
            outL.data(),outR.data(),100,false,dryFx);
        noHeap.store(false,std::memory_order_release);
        check(rendered);
        if(tick%8==0)(void)graph.pollMusicTempo(epoch,1);
    }
    auto state=graph.pollMusicTempo(epoch,1);
    check(state.active&&state.source==music);
    check(state.tempo.locked&&std::abs(state.tempo.bpm-120.f)<=3.f);
    check(state.tempo.beatAnchorFrame>0 &&
          state.tempo.beatAnchorFrame<=100000);
    check(state.tempo.observationFrame>=state.tempo.beatAnchorFrame);
    check(!NativeBeatFrameScope::current().valid); // callback scope restored
    // The *same* music ChannelId survives a graph revision with no second
    // callback or preserved stale PCM pointers.
    const auto newer=graph.commit(roster,request,1,epoch,epoch,true);
    check(newer.ok()&&newer.revision==2);
    state=graph.pollMusicTempo(epoch,2);
    check(state.active&&state.source==music);
    check(!graph.pollMusicTempo(epoch,1).active);
    graph.unpublish();
    graph.pollRetirement();
}
int main() {
    lockTempo(72);
    lockTempo(95);
    lockTempo(120);
    lockTempo(160);
    lockTempo(120,true);
    failClosed();
    actualGraphSourcePcm();
    std::puts("PASS: native music-channel BPM 72/95/120/160, silence holdover, stale graph, no RT heap");
}
