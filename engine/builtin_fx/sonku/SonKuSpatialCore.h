// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuMeasuredHrir.h"
#include "SonKuEarlyReflections.h"
#include "SonKuMusicalReverb.h"
#include "SonKuStereoIntegrity.h"
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH || \
    !defined(SONKU_P71_DIRECT_XYZ_RESEARCH) || !SONKU_P71_DIRECT_XYZ_RESEARCH
#error P8.3 speaker research requires the P8.2 baseline and P7.1 XYZ engine
#endif
#include "SonKuP83SpeakerScene.h"
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
        std::isfinite(s.sourceDistanceMeters) && s.sourceDistanceMeters>=1.6f &&
        s.sourceDistanceMeters<=10.f &&
        std::isfinite(s.roomAmount) && s.roomAmount>=0.f && s.roomAmount<=1.f &&
        std::isfinite(s.ambience) && s.ambience>=0.f && s.ambience<=1.f &&
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
    bool prepare(double rate, SonKuReflectionProfile profile=
                 SonKuReflectionProfile::Lightweight) noexcept {
        ready_=false;
        if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 &&
                                    std::abs(rate-48000.)>.1))return false;
        sampleRate_=rate;
        bank_=(std::abs(rate-44100.)<=.1)?hrir::normal44100:hrir::normal48000;
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        if(profile==SonKuReflectionProfile::SixSurfaceKemarResearch &&
           !tonalCalibration_.prepare(rate,bank_))return false;
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
        for(auto& channel:history_)channel.fill(0.f);
        for(auto& source:kernels_)for(auto& ear:source)ear.fill(0.f);
        for(auto& source:targets_)for(auto& ear:source)ear.fill(0.f);
        smoothedMix_=settings_.spatialMix;
        smoothedTrim_=settings_.outputTrim;
        smoothedDistanceGain_=1.f;
        smoothedRoomLevel_=1.f;
        smoothedBassShelf_=0.f;
        smoothedErPresence_=1.f;
        smoothedTailPresence_=1.f;
        earBass_.fill(0.f);
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        tonalCalibration_.reset();
#endif
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        originalBass_.fill(0.f);
        binauralBass_.fill(0.f);
        sideBass_=0.f;
#endif
        limiterGain_=1.f;
        write_=0;
        remaining_=0;
        degrees_=wrapDegrees(settings_.startAzimuthDegrees);
        haveKernel_=false;
        room_.reset();
        reverb_.reset();
        integrity_.reset();
    }
    bool setSettings(const Settings& s) noexcept {
        if(!valid(s))return false;
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
        if(p83Mode_==p83::Mode::SpeakerMonitor &&
           (s.stereoSpreadDegrees<20.f || s.stereoSpreadDegrees>120.f))
            return false; // distinct left and right speaker directions
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
        settings_=s;
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
        reset();
        return true;
    }
    bool setResearchHeadYaw(float yaw) noexcept {
        if(!ready_ || researchPcmStarted_ || !p83::validYaw(yaw) ||
           room_.preparedProfile()!=SonKuReflectionProfile::SixSurfaceKemarResearch)
            return false;
        p83HeadYaw_=yaw;
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
            p83Mode_!=p83::Mode::MusicBedP82;
        const bool monoObject=positionedMode && p83Mode_==p83::Mode::MonoObject;
#else
        constexpr bool positionedMode=false,monoObject=false;
#endif
        const float distanceGain=room_.relativeDistanceGain();
        const bool nativeSpace=nativeSpaceEverUsed_;
        const bool roomEnabled=settings_.spacePreset!=0;
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        const bool transparentBass=
            room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch && !positionedMode;
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
        // Deliberate music-monitor compensation: real distance perception
        // still comes from direct/reflected ratio and high-frequency cues,
        // while the direct field is not attenuated by the full physical 1/r.
        // 1.6m=1; 8m~0.55 instead of 0.20. No pow/exp on the PCM callback.
        const float targetDistance=roomEnabled?
            1.f/(1.f+0.13f*(settings_.sourceDistanceMeters-1.6f)):1.f;
        for(int i=0;i<samples;++i) {
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
            const float sceneBinauralLevel=.5f*smoothedMix_;
            if(remaining_==0) {
                updateKernels();
                remaining_=updateInterval;
            }
            const float x0=std::isfinite(left[i])?left[i]:0.f;
            const float x1=monoObject?0.f:(std::isfinite(right[i])?right[i]:0.f);
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
            history_[0][write_]=x0;
            history_[1][write_]=x1;
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
                if(musicSafe) {
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
            if(transparentBass || positionedMode)
                tonalCalibration_.process(ears[0],ears[1]);
#endif
            auto reflection=room_.active()
                ?room_.process(transparentBass?x0:(musicSafe?inputMid:x0),
                    transparentBass?x1:(musicSafe?inputMid:x1),
                    degrees_,transparentBass?settings_.stereoSpreadDegrees:
                        ((musicSafe||monoObject)?0.f:settings_.stereoSpreadDegrees)
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
                    ,positionedMode?p83HeadYaw_:0.f
#endif
                    )
                :SonKuEarlyReflections::Ears{};
            auto late=reverb_.active()
                ?reverb_.process(transparentBass?x0:(musicSafe?inputMid:x0),
                    transparentBass?x1:(musicSafe?inputMid:x1))
                :SonKuMusicalReverb::Ears{};
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
            if(positionedMode && p83Stem_!=p83::Stem::Combined) {
                if(p83Stem_!=p83::Stem::Direct)ears[0]=ears[1]=0.f;
                if(p83Stem_!=p83::Stem::Early)reflection={};
                if(p83Stem_!=p83::Stem::Late)late={};
            }
#endif
            if(!transparentBass && !nativeSpace && settings_.outputTrim<=1.f &&
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
                    (((roomEnabled&&!positionedMode)?1.12f:1.f)-smoothedRoomLevel_);
                smoothedBassShelf_+=distanceSlew_*
                    (((roomEnabled&&!positionedMode)?0.82f:0.f)-smoothedBassShelf_);
                // The original experimental return levels were intentionally
                // near-silent: ER base 0.28 * wall 0.70 * 1/r and diffuse FDN
                // output 0.23 * amount, followed by another 0.5 * trim.
                // Scene-only presence curves keep direct/reflected/tail
                // INDEPENDENT: distance still reduces direct while wall
                // echoes and the later diffuse field identify the room.
                // Gain laws are bounded affine maps, zero wet stays EXACT
                // zero, no exp/pow/alloc on the audio callback.
                const float targetErPresence=roomEnabled&&!positionedMode?
                    1.45f+2.0f*settings_.roomAmount:1.f;
                const float targetTailPresence=roomEnabled&&!positionedMode?
                    2.2f+4.0f*settings_.ambience:1.f;
                smoothedErPresence_+=distanceSlew_*
                    (targetErPresence-smoothedErPresence_);
                smoothedTailPresence_+=distanceSlew_*
                    (targetTailPresence-smoothedTailPresence_);
                float spatialL=smoothedDistanceGain_*ears[0]+
                    smoothedErPresence_*reflection.left+
                    smoothedTailPresence_*late.left;
                float spatialR=smoothedDistanceGain_*ears[1]+
                    smoothedErPresence_*reflection.right+
                    smoothedTailPresence_*late.right;
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
                    smoothedMix_*originalLowL:0.f;
                const float musicLowR=transparentBass?
                    smoothedMix_*originalLowR:0.f;
                const float baseDryL=transparentBass?dryLevel*x0:0.f;
                const float baseDryR=transparentBass?dryLevel*x1:0.f;
                float outL=baseDryL+smoothedTrim_*
                    ((transparentBass?musicLowL:dryLevel*x0)+
                     sceneBinauralLevel*(transparentBass?1.f:smoothedRoomLevel_)*spatialL)+sideRescue;
                float outR=baseDryR+smoothedTrim_*
                    ((transparentBass?musicLowR:dryLevel*x1)+
                     sceneBinauralLevel*(transparentBass?1.f:smoothedRoomLevel_)*spatialR)-sideRescue;
                // Zero-lookahead *linked* sample-peak safety guard. Quiet
                // signals are linear; peaks are gently returned below unity
                // without routing through a second engine or allocating.
                const float peak=std::max(std::abs(outL),std::abs(outR));
                const float immediate=peak>0.98f?0.98f/peak:1.f;
                limiterGain_=std::min(immediate,
                    std::min(1.f,limiterGain_+limiterRelease_));
                left[i]=outL*limiterGain_;
                right[i]=outR*limiterGain_;
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
        return room_.setRealtimeRoom(geometry,s.roomAmount,s.spacePreset!=0) &&
            reverb_.setRealtimeProgram(preset,
                s.spacePreset!=0?s.ambience:0.f);
    }
    // Positive azimuth is right. MIT KEMAR supplied azimuth 0..180 with
    // symmetric opposite-ear swapping for left hemisphere, not yaw-panning.
    using EarKernel=std::array<std::array<float,taps>,2>;
    void interpolate(EarKernel& destination,double azimuth) noexcept {
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
        if(room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch &&
           researchElevationDegrees_!=0.f){
            // Both stereo emitters AND the coherent MID correction use the
            // very same truly measured direct elevation, no third FIR.
            hrir3d::interpolate(static_cast<float>(azimuth),
                                 researchElevationDegrees_,
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
        if(p83Mode_!=p83::Mode::MusicBedP82 &&
           room_.preparedProfile()==SonKuReflectionProfile::SixSurfaceKemarResearch) {
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
    float smoothedDistanceGain_=1.f,smoothedRoomLevel_=1.f;
    float smoothedBassShelf_=0.f,limiterGain_=1.f;
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
    bool ready_=false;
    bool haveKernel_=false;
    bool nativeSpaceEverUsed_=false;
#if defined(SONKU_P83_PHYSICAL_SPEAKER_RESEARCH) && SONKU_P83_PHYSICAL_SPEAKER_RESEARCH
    p83::Mode p83Mode_=p83::Mode::MusicBedP82;
    p83::Stem p83Stem_=p83::Stem::Combined;
    float p83HeadYaw_=0.f;
#endif
#if defined(SONKU_P71_DIRECT_XYZ_RESEARCH) && SONKU_P71_DIRECT_XYZ_RESEARCH
    float researchElevationDegrees_=0.f;
    bool researchPcmStarted_=false;
#endif
};
} // namespace armix::sonku
