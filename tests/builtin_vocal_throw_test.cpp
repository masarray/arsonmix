// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/VocalThrow.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>
#define check(x) do {if(!(x)){std::fprintf(stderr,"FAIL:%s:%d %s\n",__FILE__,__LINE__,#x);std::abort();}}while(false)
// Catch allocations from inside the audio callback, not from preparation.
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
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}

using armix::builtin_fx::VocalThrow;
using armix::builtin_fx::VocalThrowSettings;
static void run(VocalThrow& fx,float l,float r,int frames,float& maxL,float& maxR) {
    std::array<float,256> left{},right{};
    for(int begin=0;begin<frames;begin+=256) {
        const int n=std::min(256,frames-begin);
        left.fill(l);right.fill(r);
        noHeap.store(true,std::memory_order_release);
        fx.processStereo(left.data(),right.data(),static_cast<std::size_t>(n));
        noHeap.store(false,std::memory_order_release);
        for(int i=0;i<n;++i) {
            check(std::isfinite(left[i])&&std::isfinite(right[i]));
            maxL=std::max(maxL,std::abs(left[i]));
            maxR=std::max(maxR,std::abs(right[i]));
        }
    }
}
static void validation() {
    check(VocalThrow::addedLatencySamples==0);
    VocalThrowSettings settings{};
    VocalThrow::PreparedEdit packet{};
    check(!VocalThrow::prepareEdit(settings,7999,packet));
    check(VocalThrow::prepareEdit(settings,48000,packet));
    check(packet.intervalSamples==24000);
    settings.division=1;
    check(VocalThrow::prepareEdit(settings,48000,packet));
    check(packet.intervalSamples==12000);
    settings.division=2;
    check(VocalThrow::prepareEdit(settings,48000,packet));
    check(packet.intervalSamples==18000);
    settings.bpm=80;settings.division=0;
    check(VocalThrow::prepareEdit(settings,48000,packet));
    check(packet.intervalSamples==36000);
    settings.thresholdDb=std::numeric_limits<float>::quiet_NaN();
    check(!VocalThrow::prepareEdit(settings,48000,packet));
    settings=VocalThrowSettings{};settings.mode=3;
    check(!VocalThrow::prepareEdit(settings,48000,packet));
    settings=VocalThrowSettings{};settings.repeats=7;
    check(!VocalThrow::prepareEdit(settings,48000,packet));
    settings=VocalThrowSettings{};settings.captureMs=1550;
    check(!VocalThrow::prepareEdit(settings,48000,packet));
}
static void manualAndDry(double rate) {
    VocalThrow fx;check(fx.prepare(rate));
    auto s=fx.settingsForTests();s.mode=0;s.captureMs=300;s.silenceMs=150;
    check(fx.resetUnpublished(s));
    float peakL=0,peakR=0;
    // No trigger: the leading vocalist is bit-exact dry, even in an insert.
    std::array<float,257> left{},right{};
    left.fill(.35f);right.fill(-.20f);
    noHeap.store(true);
    fx.processStereo(left.data(),right.data(),left.size());
    noHeap.store(false);
    for(std::size_t n=0;n<left.size();++n){
        check(left[n]==.35f);check(right[n]==-.20f);
    }
    run(fx,.6f,.3f,static_cast<int>(rate*.35),peakL,peakR);
    run(fx,0,0,static_cast<int>(rate*.06),peakL,peakR);
    check(fx.throwCount()==0);
    noHeap.store(true);
    const bool fired=fx.throwNow();
    noHeap.store(false);
    check(fired&&fx.throwCount()==1);
    peakL=peakR=0;
    run(fx,0,0,static_cast<int>(rate*.10),peakL,peakR);
    check(peakL==0&&peakR==0); // beat-scheduled, no immediate echo smear
    run(fx,0,0,static_cast<int>(rate*.75),peakL,peakR);
    check(peakL>.01f&&peakR>.005f);
    // Recorded stereo is preserved (same gain on both channels).
    check(peakL>peakR*1.4f);
    noHeap.store(true);
    const bool staleCue=fx.throwNow();
    noHeap.store(false);
    check(!staleCue); // 700ms after last vocal: never report an empty throw
}
static void automaticAndHybrid(double rate) {
    VocalThrow fx;check(fx.prepare(rate));
    auto s=fx.settingsForTests();
    s.mode=1;s.captureMs=220;s.silenceMs=120;s.thresholdDb=-30;
    check(fx.resetUnpublished(s));
    float l=0,r=0;
    run(fx,.5f,.35f,static_cast<int>(rate*.3),l,r);
    run(fx,0,0,static_cast<int>(rate*.23),l,r);
    check(fx.throwCount()==1);
    run(fx,0,0,static_cast<int>(rate*1.0),l,r);
    check(fx.throwCount()==1); // no retrigger on the echoed tail
    run(fx,.5f,.35f,static_cast<int>(rate*.28),l,r);
    run(fx,0,0,static_cast<int>(rate*.23),l,r);
    check(fx.throwCount()==2);

}
static void hybridEditAndNoHeap() {
    VocalThrow fx;check(fx.prepare(48000));
    VocalThrowSettings s{};s.mode=2;s.bpm=95;s.division=1;s.repeats=6;
    VocalThrow::PreparedEdit edit{};check(VocalThrow::prepareEdit(s,48000,edit));
    check(!fx.applyPrepared(VocalThrow::PreparedEdit{}));
    auto stale=edit;stale.sampleRate=44100;check(!fx.applyPrepared(stale));
    noHeap.store(true);const bool ok=fx.applyPrepared(edit);noHeap.store(false);
    check(ok);
    float l=0,r=0;
    run(fx,.4f,.2f,10000,l,r);
    noHeap.store(true);const bool fired=fx.throwNow();noHeap.store(false);
    check(fired);
    run(fx,0,0,200000,l,r);
    check(std::isfinite(l)&&std::isfinite(r));
}
int main() {
    validation();
    manualAndDry(8000);
    manualAndDry(44100);
    manualAndDry(48000);
    automaticAndHybrid(8000);
    automaticAndHybrid(48000);
    hybridEditAndNoHeap();
    std::puts("PASS: vocal-throw native dry/rolling capture/phrase/echo/tempo/no-heap");
}
