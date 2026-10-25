// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>

namespace armix::device
{
// Message/control-thread ONLY. No OS or JUCE dependencies and no timers owned.
// Models bounded, generation-safe retry of the previously selected DIRECT
// endpoint after an independent WASAPI failure. Never auto-reopens independent
// capture/render, so a fault cannot cause an endless device-flapping loop.
class DirectRestoreBackoff final
{
public:
    enum class State : std::uint8_t { idle, waiting, exhausted };
    struct Snapshot
    {
        State state = State::idle;
        unsigned attempts = 0;
        std::uint64_t nextDueMs = 0;
        std::uint64_t generation = 0;
    };
    // Initial attempt runs immediately. Four subsequent retries are spaced
    // without sleeping or touching the audio callback.
    static constexpr std::array<std::uint64_t, 4> delaysMs { 250, 1000, 3000, 8000 };
    static constexpr unsigned maxAttempts = static_cast<unsigned> (delaysMs.size()) + 1;

    std::uint64_t begin (std::uint64_t nowMs) noexcept
    {
        ++generation_;
        state_ = State::waiting;
        attempts_ = 0;
        nextDueMs_ = nowMs;
        return generation_;
    }

    bool due (std::uint64_t generation, std::uint64_t nowMs) const noexcept
    {
        return state_ == State::waiting && generation == generation_
            && attempts_ < maxAttempts && nowMs >= nextDueMs_;
    }

    bool failed (std::uint64_t generation, std::uint64_t nowMs) noexcept
    {
        if (!due (generation, nowMs)) return false;
        ++attempts_;
        if (attempts_ >= maxAttempts)
        {
            state_ = State::exhausted;
            nextDueMs_ = 0;
            return true;
        }
        // Next interval increases monotonically; no exponential overflow,
        // no wall-clock jumps or dependency on timer tick precision.
        nextDueMs_ = nowMs + delaysMs[attempts_ - 1];
        return true;
    }

    bool recovered (std::uint64_t generation) noexcept
    {
        if (generation != generation_ || state_ != State::waiting) return false;
        state_ = State::idle;
        nextDueMs_ = 0;
        attempts_ = 0;
        return true;
    }

    void cancel() noexcept
    {
        ++generation_; // invalidate pending async/device observations
        state_ = State::idle;
        attempts_ = 0;
        nextDueMs_ = 0;
    }

    Snapshot snapshot() const noexcept
    {
        return {state_, attempts_, nextDueMs_, generation_};
    }

    std::uint64_t remainingMs (std::uint64_t nowMs) const noexcept
    {
        return state_ == State::waiting && nowMs < nextDueMs_
            ? nextDueMs_ - nowMs : 0;
    }
private:
    State state_ = State::idle;
    unsigned attempts_ = 0;
    std::uint64_t generation_ = 0, nextDueMs_ = 0;
};
} // namespace armix::device
