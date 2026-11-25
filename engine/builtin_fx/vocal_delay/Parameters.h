// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
#include <cstdint>
namespace armix::builtin_fx::vocal_delay {
enum class StereoMode : std::uint8_t { mono, stereo, pingPong };
enum class EchoStyle : std::uint8_t { classic, voiced, dual, multitap, tape };
struct Parameters final {
    float timeMs=320.f, feedback=.30f, echo=.22f;
    float highPassHz=150.f, lowPassHz=5500.f; // zero explicitly bypasses a filter
    float duck=.20f, duckAttackMs=10.f, duckReleaseMs=180.f;
    StereoMode mode=StereoMode::stereo;
    bool enabled=true;
    EchoStyle style=EchoStyle::classic; // V2 exact PCM until explicit new style
    bool adaptiveFocus=false; // V2/V3 retain their original detector
};
inline bool validRate(double rate) noexcept {
    return std::isfinite(rate)&&rate>=8000.&&rate<=192000.;
}
inline bool inRange(float x,float lo,float hi) noexcept {
    return std::isfinite(x)&&x>=lo&&x<=hi;
}
inline bool valid(const Parameters& p) noexcept {
    return inRange(p.timeMs,20,4000)&&inRange(p.feedback,0,.90f)&&
        inRange(p.echo,0,1)&&(p.highPassHz==0||inRange(p.highPassHz,40,1000))&&
        (p.lowPassHz==0||inRange(p.lowPassHz,1000,20000))&&
        inRange(p.duck,0,1)&&inRange(p.duckAttackMs,2,100)&&
        inRange(p.duckReleaseMs,40,1000)&&
        (p.mode==StereoMode::mono||p.mode==StereoMode::stereo||p.mode==StereoMode::pingPong)&&
        static_cast<unsigned>(p.style)<=static_cast<unsigned>(EchoStyle::tape);
}
} // namespace armix::builtin_fx::vocal_delay
