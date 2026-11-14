// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Exact source-to-listener spherical -> Cartesian geometry, research only.
// No new parameter/schema; this is a quiescent off-line native DSP control.
#include <algorithm>
#include <cmath>
namespace armix::sonku::xyz {
struct Placement {
    float horizontalMeters=1.6f;
    float sourceHeightMeters=1.45f;
    float heightDeltaMeters=0.f;
    float directDistanceMeters=1.6f;
};
inline bool project(float radialMeters,float elevationDegrees,
                    float listenerHeight,float roomHeight,
                    Placement& result) noexcept {
    if(!std::isfinite(radialMeters)||!std::isfinite(elevationDegrees)||
       !std::isfinite(listenerHeight)||!std::isfinite(roomHeight)||
       radialMeters<1.6f||radialMeters>10.f||
       elevationDegrees< -40.f||elevationDegrees>60.f||
       roomHeight<2.4f||roomHeight>12.f||
       listenerHeight<.5f||listenerHeight>roomHeight-.5f)return false;
    constexpr float radians=.017453292519943295769f;
    const float z=radialMeters*std::sin(elevationDegrees*radians);
    const float horizontal=radialMeters*std::cos(elevationDegrees*radians);
    const float height=listenerHeight+z;
    if(height<.5f||height>roomHeight-.5f||horizontal<.7f)return false;
    const float distance=std::hypot(horizontal,z);
    if(!(distance>0.f)||!std::isfinite(distance))return false;
    result={horizontal,height,z,radialMeters};
    return true;
}
} // namespace armix::sonku::xyz
