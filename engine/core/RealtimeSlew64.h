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
    // Apply a shared stereo coefficient. A settled unity gate/trim is a
    // no-op; a settled non-unity coefficient is a vectorizable constant pass.
    // Never skip an in-progress sample ramp or replace zero multiplication
    // with a clear (which would hide nonfinite PCM evidence).
    void applyStereo(float* left,float* right,int frames) noexcept {
        if(remaining==0){
            if(current==1.f)return;
            const float gain=current;
            for(int i=0;i<frames;++i){left[i]*=gain;right[i]*=gain;}
            return;
        }
        for(int i=0;i<frames;++i){const float gain=next();left[i]*=gain;right[i]*=gain;}
    }
    // Zero-PDC PRE/POST route. Preserve the original two multiplications
    // and accumulation order, including zero gain and nonfinite PCM.
    // A settled gain has no per-sample ramp-state reads or writes.
    void accumulateStereo(const float* left,const float* right,
                          float* destL,float* destR,int frames,
                          float panL,float panR) noexcept {
        if(remaining==0){
            const float gain=current;
            for(int i=0;i<frames;++i){
                destL[i]+=left[i]*gain*panL;destR[i]+=right[i]*gain*panR;
            }
            return;
        }
        for(int i=0;i<frames;++i){
            const float gain=next();
            destL[i]+=left[i]*gain*panL;destR[i]+=right[i]*gain*panR;
        }
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
