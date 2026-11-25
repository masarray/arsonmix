// SPDX-License-Identifier: AGPL-3.0-only
#include "OfflineAudio.h"
#include <iostream>
#include <random>
#include <string_view>
using namespace armix::builtin_fx::anti_noiz;
int main(int argc,char** argv){try{
    const bool report=argc==2&&std::string_view(argv[1])=="--report-only";
    offline::require(argc==1||report,"unknown argument");
    for(unsigned rate:{48000u,96000u})for(unsigned channels:{1u,2u}){
        std::mt19937 gen(8041);std::uniform_real_distribution<float> rng(-.003f,.003f);
        offline::Audio learn{rate,channels,std::vector<float>(3*rate*channels)};
        offline::Audio input{rate,channels,std::vector<float>(10*rate*channels)},clean=input;
        // Flat stationary noise, independent Learn/test. At 0 s increase noise
        // power 2x (3 dB), at 8 s add strong harmonic foreground.
        for(auto& x:learn.samples)x=rng(gen);
        for(unsigned i=0;i<10*rate;++i)for(unsigned c=0;c<channels;++c){
            const auto j=i*channels+c;
            const double t=2.*std::numbers::pi*double(i)/rate;
            if(i>=8*rate)clean.samples[j]=float((.05*std::sin(t*375)+.02*std::sin(t*750))*(c?.7:1.));
            input.samples[j]=clean.samples[j]+std::sqrt(2.f)*rng(gen);
        }
        Core::Settings s;s.mode=Core::Mode::Voice;s.silenceDb=0;
        auto a=offline::render(input,&learn,s,127);
        auto measure=[&](unsigned begin,unsigned end){
            double e0=0,e1=0;
            for(unsigned i=begin*rate*channels;i<end*rate*channels;++i){
                const double x=input.samples[i]-clean.samples[i],y=a.processed.samples[i]-clean.samples[i];
                e0+=x*x;e1+=y*y;
            }
            return 10*std::log10(e0/e1);
        };
        double ref=0,proj=0;
        for(unsigned i=9*rate*channels;i<10*rate*channels;++i){
            const double x=clean.samples[i];ref+=x*x;proj+=x*a.processed.samples[i];
        }
        const double early=measure(0,1),late=measure(6,8),active=measure(9,10),retain=proj/ref;
        std::cout<<rate<<" Hz "<<channels<<" ch drift early "<<early<<" dB late "<<late
                 <<" dB active "<<active<<" dB projection "<<retain<<std::endl;
        if(!report){
            offline::require(late>=early+.7,"coherent drift improves late noise suppression");
            offline::require(active>=8.&&retain>.97&&retain<1.03,"foreground after drift retention");
            const auto b=offline::render(input,&learn,s,64);
            offline::require(a.processed.samples==b.processed.samples,"drift partition parity");
        }
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
