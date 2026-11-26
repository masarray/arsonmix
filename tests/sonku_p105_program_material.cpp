// SPDX-License-Identifier: AGPL-3.0-only
// Offline program-material A/B, same P99 native graph, no second renderer.
#include "SonKuP99DefaultOrbitStage.h"
#include "ImerSipP92Scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
using namespace armix::sonku;
int main(int argc,char** argv){
    if(argc!=3)return 2;
    constexpr int rate=48000,block=128,warm=rate;
    std::ifstream src(argv[1],std::ios::binary|std::ios::ate);
    if(!src)return 2;
    const auto bytes=static_cast<std::streamoff>(src.tellg());
    if(bytes!=13LL*rate*2*sizeof(float))return 3;
    std::vector<float> input(static_cast<size_t>(bytes)/sizeof(float));
    src.seekg(0);src.read(reinterpret_cast<char*>(input.data()),bytes);
    if(!src)return 3;
    const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
    for(int c=0;c<14;++c){
        auto dsp=std::make_unique<SonKuSpatialCore>();
        if(c<6||c==13)dsp->optInP99ForSonKuInsert();
        else {dsp->optInImmerSIBStageMonitor();dsp->optInImmerSIBVocalRoom();dsp->optInImmerSIBMusicalEq();}
        if(!dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch))return 4;
        auto s=p99::DefaultOrbitStage::settings();
        s.moving=c==0;s.startAzimuthDegrees=c==2?180.f:0.f;
        if(c==3)s.sceneVoicing=0;
        if(c==4){s.spacePreset=0;s.roomAmount=0.f;s.ambience=0.f;}
        if(c==5)s.spatialMix=0.f;
        if(c>=6&&c<13)s=armix::builtin_fx::imersipP92Factory(c-4);
        if(c==13)s.moving=true;
        if(!valid(s)||!dsp->setSettings(s))return 5;
        dsp->reset();
        std::ofstream f(out/(std::to_string(c)+".f32"),std::ios::binary);
        if(!f)return 6;
        std::array<float,block> l{},r{};
        for(int pos=0;pos<13*rate;pos+=block){
            // Case 13: same continuous program and graph, no reset between
            // edits. Boundaries aligned to the 128-frame audio callback.
            if(c==13){
                bool edit=true;
                switch(pos){
                    case 3*rate: s.sceneVoicing=0;break;
                    case 5*rate: s.sceneVoicing=1;break;
                    case 7*rate: s.spacePreset=0;s.roomAmount=0.f;s.ambience=0.f;break;
                    case 9*rate: s.spacePreset=3;s.roomAmount=1.f;s.ambience=.77f;break;
                    case 11*rate:s.spatialMix=0.f;break;
                    case 12*rate:s.spatialMix=1.f;break;
                    default:edit=false;break;
                }
                if(edit&&(!valid(s)||!dsp->setSettings(s)))return 9;
            }
            const int n=std::min(block,13*rate-pos);
            for(int i=0;i<n;++i){l[i]=input[2*(pos+i)];r[i]=input[2*(pos+i)+1];}
            dsp->process(l.data(),r.data(),n);
            for(int i=0;i<n;++i){
                if(!std::isfinite(l[i])||!std::isfinite(r[i]))return 7;
                if(pos+i<warm)continue;
                f.write(reinterpret_cast<const char*>(&l[i]),sizeof(float));
                f.write(reinterpret_cast<const char*>(&r[i]),sizeof(float));
            }
        }
        if(!f)return 8;
    }
}
