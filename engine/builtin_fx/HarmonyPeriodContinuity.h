// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

// Realtime-safe period policy for one singer's measured microphone F0.
// A confident but incorrect harmonic/octave candidate can survive two 6ms
// analysis hops during dense vowel/chorus content. Never clear every
// pitch-synchronous overlap-add grain on that transient evidence.
// Distinguishes short outliers from a sustained melodic change, then
// slews true large transitions without discontinuous renderer resets.
// No heap, locks, trigonometry or device-specific state.
namespace armix::harmony {
class PeriodContinuity final {
public:
    void reset() noexcept {
        period_=0.f;pending_=0.f;pendingHops_=0;transitioning_=false;
    }
    float current() const noexcept {return period_;}
    bool transitioning() const noexcept {return transitioning_;}

    float observe(float measured) noexcept {
        if(!std::isfinite(measured)||measured<8.f||measured>3200.f)
            return period_;
        if(period_<=0.f) {
            period_=measured;
            return period_;
        }
        const float delta=measured-period_;
        const float distance=std::abs(delta);
        if(transitioning_) {
            // A confirmed octave/large interval is a new phrase segment:
            // track it within bounded 11% analysis-hop steps while the
            // existing overlap-add windows retire naturally.
            const float limit=.11f*period_;
            period_+=std::clamp(delta,-limit,limit);
            if(std::abs(measured-period_)<=.08f*period_)
                transitioning_=false;
            return period_;
        }
        if(distance>.25f*period_) {
            // Correlation often prefers a temporary 2x/0.5x harmonic
            // during loud vocal vowels. Require three similar windows
            // before following a true large-period change.
            if(pendingHops_>0 &&
               std::abs(measured-pending_)<=.12f*pending_) {
                if(pendingHops_<3)++pendingHops_;
            }else {
                pending_=measured;
                pendingHops_=1;
            }
            if(pendingHops_>=3) {
                transitioning_=true;pendingHops_=0;
                const float limit=.11f*period_;
                period_+=std::clamp(delta,-limit,limit);
            }
            return period_;
        }
        pendingHops_=0;pending_=0.f;
        // Small F0 wobble/vibrato must not quantize grain-window lengths.
        const float bounded=std::clamp(measured,
            period_*.95f,period_*1.05f);
        period_+=.30f*(bounded-period_);
        return period_;
    }
private:
    float period_=0.f,pending_=0.f;
    int pendingHops_=0;
    bool transitioning_=false;
};
} // namespace armix::harmony
