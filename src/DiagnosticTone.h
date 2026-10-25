// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace armix::diagnostics
{
// Diagnostic routes are temporary, never persisted as the chosen user device.
enum class Route : int { normal = 0, dryThrough = 1, outputOnlyTone = 2, captureOnly = 3 };

inline constexpr const char* routeName (Route route) noexcept
{
    switch (route)
    {
        case Route::normal:         return "NORMAL";
        case Route::dryThrough:     return "DRY_THROUGH";
        case Route::outputOnlyTone: return "OUTPUT_ONLY_TONE";
        case Route::captureOnly:    return "CAPTURE_ONLY";
    }
    return "INVALID";
}

// Output-only 440 Hz at -30 dBFS. O(1) memory and O(1) work per sample,
// no trigonometric function, allocation, synchronization, or UI call in RT.
// Prepare/reset is invoked only while the audio device callback is stopped.
// A short ramp prevents an abrupt jump when monitoring is armed or muted.
class DiagnosticTone final
{
public:
    void prepare (double sampleRate) noexcept
    {
        const auto rate = std::max (1.0, sampleRate);
        const double delta = 2.0 * 3.14159265358979323846 * 440.0 / rate;
        stepCos_ = std::cos (delta);
        stepSin_ = std::sin (delta);
        rampStep_ = 1.0 / std::max (1.0, 0.008 * rate);
        sin_ = 0.0;
        cos_ = 1.0;
        gain_ = 0.0;
        cycles_ = 0;
    }

    float next (bool audible) noexcept
    {
        gain_ = audible ? std::min (1.0, gain_ + rampStep_)
                        : std::max (0.0, gain_ - rampStep_);
        const float output = static_cast<float> (sin_ * gain_ * 0.0316227766);
        const double nextSin = sin_ * stepCos_ + cos_ * stepSin_;
        const double nextCos = cos_ * stepCos_ - sin_ * stepSin_;
        sin_ = nextSin;
        cos_ = nextCos;
        // Bound oscillator round-off in long-running hardware tests.
        if ((++cycles_ & 4095u) == 0u)
        {
            const double norm = 1.0 / std::sqrt (sin_ * sin_ + cos_ * cos_);
            sin_ *= norm;
            cos_ *= norm;
        }
        return output;
    }

private:
    double sin_ = 0.0, cos_ = 1.0, stepSin_ = 0.0, stepCos_ = 1.0;
    double gain_ = 0.0, rampStep_ = 0.0;
    std::uint64_t cycles_ = 0;
};
} // namespace armix::diagnostics
