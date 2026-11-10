// SPDX-License-Identifier: AGPL-3.0-only
// Realtime pitch-synchronous coverage is governed by source periods and
// causal lookback, not by an optimistic plugin-delay knob. This regression
// deliberately REJECTS the 16 ms candidate as generally safe for 65 Hz
// low-register singers; the mixer still prepares the 32 ms production DSP.
// Synthetic evidence != actual singer formant or clicklessness approval.
#include "../engine/builtin_fx/HarmonyPitchSynchronous.h"
#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>
namespace {
using armix::harmony::Engine;
using Shifter=armix::harmony::PitchSynchronousVoice<Engine::capacity,Engine::maxVoices+1>;
constexpr double twopi=6.2831853071795864769;
void need(bool ok,const char* why){if(!ok){std::cerr<<why<<"\n";std::exit(2);}}
int grainFrames(double rate,int latencyMs){
    return static_cast<int>(std::lround(rate*latencyMs/2000.));
}
int countPitchSynchronousSamples(double rate,double hz,int latencyMs,int chunk){
    auto p=std::make_unique<Shifter>();
    auto pcm=std::make_unique<std::array<float,Engine::capacity>>();
    pcm->fill(0.f);
    const int frames=static_cast<int>(rate*.38);
    int ready=0;
    for(int offset=0;offset<frames;offset+=chunk){
        const int last=std::min(frames,offset+chunk);
        for(int i=offset;i<last;++i){
            const double t=static_cast<double>(i)/rate;
            const float vocal=static_cast<float>(
                .17*std::sin(twopi*hz*t)+
                .05*std::sin(twopi*2.*hz*t+.2)+
                .026*std::sin(twopi*3.*hz*t+.34));
            const auto cursor=static_cast<std::size_t>(i)&(Engine::capacity-1);
            (*pcm)[cursor]=vocal;
            const auto y=p->process(0,static_cast<std::uint64_t>(i),cursor,
                *pcm,static_cast<float>(rate/hz),1.5f,
                static_cast<float>(grainFrames(rate,latencyMs)*2));
            need(std::isfinite(y.sample)&&std::abs(y.sample)<2.f,
                 "pitch synchronous PCM is not finite and bounded");
            if(y.ready)++ready;
        }
    }
    return ready;
}
}
int main(){
    for(double rate:{44100.,48000.,96000.}){
        for(double hz:{65.,82.406889,110.,220.}){
            for(int latency:{32,24,16}){
                const double declaredMs=2000.*grainFrames(rate,latency)/rate;
                const double minimumCausalMs=1200./hz;
                const bool theoreticallySupported=declaredMs>=minimumCausalMs;
                const int count64=countPitchSynchronousSamples(rate,hz,latency,64);
                const int count127=countPitchSynchronousSamples(rate,hz,latency,127);
                need((count64>0)==theoreticallySupported,
                     "candidate PSOLA low-note causal support disagrees with real PCM");
                need(count64==count127,
                     "low-note candidate PSOLA readiness depends on callback partition");
                std::cout<<std::fixed<<std::setprecision(2)
                    <<"HARMONICE_PITCH_SYNC_COVERAGE rate="<<rate
                    <<" source_hz="<<hz
                    <<" candidate_ms="<<latency
                    <<" min_lookback_ms="<<minimumCausalMs
                    <<" ready_samples="<<count64
                    <<" supported="<<(count64?"true":"false")
                    <<" human_singer_qualified=false\n";
            }
        }
    }
    // At 65 Hz the shifter needs 18.46ms of causal history. A 16ms
    // candidate must not be classified as pitch-synchronous/formant-safe
    // merely because its dry impulse/PDC and finite PCM tests are green.
    need(countPitchSynchronousSamples(48000.,65.,16,64)==0,
         "unsafe 16ms low-register candidate was promoted");
    need(countPitchSynchronousSamples(48000.,65.,24,64)>0,
         "24ms candidate lost low-register PSOLA despite causal lookback");
    // Only the production path may remain automatically selected by native
    // FX. New study modes must never silently reduce the live declaration.
    auto production=std::make_unique<Engine>();
    need(production->prepare(48000.)&&production->latencySamples()==1536,
         "shipping Harmonice PDC is not 1536 frames at 48k");
    std::cout<<"Harmonice low-register causal quality gate PASS;"
                " 16ms remains research-only, no acoustic claim\n";
}
