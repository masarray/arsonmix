// SPDX-License-Identifier: AGPL-3.0-only
#include "../tools/anti-noiz/OfflineAudio.h"
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
int main(){try{
    for(unsigned rate:{44100u,48000u,88200u,96000u}){
        // Deterministic PSD policy test: colored noise dominates one band,
        // while a quiet stereo leg and unrelated bands retain veto power.
        ContextGuard<129> guard;const int hop=rate>48000?256:128;
        guard.prepare(rate,hop);
        ContextGuard<129>::Powers profile{};
        for(auto& ch:profile)ch.fill(.001f);
        for(int k=0;k<8;++k)profile[0][k]=1.f;
        const std::array<float,2> cv{2.f,0.f};
        const int frames=int(std::ceil(.5*rate/hop));
        auto varying=profile;
        for(int k=0;k<8;++k)varying[0][k]*=3.f;
        for(int i=0;i<frames;++i){
            const bool strict=guard.update(varying,profile,2,129,true,&cv);
            offline::require(!strict,"strict tracking authority unchanged");
        }
        offline::require(guard.adaptiveIdle(),"variable noise residual hold");
        auto onset=varying;onset[1][32]=.1f;
        guard.update(onset,profile,2,129,true,&cv);
        offline::require(!guard.adaptiveIdle(),"quiet stereo leg immediate veto");
        for(int i=0;i<frames;++i)guard.update(varying,profile,2,129,true,&cv);
        onset=varying;onset[0][40]=.1f;
        guard.update(onset,profile,2,129,true,&cv);
        offline::require(!guard.adaptiveIdle(),"unrelated band immediate veto");
        for(int i=0;i<frames;++i)guard.update(varying,profile,2,129,true,&cv);
        guard.update(varying,profile,2,129,true);
        offline::require(!guard.adaptiveIdle(),"policy removal clears residual authority");
        guard.reset();const std::array<float,2> stationary{.2f,.2f};
        for(int i=0;i<frames;++i)guard.update(varying,profile,2,129,true,&stationary);
        offline::require(!guard.adaptiveIdle(),"stationary profile not eligible");
        // Boolean temporal corroboration is stereo-union and must not
        // confuse a single instantaneous peak with persistent evidence.
        guard.reset();auto probe=profile;
        guard.update(probe,profile,2,129,true);
        offline::require(!guard.hasTemporalExcess(profile,2,129,40),"noise-only temporal guard");
        for(int k=39;k<=41;++k)probe[1][k]*=4.f;
        for(int i=0;i<frames;++i)guard.update(probe,profile,2,129,true);
        offline::require(guard.hasTemporalExcess(profile,2,129,40),"independent stereo temporal evidence");
        offline::require(!guard.hasTemporalExcess(profile,1,129,40),"mono cannot see other leg");
        offline::require(!guard.hasTemporalExcess(profile,2,129,0),"DC boundary stays noise-only");
        offline::require(!guard.hasTemporalExcess(profile,2,129,128),"Nyquist boundary stays noise-only");
        std::mt19937 gen(318);std::uniform_real_distribution<float> random(-.003f,.003f);
        offline::Audio learn{rate,2,std::vector<float>(std::size_t(rate)*6)};
        for(auto& x:learn.samples)x=random(gen);
        offline::Audio input=learn;
        for(auto& x:input.samples)x=random(gen);
        const auto n=input.samples.size()/2;std::vector<float> tone(n);
        // Low-energy narrowband source, right channel only; 1 second of noise
        // before and after. This is a controlled proxy, not actual music.
        for(std::size_t i=rate;i<2u*rate;++i){
            tone[i]=.00085f*std::sin(2.f*std::numbers::pi_v<float>*750.f*float(i)/float(rate));
            input.samples[2*i+1]+=tone[i];
        }
        Core::Settings s;auto a=offline::render(input,&learn,s,64),b=offline::render(input,&learn,s,127);
        offline::require(a.processed.samples==b.processed.samples,"context block parity");
        double before=0,after=0,ref=0,projection=0,rawProjection=0,tailBefore=0,tailAfter=0;
        for(std::size_t i=0;i<n;++i){
            if(i>=rate*3/4&&i<rate){for(int c=0;c<2;++c){double x=input.samples[2*i+c],y=a.processed.samples[2*i+c];before+=x*x;after+=y*y;}}
            if(i>=rate*3/2&&i<rate*19/10){ref+=double(tone[i])*tone[i];projection+=double(tone[i])*a.processed.samples[2*i+1];rawProjection+=double(tone[i])*input.samples[2*i+1];}
            if(i>=rate*11/4){for(int c=0;c<2;++c){double x=input.samples[2*i+c],y=a.processed.samples[2*i+c];tailBefore+=x*x;tailAfter+=y*y;}}
        }
        const double idle=10*std::log10(before/after),tail=10*std::log10(tailBefore/tailAfter),gain=projection/rawProjection;
        std::cout<<rate<<" Hz: initial idle "<<idle<<" dB, weak tone relative gain "<<gain<<", final idle "<<tail<<" dB\n";
        offline::require(ref>0&&rawProjection>0&&gain>.995&&gain<1.005,"weak foreground retention");
        offline::require(idle>=20&&tail>=20,"idle suppression recovery");
    }
    std::cout<<"Temporal foreground protection and stereo pause recovery PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
