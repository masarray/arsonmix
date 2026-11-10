// SPDX-License-Identifier: AGPL-3.0-only
// NON-GATING, synthetic engine-only callback distribution, not JUCE/ASIO.
// No fixture generation or setup is included in the timed region.
// Latency-study choices cannot become live settings through this executable.
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
#include <string_view>
namespace {
using Clock=std::chrono::steady_clock;
constexpr int warmup=256;
constexpr int measured=1024; // >=1000 observations for a meaningful p99.9 rank
enum class Material {Sustain,Silence,Transitions,Dense};
const char* name(Material m){
    switch(m){
        case Material::Sustain:return "sustain";
        case Material::Silence:return "silence";
        case Material::Transitions:return "voiced_unvoiced";
        case Material::Dense:return "dense_harmonics";
    }
    return "unknown";
}
std::size_t rank(double percentile){
    return std::min(static_cast<std::size_t>(measured-1),
        static_cast<std::size_t>(std::ceil(percentile*measured)-1));
}
void profile(double rate,int block,int voices,bool musicGuide=false,
             float wet=.55f,float space=.60f,int studyMs=32,
             Material material=Material::Sustain){
    auto engine=std::make_unique<armix::harmony::Engine>();
    const bool ok=studyMs==32?engine->prepare(rate):
                  engine->prepareForOfflineLatencyStudy(rate,studyMs);
    if(!ok){std::cerr<<"Harmony prepare failed\n";std::exit(2);}
    armix::harmony::Settings settings;
    settings.voices=voices;settings.wet=wet;settings.space=space;
    settings.correction=true;
    settings.guideMode=musicGuide?1:0;
    settings.guideChannelId=musicGuide?770:0;
    armix::harmony::MusicKeyDetector guide;
    if(musicGuide&&!guide.prepare(rate)){
        std::cerr<<"Music guide prepare failed\n";std::exit(2);
    }
    if(!engine->setSettings(settings)){
        std::cerr<<"Harmony settings rejected\n";std::exit(2);
    }
    std::array<float,256> l{},r{},musicL{},musicR{};
    std::array<double,measured> elapsed{};
    double phase=0.;
    const double step=6.283185307179586*220./rate;
    for(int blockIndex=0;blockIndex<warmup+measured;++blockIndex){
        // Synthesis is OUTSIDE the timed callback, including separate source.
        for(int i=0;i<block;++i){
            const bool active=material!=Material::Silence&&
                (material!=Material::Transitions||(blockIndex/24)%2==0);
            const double extra=material==Material::Dense?
                .06*std::sin(3.*phase)+.04*std::sin(5.*phase):0.;
            const float x=active?static_cast<float>(
                .16*std::sin(phase)+.04*std::sin(2.*phase)+extra):0.f;
            l[static_cast<std::size_t>(i)]=x;
            r[static_cast<std::size_t>(i)]=x;
            if(musicGuide){
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
             <<"Harmony SYNTHETIC_ENGINE_ONLY rate="<<rate
             <<" block="<<block<<" voices="<<voices
             <<" latency_ms="<<studyMs
             <<" wet="<<wet<<" space="<<space
             <<" material="<<name(material)
             <<" guide="<<(musicGuide?"Music":"Manual")
             <<" p50_us="<<elapsed[rank(.50)]
             <<" p95_us="<<elapsed[rank(.95)]
             <<" p99_us="<<elapsed[rank(.99)]
             <<" p99_9_us="<<elapsed[rank(.999)]
             <<" max_us="<<elapsed.back()
             <<" deadline_us="<<deadlineUs
             <<" p99_9_deadline_pct="<<100.*elapsed[rank(.999)]/deadlineUs
             <<" max_deadline_pct="<<100.*elapsed.back()/deadlineUs
             <<" over_budget="<<misses<<"/"<<measured
             <<" NON_GATING_NOT_ASIO\n";
}
}
int main(int argc,char** argv){
    const auto mode=argc==2?std::string_view(argv[1]):std::string_view{};
    if(argc>2||(argc==2&&mode!="--matrix"&&mode!="--latency-quick")){
        std::cerr<<"Usage: harmony_cpu_profile_test [--matrix|--latency-quick]\n";
        return 2;
    }
    if(mode=="--latency-quick"){
        // Same runner, same source PCM and settings for all grain candidates.
        // Comparison is CPU only and says NOTHING about human-singer quality.
        for(double rate:{48000.,96000.})
          for(int ms:{32,24,16})
            for(Material m:{Material::Sustain,Material::Dense})
              profile(rate,64,5,true,.5f,.6f,ms,m);
        return 0;
    }
    if(mode=="--matrix"){
        for(double rate:{44100.,48000.,96000.,192000.})
          for(int block:{64,128,256})
            for(int voices:{2,3,4,5})
              for(float wet:{0.f,.5f,1.f})
                for(float space:{0.f,.6f,1.f})
                  profile(rate,block,voices,false,wet,space);
        for(double rate:{44100.,48000.,96000.,192000.})
          for(int block:{64,128,256})
            for(Material m:{Material::Sustain,Material::Silence,
                            Material::Transitions,Material::Dense})
              profile(rate,block,5,true,1.f,1.f,32,m);
        return 0;
    }
    // Existing quick non-gating CTest: complete engine baseline at ASIO-like
    // periods. The extended grid is optional to avoid wasting paid CI.
    for(int voices:{3,4,5})
      for(double rate:{48000.,96000.})
        for(int block:{64,128,256})
          profile(rate,block,voices);
    for(int block:{64,128,256})profile(192000.,block,5);
    for(double rate:{48000.,96000.,192000.})
      profile(rate,64,5,true);
}
