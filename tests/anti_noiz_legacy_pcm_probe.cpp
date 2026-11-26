// SPDX-License-Identifier: AGPL-3.0-only
// Exact canonical A/B driver: compile identical source against both Core headers.
#include "AntiNoizCore.h"
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
using armix::builtin_fx::anti_noiz::Core;
namespace {
void require(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
void hash(std::uint64_t& h,float x){require(std::isfinite(x),"nonfinite PCM");h^=std::bit_cast<std::uint32_t>(x);h*=1099511628211ull;}
void run(int rate,int channels,Core::Mode mode,int scenario){
    Core c;Core::Settings s;s.mode=Core::Mode::Voice;
    require(c.prepare(rate,channels,Core::SourceRole::Unknown)&&c.setSettings(s)&&c.beginLearn(),"prepare Learn");
    std::mt19937 rng(2307);std::uniform_real_distribution<float> noise(-1,1);
    float hist[2]{};int pos=0;std::array<float,256> l{},r{};
    auto fill=[&](int n,bool active){for(int i=0;i<n;++i)for(int k=0;k<channels;++k){
        const float w=noise(rng);const double t=double(pos+i)/rate;float x=.002f*w;
        if(scenario==1){hist[k]=.95f*hist[k]+.05f*w;x=.01f*hist[k];}
        if(scenario==2)x=.0004f*w+.003f*float(std::sin(2*std::numbers::pi*60*t));
        if(active&&((pos+i)/(rate/4))%3!=0)x+=.025f*float(std::sin(2*std::numbers::pi*(k?329:217)*t))+.009f*float(std::sin(2*std::numbers::pi*720*t));
        (k?r:l)[i]=x;}};
    for(pos=0;pos<3*rate;){int n=std::min(127,3*rate-pos);fill(n,false);c.process(l.data(),channels==2?r.data():nullptr,n);pos+=n;}
    require(c.hasProfile()&&!c.learning(),"Learn qualified");
    c.resetStreamKeepProfile();s.mode=mode;s.cleanDb=scenario==0?14.f:scenario==1?24.f:8.f;s.silenceDb=scenario==0?32.f:scenario==1?0.f:48.f;s.musicProtect=scenario==1?.25f:1.f;
    require(c.setSettings(s),"legacy settings");
    std::uint64_t h=14695981039346656037ull;
    for(pos=0;pos<2*rate;){int n=std::min(1+(pos%256),2*rate-pos);fill(n,true);c.process(l.data(),channels==2?r.data():nullptr,n);for(int i=0;i<n;++i){hash(h,l[i]);if(channels==2)hash(h,r[i]);}pos+=n;}
    hash(h,c.spectralGain());hash(h,c.residualGain());
    std::cout<<rate<<','<<channels<<','<<int(mode)<<','<<scenario<<','<<c.latencySamples()<<','<<h<<'\n';
}
}
int main(){for(int rate:{44100,48000,88200,96000})for(int channels:{1,2})for(int mode=0;mode<4;++mode)for(int scenario=0;scenario<3;++scenario)run(rate,channels,static_cast<Core::Mode>(mode),scenario);}
