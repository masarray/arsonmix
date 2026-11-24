// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <span>
#include <optional>
#include <algorithm>

namespace armix::io {
// CONTROL only: physical port IDs are not callback ordinals.
// No graph mutation, output arm, device open or implicit bus creation.
struct OutputBusPatch final {
    using Pair=std::array<int,2>;
    struct Result { Pair main,monitor; };
    static std::optional<Result> plan(std::span<const int> physical,
        Pair main,Pair monitor,bool editMonitor,Pair expected,Pair wanted,
        std::span<const int> external) noexcept {
        if(physical.size()<2||physical.size()>256)return {};
        const auto valid=[&](Pair pair){
            return pair[0]>=0&&pair[1]>=0&&pair[0]!=pair[1]&&
                pair[0]<static_cast<int>(physical.size())&&
                pair[1]<static_cast<int>(physical.size());
        };
        const bool hasMonitor=monitor!=Pair{-1,-1};
        const auto overlap=[](Pair x,Pair y){
            return x[0]==y[0]||x[0]==y[1]||x[1]==y[0]||x[1]==y[1];
        };
        if(!valid(main)||(hasMonitor&&(!valid(monitor)||overlap(main,monitor)))||
           (editMonitor&&!hasMonitor))return {};
        const auto old=editMonitor?monitor:main;
        if(expected!=Pair{physical[old[0]],physical[old[1]]})return {};
        // First production slice: conventional ASIO stereo pairs only.
        if(wanted[0]<0||wanted[0]>254||wanted[0]%2!=0||
           wanted[1]!=wanted[0]+1||wanted==expected)return {};
        Pair next{-1,-1};
        for(int k=0;k<2;++k){
            if(std::count(physical.begin(),physical.end(),wanted[k])!=1||
               std::find(external.begin(),external.end(),wanted[k])!=external.end())return {};
            next[k]=static_cast<int>(std::find(physical.begin(),physical.end(),wanted[k])-physical.begin());
        }
        if(!valid(next))return {};
        if(editMonitor){if(overlap(main,next))return {};monitor=next;}
        else {if(hasMonitor&&overlap(monitor,next))return {};main=next;}
        return Result{main,monitor};
    }
};
}
