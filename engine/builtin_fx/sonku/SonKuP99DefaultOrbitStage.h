// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuSpatialCore.h"

// P9.9 PRIVATE factory default. A type-safe ONE-place native reference
// for brand-new 8D-SonKu inserts only. Do NOT migrate operator .smix or
// alter approved Venue/Signature P9.8 checkpoints and their PCM.
// Values are transcribed from 2026-11-16 operator screenshot:
// ORBIT/CLOCKWISE, 12s, 127°, 100% MIX, +5.1dB (1.8 linear),
// Large 28.8m room, 2.7m, 100% ER, 77% late. The screenshot does not
// disclose the hidden v12 sceneVoicing byte or orbit starting phase;
// explicitly choose active Signature ID1 and FRONT 0° so the new private
// vocal-room algorithm actually runs and playback starts deterministically.
namespace armix::sonku::p99 {
struct DefaultOrbitStage final {
    static constexpr float startAzimuth=0.f;
    static constexpr float revolutionSeconds=12.f;
    static constexpr float spreadDegrees=127.f;
    static constexpr float outputGain=1.8f;
    static constexpr float sourceDistanceMeters=2.7f;
    static constexpr float reflections=1.f;
    static constexpr float reverb=.77f;
    static constexpr int voicing=1; // typed v12 Signature; Venue is 0
    static constexpr int room=3; // Large 28.8 x 28.8
    static Settings settings() noexcept {
        Settings s{};
        s.renderMode=9;
        s.sceneVoicing=voicing;
        s.startAzimuthDegrees=startAzimuth;
        s.moving=true;
        s.clockwise=true;
        s.revolutionSeconds=revolutionSeconds;
        s.stereoSpreadDegrees=spreadDegrees;
        s.spatialMix=1.f;
        s.outputTrim=outputGain;
        s.spacePreset=room;
        s.sourceDistanceMeters=sourceDistanceMeters;
        s.roomAmount=reflections;
        s.ambience=reverb;
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
        s.directElevationDegrees=0.f;
#endif
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
        s.hrtfElevationTrimDegrees=0.f;
#endif
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        s.musicCompatibleLowFrequency=false;
        s.headYawDegrees=0.f;
#endif
        return s;
    }
};
} // namespace armix::sonku::p99
