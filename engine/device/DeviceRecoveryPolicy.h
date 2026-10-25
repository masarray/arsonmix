// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace armix::device
{
// Pure control-thread state machine. All callbacks pass generation tokens;
// delayed asynchronous completions from retired devices cannot revive a stale
// session. This module NEVER opens devices or touches the audio callback.
enum class SessionState : std::uint8_t
{
    stopped, discovering, preparing, running, degraded, recovering, failed
};
enum class DeviceEvent : std::uint8_t
{
    start, discoveryOk, discoveryFailed, preparationOk, preparationFailed,
    callbackHealthy, instability, endpointLost, recover, stop
};
enum class RecoveryAction : std::uint8_t
{
    none, enumerateDevices, prepareCandidate, publishCandidate,
    silenceAndRetire, scheduleRetry, persistStable, reportFailure
};
struct RecoveryDecision
{
    SessionState state = SessionState::stopped;
    RecoveryAction action = RecoveryAction::none;
    std::uint64_t generation = 0;
    std::uint64_t retryAfterMs = 0;
    bool accepted = false;
};
class DeviceRecoveryPolicy final
{
public:
    // One controller, one control-thread owner. nowMs must be monotonic.
    RecoveryDecision apply (DeviceEvent event, std::uint64_t token, std::uint64_t nowMs) noexcept
    {
        if (event == DeviceEvent::stop)
        {
            ++generation_; state_ = SessionState::stopped; consecutiveFailures_ = 0;
            stableSince_ = 0; nextRetry_ = 0;
            return result (RecoveryAction::silenceAndRetire, 0, true);
        }
        if (event == DeviceEvent::start)
        {
            if (state_ != SessionState::stopped && state_ != SessionState::failed)
                return result (RecoveryAction::none, 0, false);
            ++generation_; state_ = SessionState::discovering;
            consecutiveFailures_ = 0; stableSince_ = 0; nextRetry_ = 0;
            return result (RecoveryAction::enumerateDevices, 0, true);
        }
        if (token != generation_) return result (RecoveryAction::none, 0, false);

        switch (event)
        {
            case DeviceEvent::discoveryOk:
                if (state_ != SessionState::discovering) break;
                state_ = SessionState::preparing;
                return result (RecoveryAction::prepareCandidate, 0, true);
            case DeviceEvent::preparationOk:
                if (state_ != SessionState::preparing) break;
                state_ = SessionState::running; consecutiveFailures_ = 0;
                stableSince_ = nowMs;
                return result (RecoveryAction::publishCandidate, 0, true);
            case DeviceEvent::callbackHealthy:
                if (state_ == SessionState::running && stableSince_ != 0
                    && nowMs >= stableSince_ && nowMs - stableSince_ >= 30000)
                {
                    stableSince_ = 0; // one stable event per generation
                    return result (RecoveryAction::persistStable, 0, true);
                }
                if (state_ == SessionState::degraded)
                {
                    state_ = SessionState::running;
                    stableSince_ = nowMs;
                    return result (RecoveryAction::none, 0, true);
                }
                break;
            case DeviceEvent::instability:
                if (state_ == SessionState::running)
                {
                    state_ = SessionState::degraded; stableSince_ = 0;
                    return result (RecoveryAction::none, 0, true);
                }
                break;
            case DeviceEvent::endpointLost:
                if (state_ == SessionState::running || state_ == SessionState::degraded
                    || state_ == SessionState::preparing || state_ == SessionState::discovering)
                {
                    ++generation_; state_ = SessionState::recovering;
                    nextRetry_ = nowMs; stableSince_ = 0;
                    return result (RecoveryAction::silenceAndRetire, 0, true);
                }
                break;
            case DeviceEvent::discoveryFailed:
            case DeviceEvent::preparationFailed:
                if (state_ == SessionState::discovering || state_ == SessionState::preparing)
                {
                    ++consecutiveFailures_;
                    if (consecutiveFailures_ >= 8)
                    {
                        ++generation_; state_ = SessionState::failed; nextRetry_ = 0;
                        return result (RecoveryAction::reportFailure, 0, true);
                    }
                    ++generation_; state_ = SessionState::recovering;
                    // Bounded backoff, 100ms to 10s; no uncontrolled spin.
                    const auto shift = std::min (consecutiveFailures_ - 1, 7u);
                    const auto delay = std::min<std::uint64_t> (10000, 100ull << shift);
                    nextRetry_ = nowMs + delay;
                    return result (RecoveryAction::scheduleRetry, delay, true);
                }
                break;
            case DeviceEvent::recover:
                if (state_ == SessionState::recovering && nowMs >= nextRetry_)
                {
                    state_ = SessionState::discovering;
                    return result (RecoveryAction::enumerateDevices, 0, true);
                }
                break;
            default: break;
        }
        return result (RecoveryAction::none, 0, false);
    }
    std::uint64_t generation() const noexcept { return generation_; }
    SessionState state() const noexcept { return state_; }
    std::uint64_t nextRetryMs() const noexcept { return nextRetry_; }
private:
    RecoveryDecision result (RecoveryAction action, std::uint64_t delay, bool accepted) const noexcept
    { return {state_,action,generation_,delay,accepted}; }
    SessionState state_ = SessionState::stopped;
    std::uint64_t generation_ = 0, stableSince_ = 0, nextRetry_ = 0;
    unsigned consecutiveFailures_ = 0;
};
} // namespace armix::device
