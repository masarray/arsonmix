// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Compact eight-line stereo FDN for musical late ambience. The 8x8 Hadamard
// feedback matrix is orthogonal (unit-energy): stability depends solely on
// prepared per-line RT60 gains and damping. No heap/locks/FFT/trig/I/O in process.
// A small, diffuse late field complements the already-approved square early
// reflections; no source-following stereo pan is imposed on the reverb tail.
// These listening-test presets are OFF in the shipping JUCE editor.
namespace armix::sonku {
enum class SonKuReverbPreset : std::uint8_t {
    StudioGlue=0, AiryChamber=1, SpaciousChamber=2, LargeAuditorium=3
};
// Control-side room scale policy: explicit, predictable candidates rather
// than making RT60 linearly proportional to room dimensions (which sounds
// unnaturally hollow and rings). No physically calibrated hall claim.
struct SonKuRoomAmbience {
    SonKuReverbPreset preset=SonKuReverbPreset::StudioGlue;
    float wet=0.f;
};
inline SonKuRoomAmbience musicalAmbienceForRoom(
    float widthMeters,float depthMeters,float distanceMeters) noexcept {
    if(!std::isfinite(widthMeters)||!std::isfinite(depthMeters)||
       !std::isfinite(distanceMeters)||widthMeters<5.f||
       depthMeters<5.f||widthMeters>40.f||depthMeters>40.f||
       distanceMeters<1.6f||distanceMeters>10.f||
       2.f*(distanceMeters+.5f)>std::min(widthMeters,depthMeters))
        return {}; // Safe OFF, not an unbounded wet impulse.
    const float span=std::sqrt(widthMeters*depthMeters);
    const float distanceScale=std::clamp(1.6f/distanceMeters,.16f,1.f);
    // Smaller distant-source input must not suddenly exaggerate late tails.
    const float wet=.26f+.12f*std::sqrt(distanceScale);
    if(span<10.f)return {SonKuReverbPreset::StudioGlue,wet};
    if(span<20.f)return {SonKuReverbPreset::SpaciousChamber,wet};
    return {SonKuReverbPreset::LargeAuditorium,wet};
}

class SonKuMusicalReverb final {
public:
    struct Ears {float left=0.f,right=0.f;};
    static constexpr int lineCount=8;
    static constexpr int ringFrames=4096;
    bool prepare(double rate) noexcept {
        ready_=false;
        if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 &&
                                    std::abs(rate-48000.)>.1))return false;
        sampleRate_=static_cast<float>(rate);
        slew_=static_cast<float>(1.-std::exp(-1./(rate*.012)));
        amountTarget_=0.f;
        preset_=SonKuReverbPreset::StudioGlue;
        configure();
        reset();
        ready_=true;
        return true;
    }
    void reset() noexcept {
        for(auto& line:memory_)line.fill(0.f);
        lowpass_.fill(0.f);
        write_=0;activeFrames_=0;amount_=0.f;
        // The target amount is a CONTROL-side setting and survives resetting
        // host transport/processor so the same program can restart cleanly.
    }
    bool setAmount(float amount) noexcept {
        if(!std::isfinite(amount) || amount<0.f || amount>1.f)return false;
        // O(1) safe realtime rearm: no 128KiB ring clear. Delayed
        // reads are masked until new post-rearm samples fill their tap.
        if(amount_==0.f && amountTarget_==0.f && amount>0.f) {
            lowpass_.fill(0.f);write_=0;activeFrames_=0;
        }
        amountTarget_=amount;
        return true;
    }
    bool setPreset(SonKuReverbPreset value) noexcept {
        if(value!=SonKuReverbPreset::StudioGlue &&
           value!=SonKuReverbPreset::AiryChamber &&
           value!=SonKuReverbPreset::SpaciousChamber &&
           value!=SonKuReverbPreset::LargeAuditorium)return false;
        // Preset updates calculate RT60 coefficients. Never use this setter
        // on an active realtime callback; native host doesn't expose it yet.
        if(active())return false;
        preset_=value;
        configure();
        return true;
    }
    SonKuReverbPreset preset() const noexcept {return preset_;}
    bool active() const noexcept {return ready_&&(amount_>0.f||amountTarget_>0.f);}
    float targetAmount() const noexcept {return amountTarget_;}
    Ears process(float left,float right) noexcept {
        if(!active())return {};
        const float a=std::isfinite(left)?left:0.f;
        const float b=std::isfinite(right)?right:0.f;
        const float mid=.5f*(a+b);
        const float side=.5f*(a-b);
        std::array<float,lineCount> y{},mix{};
        for(int i=0;i<lineCount;++i){
            const int index=(write_-delay_[i])&(ringFrames-1);
            const float sample=activeFrames_>=delay_[i]?
                memory_[i][index]:0.f;
            lowpass_[i]+=dampCoeff_*(sample-lowpass_[i]);
            y[i]=sample;
            mix[i]=lowpass_[i]*gain_[i];
        }
        // Fast orthogonal Hadamard transform, three fixed butterfly stages.
        // 8x8 is exactly normalized by 1/sqrt(8) and cannot grow energy.
        for(int size=1;size<lineCount;size*=2){
            for(int block=0;block<lineCount;block+=size*2){
                for(int j=0;j<size;++j){
                    const float x=mix[block+j],z=mix[block+j+size];
                    mix[block+j]=x+z;
                    mix[block+j+size]=x-z;
                }
            }
        }
        constexpr float unitary=.3535533905932738f;
        constexpr std::array<float,lineCount> inMid{1,1,-1,1,1,-1,-1,1};
        constexpr std::array<float,lineCount> inSide{1,-1,1,-1,1,-1,1,-1};
        constexpr std::array<float,lineCount> outL{1,-1,1,1,-1,1,-1,1};
        constexpr std::array<float,lineCount> outR{-1,1,1,-1,1,1,-1,1};
        float wetLeft=0.f,wetRight=0.f;
        for(int i=0;i<lineCount;++i){
            wetLeft+=y[i]*outL[i];
            wetRight+=y[i]*outR[i];
            memory_[i][write_]=unitary*mix[i]+.26f*
                (mid*inMid[i]+side*inSide[i]);
        }
        write_=(write_+1)&(ringFrames-1);
        if(activeFrames_<ringFrames)++activeFrames_;
        amount_+=slew_*(amountTarget_-amount_);
        if(std::abs(amount_-amountTarget_)<1.e-5f)amount_=amountTarget_;
        // Wet-only. The KEMAR direct field and physical reflections retain
        // their unaltered level and timing. Output is deliberately restrained.
        return {.left=wetLeft*.23f*amount_,
                .right=wetRight*.23f*amount_};
    }
private:
    void configure() noexcept {
        // Incommensurate lengths at 48k, scaled once at prepare/control side.
        // First late energy arrives ~44 ms: behind earliest image reflections.
        constexpr std::array<int,lineCount> lengths{
            2111,2339,2579,2803,3079,3319,3593,3929};
        // Musical tuning, not volume-derived Sabine RT60. Prepared once,
        // never recomputed per sample; original two presets remain identical.
        float rt60=0.78f,cutoff=5900.f;
        switch(preset_) {
            case SonKuReverbPreset::StudioGlue: break;
            case SonKuReverbPreset::AiryChamber: rt60=1.28f;cutoff=7000.f;break;
            case SonKuReverbPreset::SpaciousChamber: rt60=1.65f;cutoff=5400.f;break;
            case SonKuReverbPreset::LargeAuditorium: rt60=2.20f;cutoff=4700.f;break;
        }
        constexpr float pi=3.14159265358979323846f;
        dampCoeff_=1.f-std::exp(-2.f*pi*cutoff/sampleRate_);
        for(int i=0;i<lineCount;++i){
            delay_[i]=std::clamp(static_cast<int>(std::lround(
                lengths[i]*sampleRate_/48000.f)),1,ringFrames-2);
            gain_[i]=std::exp(-6.9077553f*
                static_cast<float>(delay_[i])/(sampleRate_*rt60));
        }
    }
    static_assert((ringFrames&(ringFrames-1))==0);
    std::array<std::array<float,ringFrames>,lineCount> memory_{};
    std::array<float,lineCount> lowpass_{};
    std::array<float,lineCount> gain_{};
    std::array<int,lineCount> delay_{};
    float sampleRate_=48000.f,dampCoeff_=0.f,slew_=0.f;
    float amount_=0.f,amountTarget_=0.f;
    int write_=0,activeFrames_=0;
    SonKuReverbPreset preset_=SonKuReverbPreset::StudioGlue;
    bool ready_=false;
};
} // namespace armix::sonku
