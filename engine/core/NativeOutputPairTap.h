// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
namespace armix::mixer {
// RT-safe tap of the graph-selected callback output pair. Physical IDs
// were validated/mapped OFF RT; absent pins FAIL CLOSED, no 0/1 fallback.
struct NativeOutputPairTap final {
    static std::array<float*,2> select(float* const* outputs,int count,
                                       const std::array<int,2>& pair) noexcept {
        if(!outputs||count<2||pair[0]<0||pair[1]<0||
           pair[0]>=count||pair[1]>=count||pair[0]==pair[1])
            return {{nullptr,nullptr}};
        auto* const l=outputs[pair[0]];
        auto* const r=outputs[pair[1]];
        return l&&r?std::array<float*,2>{{l,r}}:
                    std::array<float*,2>{{nullptr,nullptr}};
    }
};
} // namespace armix::mixer
