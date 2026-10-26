// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>

namespace armix::core
{
// Pure control-domain math. One ramp per revolution, not a sine (which
// reverses direction). Normalised 0 and 1 are the same spatial bearing
// only if the selected VST parameter describes a full 360-degree azimuth.
inline float orbitRamp (float start, double elapsedSeconds,
                        double revolutionSeconds, bool reverse) noexcept
{
    if (!std::isfinite (start) || !std::isfinite (elapsedSeconds) ||
        !std::isfinite (revolutionSeconds) || revolutionSeconds <= 0.0)
        return 0.0f;
    const auto progress = std::fmod (elapsedSeconds / revolutionSeconds, 1.0);
    double phase = std::fmod (static_cast<double> (start) +
                             (reverse ? -progress : progress), 1.0);
    if (phase < 0.0) phase += 1.0;
    return static_cast<float> (phase);
}
} // namespace armix::core
