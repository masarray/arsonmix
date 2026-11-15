// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipDsp.h"
#include <algorithm>
#include <array>
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
    assert(dsp->measuredStereoStageEnabled());
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
        s.space=1.f;s.immersion=1.f;s.bassImpact=0.f;s.detail=0.f;s.outputTrim=1.f;
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
void verifyP84FusedKernelProvenanceAndLowFrequency(double rate) {
    // The standalone kernel source is transferred from P8.4; no P8.3
    // physical-renderer LF480, extra worker, 2nd runtime core or macro.
    using armix::sonku::p84::FusedMusicHrir;
    FusedMusicHrir merger;
    assert(merger.prepare(rate,.12f,750.f));
    assert(!merger.prepare(rate,.31f,750.f));
    assert(merger.prepare(rate,.12f,750.f));
    constexpr int taps=140;
    using Kernel=std::array<std::array<float,taps>,2>;
    Kernel center{},left{},right{};
    center[0][0]=1.f;center[1][0]=1.f;
    left[0][0]=1.70f;left[1][0]=.45f;
    right[0][0]=.45f;right[1][0]=1.70f;
    merger.merge(center,left,right);
    // Direction is real: L and R FIRs remain differentiated.
    assert(left[0][0]>right[0][0]);
    assert(left[1][0]<right[1][0]);
    // The 750Hz difference high-pass has ~zero DC coefficient sum
    // over the 140-tap FIR support: original music/bass center retained.
    for(int ear=0;ear<2;++ear) {
        double lsum=0.,rsum=0.;
        for(int tap=0;tap<taps;++tap) {
            lsum+=left[ear][tap];
            rsum+=right[ear][tap];
        }
        assert(std::abs(lsum-1.)<2.e-4);
        assert(std::abs(rsum-1.)<2.e-4);
    }
    // Strongly typed bounds also protect PCM update admissions.
    assert(!merger.prepare(rate,-.01f,750.f));
    assert(!merger.prepare(rate,.12f,350.f));
}
void verifyP82NormalPathRemainsUnchanged(double rate) {
    // Enabling the ImerSip-only research MODE=FALSE is exact P8.2 parity.
    // Normal SonKu direct kernels must not be recomposed or renamed.
    Stereo input;makeSignal(input,rate,4);
    auto legacy=std::make_unique<armix::sonku::SonKuSpatialCore>();
    auto disabled=std::make_unique<armix::sonku::SonKuSpatialCore>();
    using Profile=armix::sonku::SonKuReflectionProfile;
    assert(legacy->prepare(rate,Profile::SixSurfaceKemarResearch));
    assert(disabled->prepare(rate,Profile::SixSurfaceKemarResearch));
    Settings s{};s.width=.91f;s.space=.47f;s.immersion=1.f;
    const auto original=armix::imersip::sceneFor(s);
    assert(legacy->setSettings(original));
    assert(disabled->setSettings(original));
    assert(disabled->enableImerSipMeasuredStereoSide(false));
    legacy->reset();disabled->reset();
    Stereo a=input,b=input;
    for(int at=0;at<n;at+=128) {
        legacy->process(a.l.data()+at,a.r.data()+at,128);
        disabled->process(b.l.data()+at,b.r.data()+at,128);
    }
    for(int i=0;i<n;++i) {
        assert(a.l[i]==b.l[i]);
        assert(a.r[i]==b.r[i]);
    }
}
void verifyActualMeasuredDualSpeakers(double rate) {
    // P8.2 standard music-safe renderer = centered KEMAR FIR.
    // The ImerSip experimental mode must employ TWO independent measured
    // KEMAR directions, not merely relabel the WIDTH control as 175 degrees.
    Stereo input;makeSignal(input,rate,2);
    auto dryP82=std::make_unique<armix::sonku::SonKuSpatialCore>();
    auto dual=std::make_unique<armix::sonku::SonKuSpatialCore>();
    using Profile=armix::sonku::SonKuReflectionProfile;
    assert(dryP82->prepare(rate,Profile::SixSurfaceKemarResearch));
    assert(dual->prepare(rate,Profile::SixSurfaceKemarResearch));
    armix::sonku::Settings s{};
    s.moving=false;s.startAzimuthDegrees=0.f;
    s.stereoSpreadDegrees=175.f;s.spatialMix=.65f;s.outputTrim=1.f;
    assert(dryP82->setSettings(s) && dual->setSettings(s));
    assert(!dryP82->imersipMeasuredStereoSideEnabled());
    assert(dual->enableImerSipMeasuredStereoSide(true));
    assert(dual->imersipMeasuredStereoSideEnabled());
    dryP82->reset();dual->reset();
    Stereo oldRender=input,newRender=input;
    for(int at=0;at<n;at+=128) {
        dryP82->process(oldRender.l.data()+at,oldRender.r.data()+at,128);
        dual->process(newRender.l.data()+at,newRender.r.data()+at,128);
    }
    double sideDifference=0.;
    for(int i=4096;i<n;++i)
        sideDifference+=std::abs(
            (newRender.l[i]-newRender.r[i])-
            (oldRender.l[i]-oldRender.r[i]));
    // Real measured source directions give a nontrivial interaural field.
    assert(sideDifference>1.);
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
void verifyDetailIsNotMidBoost(double rate) {
    // Detail is a positive >5k high shelf, never a 1-4k shout EQ.
    // Compare the EXACT same core/venue with detail OFF and ON.
    Settings s{};s.venue=0;s.bassImpact=0.f;s.width=.95f;
    s.immersion=1.f;s.space=.45f;
    for(float hz:{90.f,500.f,1500.f,3000.f,7000.f,11000.f}) {
        Stereo input;
        for(int i=0;i<n;++i) {
            const float x=.09f*static_cast<float>(std::sin(2*pi*hz*i/rate));
            input.l[i]=x;input.r[i]=x;
        }
        s.detail=0.f;const auto before=render(rate,s,input);
        s.detail=1.f;const auto after=render(rate,s,input);
        const double a=rms(before,4096,false),b=rms(after,4096,false);
        assert(a>1.e-4);
        const double ratio=b/a;
        if(hz<=3000.f)assert(ratio>=.999&&ratio<1.055);
        else assert(ratio>1.015&&ratio<1.16);
    }
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
        verifyP84FusedKernelProvenanceAndLowFrequency(rate);
        verifyP82NormalPathRemainsUnchanged(rate);
        verifyActualMeasuredDualSpeakers(rate);
        verifyBassFoundation(rate);
        verifyDetailIsNotMidBoost(rate);
        verifyActualP82RoomChangesSideOnly(rate);
        verifyPeakSafety(rate);
    }
}
