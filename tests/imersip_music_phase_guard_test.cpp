// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipDsp.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH
#error ImerSip requires actual SonKu P8.2 six-wall/three-band room
#endif
#if !defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) || !SONKU_P80_TRANSPARENT_BASS_RESEARCH
#error ImerSip must use transparent P8.2 bass foundation
#endif
#if !defined(SONKU_P75_MUSIC_SAFE_RESEARCH) || !SONKU_P75_MUSIC_SAFE_RESEARCH
#error ImerSip must use P8.2 preserved music SIDE
#endif
namespace {
constexpr double pi=3.1415926535897932384626433832795;
constexpr int n=16384;
using armix::imersip::ImerSipDsp;
using armix::imersip::Settings;
struct Stereo {
    std::vector<float> l,r;
    explicit Stereo(int count=n):l(count),r(count){}
};
double rms(const Stereo& a,int start,bool side) {
    double e=0.;
    for(int i=start;i<static_cast<int>(a.l.size());++i) {
        const double v=.5*(static_cast<double>(a.l[i])+
            (side?-1.:1.)*a.r[i]);
        e+=v*v;
    }
    return std::sqrt(e/static_cast<double>(a.l.size()-start));
}
void makeSignal(Stereo& a,double rate,int fixture,float hz=120.f) {
    for(int i=0;i<static_cast<int>(a.l.size());++i) {
        const double t=static_cast<double>(i)/rate;
        const float v=.16f*static_cast<float>(std::sin(2.*pi*hz*t))+
                      .045f*static_cast<float>(std::sin(2.*pi*1270.*t));
        const float d=.08f*static_cast<float>(std::sin(2.*pi*733.*t));
        if(fixture==0){a.l[i]=v;a.r[i]=v;}          // mono voice
        if(fixture==1){a.l[i]=v;a.r[i]=-v;}         // full anti-phase
        if(fixture==2){a.l[i]=v;a.r[i]=0.f;}        // left hard pan
        if(fixture==3){a.l[i]=0.f;a.r[i]=v;}        // right hard pan
        if(fixture==4){a.l[i]=v;a.r[i]=.5f*v+d;}    // independent stereo
        if(fixture==5){a.l[i]=v;a.r[i]=-.48f*v+d;}  // weak negative correlation
    }
}
Stereo render(double rate,const Settings& s,const Stereo& input,int chunk=128) {
    Stereo result=input;
    auto dsp=std::make_unique<ImerSipDsp>();
    assert(dsp->setSettings(s) && dsp->prepare(rate));
    for(int at=0;at<static_cast<int>(result.l.size());at+=chunk)
        dsp->process(result.l.data()+at,result.r.data()+at,
                     std::min(chunk,static_cast<int>(result.l.size())-at));
    return result;
}
void assertChunkParity(const Stereo& a,const Stereo& b) {
    assert(a.l.size()==b.l.size());
    for(int i=0;i<static_cast<int>(a.l.size());++i) {
        assert(std::isfinite(a.l[i])&&std::isfinite(a.r[i]));
        assert(std::abs(a.l[i]-b.l[i])<2.e-6f);
        assert(std::abs(a.r[i]-b.r[i])<2.e-6f);
    }
}
void verifySceneGeometry() {
    Settings s{};
    assert(std::abs(armix::imersip::sceneFor(s).stereoSpreadDegrees-175.f)<.01f);
    s.width=1.f;
    assert(std::abs(armix::imersip::sceneFor(s).stereoSpreadDegrees-180.f)<.01f);
    s.width=0.f;
    assert(std::abs(armix::imersip::sceneFor(s).stereoSpreadDegrees-80.f)<.01f);
    for(int venue=0;venue<3;++venue) {
        s.venue=venue;
        auto scene=armix::imersip::sceneFor(s);
        assert(!scene.moving && scene.startAzimuthDegrees==0.f);
        assert(scene.spacePreset>=2);
        assert(scene.spatialMix>0.f && scene.outputTrim==1.f);
    }
}
void verifyDryAndBounds(double rate) {
    Settings bad{};bad.venue=3;assert(!armix::imersip::valid(bad));
    bad={};bad.width=2.f;assert(!armix::imersip::valid(bad));
    bad={};bad.immersion=std::numeric_limits<float>::quiet_NaN();
    assert(!armix::imersip::valid(bad));
    Settings s{};s.immersion=0.f;s.outputTrim=1.f;
    assert(armix::imersip::sceneFor(s).spacePreset==0);
    Stereo source(4096);makeSignal(source,rate,4);
    const auto dry=render(rate,s,source);
    for(int i=0;i<4096;++i) {
        assert(std::abs(dry.l[i]-source.l[i])<1.e-5f);
        assert(std::abs(dry.r[i]-source.r[i])<1.e-5f);
    }
    auto badRate=std::make_unique<ImerSipDsp>();
    assert(!badRate->prepare(96000.));
}
void verifyOriginalVocalMidIsUntouched(double rate) {
    // This is the regression v1 missed: an algebraic phase safety test
    // against an already-boxy P8.2 output falsely certified a bad vocal.
    // In v2 the reference is the ACTUAL DRY source, not that HRTF output.
    for(int venue=0;venue<3;++venue)for(int fixture=0;fixture<6;++fixture) {
        Stereo source;makeSignal(source,rate,fixture,450.f);
        Settings s{};s.venue=venue;s.width=.95f;
        s.space=1.f;s.immersion=1.f;s.bassImpact=0.f;s.outputTrim=1.f;
        auto wet=render(rate,s,source,128);
        assertChunkParity(wet,render(rate,s,source,64));
        assertChunkParity(wet,render(rate,s,source,8192));
        for(int i=0;i<n;++i) {
            const double originalMid=.5*(source.l[i]+source.r[i]);
            const double actualMid=.5*(wet.l[i]+wet.r[i]);
            assert(std::abs(originalMid-actualMid)<2.e-6);
        }
        if(fixture==0) {
            const double ref=rms(source,4096,false),now=rms(wet,4096,false);
            assert(std::abs(ref-now)<2.e-6);
            // Center source is NOT turned into two full-volume new emitters.
            assert(rms(wet,4096,true)<ref*.40+1.e-6);
        }
    }
}
void verifyOriginalStereoSurvives(double rate) {
    for(int venue=0;venue<3;++venue)
        for(int fixture=1;fixture<6;++fixture) {
            Stereo source;makeSignal(source,rate,fixture);
            Settings s{};s.venue=venue;s.width=1.f;
            s.space=1.f;s.immersion=1.f;s.bassImpact=0.f;
            const auto wide=render(rate,s,source);
            const auto tiny=render(rate,s,source,64);
            assertChunkParity(wide,tiny);
            const double ref=rms(source,4096,true);
            const double now=rms(wide,4096,true);
            // Preserve all source L/R differentiation with room active.
            assert(now>ref*.99);
            assert(now<ref*1.52+.00001);
            if(fixture==1)assert(now>.035);
        }
}
void verifyBassFoundation(double rate) {
    for(int venue=0;venue<3;++venue)for(float hz:{40.f,70.f,110.f,160.f,220.f}) {
        Stereo source;
        for(int i=0;i<n;++i) {
            const float x=.18f*static_cast<float>(std::sin(2*pi*hz*i/rate));
            source.l[i]=source.r[i]=x;
        }
        Settings s{};s.venue=venue;s.immersion=1.f;s.bassImpact=1.f;
        auto processed=render(rate,s,source);
        const auto before=rms(source,4096,false);
        const auto after=rms(processed,4096,false);
        // Coherent LF boost must not attenuate the P8.2 original music bass.
        assert(after>before*.995);
        assert(after<before*1.14);
    }
}
void verifyActualP82RoomChangesSideOnly(double rate) {
    // Independent side material + different six-wall programs must produce
    // audible stereo room differences; not a fake width-only bypass.
    Stereo source;makeSignal(source,rate,4);
    Settings s{};s.immersion=1.f;s.width=1.f;s.bassImpact=0.f;
    s.venue=0;s.space=0.f;const auto first=render(rate,s,source);
    s.space=1.f;const auto second=render(rate,s,source);
    double residual=0.;
    for(int i=4000;i<n;++i)
        residual+=std::abs((second.l[i]-second.r[i])-
                           (first.l[i]-first.r[i]));
    assert(residual>1.e-5);
    for(int i=0;i<n;++i)
        assert(std::abs((second.l[i]+second.r[i])-
                        (first.l[i]+first.r[i]))<2.e-6);
}
void verifyPeakSafety(double rate) {
    Stereo source;
    for(int i=0;i<n;++i) {
        const float x=.95f*static_cast<float>(
            std::sin(2*pi*997.*i/rate));
        source.l[i]=x;source.r[i]=0.f;
    }
    Settings s{};s.immersion=1.f;s.space=1.f;
    s.width=1.f;s.bassImpact=1.f;
    auto result=render(rate,s,source);
    for(int i=0;i<n;++i) {
        assert(std::isfinite(result.l[i])&&std::isfinite(result.r[i]));
        assert(std::abs(result.l[i])<=.9801f);
        assert(std::abs(result.r[i])<=.9801f);
    }
}
} // namespace
int main() {
    verifySceneGeometry();
    for(double rate:{44100.,48000.}) {
        verifyDryAndBounds(rate);
        verifyOriginalVocalMidIsUntouched(rate);
        verifyOriginalStereoSurvives(rate);
        verifyBassFoundation(rate);
        verifyActualP82RoomChangesSideOnly(rate);
        verifyPeakSafety(rate);
    }
}
