// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Deterministic, bounded, single-audio-owner backing-singer individualization.
// Prepared on non-realtime thread. No random generator, heap, mutex, worker
// or transcendental functions in the PCM callback.
namespace armix::harmony {
class EnsembleSpace final {
public:
    static constexpr int voices=5;
    static constexpr std::size_t reflectionCapacity=8192;
    static constexpr std::size_t sineCount=1024;
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        constexpr float tau=6.2831853071795864769f;
        constexpr std::array<float,voices> arrivalMs{{5.f,9.f,12.f,15.f,17.f}};
        constexpr std::array<float,voices> reflectionMs{{12.f,15.f,17.f,19.f,20.f}};
        constexpr std::array<float,voices> secondMs{{27.f,31.f,34.f,36.f,38.f}};
        constexpr std::array<float,voices> cents{{-2.8f,1.4f,3.2f,-1.7f,2.3f}};
        constexpr std::array<float,voices> vibratoHz{{4.7f,5.2f,4.9f,5.35f,4.55f}};
        constexpr std::array<float,voices> vibratoCents{{2.4f,3.0f,2.8f,2.6f,3.3f}};
        constexpr std::array<float,voices> timbre{{.93f,.86f,.96f,.89f,.83f}};
        for(std::size_t i=0;i<sineCount;++i)
            sine_[i]=std::sin(tau*static_cast<float>(i)/sineCount);
        for(int v=0;v<voices;++v){
            const auto ix=static_cast<std::size_t>(v);
            offset_[ix]=static_cast<float>(rate*arrivalMs[ix]*.001);
            reflectionDelay_[ix]=static_cast<int>(std::lround(rate*reflectionMs[ix]*.001));
            secondDelay_[ix]=static_cast<int>(std::lround(rate*secondMs[ix]*.001));
            if(reflectionDelay_[ix]<2||
               reflectionDelay_[ix]>=static_cast<int>(reflectionCapacity)||
               secondDelay_[ix]<=reflectionDelay_[ix]||
               secondDelay_[ix]>=static_cast<int>(reflectionCapacity))return false;
            ratio_[ix]=std::pow(2.f,cents[ix]/1200.f);
            vibratoRatio_[ix]=vibratoCents[ix]*(.69314718056f/1200.f);
            increment_[ix]=vibratoHz[ix]/static_cast<float>(rate);
            phase_[ix]=static_cast<float>(ix)*.197f;
            tone_[ix]=timbre[ix];
            reflections_[ix].fill(0.f);
        }
        // A sample-rate-stable, shallow air/room HF shading of backing
        // singers only. Bypassed exactly when Space=0; lead is untouched.
        shadingAlpha_=static_cast<float>(
            1.-std::exp(-2.*3.141592653589793*4400./rate));
        distanceLowpass_.fill(0.f);
        ageStart_=static_cast<int>(rate*.065);
        ageRamp_=static_cast<float>(1./(rate*.165));
        expressionAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.018)));
        expression_=0.f;age_=0;cursor_=0;
        return true;
    }
    float shadeBacking(int v,float sample,float space) noexcept {
        const auto i=static_cast<std::size_t>(v);
        distanceLowpass_[i]+=shadingAlpha_*(sample-distanceLowpass_[i]);
        const float blend=.52f*std::clamp(space,0.f,1.f);
        return sample*(1.f-blend)+distanceLowpass_[i]*blend;
    }
    float arrivalSamples(int v) const noexcept {
        return offset_[static_cast<std::size_t>(v)];
    }
    int reflectionSamples(int v) const noexcept {
        return reflectionDelay_[static_cast<std::size_t>(v)];
    }
    float timbreCoefficient(int v,float colorCoefficient) const noexcept {
        return colorCoefficient*tone_[static_cast<std::size_t>(v)];
    }
    // All 5 LFOs have independent, deterministic phase and frequency.
    // Sustain vibrato appears gradually; no per-block reset or random jitter.
    float pitchRatio(int v) noexcept {
        const auto i=static_cast<std::size_t>(v);
        const float position=phase_[i]*static_cast<float>(sineCount);
        const auto index=static_cast<std::size_t>(position);
        const auto following=(index+1)&(sineCount-1);
        const float fraction=position-static_cast<float>(index);
        const float wave=sine_[index]+(sine_[following]-sine_[index])*fraction;
        phase_[i]+=increment_[i];
        if(phase_[i]>=1.f)phase_[i]-=1.f;
        return ratio_[i]*(1.f+wave*vibratoRatio_[i]*expression_);
    }
    // Exactly one filtered early reflection from each independent singer.
    // Fixed integer tap delay: changing Space can never sweep a delay line
    // or create a pitch chirp. 4096 samples covers 20ms even at 192k.
    float exchangeReflection(int v,float sample) noexcept {
        const auto i=static_cast<std::size_t>(v);
        reflections_[i][cursor_]=sample;
        const auto delayed=(cursor_+reflectionCapacity-
            static_cast<std::size_t>(reflectionDelay_[i]))&(reflectionCapacity-1);
        return reflections_[i][delayed];
    }
    // A distinct later early-reflection arrival from the very same bounded
    // singer ring. Call after exchangeReflection() writes the current sample.
    float secondReflection(int singer) const noexcept {
        const auto i=static_cast<std::size_t>(singer);
        const auto at=(cursor_+reflectionCapacity-
            static_cast<std::size_t>(secondDelay_[i]))&(reflectionCapacity-1);
        return reflections_[i][at];
    }
    int secondReflectionSamples(int singer) const noexcept {
        return secondDelay_[static_cast<std::size_t>(singer)];
    }
    void beginNote() noexcept { age_=0; }
    void advance(bool voiced) noexcept {
        cursor_=(cursor_+1)&(reflectionCapacity-1);
        if(!voiced)age_=0;
        else if(age_<100000000)++age_;
        const float mature=std::clamp((age_-ageStart_)*ageRamp_,0.f,1.f);
        expression_+=expressionAlpha_*(mature-expression_);
    }
private:
    std::array<float,sineCount> sine_{};
    std::array<std::array<float,reflectionCapacity>,voices> reflections_{};
    std::array<float,voices> offset_{},ratio_{},vibratoRatio_{};
    std::array<float,voices> increment_{},phase_{},tone_{},distanceLowpass_{};
    float shadingAlpha_=0.f;
    std::array<int,voices> reflectionDelay_{},secondDelay_{};
    std::size_t cursor_=0;
    int age_=0,ageStart_=0;
    float ageRamp_=0.f,expressionAlpha_=0.f,expression_=0.f;
};
} // namespace armix::harmony
