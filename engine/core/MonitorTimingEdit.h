// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicPreparedMixerBlock.h"
#include <array>
#include <span>
#include <bitset>
namespace armix::mixer {
// CONTROL only. The current graph and physical pair must be verified by the
// same native callback. No browser-only policy or inferred output identity.
struct MonitorTimingEdit final {
    using Pair=std::array<int,2>;
    static bool allowed(bool disarmed,bool observed,bool hasMonitor,
            MonitorLatencyPolicy current,MonitorLatencyPolicy wanted,
            std::span<const int> physical,Pair main,Pair monitor,
            Pair expectedMain,Pair expectedMonitor) noexcept {
        if(!disarmed||!observed||!hasMonitor||current==wanted||
           (current!=MonitorLatencyPolicy::aligned&&current!=MonitorLatencyPolicy::independent)||
           (wanted!=MonitorLatencyPolicy::aligned&&wanted!=MonitorLatencyPolicy::independent)||
           physical.size()<4||physical.size()>256)return false;
        std::bitset<256> seen;
        for(int pin:physical){
            if(pin<0||pin>255||seen[static_cast<std::size_t>(pin)])return false;
            seen.set(static_cast<std::size_t>(pin));
        }
        const auto valid=[&](Pair pair){
            return pair[0]>=0&&pair[1]>=0&&pair[0]!=pair[1]&&
                pair[0]<static_cast<int>(physical.size())&&pair[1]<static_cast<int>(physical.size());
        };
        return valid(main)&&valid(monitor)&&main[0]!=monitor[0]&&main[0]!=monitor[1]&&
            main[1]!=monitor[0]&&main[1]!=monitor[1]&&
            expectedMain==Pair{physical[main[0]],physical[main[1]]}&&
            expectedMonitor==Pair{physical[monitor[0]],physical[monitor[1]]};
    }
};
}
