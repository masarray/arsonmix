// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuP97StageImpact.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using armix::sonku::p97::StageImpact;
static double signal(int rate,double hz,int section,float meters,bool room){
    StageImpact p;
    assert(p.prepare(rate));
    constexpr double pi=3.14159265358979323846;
    const int n=rate;
    double inputPower=0.,outputPower=0.;
    for(int i=0;i<n;++i){
        const float x=.08f*static_cast<float>(std::sin(2*pi*hz*i/rate));
        const auto src=p.source(section==0?x:0.f,section==0?x:0.f,meters,room);
        const auto direct=p.direct(section==1?x:0.f,section==1?x:0.f);
        const auto early=p.early(section==2?x:0.f,section==2?x:0.f);
        const auto finish=p.finish(section==3?x:0.f,section==3?x:0.f);
        const float y=section==0?src.left:section==1?direct.left:
                      section==2?early.left:finish.left;
        assert(std::isfinite(y)&&std::abs(y)<1.f);
        if((!room || meters<=2.f))assert(y==x);
        if(i>n/2) {inputPower+=double(x)*x; outputPower+=double(y)*y;}
    }
    return std::sqrt(outputPower/inputPower);
}
int main(){
    StageImpact bad;
    assert(!bad.prepare(96000.));
    for(int rate:{44100,48000}){
        // Exact sample preservation: disabled ROOM and <=2m at all bands.
        for(int section=0;section<4;++section){
            for(double hz:{85.,1900.,7500.}){
                assert(std::abs(signal(rate,hz,section,10.f,false)-1.)<1.e-6);
                assert(std::abs(signal(rate,hz,section,2.f,true)-1.)<1.e-6);
            }
        }
        const double bass=signal(rate,85.,1,10.f,true);
        const double vocal=signal(rate,1900.,0,10.f,true);
        const double directVoice=signal(rate,1900.,1,10.f,true);
        const double directAir=signal(rate,7500.,1,10.f,true);
        const double air=signal(rate,7500.,2,10.f,true);
        std::printf("P97_STAGE,%d,bass=%.6f,center=%.6f,earlyAir=%.6f\n",
            rate,bass,vocal,air);
        assert(bass>1.14&&bass<1.45);
        assert(vocal>1.13&&vocal<1.4);
        assert(directVoice>1.065&&directVoice<1.20);
        assert(directAir>1.01&&directAir<1.16);
        assert(air>1.07&&air<1.35);
        assert(signal(rate,85.,3,10.f,true)>1.04);
        assert(signal(rate,7500.,3,10.f,true)>1.16);
        // Center cancellation protects pure anti-phase stereo; filters are
        // linked/coefficient-identical for each independently spatialized ear.
        StageImpact p;
        assert(p.prepare(rate));
        for(int i=0;i<rate;++i){
            const float x=.08f*std::sin(2.*3.14159265358979323846*85.*i/rate);
            const auto src=p.source(x,-x,10.f,true);
            assert(src.left==x && src.right==-x);
            const auto d=p.direct(x,-x);
            const auto er=p.early(x,-x);
            assert(std::abs(d.left+d.right)<1.e-6f);
            assert(std::abs(er.left+er.right)<1.e-6f);
            const auto f=p.finish(x,-x);
            assert(std::abs(f.left+f.right)<1.e-6f);
            const auto modal=p.modal(x,-x);
            assert(std::abs(modal.left+modal.right)<1.e-6f);
            assert(std::abs(modal.left)<=std::abs(x)*1.121f);
        }
        // Reset after device reprepare, no stale filter memory.
        p.reset();assert(!p.active());
        const auto src=p.source(.1f,-.1f,2.f,true);
        assert(src.left==.1f&&src.right==-.1f);
    }
}
