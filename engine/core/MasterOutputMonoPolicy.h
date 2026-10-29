// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>

// P4 mono master output mode is a control-plane policy with a click-safe
// fixed-sample interpolation. It owns one audio-callback-local scalar.
// No allocation, lock, timer, parameter read, or extra audio buffering.
namespace armix::dsp {
class MasterOutputMonoPolicy final {
public:
    static constexpr int transitionSamples=64;
    // Hardware output was already gated to silence, so prime the correct
    // mono state BEFORE the first unmuted output sample.
    void primeWhileMuted(bool mono) noexcept { blend_=mono?1.f:0.f; }
    void process(bool mono,float& left,float& right) noexcept {
        const float target=mono?1.f:0.f;
        constexpr float step=1.f/static_cast<float>(transitionSamples);
        if(blend_<target)blend_=std::min(target,blend_+step);
        else if(blend_>target)blend_=std::max(target,blend_-step);
        if(blend_==0.f)return;
        const float center=(left+right)*.5f;
        if(blend_==1.f) {
            left=right=center; // bit-exact L/R in fully enabled MONO mode
            return;
        }
        left+=blend_*(center-left);
        right+=blend_*(center-right);
    }
    float blend() const noexcept {return blend_;}
private:
    float blend_=0.f; // audio thread exclusively
};
} // namespace armix::dsp
