// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// P9.9 PRIVATE, scene-aware musical vocal focus. This is NOT voice separation
// and does not infer a singer from a full stereo mix. Only center-dominant,
// audible midrange earns a modest, slowly smoothed forward correction.
// The existing two virtual speakers, measured KEMAR FIRs, directional first
// reflections and linked limiter remain the ONLY output graph. No FFT,
// delay line, resampling, allocation, string analysis or per-sample trig.
// Venue/Custom (scene=0), ROOM OFF, and MIX=0 return exact P9.8/P9.7 PCM.
namespace armix::sonku::p99 {
class MusicalVocalDepth final {
public:
    struct Stereo {float left=0.f,right=0.f;};
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 &&
                                   std::abs(rate-48000.)>.1))return false;
        const auto alpha=[rate](double hz) noexcept {
            constexpr double twoPi=6.283185307179586476925286766559;
            return static_cast<float>(1.-std::exp(-twoPi*hz/rate));
        };
        bodyLow_=alpha(210.);
        bodyHigh_=alpha(880.);
        focusLow_=alpha(760.);
        focusHigh_=alpha(3350.);
        widthLow_=alpha(680.);
        widthHigh_=alpha(3900.);
        lateLow_=alpha(720.);
        lateHigh_=alpha(3400.);
        attack_=alpha(7.5);          // ~21 ms envelope attack
        release_=alpha(1.3);         // ~122 ms envelope release
        attentionAttack_=alpha(5.);  // ~32 ms, no per-syllable pumping
        attentionRelease_=alpha(.85);// ~187 ms
        gainSlew_=static_cast<float>(1.-std::exp(-1./(rate*.055)));
        reset();
        return true;
    }
    void reset() noexcept {
        midBodyLow_={};midBodyHigh_={};midFocusLow_={};midFocusHigh_={};
        midFocusLow2_={};midFocusHigh2_={};
        sideFocusLow_={};sideFocusHigh_={};
        sideFocusLow2_={};sideFocusHigh2_={};
        firstLow_={};firstHigh_={};firstLow2_={};firstHigh2_={};
        tailLow_={};tailHigh_={};tailLow2_={};tailHigh2_={};
        centerEnvelope_=sideEnvelope_=attention_=0.f;
        gains_={};
        active_=false;
    }
    // ID 0 = completely unmodified approved Venue or Custom/Live Orbit.
    // Signature offers the clearest front-stage; Cinema preserves LF body;
    // Studio is deliberately a lower-delta, natural room.
    Stereo source(float left,float right,int scene,bool enabled) noexcept {
        active_=enabled && scene>=1 && scene<=3;
        const auto target=profiles_[active_?scene:0];
        for(std::size_t i=0;i<gains_.size();++i) {
            gains_[i]+=gainSlew_*(target[i]-gains_[i]);
            if(std::abs(gains_[i]-target[i])<1.e-6f)gains_[i]=target[i];
        }
        if(!active_) {
            attention_+=(attention_>0.f?attentionRelease_:attentionAttack_)*
                        (0.f-attention_);
            return {left,right}; // NO altered float arithmetic
        }

        const float mid=.5f*(left+right);
        const float side=.5f*(left-right);
        const float body=midBodyHigh_.run(bodyHigh_,mid)-
                         midBodyLow_.run(bodyLow_,mid);
        // Cascaded matched one-poles form a soft 12dB/oct upper rolloff:
        // a first-order band differential still raised 7.6kHz too much.
        // Its *actual native test* rejected that high-sibilance excess.
        const float focus=midFocusHigh2_.run(
                            focusHigh_,midFocusHigh_.run(focusHigh_,mid))-
                          midFocusLow2_.run(
                            focusLow_,midFocusLow_.run(focusLow_,mid));
        const float sideFocus=sideFocusHigh2_.run(
                            focusHigh_,sideFocusHigh_.run(focusHigh_,side))-
                              sideFocusLow2_.run(
                            focusLow_,sideFocusLow_.run(focusLow_,side));
        const float c=std::abs(focus);
        const float s=std::abs(sideFocus);
        // Envelope matching is musical-phrase-scale, not a per-sample gate.
        centerEnvelope_+=(c>centerEnvelope_?attack_:release_)*
                         (c-centerEnvelope_);
        sideEnvelope_+=(s>sideEnvelope_?attack_:release_)*
                       (s-sideEnvelope_);
        const float centerShare=centerEnvelope_/
            (centerEnvelope_+1.65f*sideEnvelope_+1.e-5f);
        const float dominance=std::clamp((centerShare-.46f)*2.f,0.f,1.f);
        // Silence and tiny leakage must never trigger a sudden voice boost.
        const float audible=std::clamp(
            (centerEnvelope_-.00075f)/.0035f,0.f,1.f);
        const float desired=dominance*audible;
        attention_+=(desired>attention_?attentionAttack_:attentionRelease_)*
                    (desired-attention_);
        // 210-880Hz body is explicitly reinforced, not cut. Most focus is
        // at 760-3350Hz. No upper-air shelf, de-esser or narrow 4-8k peak.
        const float delta=attention_*(gains_[0]*body+gains_[1]*focus);
        return {left+delta,right+delta};
    }
    // Small complementary widening of REAL directional first images, not
    // fake Haas/crossfeed. Mid stays algebraically identical. This moves
    // stereo accompaniment/reverb around the forward center without
    // collapsing width or duplicating a second room/IR processor.
    Stereo early(float left,float right) noexcept {
        if(!active_ || gains_[2]==0.f)return {left,right};
        const float side=.5f*(left-right);
        const float width=firstHigh2_.run(
                    widthHigh_,firstHigh_.run(widthHigh_,side))-
                          firstLow2_.run(
                    widthLow_,firstLow_.run(widthLow_,side));
        const float delta=gains_[2]*width;
        return {left+delta,right-delta};
    }
    // Center midrange in existing LATE field is gently relieved when the
    // direct source is confident/centered. Lateral late ambience and bass
    // remain unchanged: direct center appears closer while the already-
    // rendered instruments retain their wider acoustic layer.
    Stereo late(float left,float right) noexcept {
        if(!active_ || gains_[3]==0.f)return {left,right};
        const float mid=.5f*(left+right);
        const float band=tailHigh2_.run(
                    lateHigh_,tailHigh_.run(lateHigh_,mid))-
                         tailLow2_.run(
                    lateLow_,tailLow_.run(lateLow_,mid));
        const float delta=gains_[3]*attention_*band;
        return {left-delta,right-delta};
    }
private:
    struct LP {
        float v=0.f;
        float run(float coeff,float x) noexcept {
            v+=coeff*(x-v);
            return v;
        }
    };
    // body(210-880), focus(760-3350), directional early-side(680-3900),
    // center-late relief(720-3400); these are MAX linear mixing weights,
    // not claimed vocal-estimation confidence or EQ dB targets.
    static constexpr std::array<std::array<float,4>,4> profiles_{{
        {{0.f,0.f,0.f,0.f}},            // Venue remains exact P9.8
        {{.095f,.165f,.080f,.095f}},   // Signature: full and front-focused
        {{.040f,.062f,.030f,.038f}},   // Studio: gentle, intimate
        {{.070f,.115f,.065f,.070f}}    // Cinema: full-body clear dialog
    }};
    LP midBodyLow_{},midBodyHigh_{},midFocusLow_{},midFocusHigh_{};
    LP midFocusLow2_{},midFocusHigh2_{};
    LP sideFocusLow_{},sideFocusHigh_{},sideFocusLow2_{},sideFocusHigh2_{};
    LP firstLow_{},firstHigh_{},firstLow2_{},firstHigh2_{};
    LP tailLow_{},tailHigh_{},tailLow2_{},tailHigh2_{};
    float bodyLow_=0.f,bodyHigh_=0.f,focusLow_=0.f,focusHigh_=0.f;
    float widthLow_=0.f,widthHigh_=0.f,lateLow_=0.f,lateHigh_=0.f;
    float attack_=0.f,release_=0.f,attentionAttack_=0.f;
    float attentionRelease_=0.f,gainSlew_=0.f;
    float centerEnvelope_=0.f,sideEnvelope_=0.f,attention_=0.f;
    std::array<float,4> gains_{};
    bool active_=false;
};
} // namespace armix::sonku::p99
