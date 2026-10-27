// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

namespace armix::core
{
// Pure deterministic motion math, independent of JUCE and the browser.
// 0/1 azimuth assumes the *selected* vendor parameter maps -180 to +180 degrees.
// Distance/elevation outputs are normalized VST3 parameter values, not metres.
enum class OrbitShape : int {
    circle=0, frontBack=1, leftRight=2,
    circleRise=3, frontBackRise=4, leftRightRise=5
};
inline bool validOrbitShape(int s) noexcept { return s >= 0 && s <= 5; }
inline bool needsOrbitDistance(int s) noexcept { return s==1 || s==2 || s==4 || s==5; }
inline bool needsOrbitElevation(int s) noexcept { return s>=3 && s<=5; }
inline double orbitAxisX(int s) noexcept { return s==1 || s==4 ? 0.43 : 1.0; }
inline double orbitAxisY(int s) noexcept { return s==2 || s==5 ? 0.43 : 1.0; }
inline double orbitWrap(double x) noexcept {
    x=std::fmod(x,1.0);return x < 0.0 ? x + 1.0 : x;
}
inline double orbitShapePhaseForAzimuth(float azimuth,int shape) noexcept {
    if (!validOrbitShape(shape)||!std::isfinite(azimuth)) return 0.0;
    constexpr double pi=3.14159265358979323846;
    const double angle=(static_cast<double>(azimuth)-0.5)*2.0*pi;
    const double t=std::atan2(std::sin(angle)/orbitAxisX(shape),
                              std::cos(angle)/orbitAxisY(shape));
    return orbitWrap(t/(2.0*pi));
}
// Crossing a near/far phase extremum between timer ticks is common at 60 Hz.
// Capture the actual endpoint once (without another thread or timer) so the
// distance parameter really reaches the user-selected near/far setpoints.
inline bool crossesOrbitDistanceExtreme(double previous,double current,
                                        int shape,bool isFarEndpoint) noexcept
{
    if (!needsOrbitDistance(shape) ||
        !std::isfinite(previous)||!std::isfinite(current) ||
        std::abs(current-previous)>0.125 || previous==current)return false;
    const double lo=std::min(previous,current),hi=std::max(previous,current);
    const bool frontBack=(shape==1||shape==4);
    const double offset=frontBack?(isFarEndpoint?0.0:0.25):(isFarEndpoint?0.25:0.0);
    for(int side=0;side<2;++side) {
        const double threshold=offset+side*0.5;
        if(std::floor(hi-threshold)>std::floor(lo-threshold))return true;
    }
    return false;
}
struct OrbitMotionPoint {
    float azimuth=0.5f, distance=0.5f, elevation=0.5f;
    float x=0.0f,y=1.0f,z=0.0f; // top-down x/y, signed elevation z
};
inline OrbitMotionPoint orbitMotionPoint(double phase,int shape,
    double nearDistance=0.2,double farDistance=0.7) noexcept {
    constexpr double pi=3.14159265358979323846;
    if (!validOrbitShape(shape)||!std::isfinite(phase)) return {};
    const double t=orbitWrap(phase)*2.0*pi;
    const double x=orbitAxisX(shape)*std::sin(t);
    const double y=orbitAxisY(shape)*std::cos(t);
    const double r=std::hypot(x,y);
    // User-selected VST3-normalised near/far setpoints, never fixed 30 metres.
    if(!std::isfinite(nearDistance)||!std::isfinite(farDistance))return {};
    const double nearValue=std::clamp(nearDistance,0.0,1.0);
    const double farValue=std::clamp(farDistance,0.0,1.0);
    const double radius=std::clamp((r-0.43)/0.57,0.0,1.0);
    const double distance=nearValue+(farValue-nearValue)*radius;
    const double z=needsOrbitElevation(shape) ? 0.30*std::sin(t) : 0.0;
    const double azimuth=orbitWrap(0.5+std::atan2(x,y)/(2.0*pi));
    return {static_cast<float>(azimuth),static_cast<float>(distance),
            static_cast<float>(std::clamp(0.5+z*0.70,0.0,1.0)),
            static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)};
}
} // namespace armix::core
