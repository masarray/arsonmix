// SPDX-License-Identifier: AGPL-3.0-only
// Non-gating, reproducible synthetic ENGINE-only callback CPU profile.
// Does not measure JUCE graph scheduling, ASIO driver or singer ear latency.
#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>

namespace {
using Clock=std::chrono::steady_clock;
constexpr int warmup=256;
constexpr int measured=512;
void profile(double rate,int block,int voices){
    auto engine=std::make_unique<armix::harmony::Engine>();
    if(!engine->prepare(rate)){std::cerr<<"Harmony prepare failed\n";std::exit(2);}
    armix::harmony::Settings settings;
    settings.voices=voices;settings.wet=0.55f;settings.correction=true;
    if(!engine->setSettings(settings)){std::cerr<<"Harmony settings rejected\n";std::exit(2);}
    std::array<float,256> l{},r{};
    std::array<double,measured> elapsed{};
    double phase=0.;
    const double step=6.283185307179586*220./rate;
    for(int blockIndex=0;blockIndex<warmup+measured;++blockIndex){
        // Synthesis and timer calls are outside DSP timing.
        for(int i=0;i<block;++i){
            const float x=static_cast<float>(0.16*std::sin(phase)+
                                              0.04*std::sin(2.*phase));
            l[static_cast<std::size_t>(i)]=x;
            r[static_cast<std::size_t>(i)]=x;
            phase+=step;
            if(phase>=6.283185307179586)phase-=6.283185307179586;
        }
        const auto started=Clock::now();
        engine->process(l.data(),r.data(),block);
        const auto finished=Clock::now();
        if(blockIndex>=warmup)
            elapsed[static_cast<std::size_t>(blockIndex-warmup)]=
                std::chrono::duration<double,std::micro>(finished-started).count();
        for(int i=0;i<block;++i)
            if(!std::isfinite(l[static_cast<std::size_t>(i)])||
               !std::isfinite(r[static_cast<std::size_t>(i)])){
                std::cerr<<"Nonfinite Harmony PCM in profile\n";std::exit(2);
            }
    }
    const double deadlineUs=1.e6*static_cast<double>(block)/rate;
    const auto misses=std::count_if(elapsed.begin(),elapsed.end(),
        [deadlineUs](double elapsedUs){return elapsedUs>deadlineUs;});
    std::sort(elapsed.begin(),elapsed.end());
    std::cout<<std::fixed<<std::setprecision(2)
             <<"Harmony synthetic ENGINE ONLY rate="<<rate
             <<" block="<<block<<" voices="<<voices
             <<" p50_us="<<elapsed[measured/2]
             <<" p95_us="<<elapsed[(measured*95)/100]
             <<" p99_us="<<elapsed[(measured*99)/100]
             <<" max_us="<<elapsed.back()
             <<" deadline_us="<<deadlineUs
             <<" over_budget="<<misses<<"/"<<measured
             <<" (non-gating; not physical ASIO or multichannel PCM)\n";
}
}
int main(){
    for(int voices:{2,5}){
        for(int block:{64,128,256})profile(48000.,block,voices);
        profile(96000.,128,voices);
    }
}