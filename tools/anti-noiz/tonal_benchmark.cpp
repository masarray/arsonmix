// SPDX-License-Identifier: AGPL-3.0-only
#include "OfflineAudio.h"
#include <iomanip>
#include <iostream>
using namespace armix::builtin_fx::anti_noiz;
double amplitude(const offline::Audio& a,float f){
    double re=0.,im=0.;
    const unsigned rate=a.rate;
    for(unsigned i=2*rate;i<3*rate;++i){
        const double w=2.*std::numbers::pi*f*i/rate;
        re+=a.samples[i]*std::cos(w);im+=a.samples[i]*std::sin(w);
    }
    return 2.*std::hypot(re,im)/rate;
}
int main(){try{
    std::cout<<"rate,line,scenario,noise_amplitude,foreground_amplitude\n"<<std::setprecision(12);
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(float line:{55.f,60.f,73.f,100.f,137.5f})for(int shared=0;shared<2;++shared){
        offline::Audio learn{rate,1,std::vector<float>(rate*3)},input=learn;
        for(unsigned i=0;i<3*rate;++i){
            const double phase=2.*std::numbers::pi*i/rate;
            learn.samples[i]=float(.05*std::sin(phase*line));
            input.samples[i]=learn.samples[i]+float(.08*std::sin(phase*230.))+(shared?float(.20*std::sin(phase*line)):0.f);
        }
        Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=14;s.silenceDb=0;
        const auto a=offline::render(input,&learn,s,127);
        std::cout<<rate<<','<<line<<','<<shared<<','<<amplitude(a.processed,line)<<','<<amplitude(a.processed,230.f)<<'\n';
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
