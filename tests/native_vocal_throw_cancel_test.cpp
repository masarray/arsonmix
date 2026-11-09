// SPDX-License-Identifier: AGPL-3.0-only
// Actual JUCE SPSC boundary, no ASIO device needed: an old cue must not
// sound when the device resumes after the native UI already rejected it.
#include "../engine/builtin_fx/VocalThrowProcessor.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#define check(x) do{if(!(x)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(false)
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
using armix::builtin_fx::VocalThrowProcessor;
static void render(VocalThrowProcessor& p,juce::AudioBuffer<float>& b,
                   juce::MidiBuffer& m,float amplitude) {
    for(int c=0;c<2;++c)for(int i=0;i<b.getNumSamples();++i)
        b.setSample(c,i,c?amplitude*.7f:amplitude);
    noHeap.store(true,std::memory_order_release);
    p.processBlock(b,m);
    noHeap.store(false,std::memory_order_release);
}

// Reproduces a device restart exactly between an AUDIO-applied settings edit
// and the CONTROL-owned ACK. The latest DSP state (not the stale UI shadow)
// must win; old-rate queued edits must fail without changing it.
static bool equalSettings(const armix::builtin_fx::VocalThrowSettings& a,
                          const armix::builtin_fx::VocalThrowSettings& b) {
    return a.mode==b.mode && a.bpm==b.bpm && a.division==b.division &&
           a.repeats==b.repeats && a.captureMs==b.captureMs &&
           a.silenceMs==b.silenceMs && a.thresholdDb==b.thresholdDb &&
           a.wet==b.wet && a.decay==b.decay && a.ducking==b.ducking;
}
static void compareAudio(VocalThrowProcessor& p,armix::builtin_fx::VocalThrow& reference,
                         int blocks,float amplitude,float* echo=nullptr) {
    juce::AudioBuffer<float> block(2,128);
    juce::MidiBuffer midi;
    std::array<float,128> left{},right{};
    for(int b=0;b<blocks;++b) {
        for(int n=0;n<128;++n) {
            const auto v=amplitude;
            left[static_cast<std::size_t>(n)]=v;
            right[static_cast<std::size_t>(n)]=v*.7f;
        }
        // Audio callback must never allocate, including first post-restart.
        noHeap.store(true,std::memory_order_release);
        reference.processStereo(left.data(),right.data(),left.size());
        noHeap.store(false,std::memory_order_release);
        render(p,block,midi,amplitude);
        for(int n=0;n<128;++n) {
            const auto l=left[static_cast<std::size_t>(n)];
            const auto r=right[static_cast<std::size_t>(n)];
            check(std::isfinite(l)&&std::isfinite(r));
            check(std::abs(block.getSample(0,n)-l)<3.e-6f);
            check(std::abs(block.getSample(1,n)-r)<3.e-6f);
            if(echo && amplitude==0.f)
                *echo+=std::abs(l)+std::abs(r);
        }
    }
}
static void testAudioAppliedStateSurvivesDeviceRestart() {
    using armix::builtin_fx::VocalThrow;
    for(const double firstRate:{44100.,48000.,96000.}) {
        VocalThrowProcessor p;
        p.prepareToPlay(firstRate,128);
        check(p.isReady());
        p.sealForRealtime();
        auto latest=p.acceptedSettings();
        latest.mode=0; latest.bpm=156.f; latest.division=1;
        latest.captureMs=300.f;latest.silenceMs=180.f;
        latest.wet=.84f;latest.decay=.67f;latest.ducking=0.f;
        latest.repeats=2;
        check(p.queueSettings(latest,301));
        juce::AudioBuffer<float> first(2,128);juce::MidiBuffer midi;
        render(p,first,midi,0.f);
        check(!p.stateSnapshotQuiescent());
        check(p.acceptedSettings().bpm!=latest.bpm); // ACK not yet polled
        p.releaseResources();
        const auto secondRate=firstRate==96000.?48000.:96000.;
        p.prepareToPlay(secondRate,128);
        check(p.isReady()&&p.acceptedSampleRate()==secondRate);
        check(p.acceptedSettings().bpm!=latest.bpm);
        VocalThrow reference;
        check(reference.prepare(secondRate)&&reference.resetUnpublished(latest));
        compareAudio(p,reference,140,.7f);
        compareAudio(p,reference,45,0.f);
        check(p.queueTrigger(302));
        check(reference.throwNow());
        float echo=0.f;
        compareAudio(p,reference,320,0.f,&echo);
        check(echo>.1f); // actual post-restart wet PCM, not only settings
        VocalThrowProcessor::Completion completion{};
        check(p.pollCompletion(completion)&&completion.requestId==301&&completion.applied);
        check(equalSettings(completion.settings,latest));
        check(p.pollCompletion(completion)&&completion.requestId==302&&completion.applied);
        check(equalSettings(p.acceptedSettings(),latest));
        // Queued before yet another sample-rate change: packet is stale.
        auto other=latest;other.bpm=85.f;
        check(p.queueSettings(other,303));
        p.releaseResources();
        const auto thirdRate=secondRate==44100.?48000.:44100.;
        p.prepareToPlay(thirdRate,257);
        check(p.isReady()&&p.acceptedSampleRate()==thirdRate);
        check(reference.prepare(thirdRate)&&reference.resetUnpublished(latest));
        compareAudio(p,reference,12,.7f);
        check(p.pollCompletion(completion)&&completion.requestId==303&&!completion.applied);
        check(equalSettings(p.acceptedSettings(),latest));
        check(p.stateSnapshotQuiescent());
        // Fresh revision with new coefficients succeeds after restart.
        check(p.queueSettings(other,304));
        render(p,first,midi,0.f);
        check(p.pollCompletion(completion)&&completion.requestId==304&&completion.applied);
        check(equalSettings(p.acceptedSettings(),other));
    }
}
static void testColdShowRestoreAfterStart() {
    using armix::builtin_fx::VocalThrow;
    auto scene=armix::builtin_fx::VocalThrowSettings{};
    scene.mode=0;scene.bpm=92.f;scene.division=2;
    scene.captureMs=670.f;scene.wet=.77f;scene.repeats=4;
    juce::MemoryBlock stored;
    {
        juce::MemoryOutputStream writer(stored,false);
        writer.writeInt(0x41565448);writer.writeInt(1);
        writer.writeInt(scene.mode);writer.writeFloat(scene.bpm);
        writer.writeInt(scene.division);writer.writeInt(scene.repeats);
        writer.writeFloat(scene.captureMs);writer.writeFloat(scene.silenceMs);
        writer.writeFloat(scene.thresholdDb);writer.writeFloat(scene.wet);
        writer.writeFloat(scene.decay);writer.writeFloat(scene.ducking);
    }
    check(stored.getSize()==48);
    VocalThrowProcessor cold;
    check(cold.restoreUnpublishedState(stored.getData(),
                                        static_cast<int>(stored.getSize())));
    cold.prepareToPlay(48000.,128);
    check(cold.isReady()&&equalSettings(cold.acceptedSettings(),scene));
    VocalThrow reference;
    check(reference.prepare(48000.)&&reference.resetUnpublished(scene));
    compareAudio(cold,reference,50,.65f);
    cold.releaseResources();
    cold.prepareToPlay(96000.,256);
    check(cold.isReady()&&equalSettings(cold.acceptedSettings(),scene));
    check(reference.prepare(96000.)&&reference.resetUnpublished(scene));
    compareAudio(cold,reference,50,.65f);
}

int main(){
    testAudioAppliedStateSurvivesDeviceRestart();
    testColdShowRestoreAfterStart();
    VocalThrowProcessor p;
    p.prepareToPlay(48000.,128);
    check(p.isReady());
    p.sealForRealtime();
    juce::AudioBuffer<float> b(2,128);
    juce::MidiBuffer m;
    auto old=p.acceptedSettings();
    auto changed=old;changed.bpm=156.f;changed.wet=.71f;
    // Expired settings never mutate the DSP or its accepted settings shadow.
    check(p.queueSettings(changed,101));
    p.cancelQueuedRequest(101);
    // A second expired UI edit must not make the FIRST one executable
    // again when ASIO eventually resumes.
    check(p.queueSettings(changed,105));
    p.cancelQueuedRequest(105);
    render(p,b,m,0.f);
    VocalThrowProcessor::Completion c{};
    check(p.pollCompletion(c)&&c.requestId==101&&!c.applied);
    render(p,b,m,0.f);
    check(p.pollCompletion(c)&&c.requestId==105&&!c.applied);
    check(p.acceptedSettings().bpm==old.bpm);
    // A fresh request remains valid even after canceling an older ID.
    check(p.queueSettings(changed,102));
    render(p,b,m,0.f);
    check(p.pollCompletion(c)&&c.requestId==102&&c.applied);
    check(p.acceptedSettings().bpm==156.f);
    // Prime a real voiced phrase and then stop it; the trigger is valid.
    for(int n=0;n<100;++n)render(p,b,m,.7f);
    for(int n=0;n<32;++n)render(p,b,m,0.f);
    check(p.queueTrigger(103));
    p.cancelQueuedRequest(103);
    render(p,b,m,0.f);
    check(p.pollCompletion(c)&&c.requestId==103&&!c.applied);
    check(p.observedThrowCount()==0);
    check(p.queueTrigger(104));
    render(p,b,m,0.f);
    check(p.pollCompletion(c)&&c.requestId==104&&c.applied);
    check(p.observedThrowCount()==1);
    check(p.stateSnapshotQuiescent());
    std::puts("PASS: expired Vocal Throw cue and settings cannot replay on ASIO resume");
}
