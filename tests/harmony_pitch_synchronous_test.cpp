#include "../engine/builtin_fx/HarmonyPitchSynchronous.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using armix::harmony::PitchSynchronousVoice;
using Shifter=PitchSynchronousVoice<16384,6>;
static void need(bool yes,const char* reason){
    if(!yes){std::cerr<<reason<<"\n";std::exit(2);}
}
static std::vector<float> render(double rate,double f0,double ratio,int block){
    auto p=Shifter{};
    std::array<float,16384> ring{};
    const int n=static_cast<int>(rate*.65);
    std::vector<float> out(static_cast<std::size_t>(n));
    for(int pos=0;pos<n;pos+=block)for(int j=0;j<std::min(block,n-pos);++j){
        const int t=pos+j;
        const double phase=6.283185307179586*f0*t/rate;
        ring[static_cast<std::size_t>(t)&16383]=static_cast<float>(
            .19*std::sin(phase)+.052*std::sin(2*phase+.31)+
            .029*std::sin(3*phase+.17));
        const auto sample=p.process(0,t,static_cast<std::size_t>(t)&16383,ring,
            static_cast<float>(rate/f0),static_cast<float>(ratio),
            static_cast<float>(rate*.032));
        out[static_cast<std::size_t>(t)]=sample.sample;
        need(std::isfinite(sample.sample)&&std::abs(sample.sample)<2.f,
             "synchronous output nonfinite/unbounded");
    }
    return out;
}
static double measuredHz(const std::vector<float>& signal,double rate,double wanted){
    const int begin=static_cast<int>(rate*.27);
    const int length=static_cast<int>(rate*.23);
    const int low=static_cast<int>(rate/(wanted*1.05));
    const int high=static_cast<int>(rate/(wanted*.95));
    double best=-100.;int lag=0;
    for(int d=low;d<=high;++d){
        double numerator=0,energy=0;
        for(int t=begin;t<begin+length;t+=2){
            numerator+=signal[static_cast<std::size_t>(t)]*
                       signal[static_cast<std::size_t>(t-d)];
            energy+=signal[static_cast<std::size_t>(t)]*
                    signal[static_cast<std::size_t>(t)];
        }
        if(numerator/(energy+1.e-20)>best){
            best=numerator/(energy+1.e-20);lag=d;
        }
    }
    need(best>.6&&lag>0,"synchronous source lost periodicity");
    return rate/lag;
}
int main(){
    // Actual source-period pitch shifting, not just a sine-wave finite test.
    // Three original harmonics approximate one voiced source; FFT/formant and
    // actual adult singer evidence are still necessary before quality claims.
    for(const auto& v:std::array<std::array<double,3>,5>{{
        {{48000.,220.,1.5}},{{48000.,220.,.667}},
        {{48000.,82.40689,1.2}},{{96000.,440.,.75}},
        {{192000.,880.,1.4}}
    }}){
        const auto a=render(v[0],v[1],v[2],64);
        const auto b=render(v[0],v[1],v[2],257);
        need(a.size()==b.size(),"different source length");
        for(std::size_t i=0;i<a.size();++i)
            need(a[i]==b[i],"callback partition changed PSOLA PCM");
        const auto result=measuredHz(a,v[0],v[1]*v[2]);
        need(std::abs(result-v[1]*v[2])/(v[1]*v[2])<.024,
             "synchronous target pitch mismatch");
    }
    // Invalid or truncated source must return cleanly without allocations or
    // producing undefined samples, even after a partially scheduled phrase.
    Shifter bad;
    std::array<float,16384> silence{};
    need(!bad.process(0,0,0,silence,0.f,1.f,1536.f).ready,
         "invalid F0 accepted");
    need(!bad.process(0,100,100,silence,3200.f,1.f,1536.f).ready,
         "insufficient host latency accepted");
    std::cout<<"bounded source windows and block parity passed\n";
}
