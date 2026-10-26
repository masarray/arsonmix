// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
#include <cstdint>

namespace armix::rt
{
// Exact render callback-start timing evidence. It is NOT an OS/driver
// render-underrun counter; the host can pause, schedule irregularly, or buffer
// internally. One audio callback producer; prepare()/reset() ONLY in the JUCE
// device lifecycle after the preceding callback has stopped.
//
// Critical: the interval between START(n-1) and START(n) is compared to the
// period implied by FRAMES(n-1), never FRAMES(n). Variable JUCE block sizes
// otherwise introduce systematic false positives or false negatives.
class CallbackCadenceEvidence final
{
public:
    enum class Kind : std::uint8_t
    {
        first, onSchedule, late, veryLate, invalid, clockRegression
    };
    struct Observation
    {
        Kind kind = Kind::invalid;
        std::uint64_t actualIntervalUs = 0;
        double expectedIntervalUs = 0.0;
        double ratio = 0.0;
        bool comparable() const noexcept
        {
            return kind == Kind::onSchedule || kind == Kind::late
                || kind == Kind::veryLate;
        }
    };

    void prepare(double sampleRateHz) noexcept
    {
        rateHz_ = (std::isfinite(sampleRateHz) && sampleRateHz >= 8000.0
                   && sampleRateHz <= 384000.0) ? sampleRateHz : 0.0;
        previousStartedUs_ = 0;
        previousFrames_ = 0;
    }

    Observation observe(std::uint64_t startUs, int currentFrames) noexcept
    {
        if (startUs == 0 || currentFrames <= 0 || currentFrames > 65536
            || !(rateHz_ > 0.0))
        {
            // Do not compare across an invalid callback/clock epoch.
            previousStartedUs_ = 0;
            previousFrames_ = 0;
            return {};
        }

        if (previousStartedUs_ == 0)
        {
            previousStartedUs_ = startUs;
            previousFrames_ = currentFrames;
            return {Kind::first};
        }

        const auto previousTime = previousStartedUs_;
        const auto previousFrames = previousFrames_;
        previousStartedUs_ = startUs;
        previousFrames_ = currentFrames;
        if (startUs <= previousTime)
            return {Kind::clockRegression};

        const auto intervalUs = startUs - previousTime;
        const double expectedUs = 1000000.0 * previousFrames / rateHz_;
        if (!(expectedUs > 0.0) || !std::isfinite(expectedUs))
            return {};

        const double ratio = static_cast<double>(intervalUs) / expectedUs;
        return {ratio > 2.3 ? Kind::veryLate
                  : ratio > 1.5 ? Kind::late : Kind::onSchedule,
                intervalUs, expectedUs, ratio};
    }

    static const char* provenance() noexcept
    {
        return "HOST_MONOTONIC_CALLBACK_STARTS_PREVIOUS_BLOCK_PERIOD_NOT_RENDER_XRUN";
    }
private:
    double rateHz_ = 0.0; // immutable while callbacks run
    std::uint64_t previousStartedUs_ = 0;
    int previousFrames_ = 0;
};
} // namespace armix::rt
