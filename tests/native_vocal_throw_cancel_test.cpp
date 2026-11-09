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
int main(){
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
    render(p,b,m,0.f);
    VocalThrowProcessor::Completion c{};
    check(p.pollCompletion(c)&&c.requestId==101&&!c.applied);
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
