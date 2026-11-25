// SPDX-License-Identifier: AGPL-3.0-only
#include "../tools/anti-noiz/OfflineAudio.h"
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
int main(){try{
    using G=VoiceGain<Core::maxBins>;
    offline::require(G::restore(.3f,3.f,3.f,4.f,12.f)>.44f,"corroborated overlap restoration");
    offline::require(G::restore(.3f,3.f,3.f,4.f,1.f)==.3f,"isolated noise peak");
    offline::require(G::restore(.3f,3.f,.5f,4.f,12.f)==.3f,"quiet band unchanged");
    offline::require(G::restore(.3f,3.f,3.f,1.f,12.f)==.3f,"no persistent local evidence");
    offline::require(G::restore(.3f,1.f,3.f,4.f,12.f)==.3f,"no local excess");
    for(float g:{0.f,.1f,.4f,.9f,1.f}){
        const float v=G::restore(g,100.f,100.f,100.f,100.f);
        offline::require(v>=g&&v<=1.f&&v-g<=.150001f,"bounded restoration");
    }
    // Synthetic common-mode noise shape; a strong local foreground or
    // uncertain stereo leg must immediately release local residual cut.
    ResidualShape<32> rs;rs.prepare(48000,128);
    ResidualShape<32>::Powers anchor{},power{};
    for(int c=0;c<2;++c)for(int k=0;k<32;++k){
        anchor[c][k]=k<8?4.f:.1f;power[c][k]=2.f*anchor[c][k];
    }
    const std::array<float,2> variable{1.f,1.f};
    for(int i=0;i<600;++i)rs.update(power,anchor,2,32,variable,.025f,true);
    offline::require(rs.gain(3)<.55f&&rs.gain(20)==1.f,"shape selective residual authority");
    power[1][3]=100.f;
    rs.update(power,anchor,2,32,variable,.025f,true);
    offline::require(rs.gain(3)==1.f,"local stereo foreground release");
    rs.update(power,anchor,2,32,variable,.025f,false);
    offline::require(rs.gain(3)==1.f,"ineligible unity");
    constexpr unsigned rate=48000;
    std::mt19937 gen(2026);std::uniform_real_distribution<float> noiseDist(-.003f,.003f),unit(-1.f,1.f);
    offline::Audio learn{rate,2,std::vector<float>(rate*6)},input=learn,clean=learn;
    for(auto& x:learn.samples)x=noiseDist(gen);
    float prev=0;
    for(unsigned i=0;i<rate*3;++i){
        const float t=float(i)/rate,phase=2.f*std::numbers::pi_v<float>*t;
        const unsigned syllable=i%(rate/4);
        const float env=std::min(1.f,float(syllable)/240.f)*std::min(1.f,float(rate/4-syllable)/480.f);
        const float voiced=env*(.045f*std::sin(phase*375.f)+.018f*std::sin(phase*750.f)+.008f*std::cos(phase*1500.f));
        const float random=unit(gen),high=random-prev;prev=random;
        const float fricative=(syllable>rate/5? .015f*high:0.f);
        clean.samples[2*i]=voiced+fricative;clean.samples[2*i+1]=.7f*voiced-.5f*fricative;
        for(int c=0;c<2;++c)input.samples[2*i+c]=clean.samples[2*i+c]+noiseDist(gen);
    }
    Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=12;s.silenceDb=24;
    const auto a=offline::render(input,&learn,s,64),b=offline::render(input,&learn,s,127);
    offline::require(a.processed.samples==b.processed.samples,"voice partition parity");
    double e0=0,e1=0,ref=0,proj=0,onsetRef=0,onsetProj=0,fricRef=0,fricProj=0;
    for(unsigned i=rate/4;i<rate*3;++i){
        const unsigned syllable=i%(rate/4);
        for(int c=0;c<2;++c){const auto j=2*i+c;const double x=input.samples[j],y=a.processed.samples[j],r=clean.samples[j];
            e0+=(x-r)*(x-r);e1+=(y-r)*(y-r);ref+=r*r;proj+=r*y;
            if(syllable<480){onsetRef+=r*r;onsetProj+=r*y;}
            if(syllable>rate/5){fricRef+=r*r;fricProj+=r*y;}
        }
    }
    const double improvement=10*std::log10(e0/e1),gain=proj/ref,onset=onsetProj/onsetRef,fric=fricProj/fricRef;
    std::cout<<"Voice proxy error improvement "<<improvement<<" dB; projection "<<gain<<", onset "<<onset<<", mixed fricative interval "<<fric<<'\n';
    offline::require(improvement>=3&&gain>.97&&gain<1.03&&onset>.90&&onset<1.10&&fric>.90&&fric<1.10,"voice proxy preservation and error bounds");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
