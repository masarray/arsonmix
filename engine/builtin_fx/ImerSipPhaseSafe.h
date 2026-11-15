// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

// ImerSip after P8.2: coherent/minimum-phase LF shelf + monotone SIDE shelf.
// Unlike independent Haas delays, neither stage inserts an out-of-polarity
// crossfeed. Both L/R bass filters share IDENTICAL coefficients.
// The SIDE delta is added to L and SUBTRACTED from R: mono fold-down equals
// the same coherent bass shelf applied to P8.2 MID, sample for sample.
// Native PCM only; all exponentials run in prepare(); PCM settings are affine only.
namespace armix::imersip {

struct Settings {
    int venue=0;             // 0 Concert Hall, 1 Cinema, 2 Live Room
    float immersion=.88f;    // clearly spatial music default; 100% still stronger
    float space=.78f;        // prominent directional ER without full reverb
    float width=.67f;        // ~130.5 degree stage, 180 available at max
    float bassImpact=.70f;   // source-anchored original music warmth
    float detail=.60f;       // source-anchored treble sparkle
    float outputTrim=1.f;    // P8.2 full-output linear trim
    float stereoLock=1.f;    // 100% retains >=90% original direct SIDE
    float lateRoom=.25f;     // lighter tail preserves front speaker focus
    float depth=.78f;        // 6.1m actual source/wall geometry
    float earAlignDegrees=0.f; // separate spectral KEMAR HRTF -30..+20°
};
inline bool valid(const Settings& s) noexcept {
    return s.venue>=0 && s.venue<=2 &&
        std::isfinite(s.immersion)&&s.immersion>=0.f&&s.immersion<=1.f &&
        std::isfinite(s.space)&&s.space>=0.f&&s.space<=1.f &&
        std::isfinite(s.width)&&s.width>=0.f&&s.width<=1.f &&
        std::isfinite(s.bassImpact)&&s.bassImpact>=0.f&&s.bassImpact<=1.f &&
        std::isfinite(s.detail)&&s.detail>=0.f&&s.detail<=1.f &&
        std::isfinite(s.outputTrim)&&s.outputTrim>=.25f&&s.outputTrim<=1.5f &&
        std::isfinite(s.stereoLock)&&s.stereoLock>=0.f&&s.stereoLock<=1.f &&
        std::isfinite(s.lateRoom)&&s.lateRoom>=0.f&&s.lateRoom<=1.f &&
        std::isfinite(s.depth)&&s.depth>=0.f&&s.depth<=1.f &&
        std::isfinite(s.earAlignDegrees) &&
        s.earAlignDegrees>=-30.f && s.earAlignDegrees<=20.f;
}

// This filter is NOT a second reverb, compressor, M/S limiter or tone
// replacement. It never attenuates the P8.2 output as a safety workaround.
// Near 0 dBFS only the ADDITIONAL enhancement is suppressed: P8.2 remains.
class PhaseSafeEnhancer final {
public:
    bool prepare(double rate) noexcept {
        ready_=false;
        if(!std::isfinite(rate) ||
           (std::abs(rate-44100.)>.1 && std::abs(rate-48000.)>.1))return false;
        constexpr double pi=3.1415926535897932384626433832795;
        bassAlpha_=static_cast<float>(1.-std::exp(-2.*pi*155./rate));
        sideAlpha_=static_cast<float>(1.-std::exp(-2.*pi*680./rate));
        detailAlpha_=static_cast<float>(1.-std::exp(-2.*pi*6100./rate));
        gainSlew_=static_cast<float>(1.-std::exp(-1./(rate*.025)));
        gateSlew_=static_cast<float>(1.-std::exp(-1./(rate*.035)));
        energySlew_=static_cast<float>(1.-std::exp(-1./(rate*.075)));
        reset();
        ready_=true;
        return true;
    }
    bool setSettings(const Settings& s) noexcept {
        if(!valid(s))return false;
        settings_=s;
        // Restore ORIGINAL source warmth and sparkle, not more KEMAR
        // coloured wet MID. Maximum coherent dry LF shelf is intentionally
        // below +2.8 dB at DC (1 + 0.255 * 1.45), with a broad 155 Hz
        // transition; this is NOT added to any room feedback path.
        // Coefficients are identical for L/R and designed in prepare().
        constexpr float cinemaMax=.255f;
        constexpr float liveMax=.235f;
        constexpr float concertMax=.255f;
        const float maxDelta=s.venue==1?cinemaMax:
            s.venue==2?liveMax:concertMax;
        // VENUE SPACE links a restrained (+0..45% of the ORIGINAL
        // positive delta) coherent 120Hz shelf to perceived room weight.
        // This is NOT a measured room mode or a delayed bass reflection.
        // Same filter coefficients per ear preserve the original LF phase.
        bassTarget_=maxDelta*s.bassImpact*s.immersion*
            (1.f+.45f*s.space);
        // Music SIDE retains original stereo, with bounded positive tilt.
        sideTarget_=.25f*s.width*s.immersion;
        // Source-derived open air >6.1kHz, not processed room brightness.
        // At max the linear HF shelf coefficient is +0.59; the actual
        // gain at 10k remains smaller due to one-pole transition.
        // No 1-4k vocal lift, inverted KEMAR notch or artificial echo.
        detailTarget_=.59f*s.detail*s.immersion;
        return true;
    }
    void reset() noexcept {
        bassL_=bassR_=sideLow_=midEnergy_=sideEnergy_=gate_=0.f;
        detailL_=detailR_=0.f;
        bassGain_=bassTarget_;
        sideGain_=sideTarget_;
        detailGain_=detailTarget_;
    }
    bool ready() const noexcept {return ready_;}
    Settings settings() const noexcept {return settings_;}

    // Record per-sample ORIGINAL stereo balance before P8.2 mutates PCM.
    // Caller keeps the returned gate in a fixed prepared scratch array.
    float captureGate(float originalL,float originalR) noexcept {
        // The SonKu core already rejects non-finite input samples. This
        // original-source detector runs BEFORE that sanitization: never
        // allow NaN, Inf or giant gain spikes into squared envelope state.
        const float safeL=std::isfinite(originalL)?
            std::clamp(originalL,-4.f,4.f):0.f;
        const float safeR=std::isfinite(originalR)?
            std::clamp(originalR,-4.f,4.f):0.f;
        const float m=.5f*(safeL+safeR);
        const float s=.5f*(safeL-safeR);
        midEnergy_+=energySlew_*(m*m-midEnergy_);
        sideEnergy_+=energySlew_*(s*s-sideEnergy_);
        const float stereoRatio=sideEnergy_/
            (sideEnergy_+midEnergy_+1.e-12f);
        const float gateTarget=std::clamp((stereoRatio-.015f)*3.f,0.f,1.f);
        gate_+=gateSlew_*(gateTarget-gate_);
        return gate_;
    }
    void process(float& l,float& r,float inputStereoGate,
                 float originalL,float originalR) noexcept {
        if(!ready_)return;
        // Apply tonal shelves to the ORIGINAL independent music L/R,
        // not the already-filtered measured HRTF/FDN output. This restores
        // familiar LF pressure and HF extension without multiplying the
        // mid-coloured reflected field or changing stereo arrival cues.
        bassL_+=bassAlpha_*(originalL-bassL_);
        bassR_+=bassAlpha_*(originalR-bassR_);
        bassGain_+=gainSlew_*(bassTarget_-bassGain_);
        sideGain_+=gainSlew_*(sideTarget_-sideGain_);
        detailGain_+=gainSlew_*(detailTarget_-detailGain_);
        detailL_+=detailAlpha_*(originalL-detailL_);
        detailR_+=detailAlpha_*(originalR-detailR_);
        const float bassDeltaL=bassGain_*bassL_;
        const float bassDeltaR=bassGain_*bassR_;
        // One-pole complementary HP has Re(H)>=0. Summing a POSITIVE
        // gain * highpassed SIDE cannot make a spectral SIDE notch.
        // It also leaves mono MID exactly unchanged.
        const float side=.5f*(l-r);
        sideLow_+=sideAlpha_*(side-sideLow_);
        const float delta=sideGain_*inputStereoGate*(side-sideLow_);
        // Equal-coefficient source-detail high shelf, not an extra HRTF.
        float addL=bassDeltaL+delta+detailGain_*(originalL-detailL_);
        float addR=bassDeltaR-delta+detailGain_*(originalR-detailR_);

        // Peak safety does not pull down the trusted P8.2 signal.
        // Unlike global AGC/limiter, it only scales optional additions.
        // Sample-peak safety != oversampled true peak; qualify separately.
        const float basePeak=std::max(std::abs(l),std::abs(r));
        const float available=std::max(0.f,.98f-basePeak);
        const float extra=std::max(std::abs(addL),std::abs(addR));
        if(extra>available && extra>0.f) {
            const float scale=available/extra;
            addL*=scale;
            addR*=scale;
        }
        l+=addL;
        r+=addR;
    }
private:
    Settings settings_{};
    float bassAlpha_=0.f,sideAlpha_=0.f,detailAlpha_=0.f;
    float gainSlew_=0.f,gateSlew_=0.f,energySlew_=0.f;
    float bassTarget_=0.f,sideTarget_=0.f,detailTarget_=0.f;
    float bassGain_=0.f,sideGain_=0.f,detailGain_=0.f;
    float bassL_=0.f,bassR_=0.f,sideLow_=0.f,detailL_=0.f,detailR_=0.f;
    float midEnergy_=0.f,sideEnergy_=0.f,gate_=0.f;
    bool ready_=false;
};
} // namespace armix::imersip
