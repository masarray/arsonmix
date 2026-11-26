// SPDX-License-Identifier: AGPL-3.0-only
#include "OfflineAudio.h"
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
namespace {
// Independent colored noise fixture: two causal band-pass biquads, no
// dependency on the production FFT, classifier, or guard implementation.
struct Bandpass {
    double b=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
    void prepare(double rate){const double w=2.*std::numbers::pi*7200./rate;
        const double a=std::sin(w)/(2.*1.4);b=a/(1.+a);a1=-2.*std::cos(w)/(1.+a);a2=(1.-a)/(1.+a);}
    double process(double x){const double y=b*(x-x2)-a1*y1-a2*y2;x2=x1;x1=x;y2=y1;y1=y;return y;}
};
void check(unsigned rate,unsigned channels){
    std::mt19937 gen(60917);std::uniform_real_distribution<float> random(-.025f,.025f);
    std::array<std::array<Bandpass,2>,2> filters{};
    for(auto& ch:filters)for(auto& b:ch)b.prepare(rate);
    auto noise=[&](unsigned c){return float(filters[c][1].process(filters[c][0].process(random(gen))));};
    offline::Audio learn{rate,channels,std::vector<float>(3*rate*channels)};
    offline::Audio input{rate,channels,std::vector<float>(4*rate*channels)},clean=input;
    for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<channels;++c)learn.samples[i*channels+c]=noise(c);
    for(unsigned i=0;i<4*rate;++i)for(unsigned c=0;c<channels;++c){
        const auto j=i*channels+c;const double t=double(i)/rate;
        const float gust=1.2f+.45f*float(std::sin(2.*std::numbers::pi*.9*t));
        if(i>=2*rate&&i<3*rate&&(channels==1||c==1)){
            const double phase=2.*std::numbers::pi*t;
            clean.samples[j]=float(.005*std::sin(phase*110)+.002*std::sin(phase*220)+.001*std::sin(phase*330));
            if(i<2*rate+rate/40)clean.samples[j]+=float(.03*std::sin(phase*8400));
        }
        input.samples[j]=gust*noise(c)+clean.samples[j];
    }
    Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=24;s.silenceDb=48;
    const auto wet=offline::render(input,&learn,s,127),partition=offline::render(input,&learn,s,64);
    offline::require(wet.processed.samples==partition.processed.samples,"broadband partition PCM");
    offline::require(wet.latency==(rate>48000?512:256),"broadband latency unchanged");
    s.silenceDb=0;const auto spectral=offline::render(input,&learn,s,127);
    s.cleanDb=0;const auto unity=offline::render(input,&learn,s,127);
    for(std::size_t i=0;i<input.samples.size();++i)
        offline::require(std::abs(input.samples[i]-unity.processed.samples[i])<2e-6f,"both controls zero unity");
    s.silenceDb=48;const auto residualOnly=offline::render(input,&learn,s,127);
    const auto residualPartition=offline::render(input,&learn,s,64);
    offline::require(residualOnly.processed.samples==residualPartition.processed.samples,"residual-only partition PCM");
    double raw=0,out=0,base=0,ref=0,projection=0,onsetRef=0,onsetProj=0,residualPower=0,residualProjection=0,residualOnset=0;
    for(unsigned i=0;i<4*rate;++i)for(unsigned c=0;c<channels;++c){
        const auto j=i*channels+c;const double x=input.samples[j],y=wet.processed.samples[j],v=clean.samples[j];
        if(i>=rate/2&&i<2*rate){raw+=x*x;out+=y*y;base+=double(spectral.processed.samples[j])*spectral.processed.samples[j];residualPower+=double(residualOnly.processed.samples[j])*residualOnly.processed.samples[j];}
        if(i>=2*rate&&i<3*rate){ref+=v*v;projection+=v*y;residualProjection+=v*residualOnly.processed.samples[j];if(i<2*rate+rate/40){onsetRef+=v*v;onsetProj+=v*y;residualOnset+=v*residualOnly.processed.samples[j];}}
    }
    const double cut=10*std::log10(raw/out),extra=10*std::log10(base/out),retain=projection/ref,onset=onsetProj/onsetRef;
    std::cout<<rate<<" Hz "<<channels<<"ch broad idle "<<cut<<" dB residual "<<extra<<" dB foreground "<<retain<<" onset "<<onset<<std::endl;
    const double residualCut=10*std::log10(raw/residualPower);
    std::cout<<"residual-only "<<residualCut<<" dB foreground "<<residualProjection/ref<<" onset "<<residualOnset/onsetRef<<std::endl;
    offline::require(residualCut>=3.&&residualProjection/ref>.90&&residualProjection/ref<1.10&&residualOnset/onsetRef>.75&&residualOnset/onsetRef<1.25,"Silence independent of Clean with foreground retained");
    offline::require(cut>=12.&&extra>=3.,"broadband idle and residual target");
    offline::require(retain>.90&&retain<1.10&&onset>.75&&onset<1.25,"broadband foreground retention");
}
}
int main(){try{for(unsigned rate:{44100u,48000u,88200u,96000u})for(unsigned channels:{1u,2u})check(rate,channels);}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
