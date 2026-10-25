// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

namespace armix::dsp
{
// Branch selection occurs once per buffer, not inside the sample loop.
// Center: transparent stereo identity.
// Extremes: both channels fold into the selected output with 0.5 weights,
// so dual-mono input stays at unity instead of gaining +6 dB.
struct StereoPanMatrix final
{
    float ll = 1.f, lr = 0.f, rl = 0.f, rr = 1.f;

    static StereoPanMatrix fromNormalized (float p) noexcept
    {
        if (!std::isfinite (p)) p = 0.f;
        p = std::clamp (p, -1.f, 1.f);
        return {
            p > 0.f ? 1.f - p : 1.f + .5f * p,
            p < 0.f ? -.5f * p : 0.f,
            p > 0.f ? .5f * p : 0.f,
            p < 0.f ? 1.f + p : 1.f - .5f * p
        };
    }
};
} // namespace armix::dsp
