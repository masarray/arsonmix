// SPDX-License-Identifier: AGPL-3.0-only
// All assertions execute in optimized release mode; pure C++ / no JUCE.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuImmerSIBPleasantPresence.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
using armix::sonku::immersib::PleasantPresence;
constexpr double pi=3.14159265358979323846;
static double level(int sr,double hz,bool on,float sideRatio=1.f){
    PleasantPresence fx;
    assert(fx.prepare(sr));
    double original=0.,final=0.;
    for(int i=0;i<sr;++i) {
        const float x=.19f*std::sin(2*pi*hz*i/sr);
        const auto y=fx.process(x,x*sideRatio,on);
        assert(std::isfinite(y.left) && std::isfinite(y.right));
        if(i>sr/2){ original+=x*x;final+=y.left*y.left; }
        if(!on){assert(y.left==x && y.right==x*sideRatio);}
        // A single shared spectral transfer cannot move a coherent
        // panned source across channels.
        if(on && i>sr/2 && std::abs(y.left)>.0001f) {
            assert(std::abs(y.right-sideRatio*y.left)<.00008f);
        }
    }
    return std::sqrt(final/original);
}
static void energyConservation(int sr) {
    PleasantPresence fx;assert(fx.prepare(sr));
    double maxOutput=0.;
    for(int i=0;i<sr;++i){
        const float a=.09f*std::sin(2*pi*285*i/sr);
        const float c=.07f*std::sin(2*pi*1100*i/sr);
        const float g=.06f*std::sin(2*pi*3750*i/sr);
        const auto y=fx.process(a+c+g,a+c-.5f*g,true);
        assert(std::isfinite(y.left)&&std::isfinite(y.right));
        maxOutput=std::max(maxOutput,
                           std::max(std::abs(double(y.left)),
                                    std::abs(double(y.right))));
    }
    assert(maxOutput<.30);
}
int main(){
    for(const int sr:{44100,48000}){
        assert(level(sr,3750.,false)==1.);
        const double hz86=level(sr,86.,true);
        const double hz285=level(sr,285.,true);
        const double hz1050=level(sr,1050.,true);
        const double hz3750=level(sr,3750.,true);
        const double hz4900=level(sr,4900.,true);
        const double hz8500=level(sr,8500.,true);
        assert(hz86>.995 && hz86<1.003);
        assert(hz285>.98 && hz285<1.003);
        assert(hz1050>.91 && hz1050<=1.001);
        assert(hz3750>.67 && hz3750<.86);
        assert(hz4900>.68 && hz4900<.91);
        assert(hz8500>.88 && hz8500<=1.002);
        (void)level(sr,3750.,true,.25f);
        (void)level(sr,3750.,true,-1.f);
        energyConservation(sr);
        std::printf("IMMERSIB_PLEASANT,%d,Lf86=%.4f,body285=%.4f,presence1k=%.4f,harsh3750=%.4f,articulation4900=%.4f,air8500=%.4f\\n",
              sr,hz86,hz285,hz1050,hz3750,hz4900,hz8500);
    }
    std::puts("PASS linked-ear spectral de-harsh, bass and stage-panning control");
}
