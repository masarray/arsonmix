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
                         std::uint64_t epoch=7,std::uint64_t revision=13,
                         std::uint64_t controlNowMs=0) {
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
    if(index%8==0)(void)tap.poll(epoch,revision,controlNowMs); // host timer, not RT
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
    // Sparse pair scoring uses only qualifying BPM neighborhoods instead
    // of 121 complete rescans. This is a deterministic, hardware-neutral
    // CONTROL timer budget, NOT a wall-clock benchmark.
    check(state.tempo.scoringComparisons>0);
    check(state.tempo.scoringComparisons<120000);
    check(state.tempo.phaseLocked && state.tempo.phaseConfidence>=.50f);
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
// A full backing groove can carry snare/hi-hat accents on the eighth-note
// offbeat. Its BPM may be correct while a quarter-note phase is ambiguous.
static void harmonicAndPhaseAmbiguity() {
    NativeMusicTempoMonitor tap;
    check(tap.select(ChannelId{301},7,13,10000));
    const auto groove=[&](int index,float offbeat) {
        std::array<float,128> left{},right{};
        const int p=index%50; // quarter note at 120 BPM
        const float value=p==0?.78f:p==25?offbeat:0.f;
        left.fill(value);right.fill(value);
        noHeap.store(true,std::memory_order_release);
        (void)tap.beginAudioBlock(7,64);
        tap.capture(ChannelId{301},left.data(),right.data(),64,7,13);
        (void)tap.beginAudioBlock(7,36);
        tap.capture(ChannelId{301},left.data()+64,right.data()+64,36,7,13);
        noHeap.store(false,std::memory_order_release);
        if(index%8==0)(void)tap.poll(7,13);
    };
    for(int i=0;i<1150;++i)groove(i,.035f);
    auto baseline=tap.poll(7,13);
    check(baseline.tempo.locked && std::abs(baseline.tempo.bpm-120.f)<=3.f);
    check(baseline.tempo.phaseLocked);
    // For 11s, the equal-strength offbeat pulse could be mistaken for a
    // new beat lattice; do not switch to 60 BPM, or claim a phase lock.
    for(int i=1150;i<2450;++i)groove(i,.78f);
    auto ambiguous=tap.poll(7,13);
    check(ambiguous.tempo.locked);
    check(ambiguous.tempo.scoringComparisons>0);
    check(ambiguous.tempo.scoringComparisons<120000);
    check(std::abs(ambiguous.tempo.bpm-120.f)<=3.f);
    check(!ambiguous.tempo.phaseLocked);
    // The music clears; the true quarter-note lattice should recover.
    for(int i=2450;i<3650;++i)groove(i,.035f);
    auto recovered=tap.poll(7,13);
    if(!recovered.tempo.locked || std::abs(recovered.tempo.bpm-120.f)>3.f)
        std::fprintf(stderr,
            "MUSIC_OFFBEAT_RECOVERY tick=3650 locked=%d bpm=%.3f confidence=%.3f phase=%d phaseConfidence=%.3f comparisons=%u dropped=%u\n",
            static_cast<int>(recovered.tempo.locked),recovered.tempo.bpm,
            recovered.tempo.confidence,static_cast<int>(recovered.tempo.phaseLocked),
            recovered.tempo.phaseConfidence,recovered.tempo.scoringComparisons,
            recovered.dropped);
    check(recovered.tempo.locked);
    check(recovered.tempo.scoringComparisons>0);
    check(recovered.tempo.scoringComparisons<120000);
    check(std::abs(recovered.tempo.bpm-120.f)<=3.f);
    check(recovered.tempo.phaseLocked);
    check(recovered.dropped==0);
}
// A formerly locked backing can switch to densely accented but weak
// percussion. This is enough rhythmic activity to score a candidate, yet
// NOT enough evidence to keep the old beat grid or invent a new one.
static void weakBusyGrooveMustReleaseBeatGrid() {
    NativeMusicTempoMonitor tap;
    check(tap.select(ChannelId{301},7,13,10000));
    for(int n=0;n<1100;++n)renderWindow(tap,n,50,.78f);
    auto locked=tap.poll(7,13);
    check(locked.tempo.locked&&locked.tempo.phaseLocked);
    for(int n=1100;n<2350;++n)renderWindow(tap,n,8,.10f);
    const auto ambiguous=tap.poll(7,13);
    check(ambiguous.active);
    check(!ambiguous.tempo.locked && !ambiguous.tempo.phaseLocked);
    check(ambiguous.tempo.confidence<.28f);
    check(ambiguous.dropped==0);
    // Once clear music returns the phase can be acquired again.
    for(int n=2350;n<3550;++n)renderWindow(tap,n,50,.78f);
    const auto recovered=tap.poll(7,13);
    check(recovered.tempo.locked &&
          std::abs(recovered.tempo.bpm-120.f)<=3.f);
    check(recovered.tempo.phaseLocked);
}
// If the audio callback stops entirely (ASIO suspension/reconfiguration),
 // tick-based expiry alone cannot advance: fail closed on CONTROL time.
static void stoppedAudioCallbackMustRevokeOldBeat() {
    NativeMusicTempoMonitor tap;
    check(tap.select(ChannelId{301},7,13,10000));
    for(int n=0;n<1100;++n)
        renderWindow(tap,n,50,.75f,false,7,13,1000+static_cast<std::uint64_t>(n)*10);
    auto locked=tap.poll(7,13,12000);
    check(locked.active && locked.tempo.locked && locked.tempo.phaseLocked);
    // A short interruption must not cause a needless BPM reset.
    const auto hold=tap.poll(7,13,14450);
    check(hold.active && hold.tempo.locked);
    // No render/capture calls at all for >3.5s: stale beat cannot survive.
    const auto stale=tap.poll(7,13,15501);
    check(stale.active && stale.source.value==301);
    check(!stale.tempo.locked && !stale.tempo.phaseLocked);
    check(stale.tempo.beatAnchorFrame==0 && stale.tempo.bpm==0.f);
    // Resume the very same source; require fresh musical evidence.
    for(int n=1100;n<2300;++n)
        renderWindow(tap,n,50,.75f,false,7,13,
            15510+static_cast<std::uint64_t>(n-1100)*10);
    const auto recovered=tap.poll(7,13,27600);
    check(recovered.active && recovered.tempo.locked);
    check(recovered.tempo.phaseLocked && std::abs(recovered.tempo.bpm-120.f)<=3.f);
}
static void steadyPadIsNotAClock() {
    NativeMusicTempoMonitor tap;
    check(tap.select(ChannelId{301},7,13,10000));
    std::array<float,128> pcm{};pcm.fill(.22f);
    for(int index=0;index<1200;++index) {
        noHeap.store(true);
        (void)tap.beginAudioBlock(7,100);
        tap.capture(ChannelId{301},pcm.data(),pcm.data(),100,7,13);
        noHeap.store(false);
        if(index%8==0)(void)tap.poll(7,13);
    }
    const auto state=tap.poll(7,13);
    check(!state.tempo.locked);
    // A sustained pad is not evidence of rhythm. It cannot trigger a
    // high-cost candidate scan simply because PCM keeps arriving.
    check(state.tempo.scoringComparisons==0);
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
    check(state.tempo.phaseLocked&&state.tempo.phaseConfidence>=.50f);
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
    const auto newGraphFx=[&](ChannelId,float*,float*,int n) noexcept {
        const auto block=NativeBeatFrameScope::current();
        check(block.valid && block.epoch==epoch &&
              block.revision==2 && block.startFrame==100000 && n==100);
    };
    noHeap.store(true,std::memory_order_release);
    const bool next=graph.render(epoch,physical,1,
        outL.data(),outR.data(),100,false,newGraphFx);
    noHeap.store(false,std::memory_order_release);
    check(next);
    check(!NativeBeatFrameScope::current().valid);
    graph.unpublish();
    graph.pollRetirement();
}
static void callbackClockEpochAndNestedScope() {
    using armix::mixer::NativeBeatFrameScope;
    NativeMusicTempoMonitor tap;
    check(!NativeBeatFrameScope::current().valid);
    noHeap.store(true,std::memory_order_release);
    check(tap.beginAudioBlock(82,64)==0);
    check(tap.beginAudioBlock(82,128)==64);
    {
        NativeBeatFrameScope outer{82,7,192};
        const auto a=NativeBeatFrameScope::current();
        check(a.valid&&a.epoch==82&&a.revision==7&&a.startFrame==192);
        {
            NativeBeatFrameScope nested{82,8,320};
            const auto b=NativeBeatFrameScope::current();
            check(b.valid&&b.revision==8&&b.startFrame==320);
        }
        const auto restored=NativeBeatFrameScope::current();
        check(restored.valid&&restored.revision==7&&restored.startFrame==192);
    }
    check(!NativeBeatFrameScope::current().valid);
    check(tap.beginAudioBlock(82,256)==192);
    // Physical device restart must establish a NEW sample origin. This
    // prevents a beat from the old ASIO epoch appearing on the new device.
    check(tap.beginAudioBlock(83,64)==0);
    check(tap.beginAudioBlock(83,128)==64);
    noHeap.store(false,std::memory_order_release);
}
int main() {
    lockTempo(72);
    lockTempo(95);
    lockTempo(120);
    lockTempo(160);
    lockTempo(120,true);
    harmonicAndPhaseAmbiguity();
    weakBusyGrooveMustReleaseBeatGrid();
    stoppedAudioCallbackMustRevokeOldBeat();
    steadyPadIsNotAClock();
    failClosed();
    callbackClockEpochAndNestedScope();
    actualGraphSourcePcm();
    std::puts("PASS: native music-channel BPM 72/95/120/160, silence holdover, stale graph, no RT heap");
}
