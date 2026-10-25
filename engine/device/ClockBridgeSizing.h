// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace armix::device
{
// CONTROL THREAD only. A render clock requesting a larger block than the
// reservoir headroom guarantees repeated FIR re-primes even for healthy
// capture timing. Avoid arbitrary latency tuning by checking exact rates.
struct ClockBridgeSizing
{
    struct Decision
    {
        bool valid = false;
        double targetMs = 0.;
        std::uint64_t minFrames = 0;
        std::uint64_t renderSourceFrames = 0;
    };

    static Decision choose (double captureHz, double renderHz,
                            int capturePeriod, int renderPeriod,
                            int firTaps, int capacityFrames,
                            double requestedTargetMs) noexcept
    {
        Decision d;
        if (!std::isfinite (captureHz) || !std::isfinite (renderHz)
            || captureHz < 8000 || renderHz < 8000
            || capturePeriod < 1 || renderPeriod < 1
            || firTaps < 1 || capacityFrames < 256
            || !std::isfinite (requestedTargetMs)
            || requestedTargetMs < 5. || requestedTargetMs > 100.)
            return d;

        const double renderSource = std::ceil (
            static_cast<double> (renderPeriod) * captureHz / renderHz);
        if (renderSource > static_cast<double> (capacityFrames))
            return d;

        d.renderSourceFrames = static_cast<std::uint64_t> (renderSource);
        // One whole render quantum, one refill/jitter margin (at least one
        // capture packet), and full FIR history plus two guard frames.
        d.minFrames = d.renderSourceFrames +
            std::max (d.renderSourceFrames,
                      static_cast<std::uint64_t> (capturePeriod)) +
            static_cast<std::uint64_t> (firTaps * 2 + 4);
        d.targetMs = std::max (requestedTargetMs,
            1000.0 * static_cast<double> (d.minFrames) / captureHz);
        // The existing SPSC bridge deliberately keeps target occupancy in
        // the lower half of its capacity; reject instead of silently clamp.
        const auto maximumFill = static_cast<std::uint64_t> (capacityFrames / 2);
        const auto guard = static_cast<std::uint64_t> (firTaps * 2);
        d.valid = d.targetMs <= 180. &&
            d.minFrames + guard < maximumFill &&
            static_cast<std::uint64_t> (std::ceil (captureHz*d.targetMs/1000.)) + guard < maximumFill;
        return d;
    }
};
} // namespace armix::device
