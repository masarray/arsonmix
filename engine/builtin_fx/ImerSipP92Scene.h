// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
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
    // Authentic owner-approved P9.7 Venue uses 10m within Large 28.8m.
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    return room==1?3.05f:room==2?6.65f:10.f;
#else
    return room==1?3.05f:room==2?6.65f:7.4f;
#endif
}
inline bool validImerSipP92Scene(const armix::sonku::Settings& s) noexcept {
    return armix::sonku::valid(s) && s.renderMode==9 &&
        !s.moving && !s.musicCompatibleLowFrequency &&
        s.startAzimuthDegrees==0.f &&
        s.sourceDistanceMeters<=imersipP92MaxDistance(s.spacePreset);
}
// A single typed preset catalog drives the native UI, state recognition and
// actual SonKu P9 PCM. IDs are UI-only (IMRS persists real accepted settings),
// so removing Neutral from the picker does NOT invalidate old saved sessions.
// Keep Live Concert unchanged: the owner has auditioned and liked it.
// Don't mistake a wide stage/distant source for a guarantee of envelopment.
struct ImerSipP92Preset {
    int id;
    const char* label;
    int spacePreset;
    float stageDegrees, distanceMeters;
    float earlyAmount, lateAmount, outputDb;
    int voicing=0; // P9.8 typed profile; 0 preserves P9.7 Venue PCM
};
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
// P9.7 Venue at exactly 10m,106deg, ER100,late100,trim1.8,voice0
// is the fixed owner-accepted sonic reference. New owner-directed ImmerSIB
// programs use P9.8 native center/early/ear voicings 4=detail/5=impact.
// No dry-stereo stem fake separation, second convolution or Haas widening.
inline constexpr std::array<ImerSipP92Preset,6> imersipP92Presets{{
    {2,"Live Concert",       3,106.f,10.f, 1.f,1.00f,5.10545f,0},
    {3,"Royal Concert Hall", 3, 82.f,3.1f,1.f,.62f,5.57507f,4},
    {4,"Arena Thunder",     3,112.f,8.9f,1.f,1.00f,5.45f,5},
    {5,"Front Row Power",   3, 96.f,2.4f,1.f,.88f,5.60f,4},
    {6,"Symphonic Palace",  3, 94.f,6.2f,1.f,.88f,5.25f,4},
    {7,"Velvet Cinema",     3, 98.f,7.5f,1.f,.84f,4.60898f,3}
}};
#else
// Historical P9.2 candidate stays reproducible when private P9.8 OFF.
inline constexpr std::array<ImerSipP92Preset,6> imersipP92Presets{{
    {2,"Live Concert",       3,150.f,2.2f,1.f,.91f,4.8f},
    {3,"Royal Concert Hall", 3,155.f,1.8f,1.f,.98f,5.4f},
    {4,"Arena Thunder",     3,164.f,1.7f,1.f,1.00f,5.7f},
    {5,"Front Row Power",   3,150.f,1.6f,1.f,.97f,5.2f},
    {6,"Symphonic Palace",  3,146.f,1.9f,1.f,.99f,5.4f},
    {7,"Velvet Cinema",     2,155.f,1.6f,1.f,1.00f,5.7f}
}};
#endif
inline const ImerSipP92Preset* imersipP92PresetById(int id) noexcept {
    for(const auto& p:imersipP92Presets)if(id==p.id)return &p;
    return nullptr;
}
inline float imersipP92DbToLinear(float db) noexcept {
    return std::pow(10.f,db/20.f); // editor/control thread, never PCM callback
}
inline armix::sonku::Settings imersipP92Factory(int id) noexcept {
    auto s=imersipP92ReferenceScene(); // internal neutral test, not UI entry
    if(const auto* p=imersipP92PresetById(id)) {
        s.spacePreset=p->spacePreset;
        s.stereoSpreadDegrees=p->stageDegrees;
        s.sourceDistanceMeters=p->distanceMeters;
        s.roomAmount=p->earlyAmount;
        s.ambience=p->lateAmount;
        s.outputTrim=imersipP92DbToLinear(p->outputDb);
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
        s.sceneVoicing=p->voicing;
        // Do not let dB conversion alter the exact approved Venue trim hash.
        if(id==2)s.outputTrim=1.8f;
#endif
    }
    return s;
}
} // namespace armix::builtin_fx
