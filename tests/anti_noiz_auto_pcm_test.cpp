// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
using Audio=std::vector<float>;
Audio render(unsigned rate,unsigned channels,const Audio& input,const Audio& learn,Core::Settings s,int block){
    Core core;require(core.prepare(rate,int(channels))&&core.setSettings(s)&&core.beginLearn(),"prepare/learn");
    std::array<float,127> l{},r{};
    auto feed=[&](const Audio& a,std::size_t start,int count){
        for(int i=0;i<count;++i){const auto j=(start+std::size_t(i))*channels;l[i]=j<a.size()?a[j]:0;r[i]=channels==2&&j+1<a.size()?a[j+1]:0;}
        core.process(l.data(),r.data(),count);
    };
    for(std::size_t p=0;p<learn.size()/channels&&core.learning();){const int n=int(std::min<std::size_t>(block,learn.size()/channels-p));feed(learn,p,n);p+=std::size_t(n);}
    require(core.hasProfile()&&!core.learning(),"profile learned");core.resetStreamKeepProfile();
    const std::size_t delay=std::size_t(core.latencySamples()),frames=input.size()/channels;Audio out(input.size());
    for(std::size_t p=0;p<frames+delay;){const int n=int(std::min<std::size_t>(block,frames+delay-p));feed(input,p,n);
        for(int i=0;i<n;++i){const auto t=p+std::size_t(i);if(t<delay||t-delay>=frames)continue;const auto j=(t-delay)*channels;out[j]=l[i];if(channels==2)out[j+1]=r[i];}p+=std::size_t(n);}
    return out;
}
struct Metrics {double benefit=0,projection=0;};
Metrics measure(const Audio& input,const Audio& clean,const Audio& output,std::size_t start,std::size_t stop){
    double e0=0,e1=0,p=0,pr=0;for(auto j=start;j<stop;++j){const double x=input[j],r=clean[j],y=output[j];e0+=(x-r)*(x-r);e1+=(y-r)*(y-r);p+=r*r;pr+=r*y;}
    return {10*std::log10(e0/e1),pr/p};
}
int main(){try{
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(unsigned channels:{1u,2u}){
        std::mt19937 gen(7912);std::uniform_real_distribution<float> noise(-.003f,.003f);std::array<float,2> prev{};
        auto hiss=[&](unsigned c){float x=noise(gen),y=x-prev[c];prev[c]=x;return y;};
        Audio learn(3*rate*channels),clean(3*rate*channels),input(clean.size());
        for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<channels;++c)learn[i*channels+c]=hiss(c);
        for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<channels;++c){const auto j=i*channels+c;const double t=2.*std::numbers::pi*double(i)/rate;
            clean[j]=float((.05*std::sin(t*375)+.02*std::sin(t*750)+.008*std::sin(t*1125))*(c?.7:1.));input[j]=clean[j]+hiss(c);}
        Core::Settings s;s.silenceDb=0;
        auto out=render(rate,channels,input,learn,s,64),partition=render(rate,channels,input,learn,s,127);
        require(out==partition,"active Auto partition parity");
        auto m=measure(input,clean,out,rate*channels,3*rate*channels);
        s.musicProtect=0;auto strong=render(rate,channels,input,learn,s,64);auto n=measure(input,clean,strong,rate*channels,3*rate*channels);
        std::cout<<rate<<" Hz "<<channels<<" ch Auto: "<<m.benefit<<" dB projection "<<m.projection<<", Protect=0: "<<n.benefit<<" dB projection "<<n.projection<<std::endl;
        require(m.benefit>=5&&m.projection>.97&&m.projection<1.03,"default active benefit/retention");
        require(n.benefit>=7.5&&n.benefit>m.benefit+.3&&n.projection>.97&&n.projection<1.03,"protection control benefit/retention");
        s.cleanDb=0;auto dry=render(rate,channels,input,learn,s,64);for(std::size_t j=0;j<input.size();++j)require(std::abs(dry[j]-input[j])<2e-6f,"zero clean unity");
    }
    std::cout<<"Selective Auto PCM benefit, partition and clean-zero PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
