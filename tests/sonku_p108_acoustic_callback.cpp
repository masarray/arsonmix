// SPDX-License-Identifier: AGPL-3.0-only
// Fixed callback sizes, warmed state, original native DSP, no I/O in timed region.
#include "SonKuP99DefaultOrbitStage.h"
#include "ImerSipP92Scene.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
constexpr int measured=1024;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* file=std::fopen(argv[1],"wb");if(!file)return 2;
    constexpr double pi=3.14159265358979323846;
    for(int brand:{0,1,2})for(int rate:{44100,48000})
    for(int block:{64,128,256})for(int scenario:{0,1,2}){
        auto dsp=std::make_unique<SonKuSpatialCore>();
        if(brand!=1)dsp->optInP99ForSonKuInsert();
        else {dsp->optInImmerSIBStageMonitor();dsp->optInImmerSIBVocalRoom();dsp->optInImmerSIBMusicalEq();}
        if(!dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch))return 3;
        auto s=brand!=1?p99::DefaultOrbitStage::settings():armix::builtin_fx::imersipP92Factory(2);
        if(brand==2)s.sceneVoicing=0; // exact protected Venue
        s.moving=scenario==1;
        if(!dsp->setSettings(s))return 4;
        dsp->reset();
        const int warm=(rate+block-1)/block;
        const int total=warm+measured;
        std::vector<float> inL(total*block),inR(total*block),out(total*block*2);
        for(int i=0;i<total*block;++i){
            const double t=double(i)/rate;
            const float m=.07f*std::sin(2*pi*220*t)+.03f*std::sin(2*pi*1530*t)+.04f*std::sin(2*pi*82*t);
            const float side=static_cast<float>((.02+.018*std::sin(2*pi*t/2.3))*std::sin(2*pi*3701*t));
            inL[i]=m+side;inR[i]=m-side;
        }
        std::vector<double> durations;durations.reserve(measured);
        std::array<float,256> l{},r{};
        for(int k=0;k<total;++k){
            if(k==warm && scenario==2){s.moving=true;if(!dsp->setSettings(s))return 5;}
            std::copy_n(inL.data()+k*block,block,l.data());
            std::copy_n(inR.data()+k*block,block,r.data());
            const auto a=std::chrono::steady_clock::now();
            dsp->process(l.data(),r.data(),block);
            const auto b=std::chrono::steady_clock::now();
            if(k>=warm)durations.push_back(std::chrono::duration<double,std::micro>(b-a).count());
            for(int i=0;i<block;++i){
                if(!std::isfinite(l[i])||!std::isfinite(r[i])||std::abs(l[i])>.981f||std::abs(r[i])>.981f)return 6;
                out[2*(k*block+i)]=l[i];out[2*(k*block+i)+1]=r[i];
            }
        }
        if(std::fwrite(out.data(),sizeof(float),out.size(),file)!=out.size())return 7;
        const double transitionMax=*std::max_element(durations.begin(),durations.begin()+32);
        const double deadline=1.e6*block/rate;
        const auto missed=std::count_if(durations.begin(),durations.end(),[=](double v){return v>=deadline;});
        std::sort(durations.begin(),durations.end());
        auto q=[&](double p){return durations[static_cast<size_t>(std::ceil(p*durations.size())-1)];};
        std::printf("RT,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%zu\n",
            brand,rate,block,scenario,measured,q(.50),q(.95),q(.99),durations.back(),transitionMax,static_cast<size_t>(missed));
    }
    return std::fclose(file)==0?0:8;
}
