// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/FxWorkloadEvidence.h"
#include "../engine/core/FxLifecyclePolicy.h"
#include <limits>
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
static void lifecycle(){
    using namespace armix::rt;
    unsigned cases=0;
    for(auto cap:{FxIdleCapability::conservative,FxIdleCapability::exactNative,FxIdleCapability::qualifiedTail})
    for(unsigned mask=0;mask<32;++mask){
        const bool continuous=mask&1,requested=mask&2,fully=mask&4,fade=mask&8,fault=mask&16;
        const auto actual=chooseFxRenderAction(cap,continuous,requested,fully,fade,fault);
        if(fault||(fully&&!continuous))assert(actual==FxRenderAction::alignedDry);
        else if(continuous||requested||fully||fade||cap==FxIdleCapability::conservative)
            assert(actual==FxRenderAction::process);
        else assert(actual==(cap==FxIdleCapability::exactNative?
            FxRenderAction::probeExact:FxRenderAction::probeTail));
        ++cases;
    }
    assert(cases==96);
    float l[]{0.f,-0.f},r[]{-0.f,0.f};
    assert(exactSilentStereo(l,r,2));
    for(float nonzero:{std::bit_cast<float>(1u),1.e-20f,1.f,
                       std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        r[1]=nonzero;assert(!exactSilentStereo(l,r,2));
    }
    assert(!exactSilentStereo(nullptr,r,2));
    assert(reviewFx(false,0,0,0,0,0,0,false,false)==FxReview::noEvidence);
    assert(reviewFx(true,1,0,250,63,63,0,false,false)==FxReview::reviewBudget);
    assert(reviewFx(true,100,0,250,64,64,0,false,false)==FxReview::reviewSilentCost);
    assert(reviewFx(true,100,0,250,64,63,0,false,false)==FxReview::reviewBudget);
    assert(reviewFx(false,0,0,0,0,0,304,true,false)==FxReview::reviewLatentBypass);
    assert(reviewFx(true,100,10,2000,64,64,304,true,true)==FxReview::latencyFault);
    FxWorkloadEvidence e;e.prepare(48000.);
    for(int i=0;i<64;++i){e.observe(1000000,16);e.observeSilence(true);}
    auto s=e.snapshot();assert(s.calls==64&&s.overBudget==64&&s.silenceObserved==64&&s.silentInOut==64);
    for(int i=0;i<15;++i)e.observe(1000000,16);
    assert(e.snapshot().calls==64); // bounded publication even in overload
    e.prepare(96000.);s=e.snapshot();
    assert(s.validRate&&s.calls==0&&s.overBudget==0&&s.worstPermille==0&&s.silenceObserved==0);
}
int main(){workload();history();lifecycle();
    std::puts("PASS: 42000 latent blocks, 96 lifecycle combinations, silence/overload/reset and advisory fences");
}
