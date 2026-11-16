// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuP96DistanceReinforcement.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
using armix::sonku::p96::DistanceReinforcement;
static double render(double rate,float hz,float meters,float direct,bool on,
                     bool antiphase=false) {
    DistanceReinforcement p;
    assert(p.prepare(rate));
    const int count=static_cast<int>(rate);
    constexpr double pi=3.14159265358979323846;
    double sum=0.,cross=0.;
    for(int i=0;i<count;++i){
        const float x=.12f*static_cast<float>(std::sin(2.*pi*hz*i/rate));
        const auto y=p.process(x,antiphase?-x:x,direct,meters,on);
        assert(std::isfinite(y.left)&&std::isfinite(y.right));
        assert(std::abs(y.left)<1.f&&std::abs(y.right)<1.f);
        if(!on){
            assert(y.left==direct*x);
            assert(y.right==direct*(antiphase?-x:x));
        }
        if(antiphase)assert(std::abs(y.left+y.right)<1.e-6f);
        if(i>count/2){sum+=double(y.left)*y.left;cross+=double(x)*x;}
    }
    return std::sqrt(sum/cross);
}
int main(){
    DistanceReinforcement bad;
    assert(!bad.prepare(96000.));
    for(double rate:{44100.,48000.}){
        for(float hz:{70.f,800.f,7500.f}){
            // No ROOM, 2m or less: EXACT direct signal, no extra EQ.
            assert(std::abs(render(rate,hz,10.f,1.f,false)-1.f)<1.e-5);
            assert(std::abs(render(rate,hz,2.f,.9f,true)-.9f)<1.e-5);
            assert(std::abs(render(rate,hz,10.f,.306f,false)-.306f)<1.e-5);
        }
        const auto lf=render(rate,70.f,10.f,.306f,true,true);
        const auto mid=render(rate,800.f,10.f,.306f,true);
        const auto hf=render(rate,7500.f,10.f,.306f,true);
        std::printf("P96 bands rate=%.0f bass=%.5f mid=%.5f treble=%.5f\n",rate,lf,mid,hf);
        assert(lf>.67&&lf<.93);
        assert(mid>.44&&mid<.61);
        assert(hf>.64&&hf<.88);
        assert(lf>mid*1.25&&hf>mid*1.20);
        // Compare low/air retention against P9.5's uniformly attenuated direct.
        assert(lf>.306f*2.f&&hf>.306f*1.9f);
    }
}
