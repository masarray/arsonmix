// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

// P1K v1: 48kHz DIRECT output ONLY, 1/2 PCM output channels.
// 48k coefficients below are from ITU-R BS.1770's exact reference tables.
// 400ms overlapping gated windows (100ms hop). Histogram quantization <=0.05 LU.
// 4x windowed-sinc oversampling for intersample peak (not linear sample peak).
//
// No locks, allocations, device/OS/UI calls in process(). All expensive
// preparation occurs before audioDeviceAboutToStart() returns.
// A second renderer/clock is intentionally NOT supported until bound to its
// own output stream and verified device epoch.
namespace armix::dsp {
class NativeMasterLoudness final {
public:
    NativeMasterLoudness() noexcept
    {
        for(int i=0;i<bins;++i)
            binEnergy_[i]=std::pow(10.0,(-70.0+(i+0.5)*0.1+0.691)/10.0);
    }
    struct Reading {
        bool supported=false, integratedValid=false, truePeakValid=false;
        double integratedLufs=0.0, dbtp=0.0;
        std::uint64_t gatedWindows=0;
    };
    static constexpr int taps=32;
    static constexpr int phases=4;
    static constexpr int bins=801;
    static constexpr int quarterFrames=4800; // exact 100ms at 48k

    void prepare (double rate) noexcept
    {
        enabled_.store (false,std::memory_order_release);
        for(auto& band:hist_)band.store(0,std::memory_order_relaxed);
        peak_.store(0.f,std::memory_order_relaxed);
        framesProcessed_.store(0,std::memory_order_relaxed);
        quarterEnergy_.fill(0.0);
        for(auto& ch:history_)ch.fill(0.0f);
        for(auto& f:pre_)f.z1=f.z2=0.0;
        for(auto& f:rlb_)f.z1=f.z2=0.0;
        quarterOffset_=quarterSlot_=completeQuarters_=writeAt_=0;
        if(std::abs(rate-48000.0)>0.5)return; // NEVER invent LUFS at other rates
        constexpr double pi=3.1415926535897932384626433832795;
        for(int phase=0;phase<phases;++phase) {
            double norm=0.0;
            for(int k=0;k<taps;++k) {
                const double t=k-15.0-phase/4.0;
                const double s=std::abs(t)<1e-12?1.0:std::sin(pi*t)/(pi*t);
                const double w=0.42-0.5*std::cos(2*pi*k/(taps-1))
                                   +0.08*std::cos(4*pi*k/(taps-1));
                coeff_[phase][k]=s*w;
                norm+=coeff_[phase][k];
            }
            for(auto& x:coeff_[phase])x/=norm; // unit DC gain
        }
        enabled_.store(true,std::memory_order_release);
    }

    void process(const float* const* output,int numChannels,int frames) noexcept
    {
        if(!enabled_.load(std::memory_order_relaxed)||!output||numChannels<1||frames<=0)
            return;
        const int count=numChannels>=2?2:1;
        float blockPeak=0.f;
        for(int i=0;i<frames;++i) {
            double energy=0.0;
            for(int c=0;c<count;++c) {
                const float* src=output[c];
                const float x=src&&std::isfinite(src[i])?src[i]:0.f;
                double y=pre_[c].step(x);
                y=rlb_[c].step(y);
                energy+=y*y;
                history_[c][writeAt_]=x;
                for(int p=0;p<phases;++p) {
                    double v=0.0;
                    for(int k=0;k<taps;++k)
                        v+=coeff_[p][k]*history_[c][(writeAt_+1+k)&(taps-1)];
                    blockPeak=std::max(blockPeak,static_cast<float>(std::abs(v)));
                }
            }
            writeAt_=(writeAt_+1)&(taps-1);
            quarterEnergy_[quarterSlot_]+=energy;
            if(++quarterOffset_==quarterFrames) {
                quarterOffset_=0;
                ++completeQuarters_;
                quarterSlot_=(quarterSlot_+1)&3;
                if(completeQuarters_>=4) {
                    double window=0.0;
                    for(const double sum:quarterEnergy_) window+=sum;
                    window/=4.0*quarterFrames;
                    if(window>0.0) {
                        const double loudness=-0.691+10.0*std::log10(window);
                        if(loudness>-70.0) {
                            const int idx=static_cast<int>(std::floor((loudness+70.0)*10.0));
                            hist_[idx<0?0:idx>=bins?bins-1:idx].fetch_add(
                                1,std::memory_order_relaxed);
                        }
                    }
                }
                quarterEnergy_[quarterSlot_]=0.0;
            }
        }
        float old=peak_.load(std::memory_order_relaxed);
        while(old<blockPeak&&!peak_.compare_exchange_weak(
            old,blockPeak,std::memory_order_relaxed)){}
        framesProcessed_.fetch_add(static_cast<std::uint64_t>(frames),std::memory_order_relaxed);
    }

    Reading read() const noexcept
    {
        Reading out;
        out.supported=enabled_.load(std::memory_order_acquire);
        if(!out.supported)return out;
        const float pk=peak_.load(std::memory_order_relaxed);
        out.truePeakValid=framesProcessed_.load(std::memory_order_relaxed)>=quarterFrames && pk>0.f;
        if(out.truePeakValid)out.dbtp=20.0*std::log10(static_cast<double>(pk));
        double absoluteSum=0.0, count=0.0;
        for(int i=0;i<bins;++i) {
            const auto n=hist_[i].load(std::memory_order_relaxed);
            absoluteSum+=binEnergy_[i]*n;
            count+=n;
        }
        if(count<1.0)return out;
        const double relative=-0.691+10.0*std::log10(absoluteSum/count)-10.0;
        double gatedEnergy=0.0,gatedCount=0.0;
        for(int i=0;i<bins;++i) {
            const double loudness=-70.0+(i+0.5)*0.1;
            if(loudness<=relative)continue;
            const auto n=hist_[i].load(std::memory_order_relaxed);
            gatedEnergy+=binEnergy_[i]*n;
            gatedCount+=n;
        }
        if(gatedCount>=1.0) {
            out.integratedLufs=-0.691+10.0*std::log10(gatedEnergy/gatedCount);
            out.integratedValid=std::isfinite(out.integratedLufs);
            out.gatedWindows=static_cast<std::uint64_t>(gatedCount);
        }
        if(!enabled_.load(std::memory_order_acquire))return {};
        return out;
    }
private:
    struct Biquad {
        // Transposed DF-II states, double precision, zero allocation.
        double z1=0,z2=0;
        double b0=1,b1=0,b2=0,a1=0,a2=0;
        double step(double x) noexcept {
            const double y=b0*x+z1;
            z1=b1*x-a1*y+z2;
            z2=b2*x-a2*y;
            return y;
        }
    };
    // Exact 48kHz coefficient sets specified in BS.1770 annex 1.
    std::array<Biquad,2> pre_{{
        {0,0,1.53512485958697,-2.69169618940638,1.19839281085285,-1.69065929318241,0.73248077421585},
        {0,0,1.53512485958697,-2.69169618940638,1.19839281085285,-1.69065929318241,0.73248077421585}
    }};
    std::array<Biquad,2> rlb_{{
        {0,0,1.,-2.,1.,-1.99004745483398,0.99007225036621},
        {0,0,1.,-2.,1.,-1.99004745483398,0.99007225036621}
    }};
    std::array<std::array<float,taps>,2> history_{};
    std::array<std::array<double,taps>,phases> coeff_{};
    std::array<double,bins> binEnergy_{};
    std::array<double,4> quarterEnergy_{};
    std::array<std::atomic<std::uint32_t>,bins> hist_{};
    std::atomic<float> peak_{0.f};
    std::atomic<std::uint64_t> framesProcessed_{0};
    std::atomic<bool> enabled_{false};
    int writeAt_=0,quarterOffset_=0,quarterSlot_=0,completeQuarters_=0;
};
} // namespace armix::dsp
