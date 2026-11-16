// SPDX-License-Identifier: AGPL-3.0-only
// P8.3 realtime CONTROL -> native core PCM, no JUCE or audio devices.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) || !SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
#error Requires P8.3 source opt-in
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::Settings;
static double run(SonKuSpatialCore& core,int iteration,int n=128) {
    std::array<float,128> l{},r{};
    for(int i=0;i<n;++i){
        const float t=static_cast<float>(iteration*n+i)/48000.f;
        l[i]=.10f*std::sin(2*3.14159265f*197.f*t);
        r[i]=-.07f*std::sin(2*3.14159265f*523.f*t);
    }
    core.process(l.data(),r.data(),n);
    double sum=0.;
    for(int i=0;i<n;++i) {
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
        assert(std::abs(l[i])<1.f&&std::abs(r[i])<1.f);
        sum+=l[i]*l[i]+r[i]*r[i];
    }
    return sum;
}
int main(){
  for(double rate:{44100.,48000.}){
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    Settings s{};
    s.moving=false;s.spacePreset=1;
    s.sourceDistanceMeters=1.6f;s.roomAmount=.5f;s.ambience=.2f;
    s.stereoSpreadDegrees=60.f;
    assert(core->setSettings(s));
    for(int n=0;n<40;++n)run(*core,n);
    s.renderMode=2;s.musicCompatibleLowFrequency=true;
    s.headYawDegrees=0.f;s.moving=false;
    assert(core->setSettings(s));
    assert(core->settings().renderMode==2);
    double energy=0.;
    for(int n=40;n<90;++n)energy+=run(*core,n);
    assert(energy>1e-5);
    s.headYawDegrees=90.f;
    assert(core->setSettings(s));
    assert(core->settings().headYawDegrees==90.f);
    for(int n=90;n<110;++n)run(*core,n);
    s.musicCompatibleLowFrequency=false;
    assert(core->setSettings(s));
    for(int n=110;n<130;++n)run(*core,n);
    s.renderMode=1;s.stereoSpreadDegrees=0.f;
    assert(core->setSettings(s));
    for(int n=130;n<150;++n)run(*core,n);
    s.renderMode=0;s.stereoSpreadDegrees=60.f;
    assert(core->setSettings(s));
    for(int n=150;n<175;++n)run(*core,n);
    auto invalid=s;
    invalid.renderMode=2;invalid.stereoSpreadDegrees=0.f;
    assert(!core->setSettings(invalid));
    assert(core->settings().renderMode==0);
    invalid=s;invalid.renderMode=8;
    assert(!core->setSettings(invalid));
    invalid=s;invalid.headYawDegrees=400.f;
    assert(!core->setSettings(invalid));
    invalid=s;invalid.headYawDegrees=NAN;
    assert(!core->setSettings(invalid));
    std::printf("PASS P8.3 LIVE SOURCE: %.0f Hz typed mode switch/room/yaw/bounds, no reset, finite native PCM\n",rate);
  }
}
