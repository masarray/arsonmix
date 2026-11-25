// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using armix::builtin_fx::anti_noiz::Core;
void require(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
double rms(const std::vector<float>& x,std::size_t start) {
    double s=0;for(std::size_t i=start;i<x.size();++i)s+=double(x[i])*x[i];
    return std::sqrt(s/double(x.size()-start));
}
int main(){
    for(double rate:{44100.,48000.,88200.,96000.}) {
        Core a,b;require(a.prepare(rate,2)&&b.prepare(rate,2),"prepare");
        std::vector<float> l(8192),r(8192);
        for(std::size_t i=0;i<l.size();++i){l[i]=.2f*std::sin(float(i)*.037f);r[i]=.1f*std::cos(float(i)*.081f);}
        auto x=l,y=r,u=l,v=r;
        a.process(x.data(),y.data(),int(x.size()));
        for(std::size_t i=0;i<u.size();) {int n=int(std::min<std::size_t>(1+i%137,u.size()-i));b.process(u.data()+i,v.data()+i,n);i+=n;}
        const auto delay=std::size_t(a.latencySamples());
        for(std::size_t i=0;i<x.size();++i){
            require(x[i]==u[i]&&y[i]==v[i],"block partition parity");
            require(std::abs(x[i]-(i<delay?0.f:l[i-delay]))<2e-6f,"left reconstruction");
            require(std::abs(y[i]-(i<delay?0.f:r[i-delay]))<2e-6f,"right reconstruction");
        }
    }
    Core a;require(!a.prepare(0,2)&&a.prepare(48000,2),"format guard");
    require(!a.setSettings({Core::Mode::Voice,-1,20}),"settings validation");
    std::mt19937 gen(42);std::uniform_real_distribution<float> dist(-.003f,.003f);
    std::vector<float> l(144000),r(l.size());
    for(std::size_t i=0;i<l.size();++i){l[i]=dist(gen);r[i]=l[i]*.6f;}
    auto raw=l;require(a.beginLearn(),"learn start");
    a.process(l.data(),r.data(),int(l.size()));
    require(a.hasProfile()&&!a.learning(),"profile learned");
    require(rms(l,120000)<rms(raw,120000)*.5,"noise-only reduction");
    for(std::size_t i=0;i<l.size();++i)require(std::abs(r[i]-.6f*l[i])<2e-6f,"linked stereo");
    require(a.setSettings({Core::Mode::Music,24,48}),"music protect");
    for(std::size_t i=0;i<l.size();++i){l[i]=.03f*std::sin(float(i)*.025f);r[i]=.04f*std::cos(float(i)*.081f);}
    raw=l;auto right=r;a.process(l.data(),r.data(),int(l.size()));
    const auto delay=std::size_t(a.latencySamples());
    for(std::size_t i=2*delay;i<l.size();++i){
        require(std::abs(l[i]-raw[i-delay])<2e-6f,"music unity left");
        require(std::abs(r[i]-right[i-delay])<2e-6f,"music unity right");
    }
    Core mono;require(mono.prepare(48000,1),"mono prepare");
    float z[1024]{};z[4]=1;mono.process(z,nullptr,1024);
    require(std::abs(z[4+mono.latencySamples()]-1)<2e-6f,"mono impulse latency");
    std::cout<<"Anti Noiz baseline: reconstruction, partition, learning, residual, stereo, mono PASS\n";
}
