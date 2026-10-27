// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace armix::dsp
{
// Output-only de-click around a graph's single atomic order publication.
// 1 = fade old processing 1->0; 2 = fade new processing 0->1.
// A callback completes one stage, so channel outputs share exact envelope.
// Does not duplicate VST processing or allocate on the audio thread.
struct FxMoveRamp final
{
    static constexpr float gain (int stage, int sample, int frames) noexcept
    {
        if (stage != 1 && stage != 2) return 1.f;
        if (frames <= 1) return stage == 1 ? 0.f : 1.f;
        const float phase = static_cast<float>(sample) / static_cast<float>(frames-1);
        return stage == 1 ? 1.f-phase : phase;
    }
};
} // namespace armix::dsp
