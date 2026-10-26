// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace armix::device
{
// Passive device-period evidence for the explicit user-triggered report.
// MUST NOT reopen/change a running JUCE device, infer physical round-trip
// latency, or claim driver-reported periods have passed hardware FAT.
// Pure C++20 and O(N), without allocation or sort.
struct PeriodCapabilityEvidence final
{
    struct Decision
    {
        bool validFormat = false;
        bool activePeriodAdvertised = false;
        int smallestReportedFrames = 0;
        int conservativeReportedFrames = 0;
        int activeFrames = 0;
        double nominalCallbackMs = 0.;
        double conservativeCallbackMs = 0.;
    };

    static Decision inspect(double actualRateHz, int activeFrames,
                            std::span<const int> driverPeriods) noexcept
    {
        Decision d;
        if (!std::isfinite(actualRateHz) || actualRateHz < 8000. ||
            actualRateHz > 384000. || activeFrames <= 0 ||
            activeFrames > 65536) return d;
        d.validFormat = true;
        d.activeFrames = activeFrames;
        d.nominalCallbackMs = 1000. * activeFrames / actualRateHz;
        const int targetFrames = static_cast<int>(std::ceil(actualRateHz / 100.));
        int largest = 0, candidate = 0;
        for (const int frames : driverPeriods)
        {
            if (frames <= 0 || frames > 65536) continue;
            d.activePeriodAdvertised |= (frames == activeFrames);
            if (d.smallestReportedFrames == 0 || frames < d.smallestReportedFrames)
                d.smallestReportedFrames = frames;
            largest = std::max(largest, frames);
            if (frames >= targetFrames && (candidate == 0 || frames < candidate))
                candidate = frames;
        }
        d.conservativeReportedFrames = candidate != 0 ? candidate : largest;
        if (d.conservativeReportedFrames > 0)
            d.conservativeCallbackMs =
                1000. * d.conservativeReportedFrames / actualRateHz;
        return d;
    }
};
} // namespace armix::device
