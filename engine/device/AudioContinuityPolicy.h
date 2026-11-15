// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>

namespace armix::device {

// UI/display power is deliberately NOT audio power. These notifications must
// never change the operator's mixer graph, MAIN intent or driver selection.
enum class NativePowerEvent : std::uint8_t {
    displayOff, displayOn, systemSuspend, systemResume
};
struct AudioPowerPolicy final {
    static constexpr bool inspectDevice(NativePowerEvent event) noexcept {
        return event == NativePowerEvent::systemResume;
    }
    static constexpr bool changeMixerIntent(NativePowerEvent) noexcept {
        return false;
    }
};

// Desired MAIN/monitoring state belongs to the operator, not the JUCE device
// callback lifetime. Only a user mute/explicit stop clears it. Hardware loss
// gates physical output separately; same verified endpoint may resume later.
// A real MAIN mute and a safety feedback latch remain independent of this.
class OutputContinuityIntent final {
public:
    void operatorRequested(bool active) noexcept {
        desired_.store(active, std::memory_order_release);
        interrupted_.store(false, std::memory_order_release);
    }
    void deviceInterrupted() noexcept {
        if (desired_.load(std::memory_order_acquire))
            interrupted_.store(true, std::memory_order_release);
    }
    void deviceRecovered() noexcept {
        interrupted_.store(false, std::memory_order_release);
    }
    bool desired() const noexcept {
        return desired_.load(std::memory_order_acquire);
    }
    bool interrupted() const noexcept {
        return interrupted_.load(std::memory_order_acquire);
    }
private:
    std::atomic<bool> desired_{false}, interrupted_{false};
};

// CONTROL-thread scheduler: event-driven, non-blocking and generation-safe.
// A device can be absent for hours; never exhaust all attempts permanently,
// spin on each 12Hz tick, or substitute the Windows default audio endpoint.
class AudioContinuityRetry final {
public:
    void fault(std::uint64_t now) noexcept {
        if (pending_) return;
        pending_ = true;
        attempts_ = 0;
        dueMs_ = now + 350;
        ++generation_;
    }
    void endpointReturned(std::uint64_t now) noexcept {
        if (pending_) dueMs_ = now + 350;
    }
    bool due(std::uint64_t now) const noexcept {
        return pending_ && now >= dueMs_;
    }
    void failed(std::uint64_t now) noexcept {
        if (!pending_) return;
        ++attempts_;
        const auto waitMs = attempts_ == 1 ? 1000ull
            : attempts_ == 2 ? 3000ull
            : attempts_ == 3 ? 8000ull : 30000ull;
        dueMs_ = now > UINT64_MAX - waitMs ? UINT64_MAX : now + waitMs;
    }
    void recovered() noexcept { cancel(); }
    void cancel() noexcept {
        ++generation_;
        pending_ = false;
        attempts_ = 0;
        dueMs_ = 0;
    }
    bool pending() const noexcept { return pending_; }
    unsigned attempts() const noexcept { return attempts_; }
    std::uint64_t generation() const noexcept { return generation_; }
private:
    bool pending_ = false;
    unsigned attempts_ = 0;
    std::uint64_t generation_ = 0, dueMs_ = 0;
};
} // namespace armix::device
