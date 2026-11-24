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
static std::vector<float> render(double rate,double f0,double ratio,int block,
                                 double amplitude=1.,double polarity=1.){
    auto p=Shifter{};
    std::array<float,16384> ring{};
    const int n=static_cast<int>(rate*.65);
    std::vector<float> out(static_cast<std::size_t>(n));
    for(int pos=0;pos<n;pos+=block)for(int j=0;j<std::min(block,n-pos);++j){
        const int t=pos+j;
        const double phase=6.283185307179586*f0*t/rate;
        ring[static_cast<std::size_t>(t)&16383]=static_cast<float>(
            amplitude*polarity*(.19*std::sin(phase)+
            .052*std::sin(2*phase+.31)+.029*std::sin(3*phase+.17)));
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
// A periodic damped-resonator pulse train with independent fundamental
// and spectral resonance. Measure harmonic-envelope centroid rather than
// claiming that a sine-wave pitch test measures formants.
static std::vector<float> resonanceFixture(double rate,float scale,int block){
    Shifter p{};
    std::array<float,16384> ring{};
    const int n=static_cast<int>(rate*.65);
    std::vector<float> out(n);
    const double f0=110.,period=rate/f0;
    for(int pos=0;pos<n;pos+=block)for(int j=0;j<std::min(block,n-pos);++j){
        const int at=pos+j;
        const double age=std::fmod(at,period)/rate;
        const double phase=6.283185307179586*f0*at/rate;
        ring[static_cast<std::size_t>(at)&16383]=static_cast<float>(
            .12*std::cos(phase)+.27*std::exp(-age*750.)*
            std::cos(6.283185307179586*2200.*age));
        const auto y=p.process(0,at,static_cast<std::size_t>(at)&16383,ring,
            static_cast<float>(period),1.5f,static_cast<float>(rate*.029),scale);
        out[at]=y.sample;
        need(std::isfinite(y.sample)&&std::abs(y.sample)<1.f,"H8 nonfinite resonance");
    }
    return out;
}
static double centroid(const std::vector<float>& x,double rate){
    double weighted=0.,sum=0.;
    for(int h=8;h<=19;++h){
        const double f=h*165.;
        double c=0.,s=0.;
        for(int i=static_cast<int>(rate*.3);i<static_cast<int>(rate*.6);++i){
            const double phase=6.283185307179586*f*i/rate;
            c+=x[i]*std::cos(phase);s+=x[i]*std::sin(phase);
        }
        const double e=c*c+s*s;weighted+=f*e;sum+=e;
    }
    need(sum>1.,"H8 resonance spectrum absent");return weighted/sum;
}
static void testResonanceScaling(){
    for(double rate:{32000.,48000.,96000.,192000.}){
        const auto old=resonanceFixture(rate,1.f,127);
        const auto low=resonanceFixture(rate,.92f,127);
        const auto high=resonanceFixture(rate,1.08f,127);
        need(low==resonanceFixture(rate,.92f,64),"H8 grain block parity");
        const auto a=centroid(low,rate),b=centroid(old,rate),c=centroid(high,rate);
        std::cout<<"H8_RESONANCE rate="<<rate<<" low="<<a<<" neutral="<<b
                 <<" high="<<c<<std::endl;
        need(a<b-40.&&c>b+40.,"H8 resonance failed to move independently");
        for(const auto* x:{&low,&old,&high})
            need(std::abs(measuredHz(*x,rate,165.)-165.)<3.96,
                 "H8 resonance changed fundamental");
    }
}
static void testResumeAfterGap(){
    for(int rate:{48000,96000}){
        Shifter retained{},fresh{};
        std::array<float,16384> ring{};
        const int start=rate/10,resume=start+rate/40,end=resume+rate/10;
        for(int i=0;i<end;++i){
            ring[static_cast<std::size_t>(i)&16383]=static_cast<float>(
                .18*std::sin(6.283185307179586*440.*i/rate));
            if(i>=start&&i<resume)continue;
            const auto x=retained.process(0,i,static_cast<std::size_t>(i)&16383,
                ring,static_cast<float>(rate/440.),1.6f,rate*.036f);
            if(i>=resume){
                const auto y=fresh.process(0,i,static_cast<std::size_t>(i)&16383,
                    ring,static_cast<float>(rate/440.),1.6f,rate*.036f);
                need(x.ready==y.ready&&x.sample==y.sample,"H13 stale epoch after skipped calls");
            }
        }
    }
}
int main(){
    testResumeAfterGap();
    testResonanceScaling();
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
    // Previously a hard-coded +0.003V positive epoch threshold made
    // genuine soft high-register vowels leave the PSOLA output at zero.
    // Explicitly exercise low-amplitude *negative*-polarity waveforms at
    // ASIO-shaped block boundaries. These tests measure basic continuity,
    // not perceived naturalness in an actual sung phrase.
    for(const double polarity:{-1.,1.}){
        const auto soft64=render(48000.,1046.502261,1.4,64,.004,polarity);
        const auto soft257=render(48000.,1046.502261,1.4,257,.004,polarity);
        need(soft64==soft257,"soft soprano changed with callback partitioning");
        double energy=0.;int audible=0;
        for(int i=14400;i<26400;++i){
            const double x=soft64[static_cast<std::size_t>(i)];
            energy+=x*x;
            if(std::abs(x)>1.e-5)++audible;
        }
        need(energy>1.e-5&&audible>6000,
             "soft high vowel lost epochs or became a rapid gated voice");
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
