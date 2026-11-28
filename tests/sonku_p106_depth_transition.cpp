// SPDX-License-Identifier: AGPL-3.0-only
// Compiled exclusively against isolated P10.6 research header.
#include "SonKuP94FrontBackDepthCue.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
using armix::sonku::p94::FrontBackDepthCue;
using Gains=FrontBackDepthCue::Gains;
bool exact(Gains a,Gains b){
    return std::bit_cast<std::uint32_t>(a.direct)==std::bit_cast<std::uint32_t>(b.direct)&&
           std::bit_cast<std::uint32_t>(a.early)==std::bit_cast<std::uint32_t>(b.early);
}
int main(){
    for(int rate:{44100,48000}){
        FrontBackDepthCue ref,lab;
        if(!ref.prepare(rate)||!lab.prepare(rate))return 1;
        ref.retarget(0.f,true);lab.retarget(0.f,true);
        Gains prev{};
        for(int i=0;i<rate;++i){
            prev=lab.next();if(!exact(prev,ref.next()))return 2;
        }
        float maxDirect=0.f,maxEarly=0.f;
        auto step=[&](){
            auto g=lab.next();
            maxDirect=std::max(maxDirect,std::abs(g.direct-prev.direct));
            maxEarly=std::max(maxEarly,std::abs(g.early-prev.early));
            prev=g;return g;
        };
        // Static front, wide stereo, fully coherent blend. Switch to
        // Signature without changing original room or source geometry.
        lab.retargetCoherentStereo(0.f,-63.5f,63.5f,1.f,true);
        for(int i=0;i<rate;++i){step();ref.next();}
        if(std::abs(prev.direct-.935f)>1.e-5f||std::abs(prev.early-1.42f)>1.e-5f)return 3;
        // Return to original Venue calibration without a coefficient step.
        lab.retarget(0.f,true);
        Gains a{},b{};
        for(int i=0;i<rate;++i){a=step();b=ref.next();}
        if(!exact(a,b))return 4;
        // Upper bounds allow float rounding; a hard switch would be
        // roughly 0.02 direct/0.14 early here, orders of magnitude larger.
        if(maxDirect>.0001f||maxEarly>.0003f)return 5;
        lab.retargetCoherentStereo(180.f,120.f,-120.f,.4f,true);
        for(int i=0;i<rate/3;++i)lab.next();
        // OFF follows the pre-existing exact-unity contract, not a new
        // crossfade policy. Fresh protected scenes remain unchanged.
        lab.retarget(180.f,false);
        if(!exact(lab.next(),Gains{}))return 6;
        lab.reset();ref.reset();
        lab.retargetStereo(-45.f,45.f,true);ref.retargetStereo(-45.f,45.f,true);
        for(int i=0;i<rate;++i)if(!exact(lab.next(),ref.next()))return 7;
        std::printf("P106_DEPTH_TRANSITION_PASS %d maxDirect=%.9f maxEarly=%.9f\n",rate,maxDirect,maxEarly);
    }
}
