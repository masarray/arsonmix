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
    // Both transform sizes, full-scale bounded PCM, tiny values and silent
    // stereo legs: compare the packed kernel with the existing reference.
    {
        using armix::builtin_fx::anti_noiz::FftPlan;
        FftPlan fft;std::mt19937 rng(29);
        std::uniform_real_distribution<float> random(-64.f,64.f);
        for(int n:{256,512}){
            fft.prepare(n);
            for(int trial=0;trial<96;++trial){
                std::array<FftPlan::Complex,FftPlan::maxN> ref{},packed{};
                const float scale=trial%3==0?1.f:trial%3==1?1e-12f:0.f;
                for(int i=0;i<n;++i)ref[i]={random(rng)*scale,trial%4==0?0.f:random(rng)*scale};
                packed=ref;
                fft.transform(ref,false);fft.transformPacked<false>(packed);
                for(int i=0;i<n;++i)require(std::abs(ref[i]-packed[i])<=2e-6f*std::max(1.f,std::abs(ref[i])),"packed FFT forward parity");
                fft.transform(ref,true);fft.transformPacked<true>(packed);
                for(int i=0;i<n;++i)require(std::abs(ref[i]-packed[i])<=2e-6f*std::max(1.f,std::abs(ref[i])),"packed FFT inverse parity");
            }
        }
    }
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
    const auto idleDb=20*std::log10(rms(raw,120000)/rms(l,120000));
    require(idleDb>=20.,"noise-only reduction >=20 dB");
    std::cout<<"Synthetic idle reduction dB: "<<idleDb<<'\n';
    for(std::size_t i=0;i<l.size();++i)require(std::abs(r[i]-.6f*l[i])<2e-6f,"linked stereo");
    require(a.setSettings({Core::Mode::Music,24,48}),"music protect");
    for(std::size_t i=0;i<l.size();++i){l[i]=.03f*std::sin(float(i)*.025f);r[i]=.04f*std::cos(float(i)*.081f);}
    raw=l;auto right=r;a.process(l.data(),r.data(),int(l.size()));
    const auto delay=std::size_t(a.latencySamples());
    for(std::size_t i=2*delay;i<l.size();++i){
        require(std::abs(l[i]-raw[i-delay])<2e-6f,"music unity left");
        require(std::abs(r[i]-right[i-delay])<2e-6f,"music unity right");
    }
    // A rejected candidate must never erase the last known good profile.
    require(a.beginLearn(),"relearn start");
    std::vector<float> q(8192,.001f),w(8192,.001f);
    for(std::size_t i=4096;i<q.size();++i)q[i]=w[i]=.5f;
    a.process(q.data(),w.data(),int(q.size()));
    require(a.learning()&&a.hasProfile(),"burst does not abort Learn");
    for(int i=0;i<4*48000&&a.learning();i+=8192){q.assign(8192,.5f);w.assign(8192,.5f);a.process(q.data(),w.data(),int(q.size()));}
    require(!a.learning()&&a.hasProfile()&&!a.lastNativeLearnQualified(),"sustained contamination rejects at bounded deadline and retains profile");
    // A stationary sinusoidal proxy is NOT a real vocal/music acceptance test.
    // Exercise separate stereo sources and check that Voice attenuation does not
    // remove the main tone while Music mode remains available for full mixes.
    require(a.setSettings({Core::Mode::Voice,12,24}),"voice policy");
    std::vector<float> tone(48000),noisy(48000),other(48000);
    for(std::size_t i=0;i<tone.size();++i){
        tone[i]=.1f*std::sin(2.f*std::numbers::pi_v<float>*750.f*float(i)/48000.f);
        noisy[i]=tone[i]+dist(gen);other[i]=dist(gen);
    }
    auto before=noisy;
    a.process(noisy.data(),other.data(),int(noisy.size()));
    double refPower=0,projection=0,errBefore=0,errAfter=0;
    for(std::size_t i=2048;i<tone.size();++i){
        const double ref=tone[i-delay];refPower+=ref*ref;projection+=ref*noisy[i];
        errBefore+=std::pow(before[i-delay]-ref,2);errAfter+=std::pow(noisy[i]-ref,2);
    }
    const auto toneGain=projection/refPower;
    require(toneGain>.97&&toneGain<1.03,"voice proxy tone retention");
    std::cout<<"Voice proxy tone gain: "<<toneGain<<", error improvement dB: "<<10*std::log10(errBefore/errAfter)<<'\n';
    Core mono;require(mono.prepare(48000,1),"mono prepare");
    float z[1024]{};z[4]=1;mono.process(z,nullptr,1024);
    require(std::abs(z[4+mono.latencySamples()]-1)<2e-6f,"mono impulse latency");
    std::cout<<"Anti Noiz baseline: reconstruction, partition, learning, residual, stereo, mono PASS\n";
}
