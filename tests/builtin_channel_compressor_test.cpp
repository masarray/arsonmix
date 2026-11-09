// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/ChannelCompressor.h"
#include "../engine/builtin_fx/CompressorMeterMath.h"
#include <array>
#include <atomic>
#include <chrono>
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

static void testMeasuredPeakDbfs() {
    using armix::builtin_fx::peakToDbfs;
    check(peakToDbfs(0.f)==-72.f);
    check(peakToDbfs(-3.f)==-72.f);
    check(peakToDbfs(std::numeric_limits<float>::quiet_NaN())==-72.f);
    check(std::abs(peakToDbfs(1.f))<.00001f);
    check(std::abs(peakToDbfs(.5f)+6.0206f)<.001f);
    check(peakToDbfs(2.f)>6.f);
    check(peakToDbfs(100.f)==12.f);
}
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
    // With zero lookahead, the attack transient can initially pass at its
    // uncompressed peak. The meter must report ACTUAL PCM, not pretend that
    // block-peak OUT is always smaller than block-peak IN.
    check(c.outputPeak()>0.f&&c.outputPeak()<=c.inputPeak()+1.e-6f);
    std::array<float,256> settledL{},settledR{};
    settledL.fill(.5f);settledR.fill(.25f);
    c.processStereo(settledL.data(),settledR.data(),settledL.size());
    check(std::abs(c.inputPeak()-.5f)<1.e-6f);
    check(c.outputPeak()<c.inputPeak()-.01f); // steady-state compression
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

// Fixed pre-optimization mathematical reference (test-only). Verify the DSP
// output and three true PCM/GR meters at 44.1/48/96 kHz, including soft knee,
// hard knee, unity, positive makeup and fully dry compression. This explicitly
// protects the user's already-accepted Compressor sound from "fast DSP" drift.
static void testOptimizedMatchesLegacyStablePCM() {
    const std::array<double,3> rates{44100.,48000.,96000.};
    const std::array<CompressorSettings,7> scenes{{
        {-18.f,3.f,10.f,120.f,6.f,0.f,1.f},
        {-36.f,14.f,.1f,60.f,0.f,0.f,1.f},
        {-12.f,2.1f,30.f,500.f,18.f,8.f,.72f},
        {-52.f,20.f,3.f,80.f,24.f,-4.f,1.f},
        {-50.f,12.f,1.f,180.f,12.f,12.f,0.f},
        {-18.f,1.f,1.f,40.f,0.f,0.f,1.f},
        {-21.f,4.5f,9.f,250.f,6.f,0.f,.5f},
    }};
    for(double rate:rates)for(const auto& s:scenes) {
        ChannelCompressor c;
        check(c.prepare(rate) && c.resetUnpublished(s));
        const float attack=static_cast<float>(std::exp(-1000.0/(s.attackMs*rate)));
        const float release=static_cast<float>(std::exp(-1000.0/(s.releaseMs*rate)));
        const float makeup=static_cast<float>(std::exp(s.makeupDb*0.11512925464970229));
        const float gate=static_cast<float>(
            std::exp((s.thresholdDb-s.kneeDb*.5f)*0.11512925464970229));
        float envelope=0.f;
        std::array<float,128> left{},right{},expectedLeft{},expectedRight{};
        std::size_t sampleIndex=0;
        for(int block=0;block<24;++block) {
            float maxIn=0.f,maxOut=0.f,maxGR=0.f;
            for(int i=0;i<128;++i,++sampleIndex) {
                const float l=static_cast<float>(0.55*std::sin(sampleIndex*.013)+
                    0.31*std::cos(sampleIndex*.071));
                const float r=static_cast<float>(0.37*std::sin(sampleIndex*.017+.4)+
                    0.12*std::cos(sampleIndex*.033));
                left[i]=l;right[i]=r;
                const float peak=std::max(std::abs(l),std::abs(r));
                maxIn=std::max(maxIn,peak);
                const float coeff=peak>envelope?attack:release;
                envelope=coeff*envelope+(1.f-coeff)*peak;
                if(envelope<1.e-25f)envelope=0.f;
                float reduction=0.f;
                if(s.ratio>1.f && envelope>gate) {
                    const float above=20.f*std::log10(envelope)-s.thresholdDb;
                    const float strength=1.f-1.f/s.ratio;
                    if(s.kneeDb<.0001f)
                        reduction=std::max(0.f,above)*strength;
                    else if(above>=s.kneeDb*.5f)reduction=above*strength;
                    else if(above>-s.kneeDb*.5f) {
                        const float bend=above+s.kneeDb*.5f;
                        reduction=bend*bend*(.5f/s.kneeDb)*strength;
                    }
                }
                const float attenuation=reduction>0.f?
                    std::exp(reduction*(-0.11512925464970229f)):1.f;
                const float gain=(1.f-s.mix)+s.mix*(attenuation*makeup);
                const float expectedL=l*gain,expectedR=r*gain;
                // Existing float math (incl. soft-knee multiplication order)
                // must agree within a few ULP; no control-rate approximation.
                left[i]=l;right[i]=r; // real DSP is called after reference pass
                // Accumulate output predictions to compare the actual output.
                maxOut=std::max(maxOut,peak*std::abs(gain));
                maxGR=std::max(maxGR,reduction);
                expectedLeft[i]=expectedL;expectedRight[i]=expectedR;
            }
            // Keep the two paths independent: replay the same PCM and
            // compare the reference block meter immediately after processing.
            c.processStereo(left.data(),right.data(),left.size());
            for(std::size_t i=0;i<left.size();++i) {
                check(std::abs(left[i]-expectedLeft[i])<2.e-5f);
                check(std::abs(right[i]-expectedRight[i])<2.e-5f);
            }
            check(std::abs(c.inputPeak()-maxIn)<2.e-6f);
            check(std::abs(c.outputPeak()-maxOut)<2.e-5f);
            check(std::abs(c.gainReductionDb()-maxGR)<2.e-4f);
        }
    }
}
// No wall-clock pass/fail: hosted CI runners vary by orders of magnitude.
// Reproducible 16-instance, 128-frame steady-load timing is diagnostic only,
// never run on the callback or used as an ungrounded speedup claim.
static void testManyInstancesCpuDiagnostic() {
    constexpr int instances=16,frames=128,blocks=300;
    std::array<ChannelCompressor,instances> compressors{};
    for(int i=0;i<instances;++i) {
        check(compressors[i].prepare(48000.));
        CompressorSettings s{};
        s.thresholdDb=-32.f;s.ratio=7.f;s.attackMs=1.f;s.kneeDb=12.f;
        s.mix=(i%4==0)?0.f:1.f;
        check(compressors[i].resetUnpublished(s));
    }
    std::array<float,frames> left{},right{};
    double checksum=0.;
    const auto start=std::chrono::steady_clock::now();
    for(int b=0;b<blocks;++b)for(auto& c:compressors) {
        for(int j=0;j<frames;++j){
            left[j]=.55f+static_cast<float>(j%7)*.019f;
            right[j]=-.42f-static_cast<float>(j%5)*.012f;
        }
        c.processStereo(left.data(),right.data(),frames);
        checksum+=left[frames-1]+right[0]+c.gainReductionDb();
    }
    const auto elapsed=std::chrono::duration<double,std::nano>(
        std::chrono::steady_clock::now()-start).count();
    check(std::isfinite(checksum) && checksum>0.);
    std::printf("Compressor CPU diagnostic: %.1f ns / stereo frame / instance "
                "(16 instances x %d frames x %d blocks; non-gating)\\n",
                elapsed/(instances*frames*blocks),frames,blocks);
}
int main(){
    testMeasuredPeakDbfs();
    testFirstSampleAndNoHeapInCallback();
    testValidationAndLatency();testTransferAndStereo();
    testDryAndBypass();testSoftKneeAndRuntimeEdits();
    testMultipleRatesAndIndependentChannels();
    testOptimizedMatchesLegacyStablePCM();
    testManyInstancesCpuDiagnostic();
    std::puts("PASS: native zero-buffer compressor stereo linked / GR / parameter validation");
}
