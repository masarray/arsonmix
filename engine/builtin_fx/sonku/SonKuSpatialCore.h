// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuMeasuredHrir.h"
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
        reset();
        ready_=true;
        return true;
    }
    void reset() noexcept {
        for(auto& channel:history_)channel.fill(0.f);
        for(auto& source:kernels_)for(auto& ear:source)ear.fill(0.f);
        write_=0;
        remaining_=0;
        degrees_=wrapDegrees(settings_.startAzimuthDegrees);
        haveKernel_=false;
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
    bool ready() const noexcept {return ready_;}
    double azimuthDegrees() const noexcept {return degrees_;}
    // In-place stereo; caller owns valid, non-overlapping L/R PCM pointers.
    // No vector, heap, blocking calls, I/O or filter bank allocation.
    void process(float* left,float* right,int samples) noexcept {
        if(!ready_ || !left || !right || samples<=0)return;
        const double step=(settings_.moving ?
            (settings_.clockwise?1.:-1.)*360./(sampleRate_*settings_.revolutionSeconds) : 0.);
        const float wetLevel=0.5f*settings_.outputTrim;
        const float dryLevel=1.f-settings_.spatialMix;
        const float binauralLevel=settings_.spatialMix*wetLevel;
        for(int i=0;i<samples;++i) {
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
                ears[0]+=v0*kernels_[0][0][tap]+v1*kernels_[1][0][tap];
                ears[1]+=v0*kernels_[0][1][tap]+v1*kernels_[1][1][tap];
            }
            left[i]=dryLevel*x0+binauralLevel*ears[0];
            right[i]=dryLevel*x1+binauralLevel*ears[1];
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
                // Small bounded coefficient smoothing prevents abrupt filter
                // switches on motion/edits. No interpolating filter ownership.
                auto& current=kernels_[source][ear][k];
                current=haveKernel_?(current*0.65f+next*0.35f):next;
            }
        }
    }
    void updateKernels() noexcept {
        interpolate(0,degrees_-static_cast<double>(settings_.stereoSpreadDegrees)*0.5);
        interpolate(1,degrees_+static_cast<double>(settings_.stereoSpreadDegrees)*0.5);
        haveKernel_=true;
    }
    using Bank=const std::int16_t (*)[2][taps];
    Bank bank_=nullptr;
    std::array<std::array<float,taps>,2> history_{};
    std::array<std::array<std::array<float,taps>,2>,2> kernels_{};
    Settings settings_{};
    double sampleRate_=0.;
    double degrees_=0.;
    int write_=0;
    int remaining_=0;
    bool ready_=false;
    bool haveKernel_=false;
};
} // namespace armix::sonku
