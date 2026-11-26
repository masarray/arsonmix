// SPDX-License-Identifier: AGPL-3.0-only
// Offline-only: exact native SonKu P99 graph, deterministic generated material.
#include "SonKuP99DefaultOrbitStage.h"
#include "ImerSipP92Scene.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
using namespace armix::sonku;
namespace {
constexpr double pi=3.14159265358979323846;
struct Noise {
    unsigned x=0x713f2acdu;
    float sample(){x^=x<<13;x^=x>>17;x^=x<<5;
        return static_cast<float>(x&0xffffffu)/8388608.f-1.f;}
};
void render(const std::filesystem::path& out,int rate,int caseId){
    auto core=std::make_unique<SonKuSpatialCore>();
    const bool immersib=caseId==12;
    if(immersib){core->optInImmerSIBStageMonitor();core->optInImmerSIBVocalRoom();
        core->optInImmerSIBMusicalEq();}
    else core->optInP99ForSonKuInsert();
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    auto s=p99::DefaultOrbitStage::settings();
    s.moving=caseId==8;
    s.startAzimuthDegrees=caseId<8?static_cast<float>((caseId%4)*90):0.f;
    // Preserve exact operator-approved settings except the controlled pose.
    if(caseId==9){s.spacePreset=0;s.roomAmount=0.f;s.ambience=0.f;}
    if(caseId==10)s.spatialMix=0.f;
    if(caseId==11)s.sceneVoicing=0;
    if(immersib)s=armix::builtin_fx::imersipP92Factory(2);
    assert(valid(s)&&core->setSettings(s));core->reset();
    std::ofstream f(out,std::ios::binary);assert(f);
    const int warm=rate/2, duration=caseId==8?12*rate:2*rate;
    Noise noise;
    std::array<float,128> l{},r{};
    for(int base=0;base<warm+duration;base+=128){
        const int n=std::min(128,warm+duration-base);
        for(int i=0;i<n;++i){
            const double t=double(base+i)/rate;
            // Vocal-like harmonic/formant proxy plus bass and lateral
            // instrumentation; not real singing or extracted stems.
            const double env=.55+.25*std::sin(2*pi*2.7*t);
            const float center=static_cast<float>(env*(.055*std::sin(2*pi*220*t)+
                 .025*std::sin(2*pi*660*t)+.018*std::sin(2*pi*1540*t)));
            const float bass=.045f*std::sin(2*pi*82*t);
            const float transient=.035f*noise.sample()*
                std::exp(-15.*std::fmod(t,.25));
            const float side=.038f*std::sin(2*pi*1237*t)+
                .014f*std::sin(2*pi*4700*t)+transient;
            const float m=center+bass;
            // 0-3 coherent mono, 4-7 wide stereo, 8 continuous default orbit.
            // Coherence changes continuously on the orbit probe.
            const float width=caseId<4?0.f:caseId==8?
                static_cast<float>(.5+.5*std::sin(2*pi*t/3.)):1.f;
            l[i]=m+width*side;r[i]=m-width*side;
        }
        core->process(l.data(),r.data(),n);
        for(int i=0;i<n;++i){
            assert(std::isfinite(l[i])&&std::isfinite(r[i]));
            if(base+i<warm)continue;
            f.write(reinterpret_cast<const char*>(&l[i]),sizeof(float));
            f.write(reinterpret_cast<const char*>(&r[i]),sizeof(float));
        }
    }
    assert(f);
}
}
int main(int argc,char** argv){
    assert(argc==2);
    const std::filesystem::path folder=argv[1];std::filesystem::create_directories(folder);
    for(int rate:{44100,48000})
        for(int i=0;i<13;++i)
            render(folder/(std::to_string(rate)+"-"+std::to_string(i)+".f32"),rate,i);
}
