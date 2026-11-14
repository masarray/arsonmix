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
enum class SonKuReverbPreset : std::uint8_t { StudioGlue=0, AiryChamber=1, LargeRoom=2 };

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
        // All room-aware coefficients prepared once OFF the PCM callback.
        prepared_[0]=makeCoefficients(.78f,5900.f);
        prepared_[1]=makeCoefficients(1.28f,7000.f);
        prepared_[2]=makeCoefficients(2.05f,6000.f);
        targetGain_=gain_;targetDamp_=dampCoeff_;
#if defined(SONKU_P82_GEOMETRIC_LATE_RESEARCH) && SONKU_P82_GEOMETRIC_LATE_RESEARCH
        // Sound travels approximately one room width before a diffuse
        // multi-bounce field becomes dominant. Prepared once, NEVER
        // evaluate sqrt/pow/trig or allocate on the realtime callback.
        constexpr std::array<float,3> widths{7.2f,14.4f,28.8f};
        for(int k=0;k<3;++k)
            preparedPredelay_[k]=std::clamp(static_cast<int>(std::lround(
                widths[k]*sampleRate_/343.f)),1,predelayRing-3);
        predelayActive_=predelayTarget_=predelayPrevious_=preparedPredelay_[0];
        predelayPending_=-1;predelayRemaining_=0;
#endif
        realtimeMode_=false;
        reset();
        ready_=true;
        return true;
    }
    void reset() noexcept {
        for(auto& line:memory_)line.fill(0.f);
        lowpass_.fill(0.f);
        write_=0;amount_=0.f;validFrames_=0;
#if defined(SONKU_P82_GEOMETRIC_LATE_RESEARCH) && SONKU_P82_GEOMETRIC_LATE_RESEARCH
        for(auto& channel:predelayMemory_)channel.fill(0.f);
        predelayWrite_=0;predelayValidFrames_=0;
        predelayActive_=predelayTarget_;predelayPrevious_=predelayTarget_;
        predelayPending_=-1;predelayRemaining_=0;
#endif
        // The target amount is a CONTROL-side setting and survives resetting
        // host transport/processor so the same program can restart cleanly.
    }
    bool setAmount(float amount) noexcept {
        if(!std::isfinite(amount) || amount<0.f || amount>1.f)return false;
        if(amount_==0.f && amountTarget_==0.f && amount>0.f)reset();
        amountTarget_=amount;
        return true;
    }
    bool setPreset(SonKuReverbPreset value) noexcept {
        if(value!=SonKuReverbPreset::StudioGlue &&
           value!=SonKuReverbPreset::AiryChamber)return false;
        // Preset updates calculate RT60 coefficients. Never use this setter
        // on an active realtime callback; native host doesn't expose it yet.
        if(active())return false;
        preset_=value;
        configure();
        return true;
    }
    // Realtime-safe preset/wet update: only POD/table copies, no exponentials,
    // heap, locks or clearing 128 KiB of delay memory in the audio callback.
    // A validity horizon ensures no stale tail from a previous OFF period.
    bool setRealtimeProgram(SonKuReverbPreset preset,float amount) noexcept {
        if((preset!=SonKuReverbPreset::StudioGlue &&
            preset!=SonKuReverbPreset::AiryChamber &&
            preset!=SonKuReverbPreset::LargeRoom)||
           !std::isfinite(amount)||amount<0.f||amount>1.f)return false;
        const bool previouslyActive=active();
        if(!previouslyActive && amount>0.f){
            validFrames_=0;lowpass_.fill(0.f);
#if defined(SONKU_P82_GEOMETRIC_LATE_RESEARCH) && SONKU_P82_GEOMETRIC_LATE_RESEARCH
            // Freshness horizon, no 32 KiB RT clear and no stale tail.
            predelayValidFrames_=0;
#endif
        }
#if defined(SONKU_P82_GEOMETRIC_LATE_RESEARCH) && SONKU_P82_GEOMETRIC_LATE_RESEARCH
        const int desiredDelay=preparedPredelay_[static_cast<int>(preset)];
        if(!previouslyActive){
            predelayActive_=predelayPrevious_=predelayTarget_=desiredDelay;
            predelayRemaining_=0;predelayPending_=-1;
        }else if(desiredDelay!=predelayTarget_) {
            if(predelayRemaining_==0){
                predelayPrevious_=predelayActive_;
                predelayTarget_=desiredDelay;
                predelayRemaining_=predelayFadeFrames;
            }else{
                // Latest wins without snapping a moving fractional delay;
                // finish the audible crossfade before starting the next.
                predelayPending_=desiredDelay;
            }
        }else if(predelayRemaining_>0) {
            // A newer edit can cancel a queued retarget: never re-apply an
            // obsolete pending room after returning to the active goal.
            predelayPending_=-1;
        }
#endif
        const auto& coefficients=prepared_[static_cast<int>(preset)];
        targetGain_=coefficients.gains;
        targetDamp_=coefficients.damping;
        if(!realtimeMode_){gain_=targetGain_;dampCoeff_=targetDamp_;}
        realtimeMode_=true;
        preset_=preset;
        amountTarget_=amount;
        return true;
    }
    SonKuReverbPreset preset() const noexcept {return preset_;}
    bool active() const noexcept {return ready_&&(amount_>0.f||amountTarget_>0.f);}
    float targetAmount() const noexcept {return amountTarget_;}
#if defined(SONKU_P82_GEOMETRIC_LATE_RESEARCH) && SONKU_P82_GEOMETRIC_LATE_RESEARCH
    int preparedLatePredelaySamples() const noexcept {
        return preparedPredelay_[static_cast<int>(preset_)];
    }
#endif
    Ears process(float left,float right) noexcept {
        if(!active())return {};
        const float a=std::isfinite(left)?left:0.f;
        const float b=std::isfinite(right)?right:0.f;
        const float mid=.5f*(a+b);
        const float side=.5f*(a-b);
#if defined(SONKU_P82_GEOMETRIC_LATE_RESEARCH) && SONKU_P82_GEOMETRIC_LATE_RESEARCH
        // Direct and six-wall ER are untouched; only the late FDN input
        // takes a physically dimensioned diffuse-field propagation delay.
        auto readDelayed=[&](int d,int channel) noexcept {
            if(predelayValidFrames_<d)return 0.f;
            return predelayMemory_[channel][(predelayWrite_-d)&
                                           (predelayRing-1)];
        };
        float lateMid=readDelayed(predelayTarget_,0);
        float lateSide=readDelayed(predelayTarget_,1);
        if(predelayRemaining_>0) {
            const float t=1.f-static_cast<float>(predelayRemaining_)/
                                         predelayFadeFrames;
            lateMid=(1.f-t)*readDelayed(predelayPrevious_,0)+t*lateMid;
            lateSide=(1.f-t)*readDelayed(predelayPrevious_,1)+t*lateSide;
            --predelayRemaining_;
            if(predelayRemaining_==0) {
                predelayActive_=predelayTarget_;
                if(predelayPending_>=0 &&
                   predelayPending_!=predelayActive_) {
                    predelayPrevious_=predelayActive_;
                    predelayTarget_=predelayPending_;
                    predelayRemaining_=predelayFadeFrames;
                }
                predelayPending_=-1;
            }
        }
        predelayMemory_[0][predelayWrite_]=mid;
        predelayMemory_[1][predelayWrite_]=side;
        predelayWrite_=(predelayWrite_+1)&(predelayRing-1);
        if(predelayValidFrames_<predelayRing)++predelayValidFrames_;
#else
        const float lateMid=mid,lateSide=side;
#endif
        std::array<float,lineCount> y{},mix{};
        if(realtimeMode_)dampCoeff_+=slew_*(targetDamp_-dampCoeff_);
        for(int i=0;i<lineCount;++i){
            const int index=(write_-delay_[i])&(ringFrames-1);
            const float sample=(realtimeMode_&&validFrames_<delay_[i])
                ?0.f:memory_[i][index];
            lowpass_[i]+=dampCoeff_*(sample-lowpass_[i]);
            if(realtimeMode_)gain_[i]+=slew_*(targetGain_[i]-gain_[i]);
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
                (lateMid*inMid[i]+lateSide*inSide[i]);
        }
        write_=(write_+1)&(ringFrames-1);
        if(realtimeMode_&&validFrames_<ringFrames)++validFrames_;
        amount_+=slew_*(amountTarget_-amount_);
        if(std::abs(amount_-amountTarget_)<1.e-5f)amount_=amountTarget_;
        // Wet-only. The KEMAR direct field and physical reflections retain
        // their unaltered level and timing. Output is deliberately restrained.
        return {.left=wetLeft*.23f*amount_,
                .right=wetRight*.23f*amount_};
    }
private:
    struct Coefficients {
        std::array<float,lineCount> gains{};
        float damping=0.f;
    };
    Coefficients makeCoefficients(float rt60,float cutoff) const noexcept {
        Coefficients prepared{};
        constexpr float pi=3.14159265358979323846f;
        prepared.damping=1.f-std::exp(-2.f*pi*cutoff/sampleRate_);
        for(int i=0;i<lineCount;++i)
            prepared.gains[i]=std::exp(-6.9077553f*
                static_cast<float>(delay_[i])/(sampleRate_*rt60));
        return prepared;
    }
    void configure() noexcept {
        // Incommensurate lengths at 48k, scaled once at prepare/control side.
        // First late energy arrives ~44 ms: behind earliest image reflections.
        constexpr std::array<int,lineCount> lengths{
            2111,2339,2579,2803,3079,3319,3593,3929};
        const float rt60=preset_==SonKuReverbPreset::StudioGlue?.78f:1.28f;
        const float cutoff=preset_==SonKuReverbPreset::StudioGlue?5900.f:7000.f;
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
#if defined(SONKU_P82_GEOMETRIC_LATE_RESEARCH) && SONKU_P82_GEOMETRIC_LATE_RESEARCH
    static constexpr int predelayRing=4096;
    static constexpr int predelayFadeFrames=512;
    static_assert((predelayRing&(predelayRing-1))==0);
    std::array<std::array<float,predelayRing>,2> predelayMemory_{};
    std::array<int,3> preparedPredelay_{};
    int predelayWrite_=0,predelayValidFrames_=0;
    int predelayActive_=0,predelayTarget_=0,predelayPrevious_=0;
    int predelayPending_=-1,predelayRemaining_=0;
#endif
    std::array<std::array<float,ringFrames>,lineCount> memory_{};
    std::array<float,lineCount> lowpass_{};
    std::array<float,lineCount> gain_{},targetGain_{};
    std::array<Coefficients,3> prepared_{};
    std::array<int,lineCount> delay_{};
    float sampleRate_=48000.f,dampCoeff_=0.f,targetDamp_=0.f,slew_=0.f;
    float amount_=0.f,amountTarget_=0.f;
    int write_=0,validFrames_=0;
    bool realtimeMode_=false;
    SonKuReverbPreset preset_=SonKuReverbPreset::StudioGlue;
    bool ready_=false;
};
} // namespace armix::sonku
