// SPDX-License-Identifier: AGPL-3.0-only
// P7 Linux runner-only aggregate callback cost, no claimed hardware ASIO.
#ifndef SONKU_P7_MEASURED_ELEVATION_RESEARCH
#error build only with P7 research profile
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
namespace {
struct Result {double ms,p50,p95,p99,ratio;};
Result bench(int count,float spread,bool six,float elevation=0.f){
    using namespace armix::sonku;
    constexpr int n=128,frames=48000;
    std::vector<std::unique_ptr<SonKuSpatialCore>> processors;
    for(int c=0;c<count;++c){
        auto dsp=std::make_unique<SonKuSpatialCore>();
        if(!dsp->prepare(48000.,six?SonKuReflectionProfile::SixSurfaceKemarResearch:
                                      SonKuReflectionProfile::DirectionalKemar))
            throw "invalid P7 core";
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
        if(elevation!=0.f && !dsp->setResearchDirectElevation(elevation))
            throw "invalid P7.1 quiescent XYZ placement";
#else
        if(elevation!=0.f)throw "P7.1 bench not compiled";
#endif
        Settings s{};s.spacePreset=1;s.roomAmount=.65f;s.ambience=.2f;
        s.stereoSpreadDegrees=spread;s.moving=true;s.revolutionSeconds=4.f;
        if(!dsp->setSettings(s))throw "invalid P7 scene";
        processors.push_back(std::move(dsp));
    }
    std::array<float,n> inL{},inR{};
    for(int i=0;i<n;++i){
        inL[i]=.12f*std::sin(i*.07f)+.02f*std::sin(i*.4f);
        inR[i]=.12f*std::sin(i*.07f)-.02f*std::sin(i*.4f);
    }
    std::vector<double> times;
    times.reserve(frames/n);
    auto before=std::chrono::steady_clock::now();
    for(int frame=0;frame<frames;frame+=n){
        const auto start=std::chrono::steady_clock::now();
        for(auto& dsp:processors){
            auto l=inL,r=inR;
            dsp->process(l.data(),r.data(),n);
            if(!std::isfinite(l[0])||!std::isfinite(r[0]))throw "nonfinite P7";
        }
        const auto end=std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double,std::micro>(end-start).count());
    }
    auto after=std::chrono::steady_clock::now();
    std::sort(times.begin(),times.end());
    const auto q=[&](double frac){size_t i=static_cast<size_t>(std::ceil(frac*times.size()));
                                 return times[std::clamp(i, size_t{1},times.size())-1];};
    return {std::chrono::duration<double,std::milli>(after-before).count(),
            q(.5),q(.95),q(.99),q(.99)/(1e6*n/48000.)};
}
}
int main(){
    try{
        for(float spread:{0.f,60.f})
            for(int n:{1,2,4}){
                const auto base=bench(n,spread,false),six=bench(n,spread,true);
                std::printf("SONKU_P7_CPU rate=48000 block=128 spread=%.0f inserts=%d "
                            "four_ms=%.3f six_ms=%.3f six_over_four=%.3f "
                            "six_p50us=%.3f six_p95us=%.3f six_p99us=%.3f "
                            "six_budget_ratio=%.3f NOTE=linux_host_not_ASIO\n",
                            spread,n,base.ms,six.ms,six.ms/base.ms,
                            six.p50,six.p95,six.p99,six.ratio);
            }
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
        // Exact same six-boundary FIR count, source, ROOM and 1s clock.
        // A measured-elevation SOURCE does not admit a new convolution.
        for(float spread:{0.f,60.f}){
            for(int n:{1,2,4}){
                const auto horizontal=bench(n,spread,true,0.f);
                const auto elevated=bench(n,spread,true,25.f);
                const auto below=bench(n,spread,true,-25.f);
                std::printf("SONKU_P71_CPU rate=48000 block=128 spread=%.0f inserts=%d "
                    "zero_p99us=%.3f up25_p99us=%.3f down25_p99us=%.3f "
                    "up_over_zero=%.3f down_over_zero=%.3f "
                    "up_buffer_ratio=%.3f down_buffer_ratio=%.3f "
                    "NOTE=hosted_linux_not_ASIO\n",
                    spread,n,horizontal.p99,elevated.p99,below.p99,
                    elevated.ms/horizontal.ms,below.ms/horizontal.ms,
                    elevated.ratio,below.ratio);
            }
        }
#endif
    }catch(...){std::puts("FAIL SonKu P7 CPU research");return 1;}
}
