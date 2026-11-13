// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuMeasuredHrir.h"
#include "SonKuEarlyReflections.h"
#include "SonKuMusicalReverb.h"
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
    bool prepare(double rate) noexcept {
        ready_=false;
        if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 &&
                                    std::abs(rate-48000.)>.1))return false;
        sampleRate_=rate;
        bank_=(std::abs(rate-44100.)<.1)?hrir::normal44100:hrir::normal48000;
        // Prepared control-side: continuous coefficient and gain smoothing
        // without trigonometric/exponential work in the audio callback.
        filterSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.006)));
        levelSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.008)));
        // Music-monitor distance/loudness controls are prepared off the
        // realtime callback. Headphone low shelf ~180 Hz; no added latency.
        distanceSlew_=static_cast<float>(1.-std::exp(-1./(rate*0.030)));
        bassSlew_=static_cast<float>(1.-std::exp(-2.*3.14159265358979323846*180./rate));
        limiterRelease_=static_cast<float>(1./(rate*0.080));
        if(!room_.prepare(rate)||!reverb_.prepare(rate))return false;
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
        earBass_.fill(0.f);
        limiterGain_=1.f;
        write_=0;
        remaining_=0;
        degrees_=wrapDegrees(settings_.startAzimuthDegrees);
        haveKernel_=false;
        room_.reset();
        reverb_.reset();
    }
    bool setSettings(const Settings& s) noexcept {
        if(!valid(s))return false;
        // Keep phase continuous for live speed/spread adjustments; changing start
        // position while static is an intentional absolute reposition.
        if(!s.moving || !settings_.moving ||
           s.startAzimuthDegrees!=settings_.startAzimuthDegrees)
            degrees_=wrapDegrees(s.startAzimuthDegrees);
        const bool changed=s.spacePreset!=settings_.spacePreset ||
            s.sourceDistanceMeters!=settings_.sourceDistanceMeters ||
            s.roomAmount!=settings_.roomAmount ||
            s.ambience!=settings_.ambience;
        if(changed && !applyAcousticSettings(s))return false;
        settings_=s;
        // Once native ROOM has been enabled, retain the smoothed transition
        // when it is turned OFF. Standalone experimental V2/V3 OFF fixtures
        // continue to use their exact original 1/r distance law.
        if(s.spacePreset!=0)nativeSpaceEverUsed_=true;
        remaining_=0;
        return true;
    }
    Settings settings() const noexcept {return settings_;}
    // A/B-only control pending room's headphone/device qualification.
    // Legacy live insert remains room-free until product evidence is accepted.
    bool setEarlyReflectionsAmount(float amount) noexcept {
        return room_.setAmount(amount);
    }
    // Explicit A/B-only shape selection; never persisted or host-exposed.
    bool setEarlyReflectionsShape(SonKuRoomShape shape) noexcept {
        return room_.setShape(shape);
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
    double azimuthDegrees() const noexcept {return degrees_;}
    // In-place stereo; caller owns valid, non-overlapping L/R PCM pointers.
    // No vector, heap, blocking calls, I/O or filter bank allocation.
    void process(float* left,float* right,int samples) noexcept {
        if(!ready_ || !left || !right || samples<=0)return;
        const double step=(settings_.moving ?
            (settings_.clockwise?1.:-1.)*360./(sampleRate_*settings_.revolutionSeconds) : 0.);
        const float distanceGain=room_.relativeDistanceGain();
        const bool nativeSpace=nativeSpaceEverUsed_;
        const bool roomEnabled=settings_.spacePreset!=0;
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
            const float x1=std::isfinite(right[i])?right[i]:0.f;
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
                l1+=filterSlew_*(targets_[1][0][tap]-l1);
                r1+=filterSlew_*(targets_[1][1][tap]-r1);
                ears[0]+=v0*l0+v1*l1;
                ears[1]+=v0*r0+v1*r1;
            }
            const auto reflection=room_.active()
                ?room_.process(x0,x1,degrees_,settings_.stereoSpreadDegrees)
                :SonKuEarlyReflections::Ears{};
            const auto late=reverb_.active()
                ?reverb_.process(x0,x1)
                :SonKuMusicalReverb::Ears{};
            if(!nativeSpace && settings_.outputTrim<=1.f &&
               smoothedTrim_<=1.f) {
                // Preserve byte-for-byte original HRTF/old .smix output for
                // ROOM OFF and gain <= unity. Newly boosted GAIN > unity
                // uses the protected branch below even with ROOM OFF.
                if(distanceGain==1.f) {
                    left[i]=dryLevel*x0+legacyBinauralLevel*(ears[0]+reflection.left+late.left);
                    right[i]=dryLevel*x1+legacyBinauralLevel*(ears[1]+reflection.right+late.right);
                } else {
                    left[i]=dryLevel*x0+legacyBinauralLevel*
                        (distanceGain*ears[0]+reflection.left+late.left);
                    right[i]=dryLevel*x1+legacyBinauralLevel*
                        (distanceGain*ears[1]+reflection.right+late.right);
                }
            } else {
                // Slew gain changes per sample; a typed 1.6→8 m edit must
                // never step the direct pressure at the buffer boundary.
                smoothedDistanceGain_+=distanceSlew_*
                    (targetDistance-smoothedDistanceGain_);
                smoothedRoomLevel_+=distanceSlew_*
                    ((roomEnabled?1.12f:1.f)-smoothedRoomLevel_);
                smoothedBassShelf_+=distanceSlew_*
                    ((roomEnabled?0.82f:0.f)-smoothedBassShelf_);
                // The original experimental return levels were intentionally
                // near-silent: ER base 0.28 * wall 0.70 * 1/r and diffuse FDN
                // output 0.23 * amount, followed by another 0.5 * trim.
                // Scene-only presence curves keep direct/reflected/tail
                // INDEPENDENT: distance still reduces direct while wall
                // echoes and the later diffuse field identify the room.
                // Gain laws are bounded affine maps, zero wet stays EXACT
                // zero, no exp/pow/alloc on the audio callback.
                const float erPresence=roomEnabled?
                    1.45f+2.0f*settings_.roomAmount:1.f;
                const float tailPresence=roomEnabled?
                    2.2f+4.0f*settings_.ambience:1.f;
                float spatialL=smoothedDistanceGain_*ears[0]+
                    erPresence*reflection.left+tailPresence*late.left;
                float spatialR=smoothedDistanceGain_*ears[1]+
                    erPresence*reflection.right+tailPresence*late.right;
                // Low-frequency KEMAR response is ~-7..-11 dB below 200Hz.
                // A gentle bounded *binaural* shelf restores warmth without
                // mixing dry mono under the HRIR or altering left/right ITD.
                earBass_[0]+=bassSlew_*(spatialL-earBass_[0]);
                earBass_[1]+=bassSlew_*(spatialR-earBass_[1]);
                spatialL+=smoothedBassShelf_*earBass_[0];
                spatialR+=smoothedBassShelf_*earBass_[1];
                // GAIN is final full-output gain, not the old wet-only trim.
                // When ROOM OFF is reselected after a native scene, the
                // additional wet presence smoothly decays with its own engine.
                float outL=smoothedTrim_*
                    (dryLevel*x0+sceneBinauralLevel*smoothedRoomLevel_*spatialL);
                float outR=smoothedTrim_*
                    (dryLevel*x1+sceneBinauralLevel*smoothedRoomLevel_*spatialR);
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
        const SonKuRoomGeometry geometry{
            width,width,s.spacePreset? s.sourceDistanceMeters:1.6f};
        const auto preset=s.spacePreset==3?SonKuReverbPreset::LargeRoom:
            s.spacePreset==2?SonKuReverbPreset::AiryChamber:
                             SonKuReverbPreset::StudioGlue;
        return room_.setRealtimeRoom(geometry,s.roomAmount,s.spacePreset!=0) &&
            reverb_.setRealtimeProgram(preset,
                s.spacePreset!=0?s.ambience:0.f);
    }
    // Positive azimuth is right. MIT KEMAR supplied azimuth 0..180 with
    // symmetric opposite-ear swapping for left hemisphere, not yaw-panning.
    void interpolate(std::size_t source,double azimuth) noexcept {
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
                targets_[source][ear][k]=next;
            }
        }
    }
    void updateKernels() noexcept {
        interpolate(0,degrees_-static_cast<double>(settings_.stereoSpreadDegrees)*0.5);
        interpolate(1,degrees_+static_cast<double>(settings_.stereoSpreadDegrees)*0.5);
        // First prepared sample is a valid full-level measured HRIR; only
        // subsequent direction edits slew, avoiding initial audio dropout.
        if(!haveKernel_)kernels_=targets_;
        haveKernel_=true;
    }
    using Bank=const std::int16_t (*)[2][taps];
    Bank bank_=nullptr;
    std::array<std::array<float,taps>,2> history_{};
    std::array<std::array<std::array<float,taps>,2>,2> kernels_{};
    std::array<std::array<std::array<float,taps>,2>,2> targets_{};
    Settings settings_{};
    SonKuEarlyReflections room_{};
    SonKuMusicalReverb reverb_{};
    float smoothedMix_=1.f,smoothedTrim_=0.75f;
    float smoothedDistanceGain_=1.f,smoothedRoomLevel_=1.f;
    float smoothedBassShelf_=0.f,limiterGain_=1.f;
    std::array<float,2> earBass_{};
    float filterSlew_=0.f,levelSlew_=0.f;
    float distanceSlew_=0.f,bassSlew_=0.f,limiterRelease_=0.f;
    double sampleRate_=0.;
    double degrees_=0.;
    int write_=0;
    int remaining_=0;
    bool ready_=false;
    bool haveKernel_=false;
    bool nativeSpaceEverUsed_=false;
};
} // namespace armix::sonku
