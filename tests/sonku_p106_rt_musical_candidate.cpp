// SPDX-License-Identifier: AGPL-3.0-only
// Candidate evidence: identical input, actual full DSP, no mocked convolution.
#include "../engine/builtin_fx/sonku/SonKuP99DefaultOrbitStage.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
constexpr double pi=3.14159265358979323846;
using Clock=std::chrono::steady_clock;
static auto make(int rate,bool fast,bool balanced,Settings s){
    auto p=std::make_unique<SonKuSpatialCore>();
    p->optInP99ForSonKuInsert();
    p->optInSonKuP106(fast,balanced);
    assert(p->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    assert(p->setSettings(s));
    return p;
}
static bool bits(float a,float b){
    return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);
}
static void report(std::vector<double>& x,int rate,int block,const char* variant){
    std::sort(x.begin(),x.end());
    const auto q=[&](double f){return x[static_cast<size_t>(std::ceil(f*x.size()))-1];};
    const double budget=1.e6*block/rate;
    const auto late=std::count_if(x.begin(),x.end(),[&](double n){return n>budget;});
    std::printf("SONKU_P106_CPU rate=%d frames=%d variant=%s p50=%.2f p95=%.2f p99=%.2f max=%.2f budget=%.2f late=%zu blocks=%zu\n",
        rate,block,variant,q(.5),q(.95),q(.99),x.back(),budget,static_cast<size_t>(late),x.size());
}
static void parity(int rate,int block){
    auto s=p99::DefaultOrbitStage::settings();
    auto a=make(rate,false,false,s),b=make(rate,true,false,s);
    std::vector<double> ta,tb;
    for(int n=0;n<768;++n){
        // Include activation epochs, spread zero/fusion, direction and scene edits.
        if(n==200||n==250||n==400||n==500||n==600){
            if(n==200)s.spacePreset=0;
            if(n==250)s.spacePreset=3;
            if(n==400)s.stereoSpreadDegrees=0.f;
            if(n==500)s.stereoSpreadDegrees=127.f;
            if(n==600){s.clockwise=false;s.sceneVoicing=2;}
            assert(a->setSettings(s));assert(b->setSettings(s));
        }
        std::array<float,256> l{},r{};
        for(int i=0;i<block;++i){
            const double t=double(n*block+i)/rate;
            l[i]=float(.09*std::sin(2*pi*330*t)+.04*std::sin(2*pi*2050*t));
            r[i]=n<100?l[i]:float(.06*std::sin(2*pi*480*t)-.05*std::sin(2*pi*3700*t));
            if(n>=680)r[i]=-l[i];
            if(n>=730)l[i]=r[i]=0.f;
        }
        auto ll=l,rr=r;
        auto run=[&](auto& p,auto& x,auto& y,auto& timings){
            auto t=Clock::now();p->process(x.data(),y.data(),block);
            auto us=std::chrono::duration<double,std::micro>(Clock::now()-t).count();
            if(n>=32&&n<200)timings.push_back(us);
        };
        // Alternate measurement order to reduce systematic cache/thermal bias.
        if(n%2){run(b,ll,rr,tb);run(a,l,r,ta);}
        else{run(a,l,r,ta);run(b,ll,rr,tb);}
        for(int i=0;i<block;++i){
            assert(std::isfinite(l[i])&&std::isfinite(r[i]));
            assert(bits(l[i],ll[i])&&bits(r[i],rr[i]));
        }
    }
    report(ta,rate,block,"reference");report(tb,rate,block,"fir_only");
    std::printf("SONKU_P106_FIR_PCM_EXACT rate=%d frames=%d\n",rate,block);
}
static double tone(int rate,int hz,int scene,bool balanced){
    p98::SceneVoicing v;
    v.enableBalancedPresence(balanced);assert(v.prepare(rate));
    double power=0.;
    for(int i=0;i<rate;++i){
        const float x=float(.1*std::sin(2*pi*hz*i/rate));
        auto src=v.source(x,x,scene,true);
        auto y=v.finish(src.left,src.right);
        assert(std::isfinite(y.left)&&bits(y.left,y.right));
        if(i>=rate/2)power+=double(y.left)*y.left;
    }
    return power;
}
static void voicing(int rate){
    for(int scene=1;scene<=3;++scene){
        double a=tone(rate,2050,scene,false),b=tone(rate,2050,scene,true);
        const double delta=10*std::log10(b/a);
        std::printf("SONKU_P106_PRESENCE rate=%d scene=%d delta_db=%.3f\n",rate,scene,delta);
        assert(delta<-.1&&delta>-6.);
        const double body=10*std::log10(tone(rate,330,scene,true)/tone(rate,330,scene,false));
        assert(std::abs(body)<1.);
    }
    // Neutral and other brand profiles must remain exactly unchanged.
    for(int scene:{0,4,5})for(int hz:{86,330,2050,6500})
        assert(tone(rate,hz,scene,true)==tone(rate,hz,scene,false));
}
static void partition(int rate){
    auto s=p99::DefaultOrbitStage::settings();
    auto a=make(rate,true,true,s),b=make(rate,true,true,s);
    std::array<float,256> l{},r{};
    for(int n=0;n<96;++n){
        for(int i=0;i<256;++i){
            l[i]=float(.08*std::sin((n*256+i)*.09));
            r[i]=float(.06*std::cos((n*256+i)*.12));
        }
        auto ll=l,rr=r;
        a->process(l.data(),r.data(),256);
        for(int i=0;i<256;i+=64)b->process(ll.data()+i,rr.data()+i,64);
        for(int i=0;i<256;++i){
            assert(std::isfinite(l[i])&&std::isfinite(r[i]));
            assert(std::abs(l[i])<=.98001f&&std::abs(r[i])<=.98001f);
            assert(bits(l[i],ll[i])&&bits(r[i],rr[i]));
        }
    }
}
int main(){
    for(int rate:{44100,48000}){
        voicing(rate);for(int block:{64,128,256})parity(rate,block);
        partition(rate);
    }
    std::puts("SONKU_P106_CANDIDATE_PASS (DSP only; not ASIO hardware)");
}
