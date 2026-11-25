// SPDX-License-Identifier: AGPL-3.0-only
#include "OfflineAudio.h"
#include <string_view>
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
int main(int argc,char** argv){try{
    const bool baseline=argc==2&&std::string_view(argv[1])=="--baseline";
    offline::require(argc==1||baseline,"invalid arguments");
    unsigned windFailures=0;
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(unsigned channels:{1u,2u}){
        std::mt19937 gen(4902);std::uniform_real_distribution<float> rng(-1.f,1.f);
        offline::Audio learn{rate,channels,std::vector<float>(3*rate*channels)};
        offline::Audio input{rate,channels,std::vector<float>(4*rate*channels)},clean=input;
        std::array<float,2> state{};
        const float a=float(1.-std::exp(-2.*std::numbers::pi*65./rate));
        auto noise=[&](unsigned c){state[c]+=a*(rng(gen)-state[c]);return .06f*state[c]+.00025f*rng(gen);};
        for(auto& x:learn.samples){const auto c=unsigned(&x-learn.samples.data())%channels;x=noise(c);}
        for(unsigned i=0;i<4*rate;++i)for(unsigned c=0;c<channels;++c){
            const auto j=i*channels+c;const double t=double(i)/rate;
            const float gust=1.0f+1.2f*float(.5+.5*std::sin(2.*std::numbers::pi*3.*t));
            if(i>=2*rate&&i<3*rate&&(channels==1||c==1)){
                const double phase=2.*std::numbers::pi*t;
                clean.samples[j]=float(.05*std::sin(phase*375)+.025*std::sin(phase*750)+.012*std::sin(phase*1125));
            }
            input.samples[j]=gust*noise(c)+clean.samples[j];
        }
        Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=24;s.silenceDb=48;
        const auto wet=offline::render(input,&learn,s,127);
        const auto partition=offline::render(input,&learn,s,64);
        offline::require(wet.processed.samples==partition.processed.samples,"wind partition PCM");
        offline::require(wet.latency==(rate>48000?512:256),"no extra latency");
        s.silenceDb=0;const auto spectral=offline::render(input,&learn,s,127);
        double raw=0,out=0,base=0,ref=0,projection=0,onsetRef=0,onsetProj=0;
        for(unsigned i=0;i<4*rate;++i)for(unsigned c=0;c<channels;++c){
            const auto j=i*channels+c;const double x=input.samples[j],y=wet.processed.samples[j],v=clean.samples[j];
            if(i>=rate/2&&i<2*rate){raw+=x*x;out+=y*y;base+=double(spectral.processed.samples[j])*spectral.processed.samples[j];}
            if(i>=2*rate&&i<3*rate){ref+=v*v;projection+=v*y;if(i<2*rate+rate/100){onsetRef+=v*v;onsetProj+=v*y;}}
        }
        const double cut=10*std::log10(raw/out),extra=10*std::log10(base/out),retain=projection/ref,onset=onsetProj/onsetRef;
        std::cout<<rate<<" Hz "<<channels<<"ch wind cut "<<cut<<" dB residual "<<extra<<" dB foreground "<<retain<<" onset "<<onset<<std::endl;
        if(cut<12.||extra<3.)++windFailures;
        if(!baseline)offline::require(cut>=12.&&extra>=3.,"wind pause audible attenuation and residual contribution");
        offline::require(retain>.90&&retain<1.10&&onset>.75&&onset<1.25,"foreground/onset retention");
    }
    if(baseline)offline::require(windFailures>0,"old baseline unexpectedly passed every field proxy gate");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
