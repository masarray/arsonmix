// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "FxOrderPlan.h"
#include <array>
#include <cstdint>

// CONTROL-THREAD only: convert an ordered 8-slot physical rack into a
// normalized snapshot while preserving exact plugin instance identity.
// -1 represents a newly appended plugin; no plugin pointer travels in RT.
// Invalid permutations fail closed. A removed plugin stays owned by the old
// snapshot until the audio callback acknowledges graph retirement.
namespace armix::dsp {
struct FxStructuralEditPlan final {
    static constexpr int capacity=8;
    std::array<int,capacity> sourcePhysical{};
    int count=0;
    bool valid=false;

    static FxStructuralEditPlan make(std::uint64_t packed,int oldCount,
                                     int removedLogical,int appendCount) noexcept {
        FxStructuralEditPlan result;
        result.sourcePhysical.fill(-2);
        if(oldCount<0||oldCount>capacity ||
           removedLogical < -1||removedLogical>=oldCount ||
           appendCount<0||appendCount>1 ||
           oldCount-(removedLogical>=0?1:0)+appendCount>capacity)return result;
        std::array<bool,capacity> seen{};
        for(int logical=0;logical<oldCount;++logical){
            const int physical=FxOrderPlan::physicalAt(packed,logical);
            if(physical<0||physical>=oldCount||seen[physical])return result;
            seen[physical]=true;
            if(logical!=removedLogical)
                result.sourcePhysical[result.count++]=physical;
        }
        if(appendCount)result.sourcePhysical[result.count++]=-1;
        result.valid=true;
        return result;
    }
};
} // namespace armix::dsp
