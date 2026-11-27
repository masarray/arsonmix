// SPDX-License-Identifier: AGPL-3.0-only
// Stationary electronic noise fixture, not a physical microphone recording.
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
namespace {
void require(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
struct Signal { std::vector<float> l,r; };
Signal hiss(int rate,int channels,unsigned seed){
    std::mt19937 rng(seed);std::uniform_real_distribution<float> white(-1,1);
    Signal x{std::vector<float>(3*rate),std::vector<float>(3*rate)};
    float prev[2]{};
    for(int i=0;i<3*rate;++i){for(int k=0;k<channels;++k){float v=white(rng);(k?x.r:x.l)[i]=.0015f*(v-.4f*prev[k]);prev[k]=v;}}
    return x;
}
void process(Core& c,Signal& x,int channels){
    for(int pos=0;pos<int(x.l.size());){int n=std::min(127,int(x.l.size())-pos);c.process(x.l.data()+pos,channels==2?x.r.data()+pos:nullptr,n);pos+=n;}
}
double energy(const Signal& x,int begin,int end,int channels){
    double e=0;for(int i=begin;i<end;++i){e+=double(x.l[i])*x.l[i];if(channels==2)e+=double(x.r[i])*x.r[i];}return e;
}
double db(double a,double b){return 10*std::log10(std::max(a,1e-30)/std::max(b,1e-30));}
void test(int rate,int channels){
    Core c;Core::Settings s;s.mode=Core::Mode::Voice;s.spectralManual=true;s.silenceDb=0;s.artifactControl=0;s.vocalProtection=0;s.cleanDb=0;
    require(c.prepare(rate,channels)&&c.setSettings(s)&&c.beginLearn(),"prepare Learn");
    auto learn=hiss(rate,channels,11);process(c,learn,channels);require(c.hasProfile(),"hiss profile accepted");c.resetStreamKeepProfile();
    const auto input=hiss(rate,channels,22);const auto d=c.latencySamples();const int begin=rate,end=3*rate;
    double prev=0,deep=0,off=0;
    for(float reduction:{0.f,10.f,20.f,40.f}){
        auto fx=c;auto out=input;auto q=s;q.cleanDb=reduction;q.noiseOffsetDb=12;
        require(fx.setSettings(q),"set reduction");process(fx,out,channels);
        const double cut=db(energy(input,begin-d,end-d,channels),energy(out,begin,end,channels));
        if(reduction==0)require(std::abs(cut)<.01,"zero reduction unity");else require(cut>prev+.1,"reduction knob stronger");
        prev=cut;deep=cut;
        std::cout<<"HISS_REDUCTION,"<<rate<<','<<channels<<','<<reduction<<','<<cut<<'\n';
    }
    require(deep>15,"strong stationary hiss suppression");
    for(float offset:{-10.f,0.f,10.f,20.f}){
        auto fx=c;auto out=input;auto q=s;q.cleanDb=40;q.noiseOffsetDb=offset;
        require(fx.setSettings(q),"set noise offset");process(fx,out,channels);
        const double cut=db(energy(input,begin-d,end-d,channels),energy(out,begin,end,channels));
        if(offset!=-10)require(cut>off+.1,"noise threshold knob stronger");
        off=cut;
        std::cout<<"HISS_OFFSET,"<<rate<<','<<channels<<','<<offset<<','<<cut<<'\n';
    }
    // No Learn / no Smart Voice / spectral disabled: user-owned gate.
    Core gate;auto q=s;q.spectralEnabled=false;q.manual.enabled=true;q.manual.gate=true;q.manual.thresholdDb=-42;q.manual.rangeDb=96;q.manual.holdMs=80;q.manual.releaseMs=80;
    require(gate.prepare(rate,channels)&&gate.setSettings(q)&&!gate.hasProfile(),"standalone gate");
    auto gated=input;for(int i=rate;i<2*rate;++i){float v=.08f*float(std::sin(2*std::numbers::pi*430*i/rate));gated.l[i]+=v;if(channels==2)gated.r[i]+=v;}
    process(gate,gated,channels);require(energy(gated,rate/2,rate,channels)==0&&energy(gated,5*rate/2,3*rate,channels)==0,"hard digital silence");
    const double active=energy(gated,rate+rate/4,rate+3*rate/4,channels);require(active>rate*.0005,"gate opens for foreground");
    std::cout<<"HISS_GATE,"<<rate<<','<<channels<<",digital_zero=1,voice_open=1,learn_required=0\n";
}
}
int main(){for(int rate:{44100,48000,88200,96000})for(int channels:{1,2})test(rate,channels);}
