// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
#include <cstdint>

// Research-only speaker positions are fixed in WORLD, head yaw is separate.
// No IO, tracker, threads, heap or additional PCM authority.
namespace armix::sonku::p83 {
enum class Mode : std::uint8_t {
    MusicBedP82=0, MonoObject=1, SpeakerMonitor=2,
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
    MusicSpatialP84=3
#if defined(SONKU_P85_WORLD_COHERENCE_RESEARCH) && SONKU_P85_WORLD_COHERENCE_RESEARCH
    ,MusicSpatialP85=4
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
    ,MusicSpatialP86=5
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
    ,MusicSpatialP87=6
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
    ,MusicSpatialP88=7
#endif
#endif
#endif
#endif
#endif
};
enum class Stem : std::uint8_t { Combined=0, Direct=1, Early=2, Late=3 };
inline bool valid(Mode m) noexcept {
    return m==Mode::MusicBedP82 || m==Mode::MonoObject ||
           m==Mode::SpeakerMonitor
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
           || m==Mode::MusicSpatialP84
#if defined(SONKU_P85_WORLD_COHERENCE_RESEARCH) && SONKU_P85_WORLD_COHERENCE_RESEARCH
           || m==Mode::MusicSpatialP85
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
           || m==Mode::MusicSpatialP86
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
           || m==Mode::MusicSpatialP87
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
           || m==Mode::MusicSpatialP88
#endif
#endif
#endif
#endif
#endif
           ;
}
inline bool valid(Stem s) noexcept {
    return s==Stem::Combined || s==Stem::Direct ||
           s==Stem::Early || s==Stem::Late;
}
inline bool validYaw(float yaw) noexcept {
    return std::isfinite(yaw) && yaw>=-360.f && yaw<=360.f;
}
// Positive azimuth RIGHT; head turn +90 places world FRONT at head LEFT -90.
inline float toHead(float worldAzimuth,float headYaw) noexcept {
    // Bounded angle arithmetic avoids libm fmod in native PCM geometry.
    float a=worldAzimuth-headYaw;
    if(a>=540.f)a-=720.f;
    else if(a< -540.f)a+=720.f;
    if(a>=180.f)a-=360.f;
    else if(a< -180.f)a+=360.f;
    if(a>=180.f)a-=360.f;
    else if(a< -180.f)a+=360.f;
    return a;
}
struct Stereo {
    float leftWorld=0.f,rightWorld=0.f;
    float leftHead=0.f,rightHead=0.f;
};
inline Stereo stereo(float worldCenter,float spread,float yaw) noexcept {
    const float half=spread*.5f;
    return {worldCenter-half,worldCenter+half,
            toHead(worldCenter-half,yaw),toHead(worldCenter+half,yaw)};
}
} // namespace armix::sonku::p83
