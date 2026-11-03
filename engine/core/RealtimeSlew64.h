// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// One reusable fixed-duration DSP ramp for fader, pan and PRE/POST SEND.
// CONTROL coalesces to one latest atomic goal per callback; AUDIO owns
// current/remaining. No heap, lock, division or time API in the sample loop.
// A new gesture during a ramp starts at the *audibly applied* coefficient.
namespace armix::dsp {
struct RealtimeSlew64 final {
    static constexpr int samples=64;
    float current=1.f;
    float goal=1.f;
    float delta=0.f;
    int remaining=0;

    void reset(float initial) noexcept {
        current=goal=initial;
        delta=0.f;
        remaining=0;
    }
    void retarget(float requested) noexcept {
        if(requested==goal)return;
        goal=requested;
        remaining=samples;
        delta=(goal-current)*(1.f/static_cast<float>(samples));
    }
    float next() noexcept {
        if(remaining>0) {
            --remaining;
            if(remaining==0)current=goal; // exact steady-state endpoint
            else current+=delta;
        }
        return current;
    }
};
} // namespace armix::dsp
