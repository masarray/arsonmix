// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ImerSipPhaseSafe.h"
#include "sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cmath>

// P8.2 is the acoustic SIDE-field generator, never the owner of music MID.
// The failed first version sent the ENTIRE mix (including center vocals)
// through one centered KEMAR HRTF plus substantial six-wall room tails.
// This colored/muffled vocals and made the image sound almost mono.
// ImerSip v2 keeps each original channel's DIRECT sound and uses only
// bounded, spatially differentiated P8.2 SIDE residual for exterior ambience.
// Stereo SIDE is never downmixed to mono and no Haas/cross-channel inversion
// or per-block allocation is introduced.
namespace armix::imersip {

inline armix::sonku::Settings sceneFor(const Settings& in) noexcept {
    armix::sonku::Settings s{};
    s.moving=false;                     // Never auto-orbit music
    s.startAzimuthDegrees=0.f;          // Front stationary anchor
    s.stereoSpreadDegrees=80.f+100.f*in.width; // 0..1 => 80..180 degrees
    s.spatialMix=.75f*in.immersion;     // Only SIDE residual reaches output
    s.outputTrim=1.f;                  // Final trim belongs to ImerSip
    s.spacePreset=in.immersion==0.f?0:(in.venue==2?2:3);
    // Close front-row source in every venue; the hall itself stays large.
    // Geometry admission uses the valid P8.2 1.6m minimum.
    s.sourceDistanceMeters=1.6f;
    // Detail-first front-row hall: prioritize six-wall EARLY reflections
    // (external cues) above diffuse LATE decay (mid masking).
    // The original P8.2 room model remains the only acoustic engine.
    s.roomAmount=in.immersion==0.f?0.f:
        (in.venue==1?.17f:in.venue==2?.18f:.20f)+
        (in.venue==1?.40f:in.venue==2?.36f:.42f)*in.space;
    s.ambience=in.immersion==0.f?0.f:
        (in.venue==1?.07f:in.venue==2?.06f:.08f)+
        (in.venue==1?.18f:in.venue==2?.16f:.20f)*in.space;
    return s;
}

class ImerSipDsp final {
public:
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
        if(ready_)ready_=enhancer_.prepare(rate);
        if(ready_)ready_=enhancer_.setSettings(settings_);
        if(ready_) {
            stageSlew_=static_cast<float>(1.-std::exp(-1./(rate*.018)));
            trimSlew_=static_cast<float>(1.-std::exp(-1./(rate*.024)));
            constexpr double pi=3.14159265358979323846;
            earlyHp_=static_cast<float>(1.-std::exp(-2.*pi*220./rate));
            lateHp_=static_cast<float>(1.-std::exp(-2.*pi*160./rate));
            earlyLow_.fill(0.f);lateLow_.fill(0.f);
            updateTargets(settings_);
            sideGain_=sideTarget_;
            roomGain_=roomTarget_;
            earlyGain_=earlyTarget_;
            lateGain_=lateTarget_;
            outputGain_=settings_.outputTrim;
            core_.reset();
            enhancer_.reset();
        }
#endif
        return ready_;
    }
    bool setSettings(const Settings& next) noexcept {
        if(!valid(next))return false;
        if(ready_ && !core_.setSettings(sceneFor(next)))return false;
        if(!enhancer_.setSettings(next))return false;
        settings_=next;
        updateTargets(next);
        return true;
    }
    Settings settings() const noexcept {return settings_;}
    bool ready() const noexcept {return ready_;}
    bool measuredStereoStageEnabled() const noexcept {
        return ready_ && core_.imersipMeasuredStereoSideEnabled();
    }

    void process(float* left,float* right,int frames) noexcept {
        if(!ready_||!left||!right||frames<=0)return;
        int at=0;
        double sourceEnergy=0.,deltaEnergy=0.;
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
                sideGain_+=stageSlew_*(sideTarget_-sideGain_);
                roomGain_+=stageSlew_*(roomTarget_-roomGain_);
                earlyGain_+=stageSlew_*(earlyTarget_-earlyGain_);
                lateGain_+=stageSlew_*(lateTarget_-lateGain_);
                outputGain_+=trimSlew_*(settings_.outputTrim-outputGain_);
                const float dryL=mid_[i]+side_[i];
                const float dryR=mid_[i]-side_[i];
                const float spatialSide=.5f*(left[at+i]-right[at+i]);
                // A completely unprocessed source MID: no centered HRTF
                // combing, no double-dry feed, no room wash over the vocal.
                // A genuine P8.2 room component adds only to stereo SIDE;
                // bounded positive SIDE support retains the source image.
                const float directSide=sideGain_*side_[i]+
                    roomGain_*(spatialSide-side_[i]);
                // A SECOND room attenuation was the root of "no sound
                // change". Blend the true measured P8.2 returns at an
                // independent gain, rather than multiplying the already
                // quiet combined SonKu output by another ~0.05.
                const auto& wet=roomReturns_[i];
                earlyLow_[0]+=earlyHp_*(wet.earlyL-earlyLow_[0]);
                earlyLow_[1]+=earlyHp_*(wet.earlyR-earlyLow_[1]);
                lateLow_[0]+=lateHp_*(wet.lateL-lateLow_[0]);
                lateLow_[1]+=lateHp_*(wet.lateR-lateLow_[1]);
                const float erL=wet.earlyL-earlyLow_[0];
                const float erR=wet.earlyR-earlyLow_[1];
                const float revL=wet.lateL-lateLow_[0];
                const float revR=wet.lateR-lateLow_[1];
                const float erMid=.5f*(erL+erR),erSide=.5f*(erL-erR);
                const float revMid=.5f*(revL+revR);
                const float revSide=.5f*(revL-revR);
                // Early room MID 55% of early SIDE; late MID 12%.
                // The foreground vocal remains predominantly ORIGINAL direct input.
                float addL=directSide+
                    earlyGain_*(erSide+.55f*erMid)+
                    lateGain_*(revSide+.12f*revMid);
                float addR=-directSide+
                    earlyGain_*(-erSide+.55f*erMid)+
                    lateGain_*(-revSide+.12f*revMid);
                // Suppress only OPTIONAL enhancement near 0dBFS.
                // Never auto-mute/attenuate the original music direct path.
                const float headroom=std::max(0.f,.98f-
                    std::max(std::abs(dryL),std::abs(dryR)));
                const float peakExtra=std::max(std::abs(addL),std::abs(addR));
                if(peakExtra>headroom && peakExtra>0.f) {
                    const float scale=headroom/peakExtra;
                    addL*=scale;addR*=scale;
                }
                float l=dryL+addL,r=dryR+addR;
                enhancer_.process(l,r,sourceStereoGate_[i]);
                l*=outputGain_;r*=outputGain_;
                left[at+i]=l;right[at+i]=r;
                // Lock-free BLOCK diagnostic of actual rendered PCM, NOT
                // an optimistic GUI knob or a parameter ACK by itself.
                const double inL=dryL,inR=dryR;
                const double dl=l-inL,dr=r-inR;
                sourceEnergy+=inL*inL+inR*inR;
                deltaEnergy+=dl*dl+dr*dr;
            }
            at+=count;
        }
        lastInputEnergy_=static_cast<float>(sourceEnergy/(2.*frames));
        lastDifferenceEnergy_=static_cast<float>(deltaEnergy/(sourceEnergy+1.e-14));
    }
    float lastInputEnergy() const noexcept {return lastInputEnergy_;}
    float lastDifferenceEnergy() const noexcept {return lastDifferenceEnergy_;}
private:
    void updateTargets(const Settings& s) noexcept {
        // Separate FUSED DIRECTION and ACTUAL ROOM return controls.
        // Previous roomTarget~=0.05 at defaults, on an already attenuated
        // SonKu source: almost no acoustic material entered PCM.
        sideTarget_=.22f*s.width*s.immersion;
        roomTarget_=.24f*s.immersion*s.width;
        earlyTarget_=s.immersion*(.65f+1.35f*s.space);
        lateTarget_=s.immersion*(.30f+1.10f*s.space);
    }
    static constexpr int scratchFrames=8192;
    armix::sonku::SonKuSpatialCore core_{};
    PhaseSafeEnhancer enhancer_{};
    std::array<armix::sonku::SonKuSpatialCore::RoomReturns,scratchFrames>
        roomReturns_{};
    std::array<float,scratchFrames> sourceStereoGate_{},mid_{},side_{};
    std::array<float,2> earlyLow_{},lateLow_{};
    Settings settings_{};
    float stageSlew_=0.f,trimSlew_=0.f;
    float earlyHp_=0.f,lateHp_=0.f;
    float sideGain_=0.f,sideTarget_=0.f;
    float roomGain_=0.f,roomTarget_=0.f;
    float earlyGain_=0.f,earlyTarget_=0.f;
    float lateGain_=0.f,lateTarget_=0.f;
    float outputGain_=1.f;
    float lastInputEnergy_=0.f,lastDifferenceEnergy_=0.f;
    bool ready_=false;
};
} // namespace armix::imersip
