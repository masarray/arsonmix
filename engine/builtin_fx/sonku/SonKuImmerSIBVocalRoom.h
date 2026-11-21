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
        reset();
        return true;
    }
    void reset() noexcept {donor_.reset();strength_=0.f;}
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
        return {left+strength_*(r.left-left),right+strength_*(r.right-right)};
    }
private:
    MusicalVocalDepth donor_{}; // exact main/P9.9 source, no forked EQ
    float strengthSlew_=0.f,strength_=0.f;
};
} // namespace armix::sonku::p99
