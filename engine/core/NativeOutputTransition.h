// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "FxMoveRamp.h"
#include <algorithm>
#include <cmath>
namespace armix::mixer {
// One output-only de-click sweep after the authoritative native PCM graph.
// Covers MAIN, MONITOR and each EXTERNAL ASIO output. Constant-space AUDIO.
struct NativeOutputTransition final {
    static void apply(float* const* outputs,int count,int frames,
                      int moveStage,bool fadeOut,bool fadeIn,
                      const float* mainL,const float* mainR,
                      float& mainPeakL,float& mainPeakR) noexcept {
        mainPeakL=mainPeakR=0.f;
        if(!outputs || count<=0 || frames<=0)return;
        const float step=frames>1?1.f/static_cast<float>(frames-1):0.f;
        for(int c=0;c<count;++c) {
            float* const pcm=outputs[c];
            if(!pcm)continue;
            float peak=0.f;
            for(int i=0;i<frames;++i) {
                const float phase=static_cast<float>(i)*step;
                const float graphGain=fadeOut?1.f-phase:fadeIn?phase:1.f;
                pcm[i]*=graphGain*armix::dsp::FxMoveRamp::gain(moveStage,i,frames);
                peak=std::max(peak,std::abs(pcm[i]));
            }
            if(pcm==mainL)mainPeakL=peak;
            if(pcm==mainR)mainPeakR=peak;
        }
    }
};
} // namespace armix::mixer
