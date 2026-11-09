// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

// A tiny callback-local sample-clock context, NOT a transport or second
// callback. The native ChannelId publisher owns the authoritative frame
// counter. JUCE's built-in VocalThrow reads the same context only while
// it is rendering as an insert on that callback thread. No heap or locks.
namespace armix::mixer {
struct NativeBeatFrameContext final {
    std::uint64_t epoch=0,revision=0,startFrame=0;
    bool valid=false;
};
inline thread_local NativeBeatFrameContext currentNativeBeatFrame{};

class NativeBeatFrameScope final {
public:
    explicit NativeBeatFrameScope(std::uint64_t epoch,std::uint64_t revision,
                                  std::uint64_t startFrame) noexcept
       : previous_(currentNativeBeatFrame) {
        currentNativeBeatFrame={epoch,revision,startFrame,epoch!=0&&revision!=0};
    }
    ~NativeBeatFrameScope() noexcept {currentNativeBeatFrame=previous_;}
    NativeBeatFrameScope(const NativeBeatFrameScope&)=delete;
    NativeBeatFrameScope& operator=(const NativeBeatFrameScope&)=delete;
    static NativeBeatFrameContext current() noexcept {
        return currentNativeBeatFrame;
    }
private:
    NativeBeatFrameContext previous_{};
};
} // namespace armix::mixer
