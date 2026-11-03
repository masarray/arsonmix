// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
// One canonical packed stereo gain / MUTE / SOLO word for M4 and ChannelId.
// Only serialized CONTROL encodes; AUDIO loads once/block. 31+31-bit gains
// and 2 bits of gating state; 64-bit lock-free atomics prevent torn decisions.
namespace armix::dsp {
struct RealtimeStereoControlWord final {
    static constexpr std::uint64_t gainMask=(std::uint64_t{1}<<31)-1;
    static constexpr std::uint64_t mutedBit=std::uint64_t{1}<<62;
    static constexpr std::uint64_t soloBit=std::uint64_t{1}<<63;
    static constexpr std::uint64_t unity=536870912ULL;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "Realtime 64-bit control must never lock");
    struct Snapshot final {
        float left=1.f,right=1.f;
        bool mute=false,solo=false;
    };
    static std::uint64_t quantize(float value) noexcept {
        return static_cast<std::uint64_t>(
            static_cast<double>(value)*(static_cast<double>(gainMask)/4.)+.5);
    }
    static std::uint64_t encode(float left,float right,bool mute,bool solo) noexcept {
        return quantize(left)|(quantize(right)<<31)|
               (mute?mutedBit:0ULL)|(solo?soloBit:0ULL);
    }
    static Snapshot decode(std::uint64_t state) noexcept {
        constexpr double scale=4./static_cast<double>(gainMask);
        return {static_cast<float>(static_cast<double>(state&gainMask)*scale),
                static_cast<float>(static_cast<double>((state>>31)&gainMask)*scale),
                (state&mutedBit)!=0,(state&soloBit)!=0};
    }
};
} // namespace armix::dsp
