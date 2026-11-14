// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../third_party/signalsmith/signalsmith-stretch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace armix::player {
// WORKER THREAD ONLY. Signalsmith DSP state, FFT storage and scratch buffers
// are allocated off realtime. Never instantiate or call from JUCE callback.
//
// Tempo is rate (80% slower / 120% faster) and does not set musical pitch.
// Key transpose is independent of tempo. Both stereo channels share one
// phase-coherent processing instance rather than divergent mono stretchers.
// Normal playback stays bit-exact and free of FFT load. Re-entering DSP after
// bypass primes the spectral window from a bounded recent-input history;
// otherwise the first KEY gesture after a long neutral passage can start
// with a fresh, empty analysis window and produce an audible dropout.
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
        // Prefer the 120 ms / 30 ms default (higher-overlap) preset for
        // polyphonic backing music; split FFT work across worker blocks.
        // This is NOT called on the ASIO/audio callback.
        stretch_.presetDefault(2,static_cast<float>(sampleRate_),true);
        const auto maxHistory=static_cast<int>(std::ceil(
            stretch_.outputSeekLength(1.2f)))+8;
        historyCapacity_=std::max(1,maxHistory);
        for(int lane=0;lane<2;++lane) {
            history_[lane].assign(static_cast<std::size_t>(historyCapacity_),0.f);
            prime_[lane].assign(static_cast<std::size_t>(historyCapacity_),0.f);
        }
        historyWrite_=historyCount_=0;
        primeOnNextProcess_=false;
        currentSemitones_=static_cast<float>(c.keySemitones);
        stretch_.setTransposeSemitones(
            static_cast<float>(controls_.keySemitones),
            8000.f/static_cast<float>(sampleRate_));
        fractionalInput_=0;
        prepared_=true;
        return true;
    }
    void setControls(Controls c) {
        if(!prepared_ || !valid(c) || controls_==c)return;
        const bool wasActive=active();
        const bool willBeActive=c.keySemitones!=0 || c.tempoPercent!=100;
        // Both transitions occur exclusively on the decoding worker.
        if(!wasActive && willBeActive) {
            currentSemitones_=0.f;
            primeOnNextProcess_=true;
        } else if(wasActive && !willBeActive) {
            currentSemitones_=0.f;
            primeOnNextProcess_=false;
        }
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
            // Smooth transposition in the worker at block cadence. At 48k
            // with 1024-frame blocks the 60 ms time constant spans several
            // FFT updates, avoiding a single hard frequency-map jump.
            const float target=static_cast<float>(controls_.keySemitones);
            if(std::abs(currentSemitones_-target)>0.0001f) {
                const float alpha=1.f-std::exp(
                    -static_cast<float>(nOut)/(0.060f*sampleRate_));
                currentSemitones_+=(target-currentSemitones_)*alpha;
                if(std::abs(currentSemitones_-target)<0.0001f)
                    currentSemitones_=target;
            }
            stretch_.setTransposeSemitones(currentSemitones_,
                                           8000.f/static_cast<float>(sampleRate_));
            if(primeOnNextProcess_)primeFromRecentInput();
            stretch_.process(in,nInput,out,nOut);
        }
        for(int c=0;c<2;++c)
            for(int i=0;i<nOut;++i)
                if(!std::isfinite(out[c][i]))
                    return false; // source/FFT corruption fails closed
        rememberInput(in,nInput);
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
    void primeFromRecentInput() {
        // Signalsmith outputSeek() aligns the next processed PCM with the
        // current source position. Unlike a plain reset/seek, it synthesises
        // the required pre-roll output. All buffers are allocated in prepare().
        const int count=std::clamp(
            static_cast<int>(std::ceil(stretch_.outputSeekLength(
                static_cast<float>(controls_.tempoPercent)/100.f))),
            1,historyCapacity_);
        const int available=std::min(historyCount_,count);
        const int missing=count-available;
        for(int lane=0;lane<2;++lane) {
            std::fill_n(prime_[lane].data(),missing,0.f);
            for(int i=0;i<available;++i) {
                const int pos=(historyWrite_+historyCapacity_-available+i)
                              %historyCapacity_;
                prime_[lane][static_cast<std::size_t>(missing+i)]=
                    history_[lane][static_cast<std::size_t>(pos)];
            }
        }
        const float* const previous[]={prime_[0].data(),prime_[1].data()};
        stretch_.outputSeek(previous,count);
        primeOnNextProcess_=false;
    }
    void rememberInput(const float* const* in,int frames) noexcept {
        // No separate DSP warm-up during a neutral song: bounded two-lane
        // history lets the next active block start without a cold FFT window.
        for(int i=0;i<frames;++i) {
            history_[0][static_cast<std::size_t>(historyWrite_)]=in[0][i];
            history_[1][static_cast<std::size_t>(historyWrite_)]=in[1][i];
            if(++historyWrite_==historyCapacity_)historyWrite_=0;
            if(historyCount_<historyCapacity_)++historyCount_;
        }
    }
    signalsmith::stretch::SignalsmithStretch<float> stretch_{23};
    std::array<std::vector<float>,2> history_,prime_;
    int sampleRate_=0,historyCapacity_=0,historyWrite_=0,historyCount_=0;
    Controls controls_{};
    double fractionalInput_=0;
    float currentSemitones_=0.f;
    bool prepared_=false,primeOnNextProcess_=false;
};
} // namespace armix::player
