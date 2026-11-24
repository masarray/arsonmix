// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

// AUDIO-thread-only policy for the admitted static ImmerSIB product.
// Approved core runs at 44.1/48 kHz only. Use the higher rate for a
// conservative time bound even when JUCE's processor rate hint is unset.
// No threshold on INPUT: every nonzero sample, including subnormals, wakes.
// Output below -160 dBFS for 250 ms is a conservative near-silence criterion,
// not a bit-exact promise after sleep. Never reset or re-prepare the DSP here.
namespace armix::dsp {
class ImmerSIBIdlePolicy final {
public:
    static constexpr std::uint32_t drainFrames=3u*48000u;
    static constexpr std::uint32_t quietFrames=48000u/4u;
    static constexpr float quietLimit=1.e-8f;
    bool shouldSleep(const float* l,const float* r,int n,bool stable) noexcept {
        if(!stable || !l || !r || n<=0) { reset(); return false; }
        for(int i=0;i<n;++i)
            if(!exactZero(l[i]) || !exactZero(r[i])) { reset(); return false; }
        zeroInput_=true;
        return sleeping_;
    }
    void rendered(const float* l,const float* r,int n,bool stable) noexcept {
        if(!stable || !zeroInput_ || !l || !r || n<=0) { reset();return; }
        const auto frames=static_cast<std::uint32_t>(n);
        silent_=advance(silent_,frames,drainFrames);
        for(int i=0;i<n;++i)
            if(!(std::abs(l[i])<=quietLimit && std::abs(r[i])<=quietLimit)) {
                quiet_=0;sleeping_=false;return;
            }
        quiet_=advance(quiet_,frames,quietFrames);
        sleeping_=silent_>=drainFrames && quiet_>=quietFrames;
    }
    void reset() noexcept {silent_=quiet_=0;zeroInput_=sleeping_=false;}
    bool eligible() const noexcept {return sleeping_;}
private:
    static bool exactZero(float v) noexcept {
        // Bit-level test independent of FTZ/DAZ; both signed zeros qualify.
        return (std::bit_cast<std::uint32_t>(v)&0x7fffffffu)==0;
    }
    static std::uint32_t advance(std::uint32_t value,std::uint32_t n,
                                 std::uint32_t limit) noexcept {
        return value+std::min(n,limit-value);
    }
    std::uint32_t silent_=0,quiet_=0;
    bool zeroInput_=false,sleeping_=false;
};
} // namespace armix::dsp
