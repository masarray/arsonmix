// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "sonku/SonKuSpatialCore.h"
#include <cmath>

// GrandStage is a MUSIC/VENUE effect, NOT a moving 8D source.
// It reuses the actual P8.2 music-safe transparent-bass room renderer on a
// separate native insert. No cloned PCM graph, callback, worker or UI audio.
// P8.2 has not passed production qualification; fail closed without its flags.
namespace armix::grandstage {

struct Settings {
    int venue=0;               // 0 Concert Hall, 1 Premium Cinema, 2 Live Room
    float immersion=.58f;      // 0 exact dry, 1 strongest fixed-front scene
    float space=.45f;          // material ER and late FDN return
    float width=.60f;          // bounded stereo music SIDE, no orbit
    float outputTrim=1.f;      // full-output linear trim, not a loudness trick
};

inline bool valid(const Settings& s) noexcept {
    return s.venue>=0 && s.venue<=2 &&
        std::isfinite(s.immersion) && s.immersion>=0.f && s.immersion<=1.f &&
        std::isfinite(s.space) && s.space>=0.f && s.space<=1.f &&
        std::isfinite(s.width) && s.width>=0.f && s.width<=1.f &&
        std::isfinite(s.outputTrim) && s.outputTrim>=.25f &&
        s.outputTrim<=1.5f;
}

inline armix::sonku::Settings sceneFor(const Settings& in) noexcept {
    armix::sonku::Settings s{};
    // P8.2 approved music bed, stationary stage. Never spin a singer or source.
    s.moving=false;
    s.startAzimuthDegrees=0.f;
    s.stereoSpreadDegrees=45.f+35.f*in.width;
    s.spatialMix=.65f*in.immersion;
    s.outputTrim=in.outputTrim;
    // ZERO means genuinely dry; neither a hidden room nor an extra output gain.
    if(in.immersion==0.f) {
        s.spacePreset=0;
        s.roomAmount=0.f;
        s.ambience=0.f;
        return s;
    }
    s.spacePreset=in.venue==2?2:3;
    s.sourceDistanceMeters=in.venue==1?3.2f:
        in.venue==2?2.0f:2.6f;
    // Different direct-to-room balances; no invented subbass/exciter/AGC.
    s.roomAmount=(in.venue==1?.14f:in.venue==2?.17f:.20f)
                 +(in.venue==1?.24f:in.venue==2?.25f:.32f)*in.space;
    s.ambience=(in.venue==1?.09f:in.venue==2?.08f:.12f)
               +(in.venue==1?.22f:in.venue==2?.19f:.31f)*in.space;
    return s;
}

class GrandStageDsp final {
public:
    bool prepare(double rate) noexcept {
        ready_=false;
#if defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) && SONKU_P82_MULTIBAND_ROOM_RESEARCH \
 && defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH \
 && defined(SONKU_P75_MUSIC_SAFE_RESEARCH) && SONKU_P75_MUSIC_SAFE_RESEARCH \
 && defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) && SONKU_P7_MEASURED_ELEVATION_RESEARCH
        ready_=core_.prepare(rate,
            armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch);
        if(ready_) {
            ready_=core_.setSettings(sceneFor(settings_));
            // Initial realtime smoothers otherwise retain SonKu's default
            // moving/full-wet scene for ~8ms even on a pre-restored DRY state.
            // Reset only during prepare, never during live edits/callback.
            if(ready_)core_.reset();
        }
#endif
        return ready_;
    }
    bool setSettings(const Settings& s) noexcept {
        if(!valid(s))return false;
        if(ready_ && !core_.setSettings(sceneFor(s)))return false;
        settings_=s;
        return true;
    }
    Settings settings() const noexcept {return settings_;}
    bool ready() const noexcept {return ready_;}
    void process(float* left,float* right,int frames) noexcept {
        if(ready_)core_.process(left,right,frames);
    }
private:
    armix::sonku::SonKuSpatialCore core_{};
    Settings settings_{};
    bool ready_=false;
};

} // namespace armix::grandstage
