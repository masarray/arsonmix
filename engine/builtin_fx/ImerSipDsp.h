// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ImerSipPhaseSafe.h"
#include "ImerSipConcertStage.h"
#include "sonku/SonKuSpatialCore.h"
#include "sonku/SonKuP87MusicalAirBody.h"
#include <algorithm>
#include <array>
#include <cmath>

// Single measured P8.2/P8.4 core + P8.6 front-focused walls, genuine P8.7
// direct air/FDN body + bandlimited concert-stage lateral early reflection.
// No dry SIDE widening, fake echoes, second callback, runtime heap or FFT.
namespace armix::imersip {

inline armix::sonku::Settings sceneFor(const Settings& in) noexcept {
    armix::sonku::Settings s{};
    s.moving=false;                     // Never auto-orbit music
    s.startAzimuthDegrees=0.f;          // Front stationary anchor
    // Real two-loudspeaker front arc. Old 95% default meant 97/100°,
    // so MAX changed angle by just 3° and sounded like an inert knob.
    s.stereoSpreadDegrees=32.f+94.f*in.width; // +/-16..63 degrees
    s.spatialMix=.75f*in.immersion;     // SonKu internal HRIR, direct explicitly exported
    s.outputTrim=1.f;                  // Final trim belongs to ImerSip
    s.spacePreset=in.immersion==0.f?0:(in.venue==2?2:3);
    // The original always-fixed 1.6m was the missing externalization
    // dimension. Depth now drives actual measured six-wall path geometry
    // (and the physical source radius) from 1.6m to 7.4m, always at
    // listener ear height. No false room rotation or new audio graph.
    s.sourceDistanceMeters=1.6f+5.8f*in.depth;
    // Detail-first front-row hall: prioritize six-wall EARLY reflections
    // (external cues) above diffuse LATE decay (mid masking).
    // The original P8.2 room model remains the only acoustic engine.
    // ZERO early control is real zero, not the old hidden 20% baseline.
    // Headroom is normalized by the measured early-reflection PCM and
    // independent FDN/phase protection, not by a second room simulator.
    s.roomAmount=in.immersion==0.f?0.f:
        (in.venue==1?.77f:in.venue==2?.72f:.88f)*in.space;
    // Independent physical late FDN level: old SPACE knob silently boosted
    // early + late together and made fully-maxed sessions sound washed out.
    s.ambience=in.immersion==0.f?0.f:
        (in.venue==1?.52f:in.venue==2?.48f:.56f)*in.lateRoom;
    return s;
}

class ImerSipDsp final {
public:
    // Source-test A/B disables P8.7 while preserving exact shared geometry.
    explicit ImerSipDsp(bool musicalAirBody=true,
                        bool concertStage=true) noexcept
      : p87Enabled_(musicalAirBody),concertStageEnabled_(concertStage) {}
    bool prepare(double rate) noexcept {
        ready_=false;
#if defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) && SONKU_P82_MULTIBAND_ROOM_RESEARCH \
 && defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH \
 && defined(SONKU_P75_MUSIC_SAFE_RESEARCH) && SONKU_P75_MUSIC_SAFE_RESEARCH \
 && defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) && SONKU_P7_MEASURED_ELEVATION_RESEARCH
        ready_=core_.prepare(rate,
            armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch);
        if(ready_)ready_=core_.setSettings(sceneFor(settings_));
        // An actual pair of measured virtual loudspeakers at +/- width/2.
        // P8.2's default center-MID path is untouched for ordinary SonKu.
        // We take only L-R binaural difference; direct music MID remains dry.
        if(ready_)ready_=core_.enableImerSipMeasuredStereoSide(true);
        if(ready_)ready_=core_.setImerSipDirectHrtfElevationTrim(
            settings_.earAlignDegrees);
        if(ready_)ready_=enhancer_.prepare(rate);
        if(ready_)ready_=enhancer_.setSettings(settings_);
        if(ready_)ready_=p87Body_.prepare(rate);
        if(ready_)ready_=concertStage_.prepare(rate);
        if(ready_) {
            stageSlew_=static_cast<float>(1.-std::exp(-1./(rate*.018)));
            trimSlew_=static_cast<float>(1.-std::exp(-1./(rate*.024)));
            constexpr double pi=3.14159265358979323846;
            earlyHp_=static_cast<float>(1.-std::exp(-2.*pi*220./rate));
            lateHp_=static_cast<float>(1.-std::exp(-2.*pi*160./rate));
            // Four-pole 480Hz difference HP expands speaker midrange
            // relative to 700Hz while retaining the old strict LF tests.
            directHp_=static_cast<float>(1.-std::exp(-2.*pi*480./rate));
            limiterRelease_=static_cast<float>(1./(rate*.080));
            compareSlew_=static_cast<float>(1.-std::exp(-1./(rate*.020)));
            for(auto& ear:directDifferenceLow_)ear.fill(0.f);
            directMeasuredSideLow_.fill(0.f);
            earlyLow_.fill(0.f);lateLow_.fill(0.f);
            finalLimiter_=1.f;
            updateTargets(settings_);
            directGain_=directTarget_;
            stereoKeep_=stereoKeepTarget_;
            compareDryGain_=compareDryTarget_;
            earlyGain_=earlyTarget_;
            lateGain_=lateTarget_;
            outputGain_=settings_.outputTrim;
            core_.reset();
            enhancer_.reset();
            p87Body_.reset();
            concertStage_.reset();
        }
#endif
        return ready_;
    }
    bool setSettings(const Settings& next) noexcept {
        if(!valid(next))return false;
        if(ready_ && !core_.setSettings(sceneFor(next)))return false;
        if(ready_ && !core_.setImerSipDirectHrtfElevationTrim(
                               next.earAlignDegrees))return false;
        if(!enhancer_.setSettings(next))return false;
        settings_=next;
        updateTargets(next);
        return true;
    }
    Settings settings() const noexcept {return settings_;}
    // Transient monitor audition only: does not alter accepted settings,
    // .smix or the source/DSP graph. Called once before each audio block.
    void setCompareDry(bool dry) noexcept {compareDryTarget_=dry?1.f:0.f;}
    bool ready() const noexcept {return ready_;}
    bool measuredStereoStageEnabled() const noexcept {
        return ready_ && core_.imersipMeasuredStereoSideEnabled();
    }

    void process(float* left,float* right,int frames) noexcept {
        if(!ready_||!left||!right||frames<=0)return;
        int at=0;
        double sourceEnergy=0.,deltaEnergy=0.;
        double originalSideEnergy=0.,processedSideEnergy=0.;
        while(at<frames) {
            const int count=std::min(frames-at,scratchFrames);
            for(int i=0;i<count;++i) {
                const float l=std::isfinite(left[at+i])?left[at+i]:0.f;
                const float r=std::isfinite(right[at+i])?right[at+i]:0.f;
                // M/S orthogonality: M=(L+R)/2, S=(L-R)/2.
                // This is the source-of-truth musical direct image.
                mid_[i]=.5f*(l+r);
                side_[i]=.5f*(l-r);
                sourceStereoGate_[i]=enhancer_.captureGate(l,r);
            }
            core_.process(left+at,right+at,count,roomReturns_.data());
            for(int i=0;i<count;++i) {
                directGain_+=stageSlew_*(directTarget_-directGain_);
                stereoKeep_+=stageSlew_*(stereoKeepTarget_-stereoKeep_);
                compareDryGain_+=compareSlew_*(compareDryTarget_-compareDryGain_);
                earlyGain_+=stageSlew_*(earlyTarget_-earlyGain_);
                lateGain_+=stageSlew_*(lateTarget_-lateGain_);
                outputGain_+=trimSlew_*(settings_.outputTrim-outputGain_);
                const float dryL=mid_[i]+side_[i];
                const float dryR=mid_[i]-side_[i];
                // The old ImerSip threw away the rendered direct HRIR and
                // put original unprocessed L/R back at 100% IMMERSION.
                // This made it behave like a stereo widener plus reverb.
                const auto& wet=roomReturns_[i];
                // Cross over the *difference* between measured direct
                // HRTF and headphone dry. Cascaded first-order HP stages
                // form a four-pole high-pass; unlike one-pole split, bass
                // at 40-220 Hz receives negligible conflicting pressure.
                // P8.7 measured direct KEMAR air shelf (shared L/R pole).
                // Do NOT boost diffuse late HF or invert pinna notches.
                float hrL=wet.directL,hrR=wet.directR;
                if(p87Enabled_ && settings_.immersion>0.f)
                    p87Body_.enhanceDirect(hrL,hrR);
                float deltaL=hrL-dryL;
                float deltaR=hrR-dryR;
                for(int pole=0;pole<4;++pole) {
                    auto& lowL=directDifferenceLow_[0][pole];
                    auto& lowR=directDifferenceLow_[1][pole];
                    lowL+=directHp_*(deltaL-lowL);
                    lowR+=directHp_*(deltaR-lowR);
                    deltaL-=lowL;
                    deltaR-=lowR;
                }
                // Preserve original stereo SIDE as the authoritative direct
                // source while rendering a separate, band-limited binaural
                // speaker contrast. Keep mode must not turn off the HRTF.
                const float deltaMid=.5f*(deltaL+deltaR);
                const float deltaSide=.5f*(deltaL-deltaR);
                // Previous Stereo Keep=100% multiplied measured HRTF SIDE
                // by only 0.10: music was protected by effectively
                // disabling directional externalization. Treat fidelity
                // and binaural speaker contrast as INDEPENDENT axes.
                // In Keep mode the music's original stereo SIDE remains
                // exactly present; a band-limited measured KEMAR SIDE is
                // added at bounded 45% strength, NOT a subtractive dry
                // replacement. No fake Haas delay or decorrelated signal.
                float measuredSide=.5f*(hrL-hrR);
                for(int pole=0;pole<4;++pole) {
                    auto& low=directMeasuredSideLow_[pole];
                    low+=directHp_*(measuredSide-low);
                    measuredSide-=low;
                }
                const float keep=std::clamp(stereoKeep_,0.f,1.f);
                const float directionalSide=directGain_*(
                    (1.f-keep)*deltaSide+
                    keep*.45f*measuredSide);
                const float baseL=dryL+directGain_*deltaMid+
                                  directionalSide;
                const float baseR=dryR+directGain_*deltaMid-
                                  directionalSide;
                earlyLow_[0]+=earlyHp_*(wet.earlyL-earlyLow_[0]);
                earlyLow_[1]+=earlyHp_*(wet.earlyR-earlyLow_[1]);
                lateLow_[0]+=lateHp_*(wet.lateL-lateLow_[0]);
                lateLow_[1]+=lateHp_*(wet.lateR-lateLow_[1]);
                const float erL=wet.earlyL-earlyLow_[0];
                const float erR=wet.earlyR-earlyLow_[1];
                const float revL=wet.lateL-lateLow_[0];
                const float revR=wet.lateR-lateLow_[1];
                // Real eight-line FDN LP175 body. Existing bassImpact shelf
                // already supplies original music LF: do NOT add P8.7 +9%.
                const auto body=(p87Enabled_ && settings_.immersion>0.f)
                    ?p87Body_.diffuseLowBody(wet.lateL,wet.lateR)
                    :armix::sonku::p87::MusicalAirBody::Ears{};
                const float erMid=.5f*(erL+erR),erSide=.5f*(erL-erR);
                // Source-accurate four horizontal walls are already in ER.
                // Selectively increase 250-3800 Hz LATERAL *early* energy
                // for musical source width; do not widen original dry or
                // boost the late diffuse soundfield (envelopment != width).
                const auto lateral=concertStage_.process(wet.earlyL,wet.earlyR);
                const float revMid=.5f*(revL+revR);
                const float revSide=.5f*(revL-revR);
                // P8.6 horizontal early reflections support the DIRECT
                // speakers; diffuse late tail is deliberately restrained.
                float addL=earlyGain_*(erSide+.40f*erMid)+
                           lateGain_*(revSide+.10f*revMid+body.left)+
                           (concertStageEnabled_?lateral.left:0.f);
                float addR=earlyGain_*(-erSide+.40f*erMid)+
                           lateGain_*(-revSide+.10f*revMid+body.right)+
                           (concertStageEnabled_?lateral.right:0.f);
                // Preserve the *direct binaural substitution* at high input
                // level: only the ADDITIONAL room returns are headroom-gated.
                const float headroom=std::max(0.f,.98f-
                    std::max(std::abs(baseL),std::abs(baseR)));
                const float peakExtra=std::max(std::abs(addL),std::abs(addR));
                if(peakExtra>headroom && peakExtra>0.f) {
                    const float scale=headroom/peakExtra;
                    addL*=scale;addR*=scale;
                }
                float l=baseL+addL,r=baseR+addR;
                enhancer_.process(l,r,sourceStereoGate_[i]);
                // A/B compares actual untouched ORIGINAL L/R vs DSP using
                // the SAME output trim (not an automatic loudness match).
                // Compare gain slews 20ms and never changes .smix.
                const float dryWithTrimL=dryL*outputGain_;
                const float dryWithTrimR=dryR*outputGain_;
                l=l*outputGain_*(1.f-compareDryGain_)+
                  dryWithTrimL*compareDryGain_;
                r=r*outputGain_*(1.f-compareDryGain_)+
                  dryWithTrimR*compareDryGain_;
                // Linked zero-lookahead sample-peak protection. A 100%
                // binaural direct stage must not silently revert to dry when
                // the music approaches 0dBFS. No gain changes under unity.
                const float peak=std::max(std::abs(l),std::abs(r));
                const float immediate=peak>.98f?.98f/peak:1.f;
                finalLimiter_=std::min(immediate,
                    std::min(1.f,finalLimiter_+limiterRelease_));
                l*=finalLimiter_;r*=finalLimiter_;
                left[at+i]=l;right[at+i]=r;
                // Lock-free BLOCK diagnostic of actual rendered PCM, NOT
                // an optimistic GUI knob or a parameter ACK by itself.
                const double inL=dryL,inR=dryR;
                const double dl=l-inL,dr=r-inR;
                sourceEnergy+=inL*inL+inR*inR;
                deltaEnergy+=dl*dl+dr*dr;
                // Per-block stereo preservation, normalized for final gain.
                // Never call a large PCM delta an improvement in fidelity.
                originalSideEnergy+=static_cast<double>(side_[i])*side_[i];
                const float norm=std::max(1.e-6f,outputGain_*finalLimiter_);
                const double normalizedSide=.5*(l-r)/norm;
                processedSideEnergy+=normalizedSide*normalizedSide;
            }
            at+=count;
        }
        lastInputEnergy_=static_cast<float>(sourceEnergy/(2.*frames));
        lastDifferenceEnergy_=static_cast<float>(deltaEnergy/(sourceEnergy+1.e-14));
        lastStereoRetention_=originalSideEnergy>1.e-10?
            static_cast<float>(std::sqrt(processedSideEnergy/originalSideEnergy)):
            -1.f; // centered MONO input has no original stereo to retain
    }
    float lastInputEnergy() const noexcept {return lastInputEnergy_;}
    float lastDifferenceEnergy() const noexcept {return lastDifferenceEnergy_;}
    float lastStereoRetention() const noexcept {return lastStereoRetention_;}
private:
    void updateTargets(const Settings& s) noexcept {
        // Immersion is now chiefly DIRECT virtual speaker transfer, not
        // a reverb-gain shortcut. Nonlinear fade reaches 100% true binaural
        // at the end stop while retaining smooth 0% exact-dry admission.
        directTarget_=s.immersion*(2.f-s.immersion);
        // Both levers are independently audible. Depth modifies real
        // source-wall delays; it also increases ratio of anchored ER to
        // unchanged original stereo direct as the stage moves farther.
        // These levels are small measured reflections, NOT dry gain.
        earlyTarget_=s.immersion*(2.60f*s.space)*(1.f+.55f*s.depth);
        lateTarget_=s.immersion*(1.0f*s.lateRoom);
        // KEEP is now source-stereo preservation, NOT an inverse spatial
        // strength dial. 100% keeps original SIDE + bounded measured HRIR
        // directional contribution. 0% retains the older full transfer.
        stereoKeepTarget_=s.stereoLock;
        concertStage_.setScene(s.immersion,s.space,s.width);
    }
    static constexpr int scratchFrames=8192;
    armix::sonku::SonKuSpatialCore core_{};
    PhaseSafeEnhancer enhancer_{};
    armix::sonku::p87::MusicalAirBody p87Body_{};
    ConcertStage concertStage_{};
    const bool p87Enabled_=true,concertStageEnabled_=true;
    std::array<armix::sonku::SonKuSpatialCore::RoomReturns,scratchFrames>
        roomReturns_{};
    std::array<float,scratchFrames> sourceStereoGate_{},mid_{},side_{};
    std::array<float,2> earlyLow_{},lateLow_{};
    std::array<std::array<float,4>,2> directDifferenceLow_{};
    std::array<float,4> directMeasuredSideLow_{};
    Settings settings_{};
    float stageSlew_=0.f,trimSlew_=0.f;
    float earlyHp_=0.f,lateHp_=0.f,directHp_=0.f;
    float directGain_=0.f,directTarget_=0.f;
    float stereoKeep_=1.f,stereoKeepTarget_=1.f;
    float compareDryGain_=0.f,compareDryTarget_=0.f,compareSlew_=0.f;
    float finalLimiter_=1.f,limiterRelease_=0.f;
    float earlyGain_=0.f,earlyTarget_=0.f;
    float lateGain_=0.f,lateTarget_=0.f;
    float outputGain_=1.f;
    float lastInputEnergy_=0.f,lastDifferenceEnergy_=0.f;
    float lastStereoRetention_=-1.f;
    bool ready_=false;
};
} // namespace armix::imersip
