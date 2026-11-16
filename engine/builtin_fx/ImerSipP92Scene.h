// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <cmath>

// Thin ImerSip *product* scene, NOT another binaural DSP. Every PCM sample
// is rendered by the same owner-approved SonKu P9.1/P9.2 core (mode 9).
namespace armix::builtin_fx {
inline armix::sonku::Settings imersipP92ReferenceScene() noexcept {
    armix::sonku::Settings s{};
    s.renderMode=9;                    // P9.1/P9.2 ONE native PCM authority
    s.musicCompatibleLowFrequency=false;
    s.moving=false; s.clockwise=true;
    s.startAzimuthDegrees=0.f;        // static world-front speakers
    s.headYawDegrees=0.f;
    s.directElevationDegrees=0.f;
    s.hrtfElevationTrimDegrees=0.f;
    s.stereoSpreadDegrees=82.f;
    s.sourceDistanceMeters=1.6f;
    s.spacePreset=3;                  // Large 28.8 m, P9 listening reference
    s.roomAmount=.78f;               // P9 direct/wall architecture untouched
    s.ambience=.33f;
    s.spatialMix=1.f;
    s.outputTrim=1.f;                 // 0 dB, no P8 ImerSip shelf/limiter
    return s;
}
inline float imersipP92MaxDistance(int room) noexcept {
    // SonKu six-wall geometry requires 2*(distance+0.5) <= room size.
    return room==1?3.05f:room==2?6.65f:7.4f;
}
inline bool validImerSipP92Scene(const armix::sonku::Settings& s) noexcept {
    return armix::sonku::valid(s) && s.renderMode==9 &&
        !s.moving && !s.musicCompatibleLowFrequency &&
        s.startAzimuthDegrees==0.f &&
        s.sourceDistanceMeters<=imersipP92MaxDistance(s.spacePreset);
}
// Product presets are *scenes* of the unchanged SonKu P9.2 core.
// The original P9.2 neutral baseline is always accessible (id=6), so
// loudness-enhanced concert presets cannot erase reproducible A/B evidence.
// Reused IMRS v6 sessions restore their accepted state, NOT this factory.
inline float imersipP92DbToLinear(float db) noexcept {
    return std::pow(10.f,db/20.f);
}
inline armix::sonku::Settings imersipP92Factory(int id) noexcept {
    auto s=imersipP92ReferenceScene();
    switch(id) {
    case 2: // New insert: owner-led wide live concert, not timid near-dry
        s.stereoSpreadDegrees=150.f;
        s.sourceDistanceMeters=2.2f;
        s.roomAmount=1.f;
        s.ambience=.91f;
        s.outputTrim=imersipP92DbToLinear(4.8f);
        break;
    case 3: // Room warmth with defined direct source / less diffuse wash
        s.spacePreset=2; // Chamber 14.4 m
        s.stereoSpreadDegrees=138.f;
        s.sourceDistanceMeters=2.4f;
        s.roomAmount=.96f;
        s.ambience=.78f;
        s.outputTrim=imersipP92DbToLinear(4.5f);
        break;
    case 4: // Distance-biased grand room; avoid unlimited late-feedback
        s.stereoSpreadDegrees=164.f;
        s.sourceDistanceMeters=3.0f;
        s.roomAmount=1.f;
        s.ambience=.96f;
        s.outputTrim=imersipP92DbToLinear(5.6f);
        break;
    case 5: // Exact observed ImerSip screenshot, no inferred distance
        s.stereoSpreadDegrees=150.f;
        s.sourceDistanceMeters=1.6f;
        s.roomAmount=1.f;
        s.ambience=.91f;
        s.outputTrim=imersipP92DbToLinear(4.1f);
        break;
    case 6: // Original P9.2 neutral ImerSip reference, 0 dB for level A/B
    default:
        break;
    }
    return s;
}
} // namespace armix::builtin_fx
