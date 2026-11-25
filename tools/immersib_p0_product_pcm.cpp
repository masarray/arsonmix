// SPDX-License-Identifier: AGPL-3.0-only
// Real two-product shared-core isolation: not a JUCE/ASIO benchmark.
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::Settings;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* out=std::fopen(argv[1],"wb");
    if(!out)return 3;
    constexpr int frames=128;
    std::array<float,frames> l{},r{};
    for(int rate:{44100,48000})for(int mode:{0,5,9}){
        auto sonku=std::make_unique<SonKuSpatialCore>();
        auto immersive=std::make_unique<SonKuSpatialCore>();
        sonku->optInP99ForSonKuInsert();
        immersive->optInImmerSIBStageMonitor();
        immersive->optInImmerSIBMusicalEq();
        const auto profile=armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch;
        Settings s{};
        s.renderMode=mode;s.moving=true;s.revolutionSeconds=6.5f;
        s.stereoSpreadDegrees=82.f;s.spatialMix=1.f;s.outputTrim=1.9f;
        s.spacePreset=3;s.sourceDistanceMeters=3.1f;s.roomAmount=1.f;
        s.ambience=.62f;s.sceneVoicing=mode==9?1:0;
        auto v=armix::builtin_fx::imersipP92Factory(2);
        if(!sonku->prepare(rate,profile)||!immersive->prepare(rate,profile)||
           !sonku->setSettings(s)||!immersive->setSettings(v))return 4;
        for(int block=0;block<96;++block){
            if(block==24){s.spacePreset=0;s.roomAmount=0;s.ambience=0;}
            if(block==40){s.spacePreset=3;s.roomAmount=1;s.ambience=.62f;}
            if(block==60){s.headYawDegrees=18.f;s.stereoSpreadDegrees=0.f;}
            if(!sonku->setSettings(s))return 5;
            for(auto* core:{sonku.get(),immersive.get()}){
                for(int i=0;i<frames;++i){
                    const double t=double(block*frames+i)/rate;
                    l[i]=float(.10*std::sin(6.283185307179586*337*t));
                    r[i]=float(.08*std::sin(6.283185307179586*619*t));
                    if(block>=72&&block<80)l[i]=r[i]=0.f;
                }
                core->process(l.data(),r.data(),frames);
                for(int i=0;i<frames;++i){
                    if(!std::isfinite(l[i])||!std::isfinite(r[i]))return 6;
                    if(std::fwrite(&l[i],sizeof(float),1,out)!=1||
                       std::fwrite(&r[i],sizeof(float),1,out)!=1)return 7;
                }
            }
        }
    }
    return std::fclose(out)==0?0:8;
}
