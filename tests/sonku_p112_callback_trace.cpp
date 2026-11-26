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
#include <cstdint>
#include <cstdlib>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <time.h>
#endif

// Windows cycles are a work proxy, never converted to elapsed time.
static std::uint64_t threadWork(){
#ifdef _WIN32
    ULONG64 cycles=0;
    if(!QueryThreadCycleTime(GetCurrentThread(),&cycles))std::abort();
    return cycles;
#else
    timespec t{};
    if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)!=0)std::abort();
    return std::uint64_t(t.tv_sec)*1000000000ULL+std::uint64_t(t.tv_nsec);
#endif
}
using namespace armix::sonku;
constexpr int measured=1024;
int main(int argc,char** argv){
    if(argc!=6)return 2;
    const int selectedBrand=std::atoi(argv[2]),selectedRate=std::atoi(argv[3]),
              selectedBlock=std::atoi(argv[4]),selectedScenario=std::atoi(argv[5]);
    if(selectedBrand<0||selectedBrand>2||(selectedRate!=44100&&selectedRate!=48000)||
       (selectedBlock!=64&&selectedBlock!=128&&selectedBlock!=256)||selectedScenario<0||selectedScenario>2)return 2;
    auto* file=std::fopen(argv[1],"wb");if(!file)return 2;
    constexpr double pi=3.14159265358979323846;
    for(int brand:{0,1,2})for(int rate:{44100,48000})
    for(int block:{64,128,256})for(int scenario:{0,1,2}){
        if(brand!=selectedBrand||rate!=selectedRate||block!=selectedBlock||scenario!=selectedScenario)continue;
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
        std::array<double,measured> durations{},clockWall{};
        std::array<std::uint64_t,measured> work{},clockWork{};
        // Measure bracket overhead separately, not subtracted from DSP time.
        for(int i=0;i<measured;++i){
            const auto w0=std::chrono::steady_clock::now();
            const auto c0=threadWork();
            const auto c1=threadWork();
            const auto w1=std::chrono::steady_clock::now();
            if(c1<c0)return 9;
            clockWork[i]=c1-c0;
            clockWall[i]=std::chrono::duration<double,std::micro>(w1-w0).count();
        }
        std::array<float,256> l{},r{};
        for(int k=0;k<total;++k){
            if(k==warm && scenario==2){s.moving=true;if(!dsp->setSettings(s))return 5;}
            std::copy_n(inL.data()+k*block,block,l.data());
            std::copy_n(inR.data()+k*block,block,r.data());
            const auto a=std::chrono::steady_clock::now();
            const auto c0=threadWork();
            dsp->process(l.data(),r.data(),block);
            const auto c1=threadWork();
            const auto b=std::chrono::steady_clock::now();
            if(c1<c0)return 9;
            if(k>=warm){durations[k-warm]=std::chrono::duration<double,std::micro>(b-a).count();work[k-warm]=c1-c0;}
            for(int i=0;i<block;++i){
                if(!std::isfinite(l[i])||!std::isfinite(r[i])||std::abs(l[i])>.981f||std::abs(r[i])>.981f)return 6;
                out[2*(k*block+i)]=l[i];out[2*(k*block+i)+1]=r[i];
            }
        }
        if(std::fwrite(out.data(),sizeof(float),out.size(),file)!=out.size())return 7;
        // Trace is emitted after processing, never in timed region.
        for(int i=0;i<measured;++i)
            std::printf("TRACE,%d,%.6f,%llu,%.6f,%llu\n",i,durations[i],
                static_cast<unsigned long long>(work[i]),clockWall[i],
                static_cast<unsigned long long>(clockWork[i]));
    }
    return std::fclose(file)==0?0:8;
}
