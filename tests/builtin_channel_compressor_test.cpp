// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/ChannelCompressor.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
// Release CTests must NOT use assert(): NDEBUG would silently disable the test.
#define check(expr) do { if(!(expr)) { \
    std::fprintf(stderr,"FAIL:%s:%d: %s\\n",__FILE__,__LINE__,#expr); \
    std::abort(); \
} } while(false)

namespace {
std::atomic<bool> prohibitHeap{false};
}
void* operator new(std::size_t size) {
    if(prohibitHeap.load(std::memory_order_relaxed))std::abort();
    if(void* ptr=std::malloc(size?size:1))return ptr;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    if(prohibitHeap.load(std::memory_order_relaxed))std::abort();
    if(void* ptr=std::malloc(size?size:1))return ptr;
    throw std::bad_alloc();
}
void operator delete(void* ptr) noexcept {std::free(ptr);}
void operator delete[](void* ptr) noexcept {std::free(ptr);}
void operator delete(void* ptr,std::size_t) noexcept {std::free(ptr);}
void operator delete[](void* ptr,std::size_t) noexcept {std::free(ptr);}

using armix::builtin_fx::ChannelCompressor;
using armix::builtin_fx::CompressorSettings;

static void testValidationAndLatency() {
    ChannelCompressor compressor;
    check(ChannelCompressor::addedLatencySamples==0);
    check(!compressor.prepare(7999));
    check(compressor.prepare(48000));
    CompressorSettings s{};
    s.ratio=std::numeric_limits<float>::quiet_NaN();
    ChannelCompressor::PreparedEdit packet{};
    check(!ChannelCompressor::prepareEdit(s,48000,packet));
    s=CompressorSettings{};s.attackMs=0;
    check(!ChannelCompressor::prepareEdit(s,48000,packet));
    s=CompressorSettings{};
    check(ChannelCompressor::prepareEdit(s,48000,packet));
    check(!compressor.applyPrepared(ChannelCompressor::PreparedEdit{}));
    auto stale=packet;stale.sampleRate=44100;
    check(!compressor.applyPrepared(stale));
    check(compressor.applyPrepared(packet));
}
static float rms(const float* v,std::size_t n) {
    double power=0.;
    for(std::size_t i=0;i<n;++i)power+=v[i]*v[i];
    return std::sqrt(power/static_cast<double>(n));
}
static void testTransferAndStereo() {
    ChannelCompressor c;
    check(c.prepare(48000));
    CompressorSettings s{};
    s.thresholdDb=-20;s.ratio=4;s.attackMs=0.1f;s.releaseMs=100;
    s.kneeDb=0;s.makeupDb=0;s.mix=1;
    check(c.resetUnpublished(s));
    std::array<float,4800> l{},r{};
    l.fill(0.5f);r.fill(0.25f);
    c.processStereo(l.data(),r.data(),l.size());
    check(std::abs(c.inputPeak()-0.5f)<1.e-6f);
    check(c.outputPeak()>0.f&&c.outputPeak()<c.inputPeak());
    // Feed-forward gain at -6.02 dB, threshold -20, ratio 4 yields
    // about 10.48 dB gain reduction. Both channels share gain exactly.
    check(c.gainReductionDb()>9.5f&&c.gainReductionDb()<11.5f);
    for(std::size_t i=200;i<l.size();++i) {
        check(std::abs(l[i]-2*r[i])<1e-5f);
        check(std::isfinite(l[i])&&std::isfinite(r[i]));
    }
    check(rms(l.data()+4000,800)<0.22f);
    check(rms(l.data()+4000,800)>0.10f);
}
static void testDryAndBypass() {
    ChannelCompressor c;check(c.prepare(44100));
    CompressorSettings s{};s.thresholdDb=-55;s.ratio=20;s.mix=0.f;s.makeupDb=12;
    check(c.resetUnpublished(s));
    std::array<float,128> mono{};
    mono.fill(0.45f);
    c.processMono(mono.data(),mono.size());
    check(std::abs(c.inputPeak()-.45f)<1.e-6f);
    check(std::abs(c.outputPeak()-.45f)<1.e-6f);
    for(float value:mono)check(value==0.45f); // exact mix=0 dry
    s.mix=1;s.ratio=1;s.makeupDb=0;
    check(c.resetUnpublished(s));
    mono.fill(0.45f);c.processMono(mono.data(),mono.size());
    for(float value:mono)check(value==0.45f); // unity ratio and no makeup
}
static void testSoftKneeAndRuntimeEdits() {
    ChannelCompressor hard,soft;
    check(hard.prepare(48000)&&soft.prepare(48000));
    CompressorSettings s{};
    s.thresholdDb=-12;s.ratio=4;s.attackMs=.1f;
    s.kneeDb=0;check(hard.resetUnpublished(s));
    s.kneeDb=12;check(soft.resetUnpublished(s));
    std::array<float,2000> a{},b{};
    a.fill(0.25f);b.fill(0.25f); // -12.04 dB, soft knee bends before threshold
    hard.processMono(a.data(),a.size());
    soft.processMono(b.data(),b.size());
    check(b.back()<a.back());
    auto edit=s;edit.makeupDb=6.f;edit.mix=.65f;
    ChannelCompressor::PreparedEdit packet{};
    check(ChannelCompressor::prepareEdit(edit,48000,packet));
    check(soft.applyPrepared(packet));
    for(int n=0;n<50;++n)soft.processMono(b.data(),b.size());
    for(float value:b)check(std::isfinite(value));
}
static void testMultipleRatesAndIndependentChannels() {
    for(double rate:{44100.,48000.,96000.}) {
        ChannelCompressor a,b;
        check(a.prepare(rate)&&b.prepare(rate));
        auto loud=a.settingsForOfflineTests();
        loud.thresholdDb=-35;loud.ratio=5;
        check(a.resetUnpublished(loud));
        std::array<float,256> x{},y{};
        x.fill(.4f);y.fill(.4f);
        for(int n=0;n<16;++n) {
            a.processMono(x.data(),x.size());
            b.processMono(y.data(),y.size());
        }
        check(x.back()<y.back());
        check(b.gainReductionDb()<a.gainReductionDb());
    }
}
static void testFirstSampleAndNoHeapInCallback() {
    ChannelCompressor core;
    check(core.prepare(48000));
    auto params=core.settingsForOfflineTests();
    params.thresholdDb=-32;params.ratio=5;params.attackMs=.1f;
    ChannelCompressor::PreparedEdit edit{};
    check(ChannelCompressor::prepareEdit(params,48000,edit));
    std::array<float,512> left{},right{};
    left[0]=.75f;right[0]=-.75f;
    prohibitHeap.store(true,std::memory_order_release);
    const bool accepted=core.applyPrepared(edit);
    core.processStereo(left.data(),right.data(),1);
    prohibitHeap.store(false,std::memory_order_release);
    check(accepted);
    check(left[0]>.0f&&left[0]<=.75f); // first sample without delay
    check(core.inputPeak()==.75f);
    check(core.outputPeak()==left[0]);
    check(right[0]==-left[0]); // stereo link even under attack
    for(int frames:{1,3,17,32,63,128,257,511}) {
        left.fill(.3f);right.fill(-.15f);
        prohibitHeap.store(true,std::memory_order_release);
        core.processStereo(left.data(),right.data(),static_cast<std::size_t>(frames));
        prohibitHeap.store(false,std::memory_order_release);
        check(std::isfinite(left[frames-1])&&std::isfinite(right[frames-1]));
    }
}
int main(){
    testFirstSampleAndNoHeapInCallback();
    testValidationAndLatency();testTransferAndStereo();
    testDryAndBypass();testSoftKneeAndRuntimeEdits();
    testMultipleRatesAndIndependentChannels();
    std::puts("PASS: native zero-buffer compressor stereo linked / GR / parameter validation");
}
