// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/FxWorkloadEvidence.h"
#include "../engine/core/FxBypassEnvelope.h"
#include "../engine/core/StereoLatencyDelay.h"
#include <array>
#include <bit>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
using armix::rt::FxWorkloadEvidence;
static void workload(){
    for(double rate:{44100.,48000.,96000.,192000.}){
        FxWorkloadEvidence e;e.prepare(rate);
        assert(e.snapshot().validRate&&e.snapshot().calls==0);
        for(int i=0;i<128;++i){
            const int n=1<<(4+i%5);
            const auto ns=static_cast<std::uint64_t>((1.e9/rate)*n*(i%4==0?1.5:.25));
            e.observe(ns,n);
        }
        const auto s=e.snapshot();
        assert(s.calls==128&&s.overBudget==32);
        assert(s.worstPermille>=1499&&s.worstPermille<=1500);
    }
    FxWorkloadEvidence e;e.prepare(0.);
    e.observe(999999,16);assert(!e.snapshot().validRate&&e.snapshot().calls==0);
    FxWorkloadEvidence f;f.prepare(48000.);
    f.observe(1,0);assert(f.snapshot().calls==0);
    f.observe(1000000,16);assert(f.snapshot().overBudget==1&&f.snapshot().worstPermille>=3000);
}
static void history(){
    std::uint32_t rng=1234567;
    auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    auto bits=[](float x){return std::bit_cast<std::uint32_t>(x);};
    std::array<float,257> l{},r{},oldL{},oldR{},newL{},newR{};
    unsigned blocks=0;
    for(int latency:{1,7,19,64,304,1152,4096})
    for(double rate:{44100.,48000.,96000.}){
        armix::dsp::StereoLatencyDelay oldDelay,newDelay;
        assert(oldDelay.prepare(latency)&&newDelay.prepare(latency));
        armix::dsp::FxBypassEnvelope oldFade,newFade;
        oldFade.prepare(false,rate,latency);newFade.prepare(false,rate,latency);
        bool bypass=false;
        for(int b=0;b<2000;++b){
            const int n=1+static_cast<int>(next()%257);
            if(b%47==0||b%47==1||b%47==3)bypass=!bypass;
            oldFade.request(bypass);newFade.request(bypass);
            const bool fully=oldFade.fullyBypassed(),fading=oldFade.transitioning();
            const bool fault=b%113==0;
            assert(fully==newFade.fullyBypassed()&&fading==newFade.transitioning());
            for(int i=0;i<n;++i){
                l[i]=static_cast<float>(static_cast<int>(next()%2049)-1024)/1024.f;
                r[i]=i%11==0?-0.f:l[i]*-.75f;
                oldL[i]=l[i];oldR[i]=r[i];oldDelay.process(oldL[i],oldR[i]);
            }
            if(!fault&&!fading&&!fully)newDelay.advanceHistory(l.data(),r.data(),n);
            else for(int i=0;i<n;++i){
                newL[i]=l[i];newR[i]=r[i];newDelay.process(newL[i],newR[i]);
                assert(bits(oldL[i])==bits(newL[i])&&bits(oldR[i])==bits(newR[i]));
            }
            if(!fault&&fading)for(int i=0;i<n;++i){
                const auto a=oldFade.next(),c=newFade.next();
                assert(bits(a.wet)==bits(c.wet)&&bits(a.dry)==bits(c.dry));
                assert(bits(l[i]*a.wet+oldL[i]*a.dry)==bits(l[i]*c.wet+newL[i]*c.dry));
            }
            ++blocks;
        }
    }
    assert(blocks==42000);
}
int main(){workload();history();std::puts("PASS: 42000 variable latent FX blocks, bypass reversals/fault history and vendor-neutral workload budgets");}
