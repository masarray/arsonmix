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
} // namespace armix::harmony
