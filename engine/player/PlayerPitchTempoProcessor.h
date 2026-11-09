// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../third_party/signalsmith/signalsmith-stretch.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace armix::player {
// WORKER THREAD ONLY. Signalsmith DSP state, FFT storage and scratch buffers
// are allocated off realtime. Never instantiate or call from JUCE callback.
//
// Tempo is rate (80% slower / 120% faster) and does not set musical pitch.
// Key transpose is independent of tempo. Both stereo channels share one
// phase-coherent processing instance rather than divergent mono stretchers.
class PlayerPitchTempoProcessor final {
public:
    struct Controls {
        int keySemitones=0;
        int tempoPercent=100;
        bool operator==(const Controls&) const = default;
    };
    static bool valid(Controls c) noexcept {
        return c.keySemitones>=-6 && c.keySemitones<=6 &&
               c.tempoPercent>=80 && c.tempoPercent<=120;
    }
    static int inputFramesForOutput(int outputFrames,Controls c,
                                     double& remainder) noexcept {
        if(!valid(c)||outputFrames<=0)return 0;
        const double requested=outputFrames*
            (static_cast<double>(c.tempoPercent)/100.0)+remainder;
        const int frames=static_cast<int>(std::floor(requested));
        remainder=requested-frames;
        return frames;
    }
    bool prepare(int sampleRate,Controls c) {
        if(sampleRate<8000 || sampleRate>384000 || !valid(c))
            return false;
        sampleRate_=sampleRate;
        controls_=c;
        // Cheaper is the documented 100 ms / 40 ms quality/perf preset;
        // split computation spreads FFT bursts on the decoder worker.
        stretch_.presetCheaper(2,static_cast<float>(sampleRate_),true);
        stretch_.setTransposeSemitones(
            static_cast<float>(controls_.keySemitones),
            8000.f/static_cast<float>(sampleRate_));
        fractionalInput_=0;
        prepared_=true;
        return true;
    }
    void setControls(Controls c) {
        if(!prepared_ || !valid(c) || controls_==c)return;
        if(controls_.keySemitones!=c.keySemitones)
            stretch_.setTransposeSemitones(
                static_cast<float>(c.keySemitones),
                8000.f/static_cast<float>(sampleRate_));
        controls_=c;
    }
    Controls applied() const noexcept {return controls_;}
    bool active() const noexcept {
        return controls_.keySemitones!=0 || controls_.tempoPercent!=100;
    }
    int nextInputFrames(int outputFrames) noexcept {
        return inputFramesForOutput(outputFrames,controls_,fractionalInput_);
    }
    // in and out contain TWO separate PCM lanes, each sized for the supplied
    // count. No file IO here. Process is allowed to use heap on this WORKER.
    bool process(const float* const* in,int nInput,float* const* out,int nOut) {
        if(!prepared_ || !in || !out || !in[0] || !in[1] ||
           !out[0] || !out[1] || nInput<0 || nOut<=0)return false;
        if(!active()) {
            if(nInput!=nOut)return false;
            std::copy_n(in[0],nOut,out[0]);
            std::copy_n(in[1],nOut,out[1]);
        } else {
            stretch_.process(in,nInput,out,nOut);
        }
        for(int c=0;c<2;++c)
            for(int i=0;i<nOut;++i)
                if(!std::isfinite(out[c][i]))
                    return false; // source/FFT corruption fails closed
        return true;
    }
    int outputLatency() const noexcept {
        return prepared_&&active()?stretch_.outputLatency():0;
    }
    void flush(float* const* output,int frames) {
        if(prepared_&&active() && frames>0)
            stretch_.flush(output,frames,controls_.tempoPercent/100.f);
    }
private:
    signalsmith::stretch::SignalsmithStretch<float> stretch_{23};
    int sampleRate_=0;
    Controls controls_{};
    double fractionalInput_=0;
    bool prepared_=false;
};
} // namespace armix::player
