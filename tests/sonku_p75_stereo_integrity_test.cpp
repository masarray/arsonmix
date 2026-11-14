// SPDX-License-Identifier: AGPL-3.0-only
// Physical field failure reproduction: do NOT certify music from RMS-only
// anti-phase tone tests. The entire P7.5 PCM safety mode is research-only.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P75_MUSIC_SAFE_RESEARCH) || !SONKU_P75_MUSIC_SAFE_RESEARCH
#error P7.5 phase-integrity test must compile the experimental music-safe path
#endif
#if !defined(SONKU_P72_NATIVE_XYZ_RT) || !SONKU_P72_NATIVE_XYZ_RT
#error P7.5 test must cover the same research XYZ state as the native tester
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using armix::sonku::Settings;
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
constexpr float pi=3.14159265358979323846f;
constexpr auto profile=SonKuReflectionProfile::SixSurfaceKemarResearch;

static auto create(double rate,const Settings& s) {
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(rate,profile));
    assert(core->setSettings(s));
    return core;
}

static void antiPhaseCanNeverNull(double rate) {
    for(const float width:{0.f,60.f,180.f}) {
        Settings s{};s.moving=false;s.spacePreset=0;
        s.startAzimuthDegrees=180.f;s.spatialMix=1.f;
        s.stereoSpreadDegrees=width;s.outputTrim=.75f;
        auto core=create(rate,s);
        constexpr int n=8192;
        std::vector<float> l(n),r(n);
        const float scaledWidth=std::min(1.25f,.5f+width/120.f);
        for(int i=0;i<n;++i) {
            const float v=.21f*std::sin(2.f*pi*440.f*i/static_cast<float>(rate))
                         +.08f*std::sin(2.f*pi*3150.f*i/static_cast<float>(rate));
            l[i]=v;r[i]=-v;
        }
        const auto original=l;
        core->process(l.data(),r.data(),n);
        double pIn=0.,pOut=0.;
        for(int i=0;i<n;++i) {
            const float expected=.75f*scaledWidth*original[i];
            assert(std::isfinite(l[i]) && std::isfinite(r[i]));
            assert(std::abs(l[i]-expected)<2.e-6f);
            assert(std::abs(r[i]+expected)<2.e-6f);
            pIn+=original[i]*original[i];
            pOut+=l[i]*l[i];
        }
        assert(pOut>.15*pIn);
    }
}
static void monoMidCannotCombFromSpread(double rate) {
    Settings s{};s.moving=false;s.startAzimuthDegrees=180.f;
    s.spacePreset=0;s.spatialMix=1.f;s.outputTrim=.7f;
    s.stereoSpreadDegrees=0.f;
    auto zero=create(rate,s);
    s.stereoSpreadDegrees=120.f;
    auto wide=create(rate,s);
    constexpr int n=8192;
    std::vector<float> l(n),r(n),wl(n),wr(n);
    for(int i=0;i<n;++i) {
        const float sample=.12f*std::sin(2.f*pi*210.f*i/static_cast<float>(rate))
                          +.09f*std::sin(2.f*pi*2050.f*i/static_cast<float>(rate));
        l[i]=r[i]=wl[i]=wr[i]=sample;
    }
    zero->process(l.data(),r.data(),n);
    wide->process(wl.data(),wr.data(),n);
    double outPower=0.;
    for(int i=0;i<n;++i) {
        assert(std::isfinite(wl[i])&&std::isfinite(wr[i]));
        assert(std::abs(l[i]-wl[i])<3.e-6f);
        assert(std::abs(r[i]-wr[i])<3.e-6f);
        outPower+=wl[i]*wl[i]+wr[i]*wr[i];
    }
    assert(outPower>.0001);
}
static void coherentBassInvertedHighSurvives(double rate) {
    Settings s{};s.moving=false;s.startAzimuthDegrees=0.f;
    s.spacePreset=0;s.spatialMix=1.f;s.outputTrim=.75f;
    s.stereoSpreadDegrees=60.f;
    auto core=create(rate,s);
    constexpr int n=16384;
    const float upper=static_cast<float>(rate/20.);
    std::vector<float> l(n),r(n);
    double originalSideEnergy=0.,originalMidEnergy=0.;
    for(int i=0;i<n;++i) {
        const float mid=.25f*std::sin(2.f*pi*220.f*i/static_cast<float>(rate));
        const float side=.09f*std::sin(2.f*pi*upper*i/static_cast<float>(rate));
        l[i]=mid+side;r[i]=mid-side;
        originalSideEnergy+=side*side;
        originalMidEnergy+=mid*mid;
    }
    core->process(l.data(),r.data(),n);
    double outSide=0.,outMid=0.;
    for(int i=n/2;i<n;++i) {
        const float side=.5f*(l[i]-r[i]);
        const float mid=.5f*(l[i]+r[i]);
        assert(std::isfinite(side)&&std::isfinite(mid));
        outSide+=side*side;
        outMid+=mid*mid;
    }
    // Spectrally independent MIXED stereo must retain a real SIDE signal
    // even when correlated bass keeps the global detector positive.
    assert(outSide>originalSideEnergy*.12);
    assert(outMid>originalMidEnergy*.005);
}
static void roomAndMovingArePartitionInvariant(double rate) {
    Settings s{};s.moving=true;s.revolutionSeconds=2.5f;
    s.startAzimuthDegrees=85.f;s.stereoSpreadDegrees=95.f;
    s.spatialMix=1.f;s.outputTrim=.62f;
    s.spacePreset=1;s.sourceDistanceMeters=1.6f;
    s.roomAmount=.5f;s.ambience=.2f;
    s.directElevationDegrees=18.f;
    assert(armix::sonku::valid(s));
    auto contiguous=create(rate,s),chunked=create(rate,s);
    constexpr int n=8192;
    std::vector<float> l(n),r(n),cl(n),cr(n);
    for(int i=0;i<n;++i) {
        const float m=.13f*std::sin(2.f*pi*330.f*i/static_cast<float>(rate));
        const float side=.09f*std::sin(2.f*pi*2400.f*i/static_cast<float>(rate));
        l[i]=cl[i]=m+side;r[i]=cr[i]=m-side;
    }
    contiguous->process(l.data(),r.data(),n);
    for(int pos=0;pos<n;) {
        const int k=std::min(n-pos,(pos&1)?257:64);
        chunked->process(cl.data()+pos,cr.data()+pos,k);
        pos+=k;
    }
    double pow=0.;
    for(int i=0;i<n;++i) {
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
        assert(std::abs(l[i])<.98f&&std::abs(r[i])<.98f);
        assert(std::abs(l[i]-cl[i])<3.e-6f);
        assert(std::abs(r[i]-cr[i])<3.e-6f);
        pow+=l[i]*l[i]+r[i]*r[i];
    }
    assert(pow>.001);
}
int main() {
    for(const double rate:{44100.,48000.}) {
        antiPhaseCanNeverNull(rate);
        monoMidCannotCombFromSpread(rate);
        coherentBassInvertedHighSurvives(rate);
        roomAndMovingArePartitionInvariant(rate);
    }
    std::puts("PASS: P7.5 experimental centered measured KEMAR MID, bounded stereo SIDE, no anti-phase null, no coherent SPREAD comb, mixed-frequency integrity and moving XYZ ROOM chunk parity at 44.1/48 kHz");
}
