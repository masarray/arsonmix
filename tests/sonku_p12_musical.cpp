// SPDX-License-Identifier: AGPL-3.0-only
// P12 independent offline audition: same shipping acoustic graph in both variants.
#include "SonKuP99DefaultOrbitStage.h"
#include "ImerSipP92Scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <vector>
using namespace armix::sonku;
int main(int argc,char** argv){
    if(argc!=3)return 2;
    constexpr int rate=48000,block=128,warm=rate,seconds=31;
    std::ifstream src(argv[1],std::ios::binary|std::ios::ate);
    if(!src||src.tellg()!=static_cast<std::streamoff>(seconds*rate*2*sizeof(float)))return 3;
    std::vector<float> input(seconds*rate*2);
    src.seekg(0);src.read(reinterpret_cast<char*>(input.data()),static_cast<std::streamsize>(input.size()*sizeof(float)));
    if(!src)return 3;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    std::ofstream geometry(out/"geometry.csv");
    geometry<<"case,second,azimuth\n"<<std::setprecision(17);
    for(int c=0;c<16;++c){
        auto dsp=std::make_unique<SonKuSpatialCore>();
        if(c<9)dsp->optInP99ForSonKuInsert();
        else {dsp->optInImmerSIBStageMonitor();dsp->optInImmerSIBVocalRoom();dsp->optInImmerSIBMusicalEq();}
        if(!dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch))return 4;
        auto s=p99::DefaultOrbitStage::settings();
        s.moving=c==0||c==3||c>=4;
        s.startAzimuthDegrees=c==2?180.f:0.f;
        if(c==4)s.sceneVoicing=0;
        if(c==5)s.sceneVoicing=2;
        if(c==6)s.sceneVoicing=3;
        if(c==7){s.spacePreset=0;s.roomAmount=0.f;s.ambience=0.f;}
        if(c==8)s.spatialMix=0.f;
        if(c>=9)s=armix::builtin_fx::imersipP92Factory(c-7);
        if(!valid(s)||!dsp->setSettings(s))return 5;
        dsp->reset();
        const int total=(c<4?seconds:7)*rate;
        std::ofstream f(out/(std::to_string(c)+".f32"),std::ios::binary);
        if(!f)return 6;
        std::array<float,block> l{},r{};
        for(int pos=0;pos<total;pos+=block){
            // 12s of uninterrupted motion, 8s stopped, then resume.
            // Boundaries are in exported audio coordinates (1s warm-up removed).
            if(c==3&&(pos==warm+12*rate||pos==warm+20*rate)){
                // Core treats startAzimuthDegrees as explicit positioning
                // whenever moving changes. Supply the actual held phase,
                // not the stale factory 0 degrees. Otherwise this fixture
                // teleports on pause/resume and is not a valid audition.
                s.startAzimuthDegrees=static_cast<float>(dsp->azimuthDegrees());
                s.moving=!s.moving;
                if(!dsp->setSettings(s))return 9;
            }
            if(pos>=warm&&pos%rate==0)
                geometry<<c<<','<<(pos-warm)/rate<<','<<dsp->azimuthDegrees()<<'\n';
            for(int i=0;i<block;++i){l[i]=input[2*(pos+i)];r[i]=input[2*(pos+i)+1];}
            dsp->process(l.data(),r.data(),block);
            for(int i=0;i<block;++i){
                if(!std::isfinite(l[i])||!std::isfinite(r[i])||std::abs(l[i])>.981f||std::abs(r[i])>.981f)return 7;
                if(pos+i<warm)continue;
                f.write(reinterpret_cast<const char*>(&l[i]),sizeof(float));
                f.write(reinterpret_cast<const char*>(&r[i]),sizeof(float));
            }
        }
        if(!f)return 8;
        geometry<<c<<','<<(total-warm)/rate<<','<<dsp->azimuthDegrees()<<'\n';
    }
    return geometry?0:10;
}
