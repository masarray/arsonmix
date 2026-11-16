// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
struct Scene {
    int id;const char* name;float meters,room,late,spread,trim,mix;
};
constexpr Scene scenes[]{
    // Same exact user-approved native values as P9.7 Venue; 0=neutral.
    {0,"VENUE",10.f,1.f,1.f,106.f,1.8f,1.f},
    {1,"SIGNATURE",3.1f,1.f,.62f,82.f,1.9f,1.f},
    {2,"STUDIO",4.8f,.94f,.72f,76.f,1.65f,1.f},
    {3,"CINEMA",7.5f,1.f,.84f,98.f,1.7f,1.f},
    {0,"OFF",10.f,1.f,1.f,106.f,1.8f,1.f},
    {0,"BYPASS",10.f,1.f,1.f,106.f,1.8f,0.f}
};
void run(int rate,int az,int index){
    const auto& scene=scenes[index];
    auto core=std::make_unique<SonKuSpatialCore>();
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH && \
    !defined(SONKU_P99_SHARED_CORE_UNOPTED_TEST)
    // The SonKu-only P9.9 behavior is explicitly opted in per instance.
    // An ImmerSIB/branded instance using this core does not inherit it.
    core->optInP99ForSonKuInsert();
#endif
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    Settings s{};
    s.renderMode=9;s.moving=false;s.startAzimuthDegrees=static_cast<float>(az);
    s.stereoSpreadDegrees=scene.spread;
    s.spatialMix=scene.mix;s.outputTrim=scene.trim;
    s.spacePreset=index==4?0:3;s.sourceDistanceMeters=scene.meters;
    s.roomAmount=scene.room;s.ambience=scene.late;
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    s.sceneVoicing=scene.id;
#endif
    assert(valid(s)&&core->setSettings(s));
    const int n=rate;
    constexpr double pi=3.1415926535897932384626433832795;
#if defined(SONKU_P99_ROOM_BODY_PROBE) && SONKU_P99_ROOM_BODY_PROBE
    // Dedicated FULL-ROOM source fixture: 320Hz upper chest/body excites
    // six-wall first images and the EXISTING diffuse late network. Plain
    // P9.8 golden source fixture remains the exact 86Hz reference.
    constexpr int lowHz=320;
#else
    constexpr int lowHz=86;
#endif
    std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i) {
        const double t=double(i)/rate;
        const float x=.055f*std::sin(2*pi*lowHz*t)+
                      .041f*std::sin(2*pi*2050*t)+
                      .021f*std::sin(2*pi*7500*t);
        l[i]=x;
        r[i]=.89f*x+.010f*std::sin(2*pi*3831*t);
    }
    float peak=0;
    for(int i=0;i<n;i+=256)
        core->process(l.data()+i,r.data()+i,std::min(256,n-i));
    std::uint64_t hash=14695981039346656037ull;
    for(int i=0;i<n;++i)for(float value:{l[i],r[i]}){
        assert(std::isfinite(value)&&std::abs(value)<=.9805f);
        peak=std::max(peak,std::abs(value));
        hash^=std::bit_cast<std::uint32_t>(value);
        hash*=1099511628211ull;
    }
    constexpr std::array<int,3> hz{lowHz,2050,7500};
    std::array<double,3> amp{};
    for(int k=0;k<3;++k){
        double c=0,d=0;
        for(int i=n/2;i<n;++i){
            const double phase=2*pi*hz[k]*i/rate;
            c+=l[i]*std::cos(phase);d+=l[i]*std::sin(phase);
        }
        amp[k]=2*std::hypot(c,d)/(n-n/2);
        assert(std::isfinite(amp[k])&&amp[k]>0);
    }
    std::printf("P98_SCENE,%d,%d,%d,%s,%.9f,%.9f,%.9f,%.9f,%016llx\n",
        rate,az,index,scene.name,amp[0],amp[1],amp[2],double(peak),
        static_cast<unsigned long long>(hash));
}
int main(){
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    // A falsified v12 neutral voicing ID must not resurrect obsolete
    // physical-speaker P8.3 modes. PCM reference with P98 disabled
    // intentionally retains older compile-time behavior.
    for(int mode:{1,2}){
        Settings invalid{};
        invalid.renderMode=mode;
        invalid.sceneVoicing=0;
        assert(!valid(invalid));
    }
#endif
    for(int rate:{44100,48000})for(int az:{0,180})
        for(int scene=0;scene<6;++scene)run(rate,az,scene);
}
