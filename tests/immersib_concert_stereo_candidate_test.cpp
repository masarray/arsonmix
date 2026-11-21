// SPDX-License-Identifier: AGPL-3.0-only
// FUX-1 candidate: verify stereo music provenance without JUCE or devices.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuP90StereoDecoder.h"
#include "../engine/builtin_fx/sonku/SonKuP98SceneVoicing.h"
#include "../engine/builtin_fx/sonku/SonKuImmerSIBVocalRoom.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using armix::sonku::p90::StereoDecoder;
using armix::sonku::p98::SceneVoicing;
using armix::sonku::p99::ImmerSIBVocalRoom;
constexpr double pi=3.14159265358979323846;
static float settle(StereoDecoder& d,int rate,float rightGain){
    for(int i=0;i<rate;++i){
        const float x=.12f*std::sin(2*pi*970*i/rate);
        d.observe(x,x*rightGain);
        if((i&15)==0)d.centerBlend();
    }
    return d.centerBlend();
}
static double sideRms(int rate,double hz,bool protect){
    SceneVoicing s;assert(s.prepare(rate));
    double sum=0.;
    for(int i=0;i<rate;++i){
        const float x=.08f*std::sin(2*pi*hz*i/rate);
        auto y=s.source(x,-x,SceneVoicing::ImmerSIBDetail,true,protect);
        assert(std::isfinite(y.left)&&std::isfinite(y.right));
        if(i>rate/2) {
            const double v=.5*(y.left-y.right);
            sum+=v*v;
        }
    }
    return std::sqrt(sum/(rate/2));
}
static double roomLateRms(int rate,double hz,bool candidate){
    ImmerSIBVocalRoom fx;assert(fx.prepare(rate));
    fx.enableConcertLateClarity(candidate);
    double sum=0.;
    for(int i=0;i<rate;++i){
        const float x=.04f*std::sin(2*pi*hz*i/rate);
        (void)fx.source(x,x,SceneVoicing::ImmerSIBDetail,true);
        const auto y=fx.late(x,x);
        assert(std::isfinite(y.left)&&std::isfinite(y.right));
        if(i>rate/2)sum+=double(y.left)*y.left;
    }
    return std::sqrt(sum/(rate/2));
}
int main(){
    for(int sr:{44100,48000}){
        StereoDecoder legacy,panned,center,hardLeft;
        assert(legacy.prepare(sr)&&panned.prepare(sr)&&
               center.prepare(sr)&&hardLeft.prepare(sr));
        panned.enablePanPreservation(true);
        center.enablePanPreservation(true);
        hardLeft.enablePanPreservation(true);
        assert(settle(legacy,sr,.25f)>.96f);
        assert(settle(panned,sr,.25f)<.03f);
        assert(settle(center,sr,1.f)>.96f);
        assert(settle(hardLeft,sr,0.f)<.03f);
        assert(sideRms(sr,2050.,true)>1.3*sideRms(sr,2050.,false));
        assert(sideRms(sr,3450.,true)>1.2*sideRms(sr,3450.,false));
        const double bassBefore=roomLateRms(sr,86.,false);
        const double bassAfter=roomLateRms(sr,86.,true);
        const double vocal1kBefore=roomLateRms(sr,1050.,false);
        const double vocal1kAfter=roomLateRms(sr,1050.,true);
        const double vocal5kBefore=roomLateRms(sr,4900.,false);
        const double vocal5kAfter=roomLateRms(sr,4900.,true);
        assert(bassBefore>1.e-7);
        assert(std::abs(bassAfter-bassBefore)/bassBefore<.005);
        assert(vocal1kAfter<vocal1kBefore);
        assert(vocal5kAfter<vocal5kBefore);
        SceneVoicing a,b;assert(a.prepare(sr)&&b.prepare(sr));
        for(int i=0;i<sr;++i){
            const float x=.03f*std::sin(2*pi*1100*i/sr);
            const auto old=a.source(x,x,4,true,false);
            const auto revised=b.source(x,x,4,true,true);
            assert(old.left==revised.left && old.right==revised.right);
        }
        std::printf("PASS candidate source stereo pan + true center %d Hz\n",sr);
    }
}
