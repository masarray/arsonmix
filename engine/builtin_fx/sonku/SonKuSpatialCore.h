// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuMeasuredHrir.h"
#include "SonKuEarlyReflections.h"
#include "SonKuMusicalReverb.h"
#include "SonKuStereoIntegrity.h"
#if defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) && ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH
#include "SonKuFixedPaMusicalEq.h"
#endif
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH || \
    !defined(SONKU_P71_DIRECT_XYZ_RESEARCH) || !SONKU_P71_DIRECT_XYZ_RESEARCH
#error P8.3 speaker research requires the P8.2 baseline and P7.1 XYZ engine
#endif
#include "SonKuP83SpeakerScene.h"
#include "SonKuP83LowFrequencyTransfer.h"
#endif
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
#if !defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) || !SONKU_P83_PHYSICAL_SPEAKER_RESEARCH || \
    !defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) || !SONKU_P80_TRANSPARENT_BASS_RESEARCH || \
    !defined(SONKU_P75_MUSIC_SAFE_RESEARCH) || !SONKU_P75_MUSIC_SAFE_RESEARCH
#error P8.4 single-graph music fusion requires the explicitly measured P8.2/P8.3 engine
#endif
#include "SonKuP84FusedMusicHrir.h"
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
#include "SonKuP90StereoDecoder.h"
#include "SonKuP91P92WorldBassFoundation.h"
#if defined(SONKU_P95_MUSICAL_PRESENCE_RESEARCH) && SONKU_P95_MUSICAL_PRESENCE_RESEARCH
#include "SonKuP95MusicalPresence.h"
#endif
#if defined(SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH) && SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH
#include "SonKuP96DistanceReinforcement.h"
#endif
#if defined(SONKU_P97_STAGE_IMPACT_RESEARCH) && SONKU_P97_STAGE_IMPACT_RESEARCH
#include "SonKuP97StageImpact.h"
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
#include "SonKuP98SceneVoicing.h"
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
#include "SonKuImmerSIBVocalRoom.h"
#endif
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
#if !defined(SONKU_P98_SCENE_VOICING_RESEARCH) || !SONKU_P98_SCENE_VOICING_RESEARCH
#error "P9.9 requires the P9.8 typed scene engine"
#endif
#include "SonKuP99MusicalVocalDepth.h"
#endif
#if defined(SONKU_P94_FRONT_BACK_DEPTH_RESEARCH) && SONKU_P94_FRONT_BACK_DEPTH_RESEARCH
#include "SonKuP94FrontBackDepthCue.h"
#endif
#endif
#endif
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
#include "SonKuP87MusicalAirBody.h"
#endif
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
#include "SonKuHrtfTonalCalibration.h"
#endif
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
#include "SonKuSourcePlacement3D.h"
#endif
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
#if !defined(SONKU_P71_DIRECT_XYZ_RESEARCH) || !SONKU_P71_DIRECT_XYZ_RESEARCH || !defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) || !SONKU_P7_MEASURED_ELEVATION_RESEARCH
#error SonKu P7.2 native XYZ requires explicitly compiled measured P7 and P7.1 research banks
#endif
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Headphone binaural DSP: measured MIT KEMAR FIR, not a stereo auto-panner.
// All storage is fixed. prepare/setSettings are CONTROL-side before live render;
// process is the same native FX callback as other inserts (no own device/worker).
namespace armix::sonku {
struct Settings {
    float startAzimuthDegrees = 0.f;  // FRONT; +90 RIGHT; 180 REAR; -90 LEFT
    float revolutionSeconds = 12.f;
    float stereoSpreadDegrees = 60.f;
    float spatialMix = 1.f;           // 1=HRTF; 0=original L/R
    float outputTrim = 0.75f;         // legacy linear gain, now user-facing GAIN (-24..+12 dB)
    bool moving = true;
    bool clockwise = true;
    // Version-2 persisted, native-ACKed acoustic scene. Zero is legacy
    // HRTF-only. All other scenes are optional and OFF unless selected.
    int spacePreset = 0;  // 0=OFF, 1=Square 7.2, 2=Chamber 14.4, 3=Large 28.8
    float sourceDistanceMeters = 1.6f;
    // Stored while OFF so the first selected scene is immediately audible
    // and turning ROOM off/on remembers the previous wet preference.
    // spacePreset=0 still guarantees identical legacy HRTF-only PCM.
    float roomAmount = .55f;
    float ambience = .30f;
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
    // Experimental v3 only. Bundled atomically with ROOM/distance in
    // existing native SPSC edit, never a second FX graph or audio worker.
    float directElevationDegrees = 0.f;
#endif
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
    // Experimental FX-state v4 only; no public v2/v3 ABI changes.
    // 0=P8.2 Music Bed, 1=MonoObject LEFT input, 2=two world speakers.
    int renderMode = 0;
    bool musicCompatibleLowFrequency = false;
    float headYawDegrees = 0.f; // deterministic world/head separation
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    int sceneVoicing=0; // 0=unchanged Venue/Custom; 1=Signature; 2=Studio; 3=Cinema
#endif
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
    float hrtfElevationTrimDegrees=0.f; // spectral only, never room XYZ
#endif
#endif
};
inline bool valid(const Settings& s) noexcept {
    return std::isfinite(s.startAzimuthDegrees) &&
        std::abs(s.startAzimuthDegrees)<=360.f &&
        std::isfinite(s.revolutionSeconds) && s.revolutionSeconds>=0.5f &&
        s.revolutionSeconds<=120.f &&
        std::isfinite(s.stereoSpreadDegrees) && s.stereoSpreadDegrees>=0.f &&
        s.stereoSpreadDegrees<=180.f &&
        std::isfinite(s.spatialMix) && s.spatialMix>=0.f && s.spatialMix<=1.f &&
        std::isfinite(s.outputTrim) && s.outputTrim>=0.f && s.outputTrim<=4.f &&
        s.spacePreset>=0 && s.spacePreset<=3 &&
        std::isfinite(s.sourceDistanceMeters) && s.sourceDistanceMeters>=1.f &&
        s.sourceDistanceMeters<=10.f &&
        std::isfinite(s.roomAmount) && s.roomAmount>=0.f &&
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        s.roomAmount<=(s.renderMode==9?1.8f:1.f) &&
#else
        s.roomAmount<=1.f &&
#endif
        std::isfinite(s.ambience) && s.ambience>=0.f && s.ambience<=1.f &&
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
        p98::SceneVoicing::valid(s.sceneVoicing) &&
        // v12 deliberately excludes the retired P8.3 physical/mono
        // modes 1/2 even when neutral voicing=0. The old P8.3 renderer
        // was rejected by the operator; JSON, realtime edits and .smix
        // restore must use ONE admission rule, not three distinct gates.
        (s.renderMode==0 || (s.renderMode>=3 && s.renderMode<=9)) &&
        !s.musicCompatibleLowFrequency &&
        (s.sceneVoicing==0 || s.renderMode==9) &&
#endif
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        s.renderMode>=0 &&
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
#if defined(SONKU_P85_WORLD_COHERENCE_RESEARCH) && SONKU_P85_WORLD_COHERENCE_RESEARCH
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
        s.renderMode<=9 && ((s.renderMode<3 || s.renderMode>9) || !s.musicCompatibleLowFrequency) &&
#else
        s.renderMode<=8 && ((s.renderMode!=3 && s.renderMode!=4 && s.renderMode!=5 && s.renderMode!=6 && s.renderMode!=7 && s.renderMode!=8) || !s.musicCompatibleLowFrequency) &&
#endif
#else
        s.renderMode<=7 && ((s.renderMode!=3 && s.renderMode!=4 && s.renderMode!=5 && s.renderMode!=6 && s.renderMode!=7) || !s.musicCompatibleLowFrequency) &&
#endif
#else
        s.renderMode<=6 && ((s.renderMode!=3 && s.renderMode!=4 && s.renderMode!=5 && s.renderMode!=6) || !s.musicCompatibleLowFrequency) &&
#endif
#else
        s.renderMode<=5 && ((s.renderMode!=3 && s.renderMode!=4 && s.renderMode!=5) || !s.musicCompatibleLowFrequency) &&
#endif
#else
        s.renderMode<=4 && ((s.renderMode!=3 && s.renderMode!=4) || !s.musicCompatibleLowFrequency) &&
#endif
#else
        s.renderMode<=3 && (s.renderMode!=3 || !s.musicCompatibleLowFrequency) &&
#endif
#else
        s.renderMode<=2 &&
#endif
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
        std::isfinite(s.hrtfElevationTrimDegrees) &&
        s.hrtfElevationTrimDegrees>=-30.f && s.hrtfElevationTrimDegrees<=20.f &&
        ((s.renderMode!=7
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
           && s.renderMode!=8
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
           && s.renderMode!=9
#endif
#endif
           ) || (s.directElevationDegrees+s.hrtfElevationTrimDegrees>=-40.f &&
                  s.directElevationDegrees+s.hrtfElevationTrimDegrees<=60.f)) &&
#endif
        p83::validYaw(s.headYawDegrees) &&
        (s.renderMode!=2 || (s.stereoSpreadDegrees>=20.f &&
                             s.stereoSpreadDegrees<=120.f)) &&
#endif
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
        // Pure control-thread admission; validate even when ROOM is OFF so
        // a later enable cannot silently admit an impossible placement.
        [&]() noexcept {
            xyz::Placement projected{};
            const float height=s.spacePreset<=1?3.2f:
                s.spacePreset==2?4.8f:7.2f;
            return xyz::project(s.sourceDistanceMeters,
                                s.directElevationDegrees,1.45f,
                                height,projected);
        }() &&
#endif
        (s.spacePreset==0 || SonKuEarlyReflections::validGeometry({
            s.spacePreset==1?7.2f:s.spacePreset==2?14.4f:28.8f,
            s.spacePreset==1?7.2f:s.spacePreset==2?14.4f:28.8f,
            s.sourceDistanceMeters}));
}
inline double wrapDegrees(double degrees) noexcept {
    degrees=std::fmod(degrees,360.0);
    return degrees<0.?degrees+360.:degrees;
}
class SonKuSpatialCore final {
public:
    static constexpr int taps=hrir::taps;
    static constexpr int updateInterval=16;
#if defined(SONKU_IMMERSIB_PCM_DIAGNOSTICS) && SONKU_IMMERSIB_PCM_DIAGNOSTICS
    struct DynamicsTrace {
        std::uint64_t samples=0,limitedSamples=0;
        double limiterReductionSum=0.,makeupGainSum=0.;
        float minimumLimiterGain=1.f,currentMakeup=1.f;
    };
    DynamicsTrace dynamicsTrace() const noexcept {return dynamicsTrace_;}
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
    // Control thread, before prepare: explicitly authorize P9.9 on THIS
    // SonKu insert, not all products sharing this exact native core.
    // Separate branded processors (e.g. ImmerSIB) remain P9.8 until they
    // independently adopt its P9.9 voicing. No extra PCM graph or state ABI.
    void optInP99ForSonKuInsert() noexcept { p99ForSonKuInsert_=true; }
#endif
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
    void optInImmerSIBVocalRoom() noexcept {immersibP99Enabled_=true;}
#endif
#if defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) && ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
    // CONTROL thread before prepare; this is a per-instance product mode,
    // not a scene setting, a global variable or a new .smix field.
    void optInImmerSIBStageMonitor() noexcept {
        stageMonitor_=true;
        p90Decoder_.useFixedFrontStage(true);
        p98Voicing_.enableFixedStageWarmth(true);
        p97Stage_.suppressStageMidBoost(true);
    }
#endif
#if defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) && ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH
    void optInImmerSIBMusicalEq() noexcept { musicalEqEnabled_=stageMonitor_; }
#endif
    bool prepare(double rate, SonKuReflectionProfile profile=
                 SonKuReflectionProfile::Lightweight) noexcept {
        ready_=false;
        if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 &&
                                    std::abs(rate-48000.)>.1))return false;
        sampleRate_=rate;
#if defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) && ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH
        if(musicalEqEnabled_ && !musicalEq_.prepare(rate))return false;
#endif
        bank_=(std::abs(rate-44100.)<=.1)?hrir::normal44100:hrir::normal48000;
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        float extraDirectReliefDb=0.f;
#if defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) && ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
        if(stageMonitor_)extraDirectReliefDb=3.0f; // fixed DIRECT HRTF pinna relief; no content detector
#endif
        if(profile==SonKuReflectionProfile::SixSurfaceKemarResearch &&
           !tonalCalibration_.prepare(rate,bank_,extraDirectReliefDb))return false;
#endif
        // Prepared control-side: continuous coefficient and gain smoothing
        // without trigonometric/exponential work in the audio callback.
        filterSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.006)));
        levelSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.008)));
        // Music-monitor distance/loudness controls are prepared off the
        // realtime callback. Headphone low shelf ~180 Hz; no added latency.
        distanceSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.030)));
        bassSlew_=static_cast<float>(1.-std::exp(-2.*3.14159265358979323846*180./rate));
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        // P8 research: coherent LF pressure stays on its original stereo
        // channel. Complementary first-order HP spatial field is derived
        // from exactly the same sample-synchronous one-pole LP topology.
        // No lookahead, latency, FFT or callback coefficient computation.
        bassCrossoverAlpha_=static_cast<float>(
            1.-std::exp(-2.*3.14159265358979323846*220./rate));
#endif
        limiterRelease_=static_cast<float>(1./(rate*0.080));
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        if(!p83LowFrequency_.prepare(rate))return false;
#endif
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
        if(!p84MusicKernel_.prepare(rate))return false;
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
        if(!p90Decoder_.prepare(rate))return false;
        if(!p92Bass_.prepare(rate))return false;
#if defined(SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH) && SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH
        if(!p96Reinforcement_.prepare(rate))return false;
#endif
#if defined(SONKU_P97_STAGE_IMPACT_RESEARCH) && SONKU_P97_STAGE_IMPACT_RESEARCH
        if(!p97Stage_.prepare(rate))return false;
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
        if(!p98Voicing_.prepare(rate))return false;
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
        if(!p99VocalDepth_.prepare(rate))return false;
#endif
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
        if(!immersibP99VocalRoom_.prepare(rate))return false;
#endif
#if defined(SONKU_P95_MUSICAL_PRESENCE_RESEARCH) && SONKU_P95_MUSICAL_PRESENCE_RESEARCH
        if(!p95Presence_.prepare(rate))return false;
#endif
#if defined(SONKU_P94_FRONT_BACK_DEPTH_RESEARCH) && SONKU_P94_FRONT_BACK_DEPTH_RESEARCH
        if(!p94Depth_.prepare(rate))return false;
#endif
#endif
#endif
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
        if(!p87Body_.prepare(rate))return false;
#endif
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
        // The special elevation control is not serialized or published to
        // a public host. Downgrading the prepared profile clears it.
        if(profile!=SonKuReflectionProfile::SixSurfaceKemarResearch)
            researchElevationDegrees_=0.f;
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
        // Preserve the accepted elevation on an ASIO/device re-prepare.
        // Non-3D prepared engines cannot unknowingly carry 3D state.
        if(profile==SonKuReflectionProfile::SixSurfaceKemarResearch)
            researchElevationDegrees_=settings_.directElevationDegrees;
        else
            settings_.directElevationDegrees=0.f;
#endif
        // Only a *new* DSP prepare re-opens the quiescent research gate.
        // reset() while an existing audio graph is running must NOT do so.
        researchPcmStarted_=false;
#endif
        if(!room_.prepare(rate,profile)||!reverb_.prepare(rate)||
           !integrity_.prepare(rate))return false;
        reset();
        ready_=true;
        // Re-publish a saved active scene after device restart, but do NOT
        // select the new realtime FDN program for the legacy/offline default:
        // original Studio Glue / Airy Chamber listening renders remain exact.
        return settings_.spacePreset==0 || applyAcousticSettings(settings_);
    }
    void reset() noexcept {
#if defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) && ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH
        musicalEq_.reset();
#endif
        for(auto& channel:history_)channel.fill(0.f);
        for(auto& source:kernels_)for(auto& ear:source)ear.fill(0.f);
        for(auto& source:targets_)for(auto& ear:source)ear.fill(0.f);
        smoothedMix_=settings_.spatialMix;
        smoothedTrim_=settings_.outputTrim;
        smoothedDistanceGain_=1.f;
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
        smoothedBassDistance_=1.f;
#endif
        smoothedRoomLevel_=1.f;
        smoothedBassShelf_=0.f;
        smoothedErPresence_=1.f;
        smoothedTailPresence_=1.f;
        earBass_.fill(0.f);
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        tonalCalibration_.reset();
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
        p87Body_.reset();
#endif
#endif
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        originalBass_.fill(0.f);
        binauralBass_.fill(0.f);
        sideBass_=0.f;
#endif
        limiterGain_=1.f;
#if defined(SONKU_IMMERSIB_PCM_DIAGNOSTICS) && SONKU_IMMERSIB_PCM_DIAGNOSTICS
        dynamicsTrace_={};
#endif
        write_=0;
        remaining_=0;
        degrees_=wrapDegrees(settings_.startAzimuthDegrees);
        haveKernel_=false;
        room_.reset();
        reverb_.reset();
        integrity_.reset();
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
        p90Decoder_.reset();
        p92Bass_.reset();
#if defined(SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH) && SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH
        p96Reinforcement_.reset();
#endif
#if defined(SONKU_P97_STAGE_IMPACT_RESEARCH) && SONKU_P97_STAGE_IMPACT_RESEARCH
        p97Stage_.reset();
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
        p98Voicing_.reset();
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
        p99VocalDepth_.reset();
#endif
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
        immersibP99VocalRoom_.reset();
#endif
#if defined(SONKU_P95_MUSICAL_PRESENCE_RESEARCH) && SONKU_P95_MUSICAL_PRESENCE_RESEARCH
        p95Presence_.reset();
#endif
#if defined(SONKU_P94_FRONT_BACK_DEPTH_RESEARCH) && SONKU_P94_FRONT_BACK_DEPTH_RESEARCH
        p94Depth_.reset();
#endif
#endif
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        p83LowFrequency_.reset();
#endif
    }
    bool setSettings(const Settings& s) noexcept {
        if(!valid(s))return false;
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        if(s.renderMode!=0 &&
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch)
            return false; // Only real measured prepared native research instance
#endif
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
        const auto activeProfile=room_.preparedProfile();
        if(activeProfile!=SonKuReflectionProfile::SixSurfaceKemarResearch &&
           s.directElevationDegrees!=0.f)return false;
        // Transactional XYZ admission. The old core set degrees_ BEFORE room
        // validation, so an invalid edit could rotate a still-running orbit.
        // Stage all 3D changes and publish the phase only on success.
        const float formerElevation=researchElevationDegrees_;
        researchElevationDegrees_=s.directElevationDegrees;
#endif
        const bool changed=s.spacePreset!=settings_.spacePreset ||
            s.sourceDistanceMeters!=settings_.sourceDistanceMeters ||
            s.roomAmount!=settings_.roomAmount ||
            s.ambience!=settings_.ambience
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
            || s.directElevationDegrees!=formerElevation
#endif
            ;
        if(changed && !applyAcousticSettings(s)) {
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
            researchElevationDegrees_=formerElevation;
#endif
            return false;
        }
        // Preserve continuous motion during accepted elevation edits.
        if(!s.moving || !settings_.moving ||
           s.startAzimuthDegrees!=settings_.startAzimuthDegrees)
            degrees_=wrapDegrees(s.startAzimuthDegrees);
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
        if(s.renderMode!=settings_.renderMode ||
           s.spacePreset!=settings_.spacePreset)p87Body_.reset();
#endif
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
        // Native mode changes are already zero-gated by JUCE. Reset only
        // small P9 LF/coherence memories when entering or leaving mode 9:
        // a frozen previous P9 scene must never leak stale bass after P8 A/B.
        if((s.renderMode==9)!=(settings_.renderMode==9)){
            p90Decoder_.reset();
            p92Bass_.reset();
#if defined(SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH) && SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH
        p96Reinforcement_.reset();
#endif
#if defined(SONKU_P97_STAGE_IMPACT_RESEARCH) && SONKU_P97_STAGE_IMPACT_RESEARCH
        p97Stage_.reset();
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
        p98Voicing_.reset();
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
        p99VocalDepth_.reset();
#endif
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
        immersibP99VocalRoom_.reset();
#endif
#if defined(SONKU_P95_MUSICAL_PRESENCE_RESEARCH) && SONKU_P95_MUSICAL_PRESENCE_RESEARCH
        p95Presence_.reset();
#endif
#if defined(SONKU_P94_FRONT_BACK_DEPTH_RESEARCH) && SONKU_P94_FRONT_BACK_DEPTH_RESEARCH
        p94Depth_.reset();
#endif
        }
#endif
        settings_=s;
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        // Apply typed real-time SPSC state, not quiescent-only test setters.
        // The JUCE adapter fades out to zero BEFORE applying a mode edit.
        // No heavy DSP reset or heap work on the audio thread.
        if(p83Mode_!=static_cast<p83::Mode>(s.renderMode) ||
           p83MusicCompatibleLf_!=s.musicCompatibleLowFrequency ||
           p83HeadYaw_!=s.headYawDegrees) {
            p83LowFrequency_.reset(); // four small scalar filter memories
        }
        p83Mode_=static_cast<p83::Mode>(s.renderMode);
        p83MusicCompatibleLf_=s.musicCompatibleLowFrequency;
        p83HeadYaw_=s.headYawDegrees;
#endif
        // Once native ROOM has been enabled, retain the smoothed transition
        // when it is turned OFF. Standalone experimental V2/V3 OFF fixtures
        // continue to use their exact original 1/r distance law.
        if(s.spacePreset!=0)nativeSpaceEverUsed_=true;
        remaining_=0;
        return true;
    }
    Settings settings() const noexcept {return settings_;}
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
    // OFFLINE ONLY: quiescent, before PCM. Native session/GUI/SPSC untouched.
    bool setResearchRenderMode(p83::Mode mode) noexcept {
        if(!ready_ || researchPcmStarted_ || !p83::valid(mode) ||
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch ||
           (mode==p83::Mode::SpeakerMonitor &&
             (settings_.stereoSpreadDegrees<20.f ||
              settings_.stereoSpreadDegrees>120.f)))return false;
        p83Mode_=mode;
        settings_.renderMode=static_cast<int>(mode);
        reset();
        return true;
    }
    // Pre-PCM-only A/B: physical KEMAR transfer remains the default.
    // This changes ONLY SpeakerMonitor LF, not MonoObject or P8.2.
    bool setResearchSpeakerMusicLowFrequency(bool enabled) noexcept {
        if(!ready_ || researchPcmStarted_ ||
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch)
            return false;
        p83MusicCompatibleLf_=enabled;
        settings_.musicCompatibleLowFrequency=enabled;
        reset();
        return true;
    }
    bool setResearchSpeakerMusicLowFrequencyCutoff(float hz) noexcept {
        if(!ready_||researchPcmStarted_ ||
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch)
            return false;
        return p83LowFrequency_.setCutoff(hz);
    }
    bool researchSpeakerMusicLowFrequency() const noexcept {
        return p83MusicCompatibleLf_;
    }
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
    // OFFLINE SOURCE RESEARCH ONLY. A portable/native JUCE v4 host does
    // NOT expose this and cannot silently activate it via .smix.
    // P8.2 stereo bass/SIDE and room mixing remain authoritative.
    bool setResearchFusedMusicClarity(bool enabled) noexcept {
        if(!ready_ || researchPcmStarted_ ||
           (enabled && p83Mode_!=p83::Mode::MusicBedP82) ||
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch)
            return false;
        if(p84FusedMusic_==enabled)return true;
        p84FusedMusic_=enabled;
        reset(); // pre-PCM only: no heavy audio callback mutation
        return true;
    }
    bool researchFusedMusicClarity() const noexcept{return p84FusedMusic_;}
#endif
    bool setResearchHeadYaw(float yaw) noexcept {
        if(!ready_ || researchPcmStarted_ || !p83::validYaw(yaw) ||
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch)
            return false;
        p83HeadYaw_=yaw;
        settings_.headYawDegrees=yaw;
        reset();
        return true;
    }
    // Native PCM evidence: wall bits left,right,front,back,floor,ceiling.
    bool setResearchEvidence(p83::Stem stem,std::uint8_t wallMask=0x3f) noexcept {
        if(!ready_ || researchPcmStarted_ || !p83::valid(stem) ||
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch ||
           !room_.setResearchWallMask(wallMask))return false;
        p83Stem_=stem;
        return true;
    }
    p83::Mode researchMode() const noexcept {return p83Mode_;}
    float researchHeadYaw() const noexcept {return p83HeadYaw_;}
#endif
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
    // Only quiescent, unpublished *research* cores may change source XYZ.
    // This is intentionally NOT a new FX/state/UI/ACK field. No race with
    // a live PCM callback and no possibility of hidden session mutations.
    bool setResearchDirectElevation(float degrees) noexcept {
        if(!ready_||researchPcmStarted_||
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch)
            return false;
        xyz::Placement projected{};
        const float h=settings_.spacePreset<=1?3.2f:
                      settings_.spacePreset==2?4.8f:7.2f;
        if(!xyz::project(settings_.sourceDistanceMeters,degrees,1.45f,h,projected))
            return false;
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
        // Research setup must use the same single edit transaction as the
        // later JUCE audio-applied ACK and persisted v3. No shadow state.
        auto next=settings_;
        next.directElevationDegrees=degrees;
        return setSettings(next);
#else
        const float old=researchElevationDegrees_;
        researchElevationDegrees_=degrees;
        if(!applyAcousticSettings(settings_)) {
            researchElevationDegrees_=old;
            return false;
        }
        remaining_=0;
        haveKernel_=false;
        return true;
#endif
    }
    float researchDirectElevation() const noexcept {return researchElevationDegrees_;}
#endif
    // A/B-only control pending room's headphone/device qualification.
    // Legacy live insert remains room-free until product evidence is accepted.
    bool setEarlyReflectionsAmount(float amount) noexcept {
        return room_.setAmount(amount);
    }
    // Explicit A/B-only shape selection; never persisted or host-exposed.
    bool setEarlyReflectionsShape(SonKuRoomShape shape) noexcept {
        return room_.setShape(shape);
    }
    // Experimental offline only. No persisted or native/UI renderer switch.
    bool setDirectionalEarlyReflectionsHrir(bool on) noexcept {
        return room_.setDirectionalHrir(on);
    }
    // P3 offline-only matched-source-direction optimization; no .smix ABI.
    bool setDirectionalZeroSpreadFusion(bool on) noexcept {
        return room_.setDirectionalZeroSpreadFusion(on);
    }
    // P4 research opt-in, no on-disk state nor native GUI control.
    bool setDirectionalContiguousHrir(bool on) noexcept {
        return room_.setDirectionalContiguousHrir(on);
    }
    // Offline M3 distance/large-room acoustic trial. Not a GUI/native ABI.
    bool setAcousticRoomGeometry(const SonKuRoomGeometry& geometry) noexcept {
        return room_.setGeometry(geometry);
    }
    // Experimental wet-only late room, OFF unless explicitly enabled by
    // controlled offline A/B. The native parameter contract is unchanged.
    bool setMusicalReverbAmount(float amount) noexcept {
        return reverb_.setAmount(amount);
    }
    bool setMusicalReverbPreset(SonKuReverbPreset preset) noexcept {
        return reverb_.setPreset(preset);
    }
    bool ready() const noexcept {return ready_;}
    SonKuReflectionProfile preparedReflectionProfile() const noexcept {
        return room_.preparedProfile();
    }
    double azimuthDegrees() const noexcept {return degrees_;}
    // In-place stereo; caller owns valid, non-overlapping L/R PCM pointers.
    // No vector, heap, blocking calls, I/O or filter bank allocation.
    void process(float* left,float* right,int samples) noexcept {
        if(!ready_ || !left || !right || samples<=0)return;
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
        researchPcmStarted_=true;
#endif
        const double step=(settings_.moving ?
            (settings_.clockwise?1.:-1.)*360./(sampleRate_*settings_.revolutionSeconds) : 0.);
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        const bool positionedMode=
            room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch &&
            p83Mode_!=p83::Mode::MusicBedP82
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP84
#if defined(SONKU_P85_WORLD_COHERENCE_RESEARCH) && SONKU_P85_WORLD_COHERENCE_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP85
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP86
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP87
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP90
#endif
#endif
#endif
#endif
#endif
#endif
#endif
            ;
        const bool monoObject=positionedMode && p83Mode_==p83::Mode::MonoObject;
#else
        constexpr bool positionedMode=false,monoObject=false;
#endif
        const float distanceGain=room_.relativeDistanceGain();
        const bool nativeSpace=nativeSpaceEverUsed_;
        const bool roomEnabled=settings_.spacePreset!=0;
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        const bool transparentBass=
            room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch && !positionedMode
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            // P9 owns its complementary WORLD LF return. Never replace it
            // with P8.2's original headphone-locked low frequencies.
            && p83Mode_!=p83::Mode::MusicSpatialP90
#endif
            ;
#else
        constexpr bool transparentBass=false;
#endif
#if defined(SONKU_P75_MUSIC_SAFE_RESEARCH) && SONKU_P75_MUSIC_SAFE_RESEARCH
        // Field-failure quarantine, opt-in ONLY: spatialize the MUSIC MID
        // through one real measured center HRTF, keep stereo SIDE separated.
        // The original two-emitter renderer is never modified in public builds.
        const bool musicSafe=
            room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch && !positionedMode;
#else
        constexpr bool musicSafe=false;
#endif
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
        const bool musicFused=musicSafe &&
            (p84FusedMusic_ || p83Mode_==p83::Mode::MusicSpatialP84
#if defined(SONKU_P85_WORLD_COHERENCE_RESEARCH) && SONKU_P85_WORLD_COHERENCE_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP85
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP86
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP87
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP90
#endif
#endif
#endif
#endif
#endif
#endif
             );
#else
        constexpr bool musicFused=false;
#endif
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
        // Mode 5 changes wet only; P8.5 direct HRIR/LF/stereo/tonal remain.
        const bool horizontalFocus=p83Mode_==p83::Mode::MusicSpatialP86
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
            || p83Mode_==p83::Mode::MusicSpatialP87
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP90
#endif
#endif
#endif
#endif
            ;
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
        const bool musicalBody=p83Mode_==p83::Mode::MusicSpatialP87
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
            || p83Mode_==p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP90
#endif
#endif
#endif
            ;
#endif
#endif
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
        const bool p90World=p83Mode_==p83::Mode::MusicSpatialP90;
#else
        constexpr bool p90World=false;
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
        const bool immersiveWarm=p90World && roomEnabled &&
            settings_.spatialMix>0.f &&
            (settings_.sceneVoicing==p98::SceneVoicing::ImmerSIBDetail ||
             settings_.sceneVoicing==p98::SceneVoicing::ImmerSIBImpact);
#endif
        // Deliberate music-monitor compensation: real distance perception
        // still comes from direct/reflected ratio and high-frequency cues,
        // while the direct field is not attenuated by the full physical 1/r.
        // 1.6m=1; 8m~0.55 instead of 0.20. No pow/exp on the PCM callback.
        // P8.8: meaningful source-distance cue at 7+ m, no huge diffuse tail.
        const float targetDistance=roomEnabled?
            1.f/(1.f+
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
                ((p83Mode_==p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
                    || p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
                    || p83Mode_==p83::Mode::MusicSpatialP90
#endif
#endif
                    )?0.27f:0.13f)*
#else
                0.13f*
#endif
                (settings_.sourceDistanceMeters-1.6f)):1.f;
        // Original low-frequency direct return ignored distance; P8.8
        // attenuates it gently while preserving stereo LF weight.
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
        const float targetBassDistance=roomEnabled&&
            (p83Mode_==p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP90
#endif
#endif
             )?
            1.f/(1.f+0.10f*(settings_.sourceDistanceMeters-1.6f)):1.f;
#endif
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
        // Room-energy transfer is computed once per callback block; no new
        // FIR/filter memory. 1.6m/ROOM OFF keeps exact P8.8 PCM.
        const bool p89Stage=p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            ||p83Mode_==p83::Mode::MusicSpatialP90
#endif
            ;
        const float p89Depth=p89Stage&&roomEnabled?
            std::clamp((settings_.sourceDistanceMeters-1.6f)/6.4f,0.f,1.f):0.f;
        const float p89SideGain=1.f-.22f*p89Depth;
        const float p89DirectionalRoom=1.f+.24f*p89Depth;
        const float p89DiffuseRoom=1.f-.08f*p89Depth;
#endif
        for(int i=0;i<samples;++i) {
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
            smoothedBassDistance_+=distanceSlew_*(targetBassDistance-smoothedBassDistance_);
#endif
            smoothedMix_+=levelSlew_*(settings_.spatialMix-smoothedMix_);
            smoothedTrim_+=levelSlew_*(settings_.outputTrim-smoothedTrim_);
            // Land exactly on the accepted parameter once close enough.
            // Without the snap a one-pole asymptote never becomes bit-exact
            // dry and can retain unnecessary tiny FP tails indefinitely.
            if(std::abs(smoothedMix_-settings_.spatialMix)<1.e-6f)
                smoothedMix_=settings_.spatialMix;
            if(std::abs(smoothedTrim_-settings_.outputTrim)<1.e-6f)
                smoothedTrim_=settings_.outputTrim;
            const float dryLevel=1.f-smoothedMix_;
            // Preserve HRTF-only stored-state arithmetic exactly; scene ON
            // uses an actual full-output gain (dry+wet) instead of trim-only.
            const float legacyBinauralLevel=.5f*smoothedMix_*smoothedTrim_;
            // P9: remove the legacy monitor attenuation only for the new
            // spatialized-world scene; the linked peak guard is unchanged.
            const float sceneBinauralLevel=(p90World?.72f:.5f)*smoothedMix_;
            if(remaining_==0) {
                updateKernels();
                remaining_=updateInterval;
            }
            const float x0=std::isfinite(left[i])?left[i]:0.f;
            const float x1=monoObject?0.f:(std::isfinite(right[i])?right[i]:0.f);
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            if(p83Mode_==p83::Mode::MusicSpatialP90)p90Decoder_.observe(x0,x1);
#endif
            // Preserve strong anti-correlated music side energy without
            // collapsing it into the two virtual mono HRTF sources. The
            // independent guard has no hidden UI state or new audio bus.
            const float inputMid=.5f*(x0+x1);
            const float preservedSide=positionedMode?0.f:
                (musicSafe?.5f*(x0-x1):integrity_.preservedSide(x0,x1));
            // P7.5 research: an unconditional, orthogonal L/R side return
            // rather than correlation-gated, time-varying rescue. A centered
            // HRTF cannot annihilate anti-phase stereo. WIDTH changes level
            // of the stereo SIDE but never sets it to zero by accident.
            // 0° => 0.5x; 60° => 1x; >=90° => 1.25x (bounded).
            const float musicSideWidth=musicSafe?
                std::min(1.25f,.5f+settings_.stereoSpreadDegrees/120.f):1.f;
            float sideRescue=positionedMode?0.f:(musicSafe?
                smoothedMix_*smoothedTrim_*musicSideWidth*preservedSide:
                .65f*smoothedMix_*smoothedTrim_*preservedSide);
            float originalLowL=0.f,originalLowR=0.f;
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
            if(transparentBass){
                originalBass_[0]+=bassCrossoverAlpha_*(x0-originalBass_[0]);
                originalBass_[1]+=bassCrossoverAlpha_*(x1-originalBass_[1]);
                originalLowL=originalBass_[0];
                originalLowR=originalBass_[1];
                // P7.5's full-band SIDE return caused LF to be counted
                // twice once the original bass is restored. Only the
                // spatialized ABOVE-crossover SIDE may use the width bed.
                sideBass_+=bassCrossoverAlpha_*(preservedSide-sideBass_);
                sideRescue=smoothedMix_*smoothedTrim_*musicSideWidth*
                           (preservedSide-sideBass_);
            }
#endif
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
            // Headlocked SIDE is kept >=78% at maximum distance; early
            // world-locked measured reflections shoulder the extra space.
            sideRescue*=p89SideGain;
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            if(p83Mode_==p83::Mode::MusicSpatialP90)sideRescue=0.f;
#endif
#endif
#if defined(SONKU_P97_STAGE_IMPACT_RESEARCH) && SONKU_P97_STAGE_IMPACT_RESEARCH
            if(p90World && settings_.spatialMix>0.f){
                // Center material is reinforced ONLY before the existing
                // world-speaker HRIR; room, P92 LF, and decoder observe
                // the original stereo. No center headphone dry injection.
                const auto fed=p97Stage_.source(x0,x1,
                    settings_.sourceDistanceMeters,roomEnabled);
                history_[0][write_]=fed.left;
                history_[1][write_]=fed.right;
            }else
#endif
            {
                history_[0][write_]=x0;
                history_[1][write_]=x1;
            }
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
            if(p90World && settings_.spatialMix>0.f){
                const auto fed=p98Voicing_.source(history_[0][write_],
                    history_[1][write_],settings_.sceneVoicing,roomEnabled);
                history_[0][write_]=fed.left;
                history_[1][write_]=fed.right;
            }else{
                // Smoothly return private voicing to neutral without ever
                // injecting headphone-dry voice or mutating the original PCM.
                (void)p98Voicing_.source(0.f,0.f,0,false);
            }
#endif
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
            const float p98RoomSourceL=history_[0][write_];
            const float p98RoomSourceR=history_[1][write_];
            const bool vocalRoomOn=immersibP99Enabled_ && p90World &&
                roomEnabled && settings_.spatialMix>0.f &&
                (settings_.sceneVoicing==4 || settings_.sceneVoicing==5);
            if(immersibP99Enabled_) {
                const auto vocal=immersibP99VocalRoom_.source(
                    history_[0][write_],history_[1][write_],
                    settings_.sceneVoicing,vocalRoomOn);
                history_[0][write_]=vocal.left;
                history_[1][write_]=vocal.right;
            }
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
            if(p99ForSonKuInsert_ && p90World && settings_.spatialMix>0.f){
                // Source-depth control happens BEFORE measured two-speaker
                // KEMAR FIR. No center headphone-dry path or second bus.
                const auto focus=p99VocalDepth_.source(
                    history_[0][write_],history_[1][write_],
                    settings_.sceneVoicing,roomEnabled);
                history_[0][write_]=focus.left;
                history_[1][write_]=focus.right;
            }else if(p99ForSonKuInsert_){
                // MIX=0 and mode bypass stay exact; reset attention smoothly
                // without ever leaking a private cue into dry input PCM.
                (void)p99VocalDepth_.source(0.f,0.f,0,false);
            }
#endif
            float ears[2]{0.f,0.f};
            for(int tap=0;tap<taps;++tap) {
                int at=write_-tap;
                if(at<0)at+=taps;
                const float v0=history_[0][at],v1=history_[1][at];
                // The old implementation replaced four HRIR kernels in
                // visible 16-frame jumps. Slew continuously per audio sample.
                auto& l0=kernels_[0][0][tap];
                auto& r0=kernels_[0][1][tap];
                auto& l1=kernels_[1][0][tap];
                auto& r1=kernels_[1][1][tap];
                l0+=filterSlew_*(targets_[0][0][tap]-l0);
                r0+=filterSlew_*(targets_[0][1][tap]-r0);
                if(musicSafe && !musicFused) {
                    // Both musical input channels share exactly one centered
                    // measured HRIR. No dual-HRTF center interference; the
                    // unprocessed SIDE is returned in its own stereo axis.
                    // Only two FIR multiply-accumulates per tap, not four.
                    const float sum=v0+v1;
                    ears[0]+=sum*l0;
                    ears[1]+=sum*r0;
                }else{
                    l1+=filterSlew_*(targets_[1][0][tap]-l1);
                    r1+=filterSlew_*(targets_[1][1][tap]-r1);
                    ears[0]+=v0*l0+v1*l1;
                    ears[1]+=v0*r0+v1*r1;
                }
            }
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            p92::WorldBassFoundation::Ears p92RoomBass{};
            if(p90World)p92RoomBass=p92Bass_.process(
                x0,x1,ears[0],ears[1],
                settings_.spacePreset,settings_.roomAmount);
#endif
            if(transparentBass || positionedMode
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
               || p90World
#endif
               )
                tonalCalibration_.process(ears[0],ears[1]);
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
            if(musicalBody && roomEnabled)
                p87Body_.enhanceDirect(ears[0],ears[1]);
#endif
#endif
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
            if(positionedMode && !monoObject && p83MusicCompatibleLf_)
                p83LowFrequency_.process(x0,x1,ears[0],ears[1]);
#endif
            // In P9.9, SOURCE center body used to reach only the direct
            // KEMAR virtual speakers; room received original x0/x1.
            // Route a bounded warm MID component into the EXISTING real
            // six-wall and late reverberation processors as well, retaining
            // the original stereo SIDE and respecting their geometry/delay.
            float earlyFeedL=x0,earlyFeedR=x1;
            float lateFeedL=x0,lateFeedR=x1;
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
            if(immersiveWarm) {
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
                const float deltaL=p98RoomSourceL-x0;
                const float deltaR=p98RoomSourceR-x1;
#else
                const float deltaL=history_[0][write_]-x0;
                const float deltaR=history_[1][write_]-x1;
#endif
                earlyFeedL+=.32f*deltaL;earlyFeedR+=.32f*deltaR;
                lateFeedL+=.12f*deltaL;lateFeedR+=.12f*deltaR;
            }
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
            if(p99ForSonKuInsert_ && p90World && settings_.spatialMix>0.f && roomEnabled){
                const auto earlyFeed=p99VocalDepth_.roomEarlyFeed(x0,x1);
                const auto lateFeed=p99VocalDepth_.roomLateFeed(x0,x1);
                earlyFeedL=earlyFeed.left;
                earlyFeedR=earlyFeed.right;
                lateFeedL=lateFeed.left;
                lateFeedR=lateFeed.right;
            }
#endif
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
            if(vocalRoomOn){
                const auto e=immersibP99VocalRoom_.roomEarlyFeed(x0,x1);
                const auto l=immersibP99VocalRoom_.roomLateFeed(x0,x1);
                earlyFeedL+=e.left-x0;earlyFeedR+=e.right-x1;
                lateFeedL+=l.left-x0;lateFeedR+=l.right-x1;
            }
#endif
            auto reflection=room_.active()
                ?room_.process((p90World||transparentBass)?earlyFeedL:(musicSafe?inputMid:x0),
                    (p90World||transparentBass)?earlyFeedR:(musicSafe?inputMid:x1),
                    degrees_,(p90World||transparentBass)?settings_.stereoSpreadDegrees:
                        ((musicSafe||monoObject)?0.f:settings_.stereoSpreadDegrees)
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
                    ,(positionedMode||musicFused)?p83HeadYaw_:0.f
#else
                    ,positionedMode?p83HeadYaw_:0.f
#endif
#endif
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
                    ,horizontalFocus
#endif
                    )
                :SonKuEarlyReflections::Ears{};
            auto late=reverb_.active()
                ?reverb_.process((p90World||transparentBass)?lateFeedL:(musicSafe?inputMid:x0),
                    (p90World||transparentBass)?lateFeedR:(musicSafe?inputMid:x1))
                :SonKuMusicalReverb::Ears{};
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
            if(musicalBody && roomEnabled){
                const auto body=p87Body_.diffuseLowBody(late.left,late.right);
                late.left+=body.left;
                late.right+=body.right;
            }
#endif
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
            if(positionedMode && p83Stem_!=p83::Stem::Combined) {
                if(p83Stem_!=p83::Stem::Direct)ears[0]=ears[1]=0.f;
                if(p83Stem_!=p83::Stem::Early)reflection={};
                if(p83Stem_!=p83::Stem::Late)late={};
            }
#endif
            if(!transparentBass && !nativeSpace && !p90World &&
               settings_.outputTrim<=1.f &&
               smoothedTrim_<=1.f) {
                // Preserve byte-for-byte original HRTF/old .smix output for
                // ROOM OFF and gain <= unity. Newly boosted GAIN > unity
                // uses the protected branch below even with ROOM OFF.
                if(distanceGain==1.f) {
                    left[i]=dryLevel*x0+legacyBinauralLevel*(ears[0]+reflection.left+late.left)+sideRescue;
                    right[i]=dryLevel*x1+legacyBinauralLevel*(ears[1]+reflection.right+late.right)-sideRescue;
                } else {
                    left[i]=dryLevel*x0+legacyBinauralLevel*
                        (distanceGain*ears[0]+reflection.left+late.left)+sideRescue;
                    right[i]=dryLevel*x1+legacyBinauralLevel*
                        (distanceGain*ears[1]+reflection.right+late.right)-sideRescue;
                }
            } else {
                // Slew gain changes per sample; a typed 1.6→8 m edit must
                // never step the direct pressure at the buffer boundary.
                smoothedDistanceGain_+=distanceSlew_*
                    (targetDistance-smoothedDistanceGain_);
                smoothedRoomLevel_+=distanceSlew_*
                    (((roomEnabled&&!positionedMode&&!p90World)?1.12f:1.f)-smoothedRoomLevel_);
                smoothedBassShelf_+=distanceSlew_*
                    (((roomEnabled&&!positionedMode&&!p90World)?0.82f:0.f)-smoothedBassShelf_);
                // The original experimental return levels were intentionally
                // near-silent: ER base 0.28 * wall 0.70 * 1/r and diffuse FDN
                // output 0.23 * amount, followed by another 0.5 * trim.
                // Scene-only presence curves keep direct/reflected/tail
                // INDEPENDENT: distance still reduces direct while wall
                // echoes and the later diffuse field identify the room.
                // Gain laws are bounded affine maps, zero wet stays EXACT
                // zero, no exp/pow/alloc on the audio callback.
                const float targetErPresence=roomEnabled&&!positionedMode?
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
                    (horizontalFocus?0.76f:1.f)*
#endif
                    (1.45f+2.0f*settings_.roomAmount):1.f;
                const float targetTailPresence=roomEnabled&&!positionedMode?
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
                    (horizontalFocus?0.38f:1.f)*
#endif
                    (2.2f+4.0f*settings_.ambience):1.f;
                smoothedErPresence_+=distanceSlew_*
                    (
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
                     targetErPresence*p89DirectionalRoom
#else
                     targetErPresence
#endif
                     -smoothedErPresence_);
                smoothedTailPresence_+=distanceSlew_*
                    (
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
                     targetTailPresence*p89DiffuseRoom
#else
                     targetTailPresence
#endif
                     -smoothedTailPresence_);
#if defined(SONKU_P94_FRONT_BACK_DEPTH_RESEARCH) && SONKU_P94_FRONT_BACK_DEPTH_RESEARCH
                // Private P9.4: no angle-dependent output gain, HRTF phase
                // edits, artificial delay or changed orbit geometry.
                const auto depthCue=p90World?p94Depth_.next():p94::FrontBackDepthCue::Gains{};
                const float calibratedDirect=depthCue.direct;
                const float calibratedEarly=depthCue.early;
#else
                constexpr float calibratedDirect=1.f,calibratedEarly=1.f;
#endif
                float directL=smoothedDistanceGain_*calibratedDirect*ears[0];
                float directR=smoothedDistanceGain_*calibratedDirect*ears[1];
#if defined(SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH) && SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH
                if(p90World){
                    // Same KEMAR world speakers; do not EQ/repan wet returns.
                    // P94's independent directional calibration stays outside.
                    const auto reinforced=p96Reinforcement_.process(
                        ears[0],ears[1],smoothedDistanceGain_,
                        settings_.sourceDistanceMeters,roomEnabled);
                    // Keep P9.5 floating-point operation order bit-exact
                    // whenever ROOM OFF or <=2m has no P9.6 reinforcement.
                    if(p96Reinforcement_.active()){
                        directL=calibratedDirect*reinforced.left;
                        directR=calibratedDirect*reinforced.right;
                    }
                }
#endif
#if defined(SONKU_P97_STAGE_IMPACT_RESEARCH) && SONKU_P97_STAGE_IMPACT_RESEARCH
                if(p90World && settings_.spatialMix>0.f){
                    // Spectral stage changes remain tied to the HRTF and
                    // the existing 6-wall first-image / P92 modal fields.
                    // Identical stereo-ear coefficients do not pan/delay
                    // sources or manufacture independent mono-bass channels.
                    const auto punch=p97Stage_.direct(directL,directR);
                    directL=punch.left;directR=punch.right;
                    const auto air=p97Stage_.early(reflection.left,reflection.right);
                    reflection.left=air.left;reflection.right=air.right;
                    const auto modal=p97Stage_.modal(p92RoomBass.left,p92RoomBass.right);
                    p92RoomBass.left=modal.left;p92RoomBass.right=modal.right;
                }
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
                if(p90World && settings_.spatialMix>0.f && roomEnabled){
                    const auto air=p98Voicing_.early(reflection.left,reflection.right);
                    reflection.left=air.left;reflection.right=air.right;
                }
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
                if(p99ForSonKuInsert_ && p90World && settings_.spatialMix>0.f && roomEnabled){
                    // A slightly more lateral REAL first image gives
                    // accompaniment space without moving the singer.
                    const auto lateral=p99VocalDepth_.early(
                        reflection.left,reflection.right);
                    reflection.left=lateral.left;
                    reflection.right=lateral.right;
                    // Reduce only center-mid late energy, preserving stereo
                    // diffuse sides and full low-frequency reverberation.
                    const auto background=p99VocalDepth_.late(late.left,late.right);
                    late.left=background.left;
                    late.right=background.right;
                }
#endif
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
                if(vocalRoomOn) {
                    const auto e=immersibP99VocalRoom_.early(reflection.left,reflection.right);
                    const auto l=immersibP99VocalRoom_.late(late.left,late.right);
                    reflection.left=e.left;reflection.right=e.right;
                    late.left=l.left;late.right=l.right;
                }
#endif
                float spatialL=directL+
                    smoothedErPresence_*calibratedEarly*reflection.left+
                    smoothedTailPresence_*late.left;
                float spatialR=directR+
                    smoothedErPresence_*calibratedEarly*reflection.right+
                    smoothedTailPresence_*late.right;
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
                if(p90World){
                    // Position-aware LF modes return separately from direct
                    // pressure; room OFF is exactly zero, no added dry SIDE.
                    spatialL+=p92RoomBass.left;
                    spatialR+=p92RoomBass.right;
                }
#endif
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
                if(transparentBass){
                    // Decompose DIRECT HRTF only. Room returns remain true
                    // reflections, not low-pass input and not an EQ lift.
                    // LF stereo foundation is unaltered; at 0% MIX, bypass
                    // below is guaranteed to remain input-bit-exact.
                    binauralBass_[0]+=bassCrossoverAlpha_*
                        (ears[0]-binauralBass_[0]);
                    binauralBass_[1]+=bassCrossoverAlpha_*
                        (ears[1]-binauralBass_[1]);
                    spatialL=smoothedDistanceGain_*
                        (ears[0]-binauralBass_[0])+
                        smoothedErPresence_*reflection.left+
                        smoothedTailPresence_*late.left;
                    spatialR=smoothedDistanceGain_*
                        (ears[1]-binauralBass_[1])+
                        smoothedErPresence_*reflection.right+
                        smoothedTailPresence_*late.right;
                }else
#endif
                {
                    // Original shelf for legacy research and public parity.
                    earBass_[0]+=bassSlew_*(spatialL-earBass_[0]);
                    earBass_[1]+=bassSlew_*(spatialR-earBass_[1]);
                    spatialL+=smoothedBassShelf_*earBass_[0];
                    spatialR+=smoothedBassShelf_*earBass_[1];
                }
                // GAIN is final full-output gain, not the old wet-only trim.
                // When ROOM OFF is reselected after a native scene, the
                // additional wet presence smoothly decays with its own engine.
                const float musicLowL=transparentBass?
                    smoothedMix_*originalLowL
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
                    *(musicalBody&&roomEnabled?p87::MusicalAirBody::musicBassRoomGain:1.f)
#endif
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
                    *smoothedBassDistance_
#endif
                    :0.f;
                const float musicLowR=transparentBass?
                    smoothedMix_*originalLowR
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
                    *(musicalBody&&roomEnabled?p87::MusicalAirBody::musicBassRoomGain:1.f)
#endif
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
                    *smoothedBassDistance_
#endif
                    :0.f;
                const float baseDryL=transparentBass?dryLevel*x0:0.f;
                const float baseDryR=transparentBass?dryLevel*x1:0.f;
                float outL=baseDryL+smoothedTrim_*
                    ((transparentBass?musicLowL:dryLevel*x0)+
                     sceneBinauralLevel*(transparentBass?1.f:smoothedRoomLevel_)*spatialL)+sideRescue;
                float outR=baseDryR+smoothedTrim_*
                    ((transparentBass?musicLowR:dryLevel*x1)+
                     sceneBinauralLevel*(transparentBass?1.f:smoothedRoomLevel_)*spatialR)-sideRescue;
#if defined(SONKU_P95_MUSICAL_PRESENCE_RESEARCH) && SONKU_P95_MUSICAL_PRESENCE_RESEARCH
                if(p90World){
                    // Single native PCM graph, no WebAudio or dynamic makeup
                    // at 0% spatial MIX. Explicit user GAIN still applies.
                    if(settings_.spatialMix>0.f){
#if defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) && ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
                        // Experiment: no content-dependent automatic makeup.
                        // Keep original operator trim and existing linked peak guard.
                        if(!stageMonitor_) {
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
                        const float makeup=p95Presence_.process(x0,x1,outL,outR,
                            immersiveWarm?1.10f:1.65f);
#else
                        const float makeup=p95Presence_.process(x0,x1,outL,outR);
#endif
                        outL*=makeup;outR*=makeup;
#if defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) && ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
                        }
#endif
                    }
                }
#endif
#if defined(SONKU_P97_STAGE_IMPACT_RESEARCH) && SONKU_P97_STAGE_IMPACT_RESEARCH
                if(p90World && settings_.spatialMix>0.f){
                    // The finished ear tonal block MUST NOT touch
                    // 0% MIX (native dry/bypass) even while ROOM remains ON.
                    const auto finished=p97Stage_.finish(outL,outR);
                    outL=finished.left;outR=finished.right;
                }
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
                if(p90World && settings_.spatialMix>0.f && roomEnabled){
                    // Post-adaptive-makeup, PRE-existing 0.98 linked guard.
                    // The cinema low-end cannot attenuate the whole mix
                    // via the upstream automatic compensation meter.
                    const auto voiced=p98Voicing_.finish(outL,outR);
                    outL=voiced.left;outR=voiced.right;
                }
#endif
                // Zero-lookahead *linked* sample-peak safety guard. Quiet
                // signals are linear; peaks are gently returned below unity
                // without routing through a second engine or allocating.
                const float peak=std::max(std::abs(outL),std::abs(outR));
                const float immediate=peak>0.98f?0.98f/peak:1.f;
                limiterGain_=std::min(immediate,
                    std::min(1.f,limiterGain_+limiterRelease_));
#if defined(SONKU_IMMERSIB_PCM_DIAGNOSTICS) && SONKU_IMMERSIB_PCM_DIAGNOSTICS
                ++dynamicsTrace_.samples;
                if(limiterGain_<.999f)++dynamicsTrace_.limitedSamples;
                dynamicsTrace_.minimumLimiterGain=std::min(
                    dynamicsTrace_.minimumLimiterGain,limiterGain_);
                dynamicsTrace_.limiterReductionSum+=1.-limiterGain_;
#if defined(SONKU_P95_MUSICAL_PRESENCE_RESEARCH) && SONKU_P95_MUSICAL_PRESENCE_RESEARCH
                dynamicsTrace_.currentMakeup=p95Presence_.gain();
                dynamicsTrace_.makeupGainSum+=dynamicsTrace_.currentMakeup;
#endif
#endif
                left[i]=outL*limiterGain_;
                right[i]=outR*limiterGain_;
#if defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) && ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH
                // Exact 6399c0f5 renderer and limiter have already finished.
                // This is the user's external PEQ workflow, inside the insert.
                // Never feed its result back into stage, room or original limiter.
                if(musicalEqEnabled_) {
                    if(p90World && roomEnabled && settings_.spatialMix>0.f)
                        musicalEq_.process(left[i],right[i],smoothedMix_);
                    else musicalEq_.reset();
                }
#endif
            }
            if(++write_==taps)write_=0;
            if(step!=0.) {
                degrees_+=step;
                if(degrees_>=360.)degrees_-=360.;
                if(degrees_<0.)degrees_+=360.;
            }
            --remaining_;
        }
    }
private:
    bool applyAcousticSettings(const Settings& s) noexcept {
        const float width=s.spacePreset==1?7.2f:
            s.spacePreset==2?14.4f:28.8f;
        const float roomHeight=s.spacePreset<=1?3.2f:
                               s.spacePreset==2?4.8f:7.2f;
        float sourceHeight=1.45f;
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
        if(room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch){
            xyz::Placement xyzSource{};
            if(!xyz::project(s.sourceDistanceMeters,researchElevationDegrees_,
                             1.45f,roomHeight,xyzSource))return false;
            sourceHeight=xyzSource.sourceHeightMeters;
        }
#endif
        const SonKuRoomGeometry geometry{
            width,width,s.spacePreset? s.sourceDistanceMeters:1.6f,
            // P7.1 keeps the user's radial source distance authoritative.
            // Source XYZ only affects the explicit offline research profile.
            roomHeight,1.45f,sourceHeight};
        const auto preset=s.spacePreset==3?SonKuReverbPreset::LargeRoom:
            s.spacePreset==2?SonKuReverbPreset::AiryChamber:
                             SonKuReverbPreset::StudioGlue;
        return room_.setRealtimeRoom(geometry,std::min(s.roomAmount,1.f),s.spacePreset!=0) &&
            reverb_.setRealtimeProgram(preset,
                s.spacePreset!=0?s.ambience:0.f);
    }
    // Positive azimuth is right. MIT KEMAR supplied azimuth 0..180 with
    // symmetric opposite-ear swapping for left hemisphere, not yaw-panning.
    using EarKernel=std::array<std::array<float,taps>,2>;
    void interpolate(EarKernel& destination,double azimuth) noexcept {
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
        if(room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch &&
           (researchElevationDegrees_!=0.f
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
             || ((p83Mode_==p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
                      || p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
                      || p83Mode_==p83::Mode::MusicSpatialP90
#endif
#endif
                      ) &&
                 settings_.hrtfElevationTrimDegrees!=0.f)
#endif
             )){
            // Both stereo emitters AND the coherent MID correction use the
            // very same truly measured direct elevation, no third FIR.
            const float spectralElevation=researchElevationDegrees_
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
                +((p83Mode_==p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP90
#endif
#endif
             )?
                     settings_.hrtfElevationTrimDegrees:0.f)
#endif
                ;
            hrir3d::interpolate(static_cast<float>(azimuth),
                                 spectralElevation,
                                 std::abs(sampleRate_-44100.)<=.1,destination);
            return;
        }
#endif
        const double norm=wrapDegrees(azimuth);
        const bool swapped=norm>180.;
        const double positive=swapped?360.-norm:norm;
        const double index=positive/static_cast<double>(hrir::spacingDegrees);
        const int lo=std::clamp(static_cast<int>(index),0,hrir::directions-1);
        const int hi=std::min(lo+1,hrir::directions-1);
        const float u=static_cast<float>(index-static_cast<double>(lo));
        constexpr float scale=1.f/32768.f;
        for(int ear=0;ear<2;++ear) {
            const int measuredEar=swapped?1-ear:ear;
            for(int k=0;k<taps;++k) {
                const float next=scale*(static_cast<float>(bank_[lo][measuredEar][k])*(1.f-u)
                    + static_cast<float>(bank_[hi][measuredEar][k])*u);
                // Recompute target every 16 samples, interpolate per sample.
                destination[ear][k]=next;
            }
        }
    }
    void updateKernels() noexcept {
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        if(p83Mode_!=p83::Mode::MusicBedP82
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
           && p83Mode_!=p83::Mode::MusicSpatialP84
#if defined(SONKU_P85_WORLD_COHERENCE_RESEARCH) && SONKU_P85_WORLD_COHERENCE_RESEARCH
           && p83Mode_!=p83::Mode::MusicSpatialP85
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
           && p83Mode_!=p83::Mode::MusicSpatialP86
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP87
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            && p83Mode_!=p83::Mode::MusicSpatialP90
#endif
#endif
#endif
#endif
#endif
#endif
#endif
           && room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch) {
            // 2x2 measured source-to-ear transfer. No MID center
            // projection, no headphone SIDE, no coherence-dependent mode.
            const auto positions=p83::stereo(static_cast<float>(degrees_),
                settings_.stereoSpreadDegrees,p83HeadYaw_);
            interpolate(targets_[0],p83Mode_==p83::Mode::MonoObject?
                p83::toHead(static_cast<float>(degrees_),p83HeadYaw_):
                positions.leftHead);
            if(p83Mode_==p83::Mode::SpeakerMonitor)
                interpolate(targets_[1],positions.rightHead);
            else
                for(auto& ear:targets_[1])ear.fill(0.f);
            if(p83MusicCompatibleLf_ && p83Mode_==p83::Mode::SpeakerMonitor)
                p83LowFrequency_.updateDirections(positions.leftHead,
                                                   positions.rightHead);
            if(!haveKernel_)kernels_=targets_;
            haveKernel_=true;
            return;
        }
#endif
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
        if((p83Mode_==p83::Mode::MusicSpatialP84 ||
#if defined(SONKU_P85_WORLD_COHERENCE_RESEARCH) && SONKU_P85_WORLD_COHERENCE_RESEARCH
            p83Mode_==p83::Mode::MusicSpatialP85 ||
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
            p83Mode_==p83::Mode::MusicSpatialP86 ||
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
            p83Mode_==p83::Mode::MusicSpatialP87 ||
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
            p83Mode_==p83::Mode::MusicSpatialP88 ||
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
            p83Mode_==p83::Mode::MusicSpatialP89 ||
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            p83Mode_==p83::Mode::MusicSpatialP90 ||
#endif
#endif
#endif
#endif
#endif
#endif
            (p84FusedMusic_ && p83Mode_==p83::Mode::MusicBedP82)) &&
           room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch){
            // One 2x2 FIR graph. Preserve the EXACT P8.2 mixed-channel
            // foundation while injecting only a bounded high-frequency
            // measured directional difference into each source HRIR.
            // Avoid LF480: no invented bass matrix or additional DSP core.
#if defined(SONKU_P85_WORLD_COHERENCE_RESEARCH) && SONKU_P85_WORLD_COHERENCE_RESEARCH
            // P8.5: common music MID and both virtual speakers are in
            // the same head-relative frame. Earlier musical modes remain
            // byte-identical; only mode 4 uses this yaw-coherent anchor.
            // P8.6 retains P8.5's approved world/head direct music anchor.
            interpolate(centerTarget_,(p83Mode_==p83::Mode::MusicSpatialP85
#if defined(SONKU_P86_HORIZONTAL_FOCUS_RESEARCH) && SONKU_P86_HORIZONTAL_FOCUS_RESEARCH
                || p83Mode_==p83::Mode::MusicSpatialP86
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP87
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP88
#if defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) && SONKU_P89_DEPTH_COHERENCE_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP89
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
             || p83Mode_==p83::Mode::MusicSpatialP90
#endif
#endif
#endif
#endif
#endif
                )?static_cast<double>(p83::toHead(static_cast<float>(degrees_),p83HeadYaw_))
                :degrees_);
#else
            interpolate(centerTarget_,degrees_);
#endif
            const auto positions=p83::stereo(static_cast<float>(degrees_),
                settings_.stereoSpreadDegrees,p83HeadYaw_);
            interpolate(targets_[0],positions.leftHead);
            interpolate(targets_[1],positions.rightHead);
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
            if(p83Mode_==p83::Mode::MusicSpatialP90){
                p90Decoder_.compose(centerTarget_,targets_[0],targets_[1]);
                p92Bass_.updateDirections(positions.leftHead,positions.rightHead);
#if defined(SONKU_P94_FRONT_BACK_DEPTH_RESEARCH) && SONKU_P94_FRONT_BACK_DEPTH_RESEARCH
#if defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) && ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
                if(stageMonitor_) {
                    p94Depth_.retargetStereo(positions.leftHead,positions.rightHead,
                        settings_.spacePreset!=0 && settings_.roomAmount>0.f);
                } else
#endif
                p94Depth_.retarget(p83::toHead(static_cast<float>(degrees_),p83HeadYaw_),
                                  settings_.spacePreset!=0 && settings_.roomAmount>0.f);
#endif
            }
            else
#endif
                p84MusicKernel_.merge(centerTarget_,targets_[0],targets_[1]);
            if(!haveKernel_)kernels_=targets_;
            haveKernel_=true;
            return;
        }
#endif
#if defined(SONKU_P75_MUSIC_SAFE_RESEARCH) && SONKU_P75_MUSIC_SAFE_RESEARCH
        if(room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch) {
            // Control evidence no longer gates musical MID coefficients:
            // the first sample already has a single centered KEMAR HRIR.
            // Never add two offset HRTFs for correlated source material.
            interpolate(centerTarget_,degrees_);
            targets_[0]=centerTarget_;
            targets_[1]=centerTarget_;
            if(!haveKernel_)kernels_=targets_;
            haveKernel_=true;
            return;
        }
#endif
        interpolate(targets_[0],
            degrees_-static_cast<double>(settings_.stereoSpreadDegrees)*0.5);
        interpolate(targets_[1],
            degrees_+static_cast<double>(settings_.stereoSpreadDegrees)*0.5);
        // A stereo L=R object should become ONE virtual source at the central
        // azimuth. Averaging two displaced HRIRs generates comb filtering
        // (for example ~11dB attenuation near 2 kHz at rear/60° in KEMAR).
        // Algebraic M/S projection, without a third convolution:
        //  H_L' = H_L + mu*(H_center - (H_L+H_R)/2)
        //  H_R' = H_R + mu*(H_center - (H_L+H_R)/2)
        // The mid gets the true center HRIR as mu→1, while the SIDE
        // transfer H_L'-H_R' is unchanged for ANY mu. Mono channel
        // inputs/correlated scene intensity remain native PCM-only.
        const float coherent=integrity_.coherentAmount();
        if(coherent>1.e-5f && settings_.stereoSpreadDegrees>0.f) {
            interpolate(centerTarget_,degrees_);
            for(int ear=0;ear<2;++ear) {
                for(int k=0;k<taps;++k) {
                    const float half=.5f*
                        (targets_[0][ear][k]+targets_[1][ear][k]);
                    const float correction=coherent*
                        (centerTarget_[ear][k]-half);
                    targets_[0][ear][k]+=correction;
                    targets_[1][ear][k]+=correction;
                }
            }
        }
        // The first prepared sample uses the original full-level measured
        // HRIR; the correction and later direction edits slew per sample.
        if(!haveKernel_)kernels_=targets_;
        haveKernel_=true;
    }
    using Bank=const std::int16_t (*)[2][taps];
    Bank bank_=nullptr;
    std::array<std::array<float,taps>,2> history_{};
    std::array<std::array<std::array<float,taps>,2>,2> kernels_{};
    std::array<std::array<std::array<float,taps>,2>,2> targets_{};
    EarKernel centerTarget_{};  // fixed target coefficients, no extra PCM FIR
    Settings settings_{};
    SonKuEarlyReflections room_{};
    SonKuMusicalReverb reverb_{};
    SonKuStereoIntegrity integrity_{};
    float smoothedMix_=1.f,smoothedTrim_=0.75f;
#if defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) && SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
    float smoothedBassDistance_=1.f;
#endif
    float smoothedDistanceGain_=1.f,smoothedRoomLevel_=1.f;
    float smoothedBassShelf_=0.f,limiterGain_=1.f;
#if defined(SONKU_IMMERSIB_PCM_DIAGNOSTICS) && SONKU_IMMERSIB_PCM_DIAGNOSTICS
    DynamicsTrace dynamicsTrace_{};
#endif
    float smoothedErPresence_=1.f,smoothedTailPresence_=1.f;
    std::array<float,2> earBass_{};
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
    SonKuHrtfTonalCalibration tonalCalibration_{};
#endif
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
    std::array<float,2> originalBass_{},binauralBass_{};
    float sideBass_=0.f,bassCrossoverAlpha_=0.f;
#endif
    float filterSlew_=0.f,levelSlew_=0.f;
    float distanceSlew_=0.f,bassSlew_=0.f,limiterRelease_=0.f;
    double sampleRate_=0.;
    double degrees_=0.;
    int write_=0;
    int remaining_=0;
#if defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) && ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
    bool stageMonitor_=false;
#if defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) && ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH
    p102::FixedPaMusicalEq musicalEq_{};
    bool musicalEqEnabled_=false;
#endif
#endif
    bool ready_=false;
    bool haveKernel_=false;
    bool nativeSpaceEverUsed_=false;
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
    p83::Mode p83Mode_=p83::Mode::MusicBedP82;
    p83::Stem p83Stem_=p83::Stem::Combined;
    p83::LowFrequencySpeakerTransfer p83LowFrequency_{};
    bool p83MusicCompatibleLf_=false;
    float p83HeadYaw_=0.f;
#endif
#if defined(SONKU_P84_SINGLE_CORE_FUSION_RESEARCH) && SONKU_P84_SINGLE_CORE_FUSION_RESEARCH
    p84::FusedMusicHrir p84MusicKernel_{};
#if defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) && SONKU_P90_TRUE_EXTERNAL_RESEARCH
    p90::StereoDecoder p90Decoder_{};
    p92::WorldBassFoundation p92Bass_{};
#if defined(SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH) && SONKU_P96_DISTANCE_REINFORCEMENT_RESEARCH
    p96::DistanceReinforcement p96Reinforcement_{};
#endif
#if defined(SONKU_P97_STAGE_IMPACT_RESEARCH) && SONKU_P97_STAGE_IMPACT_RESEARCH
    p97::StageImpact p97Stage_{};
#endif
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
    p98::SceneVoicing p98Voicing_{};
#endif
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
    p99::MusicalVocalDepth p99VocalDepth_{};
    bool p99ForSonKuInsert_=false; // per-instance, never serialized
#endif
#if defined(SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH) && SONKU_IMMERSIB_P99_VOCAL_ROOM_RESEARCH
    p99::ImmerSIBVocalRoom immersibP99VocalRoom_{};
    bool immersibP99Enabled_=false;
#endif
#if defined(SONKU_P95_MUSICAL_PRESENCE_RESEARCH) && SONKU_P95_MUSICAL_PRESENCE_RESEARCH
    p95::MusicalPresence p95Presence_{};
#endif
#if defined(SONKU_P94_FRONT_BACK_DEPTH_RESEARCH) && SONKU_P94_FRONT_BACK_DEPTH_RESEARCH
    p94::FrontBackDepthCue p94Depth_{};
#endif
#endif
#if defined(SONKU_P87_MUSIC_BODY_RESEARCH) && SONKU_P87_MUSIC_BODY_RESEARCH
    p87::MusicalAirBody p87Body_{};
#endif
    bool p84FusedMusic_=false; // research opt-in only; P8.2 default untouched
#endif
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
    float researchElevationDegrees_=0.f;
    bool researchPcmStarted_=false;
#endif
};
} // namespace armix::sonku
