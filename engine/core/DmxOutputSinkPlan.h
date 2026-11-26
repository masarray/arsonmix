// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
namespace armix::mixer {
// {-1,-1} MAIN is legal only for secondary-only MAIN, with the sole
// physical stereo pair exclusively owned by MONITOR.
struct DmxOutputSinkPlan final {
    using Pair=std::array<int,2>;
    static constexpr Pair absent{-1,-1};
    int count=0;
    Pair main=absent,monitor=absent;
    bool monitorEnabled=false;
    constexpr bool secondaryMain() const noexcept {
        return count==2&&main==absent&&monitorEnabled&&
               ((monitor==Pair{0,1})||(monitor==Pair{1,0}));
    }
    constexpr bool valid() const noexcept {
        if(count<2||count>256)return false;
        if(secondaryMain())return true;
        const auto pairValid=[&](Pair p){return p[0]>=0&&p[1]>=0&&p[0]<count&&p[1]<count&&p[0]!=p[1];};
        if(!pairValid(main))return false;
        if(!monitorEnabled)return monitor==absent;
        return pairValid(monitor)&&monitor[0]!=main[0]&&monitor[0]!=main[1]&&
               monitor[1]!=main[0]&&monitor[1]!=main[1];
    }
    constexpr std::array<int,4> reserved() const noexcept {
        return secondaryMain()?std::array<int,4>{monitor[0],monitor[1],-1,-1}:
            std::array<int,4>{main[0],main[1],monitor[0],monitor[1]};
    }
    constexpr std::size_t reservedCount() const noexcept {
        return secondaryMain()||!monitorEnabled?2U:4U;
    }
};
}
