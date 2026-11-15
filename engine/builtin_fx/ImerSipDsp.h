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
    s.sourceDistanceMeters=in.venue==1?3.f:
                           in.venue==2?2.f:2.6f;
    // Smaller, bounded reflections than v1. The room cannot replace the
    // unfiltered musical MID; only interaural differences are retained.
    s.roomAmount=in.immersion==0.f?0.f:
        (in.venue==1?.035f:in.venue==2?.055f:.05f)+
        (in.venue==1?.12f:in.venue==2?.13f:.16f)*in.space;
    s.ambience=in.immersion==0.f?0.f:
        (in.venue==1?.035f:in.venue==2?.03f:.045f)+
        (in.venue==1?.11f:in.venue==2?.10f:.14f)*in.space;
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
            updateTargets(settings_);
            sideGain_=sideTarget_;
            roomGain_=roomTarget_;
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
            core_.process(left+at,right+at,count);
            for(int i=0;i<count;++i) {
                sideGain_+=stageSlew_*(sideTarget_-sideGain_);
                roomGain_+=stageSlew_*(roomTarget_-roomGain_);
                outputGain_+=trimSlew_*(settings_.outputTrim-outputGain_);
                const float dryL=mid_[i]+side_[i];
                const float dryR=mid_[i]-side_[i];
                const float spatialSide=.5f*(left[at+i]-right[at+i]);
                // A completely unprocessed source MID: no centered HRTF
                // combing, no double-dry feed, no room wash over the vocal.
                // A genuine P8.2 room component adds only to stereo SIDE;
                // bounded positive SIDE support retains the source image.
                float extra=sideGain_*side_[i]+
                    roomGain_*(spatialSide-side_[i]);
                // Apply a common headroom budget ONLY to the enhancement.
                // Never apply an AGC/limiter that pulls down the original.
                const float headroom=std::max(0.f,.98f-
                    std::max(std::abs(dryL),std::abs(dryR)));
                extra=std::clamp(extra,-headroom,headroom);
                float l=dryL+extra,r=dryR-extra;
                enhancer_.process(l,r,sourceStereoGate_[i]);
                left[at+i]=l*outputGain_;
                right[at+i]=r*outputGain_;
            }
            at+=count;
        }
    }
private:
    void updateTargets(const Settings& s) noexcept {
        sideTarget_=.30f*s.width*s.immersion;
        // Maximum 14% of the P8.2 spatial SIDE difference; preserving the
        // original direct SIDE at 100% is much more important than reverb.
        roomTarget_=.14f*s.immersion*s.width*(.35f+.65f*s.space);
    }
    static constexpr int scratchFrames=8192;
    armix::sonku::SonKuSpatialCore core_{};
    PhaseSafeEnhancer enhancer_{};
    std::array<float,scratchFrames> sourceStereoGate_{},mid_{},side_{};
    Settings settings_{};
    float stageSlew_=0.f,trimSlew_=0.f;
    float sideGain_=0.f,sideTarget_=0.f;
    float roomGain_=0.f,roomTarget_=0.f;
    float outputGain_=1.f;
    bool ready_=false;
};
} // namespace armix::imersip
