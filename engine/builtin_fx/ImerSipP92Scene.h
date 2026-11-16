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
inline armix::sonku::Settings imersipP92Factory(int id) noexcept {
    auto s=imersipP92ReferenceScene();
    switch(id){
    case 3: // A genuinely near source in a smaller room
        s.spacePreset=2;s.stereoSpreadDegrees=72.f;
        s.roomAmount=.46f;s.ambience=.14f;break;
    case 4: // Wider but more reverberant hall, explicit audition
        s.spacePreset=3;s.stereoSpreadDegrees=104.f;
        s.sourceDistanceMeters=2.2f;s.roomAmount=.84f;
        s.ambience=.40f;break;
    default:break; // id 2 and first-insert default: exact P9.2 reference
    }
    return s;
}
} // namespace armix::builtin_fx
