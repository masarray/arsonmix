// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>

namespace armix::device
{
// Pure control-thread state machine. No OS handles or allocation in this class.
// A generation-bound transaction prevents stale hot-plug notifications from
// resurrecting retired endpoints. The caller owns actual backend start/stop.
class SessionLifecycle final
{
public:
    enum class State : std::uint8_t
    {
        offline, preparing, priming, running, degraded, recovering, faulted
    };
    enum class Failure : std::uint8_t
    {
        none, unavailable, unsupported, startup, starvation, deviceLost
    };
    struct Snapshot
    {
        State state;
        Failure lastFailure;
        std::uint64_t generation;
        unsigned retries;
        bool outputAllowed;
    };

    std::uint64_t begin() noexcept
    {
        ++generation_;
        state_ = State::preparing;
        failure_ = Failure::none;
        outputAllowed_ = false;
        return generation_;
    }

    bool prepared(std::uint64_t generation) noexcept
    {
        if (!valid(generation, State::preparing)) return false;
        state_ = State::priming;
        return true;
    }

    bool ready(std::uint64_t generation) noexcept
    {
        if (!valid(generation, State::priming)) return false;
        state_ = State::running;
        outputAllowed_ = true;
        retries_ = 0;
        return true;
    }

    bool degrade(std::uint64_t generation, Failure reason) noexcept
    {
        if (generation != generation_ || state_ != State::running) return false;
        state_ = State::degraded;
        outputAllowed_ = false; // fail closed; caller ramps if audio still runs
        failure_ = reason;
        return true;
    }

    bool recover(std::uint64_t generation) noexcept
    {
        if (generation != generation_ ||
            (state_ != State::degraded && state_ != State::faulted) ||
            retries_ >= maxRetries) return false;
        ++retries_;
        state_ = State::recovering;
        outputAllowed_ = false;
        return true;
    }

    std::uint64_t retryPrepare(std::uint64_t generation) noexcept
    {
        if (!valid(generation, State::recovering)) return 0;
        ++generation_;
        state_ = State::preparing;
        return generation_;
    }

    bool fail(std::uint64_t generation, Failure reason) noexcept
    {
        if (generation != generation_ || state_ == State::offline) return false;
        state_ = State::faulted;
        outputAllowed_ = false;
        failure_ = reason;
        return true;
    }

    void stop() noexcept
    {
        ++generation_; // invalidate all outstanding async completion tokens
        state_ = State::offline;
        failure_ = Failure::none;
        retries_ = 0;
        outputAllowed_ = false;
    }

    Snapshot snapshot() const noexcept
    {
        return {state_,failure_,generation_,retries_,outputAllowed_};
    }

    static constexpr unsigned maxRetries = 3;
private:
    bool valid(std::uint64_t generation, State state) const noexcept
    {
        return generation == generation_ && state_ == state;
    }
    State state_ = State::offline;
    Failure failure_ = Failure::none;
    std::uint64_t generation_ = 0;
    unsigned retries_ = 0;
    bool outputAllowed_ = false;
};
} // namespace armix::device
