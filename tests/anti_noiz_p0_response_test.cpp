// SPDX-License-Identifier: AGPL-3.0-only
#include "OfflineAudio.h"
#include <iomanip>
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;

// Synthetic paired regression: independently seeded noise and foreground,
// four sample rates, two layouts, two fundamental frequencies, four levels.
// A projection is NOT a listening or intelligibility metric.
static void active(unsigned rate,unsigned ch,float amp,float f){
    std::mt19937 gen(37429);std::uniform_real_distribution<float> rng(-1.f,1.f);
    offline::Audio learn{rate,ch,std::vector<float>(3*rate*ch)};
    offline::Audio input{rate,ch,std::vector<float>(4*rate*ch)},clean=input;
    std::array<float,2> state{};
    const float a=float(1.-std::exp(-2.*std::numbers::pi*65./rate));
    auto noise=[&](unsigned c){state[c]+=a*(rng(gen)-state[c]);return .06f*state[c]+.00025f*rng(gen);};
    for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<ch;++c)learn.samples[i*ch+c]=noise(c);
    for(unsigned i=0;i<4*rate;++i)for(unsigned c=0;c<ch;++c){
        const auto j=i*ch+c;const double t=double(i)/rate;
        const float gust=1.f+1.2f*float(.5+.5*std::sin(2.*std::numbers::pi*3.*t));
        if(i>=rate&&i<3*rate&&(ch==1||c==1)){
            const double phase=2.*std::numbers::pi*t*f;const unsigned dt=i-rate;
            const float env=std::min(1.f,float(dt)/(rate*.008f))*std::min(1.f,float(3*rate-i)/(rate*.018f));
            clean.samples[j]=env*amp*float(std::sin(phase)+.30*std::sin(phase*2)+.20*std::sin(phase*3));
        }
        input.samples[j]=gust*noise(c)+clean.samples[j];
    }
    Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=24;s.silenceDb=48;
    const auto wet=offline::render(input,&learn,s,127),cleanWet=offline::render(clean,&learn,s,127);
    offline::require(wet.latency==(rate>48000?512:256),"field active latency");
    if(amp==.04f||amp==.0015f){
        const auto other=offline::render(input,&learn,s,64);
        offline::require(wet.processed.samples==other.processed.samples,"field active partition PCM");
    }
    double e0=0,e1=0,p=0,pr=0,op=0,oraw=0,cd=0;
    for(unsigned i=rate;i<3*rate;++i){
        const unsigned j=i*ch+ch-1;const double r=clean.samples[j],x=input.samples[j],y=wet.processed.samples[j],cy=cleanWet.processed.samples[j];
        e0+=(x-r)*(x-r);e1+=(y-r)*(y-r);p+=r*r;pr+=r*y;cd+=(cy-r)*(cy-r);
        if(i<rate+rate/50){op+=r*y;oraw+=r*r;}
    }
    const double improvement=10*std::log10(e0/e1),projection=pr/p,onset=op/oraw,cleanError=10*std::log10(cd/p);
    offline::require(std::isfinite(improvement)&&std::isfinite(projection)&&std::isfinite(onset)&&std::isfinite(cleanError),"finite paired metrics");
    std::cout<<"ACTIVE,"<<rate<<','<<ch<<','<<int(std::lround(amp*1000000))<<','<<int(f)<<','<<improvement<<','<<projection<<','<<onset<<','<<cleanError<<'\n';
}

// Measure actual core residual gain timing, not a GUI interpolation. This
// stationary-noise fixture does not establish wind/pumping acceptance.
static void response(unsigned rate,unsigned ch){
    Core core;Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=14;s.silenceDb=32;
    offline::require(core.prepare(rate,int(ch))&&core.setSettings(s)&&core.beginLearn(),"response prepare");
    const int hop=rate>48000?256:128;std::array<float,256> l{},r{};
    std::mt19937 gen(6723);std::uniform_real_distribution<float> rng(-.003f,.003f);
    auto feed=[&](unsigned count,bool voice){
        for(int j=0;j<hop;++j){
            const float t=float(count+j)/rate;const float v=voice?.08f*std::sin(2.f*std::numbers::pi_v<float>*375.f*t):0.f;
            l[j]=rng(gen)+v;r[j]=rng(gen)+v*.7f;
        }
        core.process(l.data(),ch==2?r.data():nullptr,hop);
    };
    unsigned pos=0;while(core.learning()&&pos<3*rate){feed(pos,false);pos+=hop;}
    offline::require(core.hasProfile(),"response learn");core.resetStreamKeepProfile();
    for(pos=0;pos<rate;pos+=hop){feed(pos,true);}
    const unsigned start=pos;double first12=-1,first20=-1;float previous=1.f;int upward=0;double maxCloseStep=0;
    for(;pos<start+2*rate;pos+=hop){
        feed(pos,false);const float gain=core.residualGain();const double db=-20*std::log10(std::max(1e-9f,gain));
        if(first12<0&&db>=12)first12=1000.*(pos+hop-start)/rate;
        if(first20<0&&db>=20)first20=1000.*(pos+hop-start)/rate;
        if(gain>previous+1e-5f)++upward;
        maxCloseStep=std::max(maxCloseStep,double(previous-gain));previous=gain;
    }
    feed(pos,true);const double open=core.residualGain();
    offline::require(first12>0&&first20>=first12&&std::isfinite(maxCloseStep)&&open>.99,"stationary response and reopen");
    std::cout<<"RESPONSE,"<<rate<<','<<ch<<','<<first12<<','<<first20<<','<<upward<<','<<maxCloseStep<<','<<open<<'\n';
}
int main(){try{
    std::cout<<std::setprecision(10);
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(unsigned ch:{1u,2u}){
        for(float amp:{.0015f,.004f,.012f,.04f})for(float f:{110.f,375.f})active(rate,ch,amp,f);
        response(rate,ch);
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
