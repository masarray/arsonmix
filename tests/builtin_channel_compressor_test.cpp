// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/ChannelCompressor.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using armix::builtin_fx::ChannelCompressor;
using armix::builtin_fx::CompressorSettings;

static void testValidationAndLatency() {
    ChannelCompressor compressor;
    assert(ChannelCompressor::addedLatencySamples==0);
    assert(!compressor.prepare(7999));
    assert(compressor.prepare(48000));
    CompressorSettings s{};
    s.ratio=std::numeric_limits<float>::quiet_NaN();
    ChannelCompressor::PreparedEdit packet{};
    assert(!ChannelCompressor::prepareEdit(s,48000,packet));
    s=CompressorSettings{};s.attackMs=0;
    assert(!ChannelCompressor::prepareEdit(s,48000,packet));
    s=CompressorSettings{};
    assert(ChannelCompressor::prepareEdit(s,48000,packet));
    assert(!compressor.applyPrepared(ChannelCompressor::PreparedEdit{}));
    auto stale=packet;stale.sampleRate=44100;
    assert(!compressor.applyPrepared(stale));
    assert(compressor.applyPrepared(packet));
}
static float rms(const float* v,std::size_t n) {
    double power=0.;
    for(std::size_t i=0;i<n;++i)power+=v[i]*v[i];
    return std::sqrt(power/static_cast<double>(n));
}
static void testTransferAndStereo() {
    ChannelCompressor c;
    assert(c.prepare(48000));
    CompressorSettings s{};
    s.thresholdDb=-20;s.ratio=4;s.attackMs=0.1f;s.releaseMs=100;
    s.kneeDb=0;s.makeupDb=0;s.mix=1;
    assert(c.resetUnpublished(s));
    std::array<float,4800> l{},r{};
    l.fill(0.5f);r.fill(0.25f);
    c.processStereo(l.data(),r.data(),l.size());
    // Feed-forward gain at -6.02 dB, threshold -20, ratio 4 yields
    // about 10.48 dB gain reduction. Both channels share gain exactly.
    assert(c.gainReductionDb()>9.5f&&c.gainReductionDb()<11.5f);
    for(std::size_t i=200;i<l.size();++i) {
        assert(std::abs(l[i]-2*r[i])<1e-5f);
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
    }
    assert(rms(l.data()+4000,800)<0.22f);
    assert(rms(l.data()+4000,800)>0.10f);
}
static void testDryAndBypass() {
    ChannelCompressor c;assert(c.prepare(44100));
    CompressorSettings s{};s.thresholdDb=-55;s.ratio=20;s.mix=0.f;s.makeupDb=12;
    assert(c.resetUnpublished(s));
    std::array<float,128> mono{};
    mono.fill(0.45f);
    c.processMono(mono.data(),mono.size());
    for(float value:mono)assert(value==0.45f); // exact mix=0 dry
    s.mix=1;s.ratio=1;s.makeupDb=0;
    assert(c.resetUnpublished(s));
    mono.fill(0.45f);c.processMono(mono.data(),mono.size());
    for(float value:mono)assert(value==0.45f); // unity ratio and no makeup
}
static void testSoftKneeAndRuntimeEdits() {
    ChannelCompressor hard,soft;
    assert(hard.prepare(48000)&&soft.prepare(48000));
    CompressorSettings s{};
    s.thresholdDb=-12;s.ratio=4;s.attackMs=.1f;
    s.kneeDb=0;assert(hard.resetUnpublished(s));
    s.kneeDb=12;assert(soft.resetUnpublished(s));
    std::array<float,2000> a{},b{};
    a.fill(0.25f);b.fill(0.25f); // -12.04 dB, soft knee bends before threshold
    hard.processMono(a.data(),a.size());
    soft.processMono(b.data(),b.size());
    assert(b.back()<a.back());
    auto edit=s;edit.makeupDb=6.f;edit.mix=.65f;
    ChannelCompressor::PreparedEdit packet{};
    assert(ChannelCompressor::prepareEdit(edit,48000,packet));
    assert(soft.applyPrepared(packet));
    for(int n=0;n<50;++n)soft.processMono(b.data(),b.size());
    for(float value:b)assert(std::isfinite(value));
}
static void testMultipleRatesAndIndependentChannels() {
    for(double rate:{44100.,48000.,96000.}) {
        ChannelCompressor a,b;
        assert(a.prepare(rate)&&b.prepare(rate));
        auto loud=a.settingsForOfflineTests();
        loud.thresholdDb=-35;loud.ratio=5;
        assert(a.resetUnpublished(loud));
        std::array<float,256> x{},y{};
        x.fill(.4f);y.fill(.4f);
        for(int n=0;n<16;++n) {
            a.processMono(x.data(),x.size());
            b.processMono(y.data(),y.size());
        }
        assert(x.back()<y.back());
        assert(b.gainReductionDb()<a.gainReductionDb());
    }
}
int main(){
    testValidationAndLatency();testTransferAndStereo();
    testDryAndBypass();testSoftKneeAndRuntimeEdits();
    testMultipleRatesAndIndependentChannels();
    std::puts("PASS: native zero-buffer compressor stereo linked / GR / parameter validation");
}
