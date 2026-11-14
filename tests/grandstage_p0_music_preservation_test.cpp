// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/GrandStageDsp.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

// Compile the ACTUAL P8.2 static-front renderer. Never substitute mock DSP.
#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH
#error GrandStage source tests require the original P8.2 multiband room
#endif
#if !defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) || !SONKU_P80_TRANSPARENT_BASS_RESEARCH
#error GrandStage tests require the original music-bass protection
#endif
namespace {
constexpr float pi=3.14159265358979323846f;
constexpr int n=16384;
using armix::grandstage::GrandStageDsp;
using armix::grandstage::Settings;

void verifyDryAndValidation(double rate) {
    Settings bad{};
    bad.venue=3;assert(!armix::grandstage::valid(bad));
    bad={};bad.space=std::numeric_limits<float>::quiet_NaN();
    assert(!armix::grandstage::valid(bad));
    Settings dry{};dry.immersion=0.f;dry.outputTrim=1.f;
    assert(armix::grandstage::sceneFor(dry).spacePreset==0);
    auto fx=std::make_unique<GrandStageDsp>();
    assert(fx->setSettings(dry) && fx->prepare(rate));
    std::array<float,1024> l{},r{},originalL{},originalR{};
    for(int i=0;i<1024;++i) {
        l[i]=originalL[i]=.22f*std::sin(2*pi*132.f*i/static_cast<float>(rate));
        r[i]=originalR[i]=.19f*std::cos(2*pi*736.f*i/static_cast<float>(rate));
    }
    fx->process(l.data(),r.data(),1024);
    for(int i=0;i<1024;++i) {
        assert(std::abs(l[i]-originalL[i])<1.e-5f);
        assert(std::abs(r[i]-originalR[i])<1.e-5f);
    }
    auto invalid=std::make_unique<GrandStageDsp>();
    assert(!invalid->prepare(96000.));
}

void verifyMusicPreservation(double rate,int venue,int fixture) {
    Settings state{};state.venue=venue;
    assert(armix::grandstage::valid(state));
    const auto scene=armix::grandstage::sceneFor(state);
    assert(!scene.moving && scene.startAzimuthDegrees==0.f);
    assert(scene.spacePreset!=0);
    auto whole=std::make_unique<GrandStageDsp>();
    auto chunked=std::make_unique<GrandStageDsp>();
    assert(whole->setSettings(state) && chunked->setSettings(state));
    assert(whole->prepare(rate) && chunked->prepare(rate));
    std::vector<float> l(n),r(n),cl(n),cr(n);
    double inputEnergy=0.;
    for(int i=0;i<n;++i) {
        const float t=static_cast<float>(i)/static_cast<float>(rate);
        const float bass=.23f*std::sin(2*pi*110.f*t);
        const float body=.08f*std::sin(2*pi*840.f*t);
        const float a=bass+body;
        const float b=(fixture==1?-a:fixture==2?0.f:
            fixture==3?-.48f*a:.22f*std::sin(2*pi*420.f*t));
        l[i]=cl[i]=(fixture==2?0.f:a);
        r[i]=cr[i]=(fixture==2?a:b);
        if(i>4096)inputEnergy+=static_cast<double>(l[i])*l[i]+
                                    static_cast<double>(r[i])*r[i];
    }
    whole->process(l.data(),r.data(),n);
    for(int at=0;at<n;at+=64)
        chunked->process(cl.data()+at,cr.data()+at,std::min(64,n-at));
    double outputEnergy=0.,sideEnergy=0.;
    for(int i=0;i<n;++i) {
        assert(std::isfinite(l[i]) && std::isfinite(r[i]));
        assert(std::abs(l[i]-cl[i])<2.e-6f);
        assert(std::abs(r[i]-cr[i])<2.e-6f);
        assert(std::abs(l[i])<.99f && std::abs(r[i])<.99f);
        if(i>4096) {
            outputEnergy+=static_cast<double>(l[i])*l[i]+
                          static_cast<double>(r[i])*r[i];
            sideEnergy+=static_cast<double>(l[i]-r[i])*(l[i]-r[i]);
        }
    }
    // This is a source regression tripwire, NOT an auditory "premium" proof.
    // Reject catastrophic energy collapse even when measured direction moves.
    assert(inputEnergy>1.);
    assert(std::sqrt(outputEnergy/inputEnergy)>.55);
    if(fixture==1)assert(sideEnergy>1.);
}
} // namespace

int main() {
    for(double rate:{44100.,48000.}) {
        verifyDryAndValidation(rate);
        for(int venue=0;venue<3;++venue)
            for(int fixture=0;fixture<4;++fixture)
                verifyMusicPreservation(rate,venue,fixture);
    }
}
