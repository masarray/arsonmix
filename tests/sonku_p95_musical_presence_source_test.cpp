// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P95_MUSICAL_PRESENCE_RESEARCH) || !SONKU_P95_MUSICAL_PRESENCE_RESEARCH
#error P95 tests MUST use the experimental DSP, no disguised P94 baseline
#endif
#include "../engine/builtin_fx/sonku/SonKuP95MusicalPresence.h"
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
static void makeupGate(double rate){
    p95::MusicalPresence p;
    assert(p.prepare(rate));
    float prev=1.f;
    // Over 2 seconds, candidate at half amplitude must slowly approach
    // 1.65x makeup, never jump or overshoot.
    for(int i=0;i<2*int(rate);++i){
        const float g=p.process(.20f,.20f,.10f,.10f);
        assert(g>=1.f&&g<=1.65001f&&std::isfinite(g));
        if(i>int(rate))assert(g>=prev-1.e-5f);
        prev=g;
    }
    assert(p.gain()>1.55f);
    p.reset();
    assert(p.gain()==1.f);
    // Already louder than input: NEVER pull down user gain.
    for(int i=0;i<2*int(rate);++i){
        const float g=p.process(.1f,.1f,.3f,.3f);
        assert(g>=1.f&&g<=1.65001f);
    }
    assert(std::abs(p.gain()-1.f)<.0001f);
}
static double render(int rate,float azimuth,int block){
    auto p=std::make_unique<SonKuSpatialCore>();
    assert(p->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    Settings s{};
    s.renderMode=9;s.moving=false;s.startAzimuthDegrees=azimuth;
    s.stereoSpreadDegrees=82.f;s.spatialMix=1.f;
    s.outputTrim=1.9f;s.spacePreset=3;s.sourceDistanceMeters=3.1f;
    s.roomAmount=1.f;s.ambience=.62f;
    assert(valid(s)&&p->setSettings(s));
    constexpr float pi=3.14159265358979323846f;
    const int frames=rate;
    std::vector<float> l(frames),r(frames);
    for(int i=0;i<frames;++i){
        const float t=float(i)/rate;
        const float music=.15f*std::sin(2*pi*70*t)+
            .10f*std::sin(2*pi*119*t)+.10f*std::sin(2*pi*880*t)+
            .035f*std::sin(2*pi*6800*t);
        l[i]=music;
        r[i]=.88f*music+.026f*std::sin(2*pi*1760*t);
    }
    for(int i=0;i<frames;i+=block)
        p->process(l.data()+i,r.data()+i,std::min(block,frames-i));
    double energy=0.;
    for(int i=frames/3;i<frames;++i){
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
        assert(std::abs(l[i])<=.981f&&std::abs(r[i])<=.981f);
        energy+=double(l[i])*l[i]+double(r[i])*r[i];
    }
    return std::sqrt(energy/(2.0*(frames-frames/3)));
}
int main(){
    for(int rate:{44100,48000}){
        makeupGate(rate);
        for(float az:{0.f,90.f,180.f,270.f}){
            const auto a=render(rate,az,64),b=render(rate,az,256);
            assert(a>.04 && a<.95 && std::abs(a-b)<1.e-8);
            std::printf("P95_MUSIC,rate=%d,az=%.0f,rms=%.7f\n",rate,az,a);
        }
    }
    std::puts("PASS P95 Musical Presence: slow no-pumping upward guard, user gain preserved, 44.1/48k, four directions, 64/256 PCM block parity, finite peaks");
}
