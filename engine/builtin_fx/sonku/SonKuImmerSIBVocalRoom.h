// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuP99MusicalVocalDepth.h"
#include <algorithm>
#include <cmath>

// ImmerSIB-only conservative adapter over the SAME approved P9.9 math.
// No second PCM/HRTF graph, no spectral duplicate, no stem inference.
// P9.8 warm ID4/5 map to donor's Signature P9.9 center/room logic,
// but blend a bounded fraction so existing P9.8 body is not doubled.
// A hard disengage (ROOM OFF/MIX0) returns original floats exactly.
namespace armix::sonku::p99 {
class ImmerSIBVocalRoom final {
public:
    using Stereo=MusicalVocalDepth::Stereo;
    bool prepare(double rate) noexcept {
        if(!donor_.prepare(rate))return false;
        // ~35ms smooth transition between warm profiles, not per-phoneme AGC.
        strengthSlew_=static_cast<float>(1.-std::exp(-1./(rate*.035)));
        clarity1000_=bandpass(rate,1050.,1.0);
        clarity5000_=bandpass(rate,4900.,1.25);
        reset();
        return true;
    }
    void enableConcertLateClarity(bool enabled) noexcept {
        concertLateClarity_=enabled;
    }
    void reset() noexcept {
        donor_.reset();strength_=0.f;
        clarity1000State_={};clarity5000State_={};
    }
    Stereo source(float left,float right,int scene,bool enabled) noexcept {
        const float goal=enabled?(scene==4?.42f:(scene==5?.34f:0.f)):0.f;
        strength_+=strengthSlew_*(goal-strength_);
        if(!enabled || goal==0.f) {
            (void)donor_.source(0.f,0.f,0,false);
            return {left,right};
        }
        // Use donor Signature's accepted center attention, 210-880Hz
        // warmth and 330Hz room coupling, but at ImmerSIB-safe weight.
        const auto shaped=donor_.source(left,right,1,true);
        return {left+strength_*(shaped.left-left),
                right+strength_*(shaped.right-right)};
    }
    Stereo roomEarlyFeed(float left,float right) const noexcept {
        const auto r=donor_.roomEarlyFeed(left,right);
        return {left+strength_*(r.left-left),right+strength_*(r.right-right)};
    }
    Stereo roomLateFeed(float left,float right) const noexcept {
        const auto r=donor_.roomLateFeed(left,right);
        return {left+strength_*(r.left-left),right+strength_*(r.right-right)};
    }
    Stereo early(float left,float right) noexcept {
        const auto r=donor_.early(left,right);
        return {left+strength_*(r.left-left),right+strength_*(r.right-right)};
    }
    Stereo late(float left,float right) noexcept {
        const auto r=donor_.late(left,right);
        if(!concertLateClarity_ || strength_==0.f)
            return {left+strength_*(r.left-left),
                    right+strength_*(r.right-right)};
        // Vocal intelligibility: ease center-correlated 1k/5k LATE
        // room masking. DIRECT source and 200-300Hz body stay intact;
        // no new dry path, stereo widening or sibilance boost.
        const float mid=.5f*(left+right);
        const float presence=clarity1000State_.run(clarity1000_,mid);
        const float diction=clarity5000State_.run(clarity5000_,mid);
        const float relief=strength_*(.23f*presence+.19f*diction);
        return {left+strength_*(r.left-left)-relief,
                right+strength_*(r.right-right)-relief};
    }
private:
    struct Coeff {float b0=0.f,b1=0.f,b2=0.f,a1=0.f,a2=0.f;};
    struct Filter {
        float z1=0.f,z2=0.f;
        float run(const Coeff& c,float x) noexcept {
            const float y=c.b0*x+z1;
            z1=c.b1*x-c.a1*y+z2;
            z2=c.b2*x-c.a2*y;
            return y;
        }
    };
    static Coeff bandpass(double rate,double hz,double q) noexcept {
        constexpr double pi=3.14159265358979323846;
        const double w=2.*pi*hz/rate;
        const double a=std::sin(w)/(2.*q);
        const double inv=1./(1.+a);
        return {static_cast<float>(a*inv),0.f,
                static_cast<float>(-a*inv),
                static_cast<float>(-2.*std::cos(w)*inv),
                static_cast<float>((1.-a)*inv)};
    }
    MusicalVocalDepth donor_{}; // exact main/P9.9 source, no forked EQ
    Coeff clarity1000_{},clarity5000_{};
    Filter clarity1000State_{},clarity5000State_{};
    float strengthSlew_=0.f,strength_=0.f;
    bool concertLateClarity_=false;
};
} // namespace armix::sonku::p99
