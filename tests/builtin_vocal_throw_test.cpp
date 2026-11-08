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
// Two separate frozen phrases must coexist. A new cue must not chop the
// already sounding repeat. All fixtures run as bounded PCM chunks without
// any callback allocations, including at the highest allowed device rate.
static void twoTailsStayLive(double rate) {
    VocalThrow reference,overlap;
    check(reference.prepare(rate)&&overlap.prepare(rate));
    auto s=reference.settingsForTests();
    s.mode=0;s.bpm=200;s.division=1;s.repeats=3;
    s.captureMs=300;s.silenceMs=120;
    check(reference.resetUnpublished(s)&&overlap.resetUnpublished(s));
    float l=0.f,r=0.f;
    auto both=[&](float input,int frames) {
        run(reference,input,input*.5f,frames,l,r);
        run(overlap,input,input*.5f,frames,l,r);
    };
    both(.5f,static_cast<int>(rate*.20));
    both(0.f,static_cast<int>(rate*.10));
    noHeap.store(true);
    const bool firstA=reference.throwNow(),firstB=overlap.throwNow();
    noHeap.store(false);
    check(firstA&&firstB);
    both(.4f,static_cast<int>(rate*.12));
    both(0.f,static_cast<int>(rate*.03));
    noHeap.store(true);
    const bool second=overlap.throwNow();
    const bool saturated=overlap.throwNow();
    noHeap.store(false);
    check(second&&!saturated);
    check(overlap.throwCount()==2);
    // From 0.45 to 0.485 s the second voice is preparing, and its first
    // scheduled echo is still in the future. Output must be IDENTICAL to
    // the reference's first tail, not abruptly muted by the second cue.
    std::array<float,128> refL{},refR{},mixL{},mixR{};
    float maxReference=0.f,maxOverlap=0.f;
    const int compareFrames=static_cast<int>(rate*.035);
    for(int done=0;done<compareFrames;done+=128) {
        const auto count=static_cast<std::size_t>(std::min(128,compareFrames-done));
        refL.fill(0.f);refR.fill(0.f);mixL.fill(0.f);mixR.fill(0.f);
        noHeap.store(true);
        reference.processStereo(refL.data(),refR.data(),count);
        overlap.processStereo(mixL.data(),mixR.data(),count);
        noHeap.store(false);
        for(std::size_t i=0;i<count;++i) {
            maxReference=std::max(maxReference,std::abs(refL[i]));
            maxOverlap=std::max(maxOverlap,std::abs(mixL[i]));
            check(std::abs(refL[i]-mixL[i])<1.e-5f);
        }
    }
    check(maxReference>.001f&&maxOverlap>.001f);
    run(overlap,0.f,0.f,static_cast<int>(rate*.8),l,r);
    check(overlap.throwCount()==2);
}
static void autoThenManualMustNotDoubleEcho() {
    VocalThrow fx;check(fx.prepare(48000));
    auto s=fx.settingsForTests();
    s.mode=2;s.silenceMs=120;s.captureMs=250;
    check(fx.resetUnpublished(s));
    float l=0.f,r=0.f;
    run(fx,.5f,.25f,14400,l,r);
    run(fx,0.f,0.f,12000,l,r);
    check(fx.throwCount()==1);
    noHeap.store(true);
    const bool duplicated=fx.throwNow();
    noHeap.store(false);
    check(!duplicated&&fx.throwCount()==1);
    // A fresh phrase re-arms the manual path: previous auto trigger must
    // not poison legitimate new cues when the singer starts again.
    run(fx,.5f,.25f,12000,l,r);
    noHeap.store(true);
    const bool newPhrase=fx.throwNow();
    noHeap.store(false);
    check(newPhrase&&fx.throwCount()==2);
}
static void denseRepeatsStayBounded() {
    VocalThrow fx;check(fx.prepare(48000));
    auto settings=fx.settingsForTests();
    settings.mode=0;settings.bpm=200;settings.division=1;
    settings.repeats=6;settings.decay=.9f;
    settings.captureMs=1200.f;settings.wet=1.f;settings.ducking=0.f;
    check(fx.resetUnpublished(settings));
    float maxL=0.f,maxR=0.f;
    run(fx,.75f,.6f,48000,maxL,maxR);
    noHeap.store(true);
    const bool fired=fx.throwNow();
    noHeap.store(false);
    check(fired);
    maxL=maxR=0.f;
    run(fx,0.f,0.f,static_cast<int>(48000*2.5),maxL,maxR);
    check(maxL>.20f&&maxR>.10f);
    // An exposed six-tap feedback-like burst must not produce >0dBFS
    // solely because echo taps overlap (dry path is unchanged).
    check(maxL<=.751f&&maxR<=.601f);
}
static void manualOverrideSuppressesAuto() {
    VocalThrow fx;check(fx.prepare(48000));
    auto s=fx.settingsForTests();
    s.mode=2;s.silenceMs=160;s.captureMs=250;
    check(fx.resetUnpublished(s));
    float l=0.f,r=0.f;
    run(fx,.5f,.25f,12000,l,r); // phrase 1
    run(fx,0.f,0.f,2400,l,r); // manual cue before auto silence timeout
    noHeap.store(true);
    const bool fired=fx.throwNow();
    noHeap.store(false);
    check(fired&&fx.throwCount()==1);
    run(fx,0.f,0.f,12000,l,r);
    check(fx.throwCount()==1); // same phrase must not double-fire
    run(fx,.5f,.25f,12000,l,r); // genuinely new phrase
    // The 18ms release envelope must fall below the voice gate BEFORE
    // the 160ms silence threshold begins. 208ms total is insufficient
    // with a sustained 0.5 peak and threshold -35dB.
    run(fx,0.f,0.f,9000,l,r);
    check(fx.throwCount()==1); // no premature false-positive throw
    run(fx,0.f,0.f,5000,l,r);
    check(fx.throwCount()==2); // new phrase, after full release + gate
}
// Accepted music BPM affects only new frozen tails, never retimes a tail
// already singing. An unlocked source must use persisted Manual/TAP BPM.
static void musicTempoChangesOnlyFutureTail() {
    VocalThrow music,manual;
    check(music.prepare(48000)&&manual.prepare(48000));
    auto s=music.settingsForTests();
    s.mode=0;s.bpm=120.f;s.division=0;s.repeats=1;
    s.captureMs=120.f;s.wet=1.f;s.ducking=0.f;
    check(music.resetUnpublished(s)&&manual.resetUnpublished(s));
    float a=0.f,b=0.f;
    run(music,.6f,.4f,12000,a,b);
    run(manual,.6f,.4f,12000,a,b);
    run(music,0.f,0.f,2880,a,b);
    run(manual,0.f,0.f,2880,a,b);
    noHeap.store(true);
    music.setMusicIntervalSamples(16000); // quarter-note at 180 BPM
    const bool firedMusic=music.throwNow();
    const bool firedManual=manual.throwNow();
    // The selected backing bus can disappear immediately after the cue.
    // Its already frozen tail must retain the accepted .25s repeat interval.
    music.setMusicIntervalSamples(0);
    noHeap.store(false);
    check(firedMusic&&firedManual);
    a=b=0.f;
    run(music,0.f,0.f,12960,a,b); // 270 ms after cue
    check(a==0.f&&b==0.f);
    a=b=0.f;
    run(music,0.f,0.f,8160,a,b); // repeat around 290-410 ms
    check(a>.01f&&b>.01f);
    a=b=0.f;
    run(manual,0.f,0.f,17760,a,b); // manual .5s must still be silent
    check(a==0.f&&b==0.f);
    a=b=0.f;
    run(manual,0.f,0.f,7680,a,b);
    check(a>.01f&&b>.01f);
}
int main() {
    validation();
    manualAndDry(8000);
    manualAndDry(44100);
    manualAndDry(48000);
    automaticAndHybrid(8000);
    automaticAndHybrid(48000);
    hybridEditAndNoHeap();
    twoTailsStayLive(8000);
    twoTailsStayLive(48000);
    twoTailsStayLive(96000);
    manualOverrideSuppressesAuto();
    autoThenManualMustNotDoubleEcho();
    denseRepeatsStayBounded();
    musicTempoChangesOnlyFutureTail();
    std::puts("PASS: vocal-throw native dry/rolling capture/phrase/echo/tempo/no-heap");
}
