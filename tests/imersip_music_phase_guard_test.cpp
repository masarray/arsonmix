// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipDsp.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH
#error ImerSip tests must use SonKu P8.2 actual six-wall material reverb
#endif
#if !defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) || !SONKU_P80_TRANSPARENT_BASS_RESEARCH
#error ImerSip tests must use P8.2 transparent bass source
#endif
#if !defined(SONKU_P75_MUSIC_SAFE_RESEARCH) || !SONKU_P75_MUSIC_SAFE_RESEARCH
#error ImerSip tests must preserve P8.2 music-stereo SIDE
#endif
namespace {
constexpr double pi=3.1415926535897932384626433832795;
constexpr int n=16384;
using armix::imersip::ImerSipDsp;
using armix::imersip::Settings;
using armix::sonku::SonKuSpatialCore;
struct Stereo {
    std::vector<float> l,r;
    explicit Stereo(int count):l(count),r(count){}
};
double rms(const std::vector<float>& a,const std::vector<float>& b,int start) {
    double e=0.;
    for(int i=start;i<static_cast<int>(a.size());++i)
        e+=static_cast<double>(a[i])*a[i]+static_cast<double>(b[i])*b[i];
    return std::sqrt(e/static_cast<double>(2*(a.size()-start)));
}
double sumRms(const Stereo& a,int start) {
    double e=0.;
    for(int i=start;i<static_cast<int>(a.l.size());++i) {
        const double mid=.5*(static_cast<double>(a.l[i])+a.r[i]);
        e+=mid*mid;
    }
    return std::sqrt(e/static_cast<double>(a.l.size()-start));
}
double sideRms(const Stereo& a,int start) {
    double e=0.;
    for(int i=start;i<static_cast<int>(a.l.size());++i) {
        const double side=.5*(static_cast<double>(a.l[i])-a.r[i]);
        e+=side*side;
    }
    return std::sqrt(e/static_cast<double>(a.l.size()-start));
}
void makeSignal(Stereo& a,double rate,int fixture,double hz=120.) {
    for(int i=0;i<static_cast<int>(a.l.size());++i) {
        const double t=static_cast<double>(i)/rate;
        const float v=.16f*static_cast<float>(std::sin(2.*pi*hz*t))+
                      .045f*static_cast<float>(std::sin(2.*pi*1270.*t));
        const float d=.08f*static_cast<float>(std::sin(2.*pi*733.*t));
        if(fixture==0){a.l[i]=v;a.r[i]=v;}           // MONO center
        if(fixture==1){a.l[i]=v;a.r[i]=-v;}          // worst anti-phase
        if(fixture==2){a.l[i]=v;a.r[i]=0.f;}         // hard left
        if(fixture==3){a.l[i]=0.f;a.r[i]=v;}         // hard right
        if(fixture==4){a.l[i]=v;a.r[i]=.5f*v+d;}     // partial coherence
        if(fixture==5){a.l[i]=v;a.r[i]=-.48f*v+d;}   // negative correlation
    }
}
Stereo render(double rate,const Settings& s,const Stereo& input,
              bool enhanced,int chunk=128) {
    Stereo out=input;
    if(enhanced) {
        auto fx=std::make_unique<ImerSipDsp>();
        assert(fx->setSettings(s)&&fx->prepare(rate));
        for(int p=0;p<static_cast<int>(out.l.size());p+=chunk)
            fx->process(out.l.data()+p,out.r.data()+p,
                        std::min(chunk,static_cast<int>(out.l.size())-p));
    } else {
        auto fx=std::make_unique<SonKuSpatialCore>();
        assert(fx->prepare(rate,
            armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch));
        assert(fx->setSettings(armix::imersip::sceneFor(s)));
        fx->reset(); // exact same prepared P8.2 core scene
        for(int p=0;p<static_cast<int>(out.l.size());p+=chunk)
            fx->process(out.l.data()+p,out.r.data()+p,
                        std::min(chunk,static_cast<int>(out.l.size())-p));
    }
    return out;
}
void checkFiniteAndChunk(const Stereo& left,const Stereo& other) {
    assert(left.l.size()==other.l.size());
    for(int i=0;i<static_cast<int>(left.l.size());++i) {
        assert(std::isfinite(left.l[i])&&std::isfinite(left.r[i]));
        assert(std::abs(left.l[i]-other.l[i])<2.e-6f);
        assert(std::abs(left.r[i]-other.r[i])<2.e-6f);
    }
}
void verifyDryAndInvalid(double rate) {
    // The original-source stereo detector precedes the P8.2 NaN sanitizer.
    // A broken upstream insert must not poison the moving correlation gate.
    armix::imersip::PhaseSafeEnhancer filter;
    assert(filter.prepare(rate));
    Settings normal{};
    assert(filter.setSettings(normal));
    filter.reset();
    for(float badSample:{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::max()}) {
        const float gate=filter.captureGate(badSample,.2f);
        assert(std::isfinite(gate)&&gate>=0.f&&gate<=1.f);
        float l=.1f,r=-.07f;
        filter.process(l,r,gate);
        assert(std::isfinite(l)&&std::isfinite(r));
    }
    Settings bad{};
    bad.venue=3;assert(!armix::imersip::valid(bad));
    bad={};bad.bassImpact=std::numeric_limits<float>::quiet_NaN();
    assert(!armix::imersip::valid(bad));
    bad={};bad.width=2.f;
    assert(!armix::imersip::valid(bad));
    Settings s{};s.immersion=0.f;s.outputTrim=1.f;
    assert(armix::imersip::sceneFor(s).spacePreset==0);
    Stereo a(4096);makeSignal(a,rate,4);
    auto dry=render(rate,s,a,true);
    for(int i=0;i<4096;++i) {
        assert(std::abs(dry.l[i]-a.l[i])<1.e-5f);
        assert(std::abs(dry.r[i]-a.r[i])<1.e-5f);
    }
    auto fail=std::make_unique<ImerSipDsp>();
    assert(!fail->prepare(96000.));
}
void verifyActualP82Anchor(double rate) {
    for(int venue=0;venue<3;++venue)for(int fixture=0;fixture<6;++fixture) {
        Stereo input(n);makeSignal(input,rate,fixture);
        Settings state{};state.venue=venue;
        state.bassImpact=0.f;state.width=0.f;
        const auto base=render(rate,state,input,false);
        const auto exact=render(rate,state,input,true);
        checkFiniteAndChunk(base,exact);
        const auto chunk=render(rate,state,input,true,8192);
        checkFiniteAndChunk(chunk,exact);
    }
}
void verifyPhaseSafeNoMonoCancellation(double rate) {
    for(int venue=0;venue<3;++venue)for(int fixture=0;fixture<6;++fixture) {
        Stereo input(n);makeSignal(input,rate,fixture);
        Settings s{};s.venue=venue;s.bassImpact=0.f;s.width=1.f;
        const auto base=render(rate,s,input,false);
        const auto enhanced=render(rate,s,input,true);
        const auto chunk=render(rate,s,input,true,64);
        checkFiniteAndChunk(enhanced,chunk);
        for(int i=0;i<n;++i) {
            // The optional side enhancement must NEVER remove P8.2 MID.
            const double before=.5*(base.l[i]+base.r[i]);
            const double after=.5*(enhanced.l[i]+enhanced.r[i]);
            assert(std::abs(before-after)<2.e-6);
            // For these moderate levels peak safety cannot be invoked.
            assert(std::abs(enhanced.l[i])<.98f);
            assert(std::abs(enhanced.r[i])<.98f);
        }
        const double baseSide=sideRms(base,4096);
        const double newSide=sideRms(enhanced,4096);
        // No cancellation of stereo anti-phase energy by the enhancer.
        assert(newSide+.00001>=baseSide*.995);
        assert(newSide<=baseSide*1.28+.00001);
        if(fixture==1)assert(newSide>.035); // retain anti-phase music body
        if(fixture==0) {
            // Original mono center does not trigger extra width layer.
            assert(std::abs(newSide-baseSide)<.002);
        }
    }
}
void verifyBassNeverDrops(double rate) {
    for(int venue=0;venue<3;++venue)for(float hz:{40.f,70.f,110.f,160.f,220.f}) {
        Stereo input(n);
        for(int i=0;i<n;++i) {
            const float v=.18f*static_cast<float>(
                std::sin(2*pi*hz*i/rate));
            input.l[i]=input.r[i]=v;
        }
        Settings s{};s.venue=venue;s.bassImpact=1.f;s.width=0.f;
        const auto base=render(rate,s,input,false);
        const auto richer=render(rate,s,input,true);
        const double oldRms=sumRms(base,4096);
        const double newRms=sumRms(richer,4096);
        assert(oldRms>1.e-4);
        // Coherent positive LF shelf: zero tonal bass drop on mono signal.
        assert(newRms+1.e-6>=oldRms*.995);
        assert(newRms<oldRms*1.13);
    }
}
void verifyBothEnhancements(double rate) {
    Settings state{};
    for(int venue=0;venue<3;++venue)for(int fixture=0;fixture<6;++fixture) {
        Stereo input(n);makeSignal(input,rate,fixture);
        state.venue=venue;state.width=1.f;state.bassImpact=1.f;
        const auto base=render(rate,state,input,false);
        const auto both=render(rate,state,input,true);
        const auto chunk=render(rate,state,input,true,64);
        checkFiniteAndChunk(both,chunk);
        for(int i=0;i<n;++i) {
            // No new unsafe peak even for joint coherent LF+SIDE additions.
            const float b=std::max(std::abs(base.l[i]),std::abs(base.r[i]));
            const float p=std::max(std::abs(both.l[i]),std::abs(both.r[i]));
            assert(p<=std::max(.98f,b)+2.e-6f);
        }
        const double baseEnergy=rms(base.l,base.r,4096);
        const double newEnergy=rms(both.l,both.r,4096);
        assert(newEnergy+1.e-6>=baseEnergy*.99);
    }
}
} // namespace
int main() {
    for(double rate:{44100.,48000.}) {
        verifyDryAndInvalid(rate);
        verifyActualP82Anchor(rate);
        verifyPhaseSafeNoMonoCancellation(rate);
        verifyBassNeverDrops(rate);
        verifyBothEnhancements(rate);
    }
}
