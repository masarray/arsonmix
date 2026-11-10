// SPDX-License-Identifier: AGPL-3.0-only
// Exploratory P1 grain/latency comparison. These are PCM SAFETY checks,
// not auditory quality approval. The shipping DSP still prepares at 32 ms.
#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
namespace {
using armix::harmony::Engine;
using armix::harmony::Settings;
constexpr double tau=6.2831853071795864769;
void check(bool value,const char* why){
    if(!value){std::cerr<<"Harmonice latency study: "<<why<<"\n";std::exit(2);}
}
std::unique_ptr<Engine> make(double rate,int milliseconds,const Settings& s){
    auto e=std::make_unique<Engine>(); // fixed DSP rings are not stack objects
    check(milliseconds==32?e->prepare(rate):
          e->prepareForOfflineLatencyStudy(rate,milliseconds),"prepare rejected");
    check(e->setSettings(s),"settings rejected");
    const int grain=static_cast<int>(std::round(
        rate*static_cast<double>(milliseconds)/2000.));
    check(e->latencySamples()==2*grain,"latency does not match physical grain");
    return e;
}
void dryImpulse(double rate,int ms,int block){
    Settings dry;dry.wet=0.f;
    auto e=make(rate,ms,dry);
    const int latency=e->latencySamples();
    const int total=latency+std::max(32,block)+1;
    std::vector<float> left(static_cast<std::size_t>(total),0.f);
    std::vector<float> right(static_cast<std::size_t>(total),0.f);
    left[0]=.5f;right[0]=-.25f;
    for(int pos=0;pos<total;pos+=block)
        e->process(left.data()+pos,right.data()+pos,std::min(block,total-pos));
    for(int i=0;i<total;++i){
        const float l=i==latency?.5f:0.f;
        const float r=i==latency?-.25f:0.f;
        check(std::abs(left[static_cast<std::size_t>(i)]-l)<1.e-6f&&
              std::abs(right[static_cast<std::size_t>(i)]-r)<1.e-6f,
              "Wet0 dry PDC not sample exact");
    }
}
struct Audio {std::vector<float> left,right;};
Audio vocal(double rate,bool opposed){
    const int n=static_cast<int>(std::round(rate*.55));
    Audio a{std::vector<float>(static_cast<std::size_t>(n)),
            std::vector<float>(static_cast<std::size_t>(n))};
    for(int i=0;i<n;++i){
        const double t=static_cast<double>(i)/rate;
        const float v=static_cast<float>(
            .16*std::sin(tau*220.*t)+.045*std::sin(tau*440.*t+.2)+
            .022*std::sin(tau*660.*t+.1));
        a.left[static_cast<std::size_t>(i)]=v;
        a.right[static_cast<std::size_t>(i)]=opposed?-v:v;
    }
    return a;
}
Audio render(double rate,int ms,int block,const Audio& source){
    Settings s;s.voices=5;s.wet=.75f;s.space=.60f;s.correction=false;
    auto e=make(rate,ms,s);
    auto out=source;
    const int total=static_cast<int>(out.left.size());
    for(int pos=0;pos<total;pos+=block)
        e->process(out.left.data()+pos,out.right.data()+pos,
                   std::min(block,total-pos));
    check(e->voiced()&&e->observedMidiNote()==57,
          "fixed singer A3 tonal evidence lost");
    double backing=0.;
    const int delay=e->latencySamples();
    for(int i=total/2;i<total;++i){
        const auto at=static_cast<std::size_t>(i);
        const auto earlier=static_cast<std::size_t>(i-delay);
        const float l=out.left[at],r=out.right[at];
        check(std::isfinite(l)&&std::isfinite(r)&&
              std::abs(l)<=4.f&&std::abs(r)<=4.f,
              "nonfinite/unbounded candidate PCM");
        const double dl=l-source.left[earlier];
        const double dr=r-source.right[earlier];
        backing+=dl*dl+dr*dr;
    }
    check(backing>.001,"wet backing disappeared in candidate");
    return out;
}
}
int main(){
    Settings s;
    check(!Engine{}.prepareForOfflineLatencyStudy(48000.,8),
          "unsupported 8 ms candidate admitted");
    check(!Engine{}.prepareForOfflineLatencyStudy(48000.,32),
          "offline API incorrectly duplicates production preparation");
    for(double rate:{44100.,48000.,96000.}){
        for(int ms:{32,24,16}){
            for(int block:{64,127,256})dryImpulse(rate,ms,block);
            for(bool opposed:{false,true}){
                const auto input=vocal(rate,opposed);
                const auto reference=render(rate,ms,64,input);
                for(int block:{127,256}){
                    const auto alternate=render(rate,ms,block,input);
                    for(std::size_t i=0;i<reference.left.size();++i)
                        check(std::abs(reference.left[i]-alternate.left[i])<1.e-6f&&
                              std::abs(reference.right[i]-alternate.right[i])<1.e-6f,
                              "candidate output depends on callback partition");
                }
            }
            std::cout<<"candidate_pcm_safe rate="<<rate<<" latency_ms="<<ms
                     <<" frames="<<2*static_cast<int>(std::round(rate*ms/2000.))
                     <<" human_listening_qualified=false\n";
        }
    }
    std::cout<<"Harmonice 32/24/16 ms dry PDC, finite wet PCM and "
                "ASIO-like partition parity completed; no quality approval\n";
}
