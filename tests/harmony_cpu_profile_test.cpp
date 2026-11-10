// SPDX-License-Identifier: AGPL-3.0-only
// Non-gating, reproducible synthetic ENGINE-only callback CPU profile.
// Does not measure JUCE graph scheduling, ASIO driver or singer ear latency.
#include "../engine/builtin_fx/HarmonyDsp.h"
#include "../engine/builtin_fx/HarmonyMusicKey.h"
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
void profile(double rate,int block,int voices,bool musicGuide=false){
    auto engine=std::make_unique<armix::harmony::Engine>();
    if(!engine->prepare(rate)){std::cerr<<"Harmony prepare failed\n";std::exit(2);}
    armix::harmony::Settings settings;
    settings.voices=voices;settings.wet=0.55f;settings.correction=true;
    settings.guideMode=musicGuide?1:0;
    settings.guideChannelId=musicGuide?770:0;
    armix::harmony::MusicKeyDetector guide;
    if(musicGuide&&!guide.prepare(rate)){
        std::cerr<<"Music guide prepare failed\\n";std::exit(2);
    }
    if(!engine->setSettings(settings)){std::cerr<<"Harmony settings rejected\n";std::exit(2);}
    std::array<float,256> l{},r{},musicL{},musicR{};
    std::array<double,measured> elapsed{};
    double phase=0.;
    // 35 cents sharp so the enabled lead shifter actually runs.
    const double step=6.283185307179586*220.*std::exp2(35./1200.)/rate;
    for(int blockIndex=0;blockIndex<warmup+measured;++blockIndex){
        // Synthesis and timer calls are outside DSP timing.
        for(int i=0;i<block;++i){
            const float x=static_cast<float>(0.16*std::sin(phase)+
                                              0.04*std::sin(2.*phase));
            l[static_cast<std::size_t>(i)]=x;
            r[static_cast<std::size_t>(i)]=x;
            if(musicGuide){
                // Different ChannelId source; no music PCM mixed into vocal.
                // Exclude guide-tone fixture synthesis from timed callback.
                const double t=static_cast<double>(blockIndex*block+i)/rate;
                musicL[static_cast<std::size_t>(i)]=
                    musicR[static_cast<std::size_t>(i)]=static_cast<float>(
                      .06*(std::sin(6.283185307179586*261.625565*t)+
                           std::sin(6.283185307179586*329.627557*t)+
                           std::sin(6.283185307179586*391.995436*t)));
            }
            phase+=step;
            if(phase>=6.283185307179586)phase-=6.283185307179586;
        }
        const auto started=Clock::now();
        if(musicGuide){
            guide.process(musicL.data(),musicR.data(),block);
            const auto key=guide.estimate();
            engine->setGuideKey(key.valid()?key.key:-1,key.valid()?key.scale:-1);
        }
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
             <<" guide="<<(musicGuide?"Music ChannelId":"Manual")
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
    // All singer configurations in ASIO-like callback partitions.
    for(int voices:{3,4,5})
      for(double rate:{48000.,96000.})
        for(int block:{64,128,256})
          profile(rate,block,voices);
    // Device upper-bound stress: never interpreted as physical ASIO proof.
    for(int block:{64,128,256})profile(192000.,block,5);
    // Real guide detector + vocal DSP in ONE timed region, without
    // synthesizing the separate music source inside the measurement.
    for(double rate:{48000.,96000.,192000.})
      profile(rate,64,5,true);
}