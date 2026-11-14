// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ImerSipPhaseSafe.h"
#include "sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>

// ImerSip = P8.2 musical venue renderer + mathematically constrained
// coherent bass and MID-preserving stereo enhancement, NOT 8D motion.
// All PCM is processed by the existing native JUCE FX callback.
// Fixed 8192-sample stereo-gate scratch, no per-block heap or file I/O.
namespace armix::imersip {

inline armix::sonku::Settings sceneFor(const Settings& in) noexcept {
    armix::sonku::Settings s{};
    s.moving=false;
    s.startAzimuthDegrees=0.f;     // forever front, no orbit/pan animation
    s.stereoSpreadDegrees=60.f;    // stable P8.2 music MID/SIDE reference
    s.spatialMix=.65f*in.immersion;
    s.outputTrim=in.outputTrim;
    if(in.immersion==0.f) {
        s.spacePreset=0;
        s.roomAmount=0.f;
        s.ambience=0.f;
        return s;
    }
    s.spacePreset=in.venue==2?2:3;
    s.sourceDistanceMeters=in.venue==1?3.2f:
        in.venue==2?2.0f:2.6f;
    s.roomAmount=(in.venue==1?.14f:in.venue==2?.17f:.20f)
                 +(in.venue==1?.24f:in.venue==2?.25f:.32f)*in.space;
    s.ambience=(in.venue==1?.09f:in.venue==2?.08f:.12f)
               +(in.venue==1?.22f:in.venue==2?.19f:.31f)*in.space;
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
        if(ready_)ready_=enhancer_.prepare(rate);
        if(ready_)ready_=enhancer_.setSettings(settings_);
        if(ready_) {
            // Prepare-only initialization; no initial accidental wet rotation.
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
        return true;
    }
    Settings settings() const noexcept {return settings_;}
    bool ready() const noexcept {return ready_;}

    void process(float* left,float* right,int frames) noexcept {
        if(!ready_||!left||!right||frames<=0)return;
        int at=0;
        while(at<frames) {
            const int count=std::min(frames-at,scratchFrames);
            // Side detector from ORIGINAL stereo. No streaming block based
            // correlation: per-sample causal gate is chunk-invariant.
            for(int i=0;i<count;++i)
                sourceStereoGate_[i]=enhancer_.captureGate(
                    left[at+i],right[at+i]);
            core_.process(left+at,right+at,count);
            for(int i=0;i<count;++i)
                enhancer_.process(left[at+i],right[at+i],
                                  sourceStereoGate_[i]);
            at+=count;
        }
    }
private:
    static constexpr int scratchFrames=8192; // existing native hardFrameLimit
    armix::sonku::SonKuSpatialCore core_{};
    PhaseSafeEnhancer enhancer_{};
    std::array<float,scratchFrames> sourceStereoGate_{};
    Settings settings_{};
    bool ready_=false;
};
} // namespace armix::imersip
