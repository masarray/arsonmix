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
    float outputTrim = 0.75f;         // avoid summing two uncorrelated emitters at unity
    bool moving = true;
    bool clockwise = true;
};
inline bool valid(const Settings& s) noexcept {
    return std::isfinite(s.startAzimuthDegrees) &&
        std::abs(s.startAzimuthDegrees)<=360.f &&
        std::isfinite(s.revolutionSeconds) && s.revolutionSeconds>=0.5f &&
        s.revolutionSeconds<=120.f &&
        std::isfinite(s.stereoSpreadDegrees) && s.stereoSpreadDegrees>=0.f &&
        s.stereoSpreadDegrees<=180.f &&
        std::isfinite(s.spatialMix) && s.spatialMix>=0.f && s.spatialMix<=1.f &&
        std::isfinite(s.outputTrim) && s.outputTrim>=0.f && s.outputTrim<=1.f;
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
        if(!room_.prepare(rate)||!reverb_.prepare(rate))return false;
        reset();
        ready_=true;
        return true;
    }
    void reset() noexcept {
        for(auto& channel:history_)channel.fill(0.f);
        for(auto& source:kernels_)for(auto& ear:source)ear.fill(0.f);
        for(auto& source:targets_)for(auto& ear:source)ear.fill(0.f);
        smoothedMix_=settings_.spatialMix;
        smoothedTrim_=settings_.outputTrim;
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
        settings_=s;
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
            const float binauralLevel=0.5f*smoothedMix_*smoothedTrim_;
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
            // Exact legacy PCM arithmetic on the 1.6m reference (including
            // all native instances). More distant experimental sources get
            // inverse-distance pressure falloff on the DIRECT binaural field.
            // ER uses matching absolute path pressure; diffuse late field
            // remains an independent environmental stage.
            if(distanceGain==1.f) {
                left[i]=dryLevel*x0+binauralLevel*(ears[0]+reflection.left+late.left);
                right[i]=dryLevel*x1+binauralLevel*(ears[1]+reflection.right+late.right);
            } else {
                left[i]=dryLevel*x0+binauralLevel*
                    (distanceGain*ears[0]+reflection.left+late.left);
                right[i]=dryLevel*x1+binauralLevel*
                    (distanceGain*ears[1]+reflection.right+late.right);
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
    float filterSlew_=0.f,levelSlew_=0.f;
    double sampleRate_=0.;
    double degrees_=0.;
    int write_=0;
    int remaining_=0;
    bool ready_=false;
    bool haveKernel_=false;
};
} // namespace armix::sonku
