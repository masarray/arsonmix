// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/NativeMusicTempoMonitor.h"
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
    tap.capture(ChannelId{301},l.data(),r.data(),64,epoch,revision);
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
int main() {
    lockTempo(72);
    lockTempo(95);
    lockTempo(120);
    lockTempo(160);
    lockTempo(120,true);
    failClosed();
    std::puts("PASS: native music-channel BPM 72/95/120/160, silence holdover, stale graph, no RT heap");
}
