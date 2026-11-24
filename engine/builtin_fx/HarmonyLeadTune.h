// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

namespace armix::harmony {
// Hop-rate lead policy. The slow note center drives correction while the
// instantaneous period still drives synthesis, retaining pitch modulation.
class LeadTune final {
public:
    void prepare(double hopSeconds) noexcept {
        centerAlpha_=static_cast<float>(1.-std::exp(-hopSeconds/.16));
        stableHops_=static_cast<int>(std::ceil(.080/hopSeconds));
        release();
    }

    void release() noexcept {
        note_=-1;
        age_=0;
        ratio_=1.f;
    }

    void observe(float midi,int note,float confidence) noexcept {
        if(confidence<.87f||std::abs(midi-note)>.65f){
            release();
            return;
        }
        // Keep the same intent through normal vibrato around the note. A
        // large excursion, or a different nearby scale note, reacquires.
        if(note_!=note||std::abs(midi-center_)>.75f){
            note_=note;
            center_=midi;
            age_=0;
            ratio_=1.f;
            return;
        }
        center_+=centerAlpha_*(midi-center_);
        if(age_<stableHops_)++age_;
        if(age_<stableHops_)return;
        const float error=static_cast<float>(note_)-center_;
        const float correction=std::copysign(
            std::min(.34f,.8f*std::max(0.f,std::abs(error)-.09f)),error);
        ratio_=std::exp2(correction/12.f);
    }

    float ratio() const noexcept {return ratio_;}
private:
    int note_=-1,age_=0,stableHops_=1;
    float center_=0.f,centerAlpha_=1.f,ratio_=1.f;
};
// Sample-rate-stable transition of the *correction residual*, not a held
// vocal sample. Dry PCM always advances. A missing synthesis epoch fades the
// last residual to zero over 3ms; reacquire after 2ms consecutive valid PCM.
// Restart from the actual previous residual on every edge, avoiding a step
// even under repeated ready/not-ready transitions. No added history or delay.
class LeadRenderSeam final {
public:
    void prepare(double rate) noexcept {
        ramp_=std::max(1,static_cast<int>(std::lround(rate*.003)));
        prewarm_=std::max(1,static_cast<int>(std::lround(rate*.002)));
        reset();
    }
    void reset() noexcept {
        run_=0;position_=ramp_;active_=false;
        lastL_=lastR_=startL_=startR_=0.f;
    }
    void process(float& dryL,float& dryR,float tunedL,float tunedR,
                 bool ready,float mix) noexcept {
        if(ready){if(run_<prewarm_)++run_;}
        else run_=0;
        const bool valid=run_>=prewarm_;
        if(valid!=active_){
            startL_=lastL_;startR_=lastR_;
            position_=0;active_=valid;
        }
        const float targetL=active_?(tunedL-dryL)*mix:0.f;
        const float targetR=active_?(tunedR-dryR)*mix:0.f;
        if(position_<ramp_){
            const float u=static_cast<float>(position_++)/ramp_;
            const float w=u*u*(3.f-2.f*u);
            lastL_=startL_*(1.f-w)+targetL*w;
            lastR_=startR_*(1.f-w)+targetR*w;
        }else{
            lastL_=targetL;lastR_=targetR;
        }
        dryL+=lastL_;dryR+=lastR_;
    }
private:
    int ramp_=1,prewarm_=1,run_=0,position_=1;
    bool active_=false;
    float lastL_=0.f,lastR_=0.f,startL_=0.f,startR_=0.f;
};
} // namespace armix::harmony
