// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
#include <array>
#include <algorithm>
#include <cstdint>
namespace armix::builtin_fx::vocal_delay {
enum class StereoMode : std::uint8_t { mono, stereo, pingPong };
enum class EchoStyle : std::uint8_t { classic, voiced, dual, multitap, tape };
enum class ThrowMode : std::uint8_t { off, concert, ballad, pop, dangdut, rock, ambient, intimate, manual };
struct Parameters final {
    float timeMs=320.f, feedback=.30f, echo=.22f;
    float highPassHz=150.f, lowPassHz=5500.f; // zero explicitly bypasses a filter
    float duck=.20f, duckAttackMs=10.f, duckReleaseMs=180.f;
    StereoMode mode=StereoMode::stereo;
    bool enabled=true;
    EchoStyle style=EchoStyle::classic; // V2 exact PCM until explicit new style
    bool adaptiveFocus=false; // V2/V3 retain their original detector
    ThrowMode throwMode=ThrowMode::off; // V7 selective wet capture; off preserves VD11
    bool smartStage=false; // opt-in VD7 policy, never inferred from legacy settings
    bool spillover=false,hit=false; // HIT is transient, never armed by saved state
    std::uint16_t bpmQuarter=0; // 0=free time, otherwise BPM x4 (40..240)
    std::uint8_t division=3; // quarter note default
};
inline constexpr std::array<double,7> beatMultipliers{{.25,.5,.75,1,1.5,2,4}};
inline bool beatTime(double bpm,std::size_t division,float& ms) noexcept{
    if(!std::isfinite(bpm)||bpm<40||bpm>240||division>=beatMultipliers.size())return false;
    const auto t=60000.*beatMultipliers[division]/bpm;
    if(t<20||t>4000)return false;
    ms=float(t);return true;
}
inline bool setTempo(Parameters& p,double bpm,std::size_t division) noexcept{
    if(!std::isfinite(bpm)||bpm<40||bpm>240)return false;
    const auto q=static_cast<std::uint16_t>(std::lround(bpm*4.));
    float ms=0;if(!beatTime(double(q)*.25,division,ms))return false;
    p.bpmQuarter=q;p.division=static_cast<std::uint8_t>(division);p.timeMs=ms;return true;
}
inline void freeTime(Parameters& p,float ms) noexcept{p.timeMs=ms;p.bpmQuarter=0;p.division=3;}
inline bool validTempo(const Parameters& p) noexcept{
    if(!p.bpmQuarter)return p.division==3;
    float ms=0;return beatTime(double(p.bpmQuarter)*.25,p.division,ms)&&std::abs(ms-p.timeMs)<.001f;
}
inline Parameters performanceSettings(Parameters p) noexcept{
    // A bounded wet-only overlay. Base knobs remain editable, HIT never
    // rewrites them; at zero echo HIT cannot unexpectedly enable an effect.
    if(p.hit&&p.throwMode==ThrowMode::off){
        p.echo=std::min(1.f,p.echo*1.5f);
        p.feedback=std::min(.90f,p.feedback+.12f);
    }
    return p;
}
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
        static_cast<unsigned>(p.style)<=static_cast<unsigned>(EchoStyle::tape)&&
        static_cast<unsigned>(p.throwMode)<=static_cast<unsigned>(ThrowMode::manual)&&
        (p.throwMode==ThrowMode::off||p.smartStage)&&validTempo(p);
}
} // namespace armix::builtin_fx::vocal_delay
