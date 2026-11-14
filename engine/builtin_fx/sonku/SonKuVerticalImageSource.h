// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
// Independent six-boundary first-order image-source geometry.
// Coordinates relative to LISTENER at (0,0,listenerHeight), source x/y
// position determined by SonKu azimuth and horizontal source distance.
// Only true measured KEMAR elevation datasets are used for pinna cues.
namespace armix::sonku::sixsurface {
enum class Boundary {Floor=4,Ceiling=5};
struct RoomHeight {
    float room=3.2f,listener=1.6f,source=1.6f;
};
struct Arrival {
    float azimuth=0.f,elevation=0.f,pathMeters=0.f;
    float delaySamples=0.f,gain=0.f;
};
inline bool validHeight(RoomHeight g) noexcept {
    return std::isfinite(g.room)&&std::isfinite(g.listener)&&
        std::isfinite(g.source) && g.room>=2.4f&&g.room<=12.f&&
        g.listener>=.5f&&g.source>=.5f&&
        g.listener<=g.room-.5f&&g.source<=g.room-.5f&&
        true; // P7.1 may admit validated true XYZ source placement.
}
inline Arrival image(const RoomHeight g,float horizontalDistance,
                     float azimuthDegrees,Boundary boundary,float sampleRate) noexcept {
    constexpr float pi=3.14159265358979323846f;
    constexpr float soundSpeed=343.f;
    const float rad=azimuthDegrees*pi/180.f;
    const float x=horizontalDistance*std::sin(rad);
    const float y=horizontalDistance*std::cos(rad);
    const float z=boundary==Boundary::Floor
        ?-g.source-g.listener:(2.f*g.room-g.source-g.listener);
    const float path=std::sqrt(x*x+y*y+z*z);
    const float direct=std::hypot(horizontalDistance,g.source-g.listener);
    return {azimuthDegrees,std::atan2(z,horizontalDistance)*180.f/pi,
            path,(path-direct)*sampleRate/soundSpeed,
            .28f*.70f*(1.6f/path)};
}
inline bool fitsDelayRing(RoomHeight g,float horizontalDistance,
                          float rate,int ringFrames,int firTaps) noexcept {
    if(!validHeight(g)||!std::isfinite(horizontalDistance)||
       horizontalDistance<.7f||horizontalDistance>10.f||!std::isfinite(rate)||
       rate<44099.9f||rate>48000.1f)return false;
    const auto a=image(g,horizontalDistance,0.f,Boundary::Floor,rate);
    const auto b=image(g,horizontalDistance,0.f,Boundary::Ceiling,rate);
    return std::max(a.delaySamples,b.delaySamples)<ringFrames-firTaps-4;
}
} // namespace armix::sonku::sixsurface
