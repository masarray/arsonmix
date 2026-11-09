// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
namespace armix::harmony {
// A fixed-clock backing-only virtual stage, not individualized HRTF rendering.
// Per-voice far-ear precedence timing and two physically separated early taps
// supplement the existing ONE shared room. No modulated delay or polarity flips.
class BackingStageSpatial final {
public:
    static constexpr int voices=5;
    static constexpr std::size_t earCapacity=128;
    struct Gains {float direct=1.f,early=0.f,late=0.f;};
    struct Output {float directL=0, directR=0, earlyL=0, earlyR=0, roomL=0, roomR=0;};
    static Gains gains(float space) noexcept {
        const float s=std::clamp(space,0.f,1.f);
        return {1.f-.62f*s,.49f*s,1.08f*s};
    }
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        const auto delay=static_cast<std::size_t>(std::lround(rate*.00022));
        if(delay<2||delay>=earCapacity)return false;
        farEarDelay_=delay;
        // Head-shadow proxy: the contralateral signal is gently softened
        // before a fixed precedence delay. Prepared once per device rate.
        // Not a measured HRTF and never applied to the original dry lead.
        farEarLowpassAlpha_=static_cast<float>(
            1.-std::exp(-2.*3.141592653589793*4100./rate));
        if(!(farEarLowpassAlpha_>0.f&&farEarLowpassAlpha_<1.f))return false;
        farEarLowpassState_.fill(0.f);
        for(auto& ring:farEarHistory_)ring.fill(0.f);
        cursor_=0;
        return true;
    }
    Output place(int singer,float source,float firstReflection,
                 float secondReflection,float mixL,float mixR,
                 float space) noexcept {
        // Engine calls with validated singer indices [0,4].
        const auto i=static_cast<std::size_t>(singer);
        auto& ring=farEarHistory_[i];
        float& filtered=farEarLowpassState_[i];
        filtered+=farEarLowpassAlpha_*(source-filtered);
        // Store filtered signal for the far ear only. The near ear keeps the
        // original pitch-synchronous PCM unchanged. Reuse the old fixed ring:
        // no extra audio buffer, allocation or reported latency.
        ring[cursor_]=filtered;
        const float delayed=ring[(cursor_+earCapacity-farEarDelay_)&(earCapacity-1)];
        const float sum=std::abs(mixL)+std::abs(mixR);
        const float direction=sum>1.e-9f?(mixR-mixL)/sum:0.f;
        // Only the quieter, contralateral ear carries the tiny fixed lag.
        // Space crossfades the level, NOT the delay (no Doppler sweep).
        const float cross=.68f*std::clamp(space,0.f,1.f)*
            std::clamp(2.15f*std::abs(direction),0.f,1.f);
        const float far=source*(1.f-cross)+delayed*cross;
        const float left=(direction>0.f?far:source)*mixL;
        const float right=(direction<0.f?far:source)*mixR;
        // Early sound spreads in both directions with positive polarity:
        // first reflection is contralateral, second modestly ipsilateral.
        const float earlyLeft=.68f*firstReflection*mixR+
            .32f*secondReflection*(.67f*mixL+.33f*mixR);
        const float earlyRight=.68f*firstReflection*mixL+
            .32f*secondReflection*(.33f*mixL+.67f*mixR);
        return {left,right,earlyLeft,earlyRight,left,right};
    }
    void advance() noexcept {cursor_=(cursor_+1)&(earCapacity-1);}
    std::size_t farEarDelaySamples()const noexcept{return farEarDelay_;}
private:
    std::array<std::array<float,earCapacity>,voices> farEarHistory_{};
    std::array<float,voices> farEarLowpassState_{};
    float farEarLowpassAlpha_=0.f;
    std::size_t cursor_=0,farEarDelay_=0;
};
} // namespace armix::harmony
