// SPDX-License-Identifier: AGPL-3.0-only
#include "../tools/anti-noiz/OfflineAudio.h"
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
int main(){try{
    constexpr unsigned rate=48000;
    for(unsigned channels:{1u,2u}){
        std::mt19937 gen(912);std::uniform_real_distribution<float> rng(-.003f,.003f);
        offline::Audio noise{rate,channels,std::vector<float>(3*rate*channels)},clean=noise,input=noise;
        std::array<float,2> prev{};
        auto hiss=[&](unsigned c){float x=rng(gen),y=x-prev[c];prev[c]=x;return y;};
        for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<channels;++c)noise.samples[i*channels+c]=hiss(c);
        for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<channels;++c){
            const auto j=i*channels+c;const float phase=2.f*std::numbers::pi_v<float>*float(i)/rate;
            clean.samples[j]=(.05f*std::sin(phase*375)+.02f*std::sin(phase*750)+.008f*std::sin(phase*1125))*(c?.7f:1.f);
            input.samples[j]=clean.samples[j]+hiss(c);
        }
        double previous=0;
        for(float amount:{0.f,14.f,24.f}){
            Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=amount;s.silenceDb=0;
            const auto a=offline::render(input,&noise,s,64);
            double e0=0,e1=0,p=0,pr=0;
            for(unsigned j=rate*channels;j<input.samples.size();++j){const double x=input.samples[j],y=a.processed.samples[j],r=clean.samples[j];e0+=(x-r)*(x-r);e1+=(y-r)*(y-r);p+=r*r;pr+=r*y;}
            const double benefit=10*std::log10(e0/e1),projection=pr/p;
            offline::require(projection>.97&&projection<1.03,"desired signal projection");
            if(amount==0)offline::require(a.deltaRms<1e-7,"zero Clean unity");
            else {
                offline::require(benefit>=(amount==14?10.:11.),"active hiss benefit floor");
                offline::require(benefit>previous+.5,"Clean amount must increase benefit");
            }
            previous=benefit;
            std::cout<<channels<<" ch Clean "<<amount<<": active hiss error improvement "<<benefit<<" dB; signal projection "<<projection<<'\n';
        }
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
