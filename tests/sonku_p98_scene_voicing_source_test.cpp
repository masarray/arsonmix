// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuP98SceneVoicing.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using armix::sonku::p98::SceneVoicing;
static double measure(int rate,int scene,int stage,double hz) {
    SceneVoicing e;
    assert(e.prepare(rate));
    constexpr double pi=3.1415926535897932384626433832795;
    double before=0,after=0;
    for(int i=0;i<rate;++i){
        const float x=.05f*std::sin(2.*pi*hz*i/rate);
        const auto src=e.source(stage==0?x:0.f,stage==0?x:0.f,scene,true);
        const auto early=e.early(stage==2?x:0.f,stage==2?x:0.f);
        const auto end=e.finish(stage==1?x:0.f,stage==1?x:0.f);
        const float y=stage==0?src.left:stage==1?end.left:early.left;
        assert(std::isfinite(y)&&std::abs(y)<1.f);
        if(scene==0)assert(y==x); // exact Venue preservation, all processing stages
        if(i>rate/2){before+=x*x;after+=y*y;}
    }
    return std::sqrt(after/before);
}
int main(){
    SceneVoicing bad;
    assert(!bad.prepare(96000));
    assert(!SceneVoicing::valid(-1)&&!SceneVoicing::valid(4));
    for(int rate:{44100,48000}){
        // Voice and bass gains are physically bounded; Studio < Cinema.
        const auto signatureBass=measure(rate,1,1,86.);
        const auto studioBass=measure(rate,2,1,86.);
        const auto cinemaBass=measure(rate,3,1,86.);
        const auto signatureVoice=measure(rate,1,0,2050.);
        const auto studioVoice=measure(rate,2,0,2050.);
        const auto cinemaVoice=measure(rate,3,0,2050.);
        const auto signatureEarly=measure(rate,1,2,7500.);
        assert(studioBass>1.035&&studioBass<1.20);
        assert(cinemaBass>studioBass+.07);
        assert(signatureBass>cinemaBass-.035);
        assert(signatureVoice>1.16);
        assert(signatureVoice>cinemaVoice&&cinemaVoice>studioVoice);
        assert(signatureEarly>1.04&&signatureEarly<1.25);
        for(int stage=0;stage<3;++stage)
            for(double hz:{86.,2050.,7500.})
                assert(std::abs(measure(rate,0,stage,hz)-1.)<1.e-7);
        // Mono-safe antiphase: no synthetic center vocal, no channel crossing.
        SceneVoicing p;
        assert(p.prepare(rate));
        for(int i=0;i<rate;++i){
            const float v=.03f*std::sin(2.*3.14159265358979323846*86.*i/rate);
            const auto s=p.source(v,-v,1,true);
            const auto e=p.early(v,-v);
            const auto y=p.finish(v,-v);
            assert(s.left==v&&s.right==-v);
            assert(std::abs(e.left+e.right)<1.e-5f);
            assert(std::abs(y.left+y.right)<1.e-5f);
        }
        p.reset();
        const auto neutral=p.source(.1f,.05f,0,true);
        assert(neutral.left==.1f&&neutral.right==.05f);
        std::printf("P98_VOICINGS,%d,SigBass=%.4f,StudioBass=%.4f,CinemaBass=%.4f,SigVoice=%.4f,StudioVoice=%.4f,CinemaVoice=%.4f,EarlyAir=%.4f\n",
            rate,signatureBass,studioBass,cinemaBass,
            signatureVoice,studioVoice,cinemaVoice,signatureEarly);
    }
}
