// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>

// One gate across the real installed M4 renderer and the future ChannelId
// publisher. If a misbehaving device/backend overlaps callbacks, do not
// concurrently write prepared scratch or process the same VST3 instance.
// Nonblocking: failed acquisition is SILENCE, never a spin, mutex or wait.
// RAII releases on every fail-closed return after entering the DSP block.
namespace armix::rt {
class RealtimeRenderPermit final {
public:
    explicit RealtimeRenderPermit(std::atomic_flag& gate) noexcept
        : gate_(gate),
          entered_(!gate_.test_and_set(std::memory_order_acquire)) {}
    RealtimeRenderPermit(const RealtimeRenderPermit&)=delete;
    RealtimeRenderPermit& operator=(const RealtimeRenderPermit&)=delete;
    ~RealtimeRenderPermit() noexcept {
        if(entered_)gate_.clear(std::memory_order_release);
    }
    explicit operator bool() const noexcept {return entered_;}
private:
    std::atomic_flag& gate_;
    bool entered_;
};
} // namespace armix::rt
