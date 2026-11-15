// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cmath>
#include <cstddef>

// SonKu P8.4 single-graph research kernel compositor.
// Fuse center music-safe HRIR + bounded HIGH-PASS direction differences
// BEFORE the existing 2x2 FIR loop: one PCM publisher, no second core.
// A first-order high-pass is calculated by convolving a difference FIR
// with a one-pole low-pass. Kernel support remains bounded at N taps;
// the truncated LP tail must be included in independent impulse tests.
// Preparation: control only. merge(): target-update every 16 frames;
// no allocation, mutex, sleep, IO, FFT or extra audio callback.
namespace armix::sonku::p84 {
class FusedMusicHrir final {
public:
    bool prepare(double rate,float strength=.12f,float cutoffHz=750.f) noexcept {
        if(!std::isfinite(rate)||rate<44000.||rate>48100.||
           !std::isfinite(strength)||strength<0.f||strength>.30f||
           !std::isfinite(cutoffHz)||cutoffHz<400.f||cutoffHz>2400.f)
            return false;
        strength_=strength;
        alpha_=static_cast<float>(1.-std::exp(
            -2.*3.14159265358979323846*cutoffHz/rate));
        return true;
    }
    template<std::size_t N>
    void merge(const std::array<std::array<float,N>,2>& musicCenter,
               std::array<std::array<float,N>,2>& leftSpeaker,
               std::array<std::array<float,N>,2>& rightSpeaker) const noexcept {
        for(int ear=0;ear<2;++ear){
            float leftLow=0.f,rightLow=0.f;
            for(std::size_t tap=0;tap<N;++tap){
                const float center=musicCenter[ear][tap];
                const float leftDifference=leftSpeaker[ear][tap]-center;
                const float rightDifference=rightSpeaker[ear][tap]-center;
                leftLow+=alpha_*(leftDifference-leftLow);
                rightLow+=alpha_*(rightDifference-rightLow);
                leftSpeaker[ear][tap]=center+strength_*(leftDifference-leftLow);
                rightSpeaker[ear][tap]=center+strength_*(rightDifference-rightLow);
            }
        }
    }
private:
    float alpha_=0.f,strength_=0.f;
};
} // namespace armix::sonku::p84
