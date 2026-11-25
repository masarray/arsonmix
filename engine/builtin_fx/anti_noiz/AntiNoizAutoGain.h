// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
namespace armix::builtin_fx::anti_noiz {
// Bounded heuristic confidence, not a calibrated signal-presence probability.
// No state, allocation, transcendental calls, or per-sample work.
struct AutoGain final {
    static float activeFloor(float floor,float broadbandRatio) noexcept {
        // Weak foreground stays transparent; increase selective authority only
        // with substantial broadband evidence above the learned reference.
        const float authority=std::clamp((broadbandRatio-4.f)*.125f,0.f,1.f);
        return 1.f-authority*(1.f-floor);
    }
    // Spectrally concentrated foreground may sit below the full-band gate.
    // Require stronger local evidence than the broadband weak-signal guard.
    // The eight-bin group is already evaluated by ContextGuard.
    static float activeFloor(float floor,float broadbandRatio,float subbandPeak) noexcept {
        const float local=std::clamp((subbandPeak-5.f)*(1.f/15.f),0.f,1.f);
        return std::min(activeFloor(floor,broadbandRatio),1.f-local*(1.f-floor));
    }
    // A coherent neighborhood can protect weaker bins near a stronger partial.
    // This is a bounded gain guard, not speech probability or source separation.
    static float voiceProtection(float temporalRatio,float instantaneousRatio) noexcept {
        const float support=std::clamp((temporalRatio-2.6f)/2.4f,0.f,1.f);
        const float present=std::clamp((instantaneousRatio-1.f)*.5f,0.f,1.f);
        return .85f*support*present;
    }
    static float target(float floor,float temporalRatio,float instantaneousRatio) noexcept {
        // Temporal neighboring evidence protects tonal sustain; a fresh
        // strong bin opens immediately even before the temporal guard catches up.
        const float noiseConfidence=std::clamp((2.6f-temporalRatio)/1.4f,0.f,1.f);
        const float onset=std::clamp((instantaneousRatio-3.f)*.25f,0.f,1.f);
        return 1.f-(1.f-floor)*noiseConfidence*(1.f-onset);
    }
};
} // namespace armix::builtin_fx::anti_noiz
