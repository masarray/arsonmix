// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace armix::startup
{
// Presentation-only state and monotonic elapsed timings. No JUCE, timers,
// I/O, threads, device readiness claims, or ownership of the startup lifecycle.
// Negative timestamps mean that the corresponding event was not observed.
class SplashProgress final
{
public:
    constexpr int stage() const noexcept { return stage_; }
    constexpr float fraction() const noexcept
    {
        return static_cast<float> (stage_ + 1) / 3.0f;
    }

    constexpr bool advance (int stage, double elapsedMs) noexcept
    {
        // Caller owns error dismissal (stage 3); never turn it into success.
        if (stage <= stage_ || stage > 2)
            return false;
        stage_ = stage;
        if (stage == 1)
            guiHandshakeMs_ = elapsedMs;
        else
            handoffMs_ = elapsedMs;
        return true;
    }

    constexpr void markAssetsDecoded (double elapsedMs) noexcept
    {
        if (assetsDecodedMs_ < 0.0)
            assetsDecodedMs_ = elapsedMs;
    }

    constexpr void markFirstPaint (double elapsedMs) noexcept
    {
        if (firstPaintMs_ < 0.0)
            firstPaintMs_ = elapsedMs;
    }

    constexpr double assetsDecodedMs() const noexcept { return assetsDecodedMs_; }
    constexpr double firstPaintMs() const noexcept { return firstPaintMs_; }
    constexpr double guiHandshakeMs() const noexcept { return guiHandshakeMs_; }
    constexpr double handoffMs() const noexcept { return handoffMs_; }

private:
    int stage_ = 0;
    double assetsDecodedMs_ = -1.0;
    double firstPaintMs_ = -1.0;
    double guiHandshakeMs_ = -1.0;
    double handoffMs_ = -1.0;
};
}
