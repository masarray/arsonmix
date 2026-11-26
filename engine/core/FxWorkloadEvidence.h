// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace armix::rt {
// Vendor-independent evidence, never an automatic bypass/sleep authority.
// One audio writer under the existing FX lease gate. Exact counts published
// in batches, plus immediate publication on a new worst or FIRST deadline miss.
// An invocation budget is frames/rate, NOT an actual hardware callback xrun.
class FxWorkloadEvidence final {
public:
    static constexpr unsigned windowCalls=32;
    struct Window {
        bool valid=false;
        unsigned meanPermille=0,peakPermille=0,overBudget=0;
    };
    struct Snapshot {
        bool validRate=false;
        std::uint64_t calls=0,overBudget=0,worstPermille=0;
        std::uint64_t silenceObserved=0,silentInOut=0;
        Window lastWindow{};
    };
    // CONTROL only, unpublished or device-stopped graph. New evidence epoch.
    void prepare(double rate) noexcept {
        // Device-stop reprepare may overlap a read-only status poll. The
        // callback-local rate stays private; publish validity atomically.
        publishedValidRate_.store(false,std::memory_order_release);
        calls_=over_=silenceObserved_=silentInOut_=0;worst_=0;
        windowCount_=windowOver_=0;windowNs_=windowBudget_=windowPeakNs_=0.;windowPeakBudget_=1.;
        publishedWindow_.store(0,std::memory_order_relaxed);
        publishedCalls_.store(0,std::memory_order_relaxed);
        publishedOver_.store(0,std::memory_order_relaxed);
        publishedWorst_.store(0,std::memory_order_relaxed);
        publishedSilence_.store(0,std::memory_order_relaxed);
        publishedSilentInOut_.store(0,std::memory_order_relaxed);
        validRate_=std::isfinite(rate)&&rate>=8000.&&rate<=384000.;
        nsPerFrame_=validRate_?1.e9/rate:0.;
        publishedValidRate_.store(validRate_,std::memory_order_release);
    }
    void observe(std::uint64_t ns,int frames) noexcept {
        if(!validRate_||frames<=0)return;
        const double budget=nsPerFrame_*frames;
        ++calls_;
        const bool over=static_cast<double>(ns)>budget;
        if(over)++over_;
        // Non-overlapping observation windows, weighted by actual frames:
        // a 16-frame callback is not treated like a 512-frame callback.
        // No clock read, heap, division per ordinary invocation or CAS.
        windowNs_+=static_cast<double>(ns);windowBudget_+=budget;
        if(over)++windowOver_;
        if(static_cast<double>(ns)*windowPeakBudget_>windowPeakNs_*budget){
            windowPeakNs_=static_cast<double>(ns);windowPeakBudget_=budget;
        }
        if(++windowCount_==windowCalls){
            const auto mean=windowRatio(windowNs_,windowBudget_);
            const auto peak=windowRatio(windowPeakNs_,windowPeakBudget_);
            // One atomic publishes the entire interval coherently.
            publishedWindow_.store((std::uint64_t{1}<<63)|
                (static_cast<std::uint64_t>(windowOver_)<<40)|
                (static_cast<std::uint64_t>(peak)<<20)|mean,std::memory_order_release);
            windowCount_=windowOver_=0;windowNs_=windowBudget_=windowPeakNs_=0.;windowPeakBudget_=1.;
        }
        // Compare without dividing on the ordinary hot path. Divide only
        // when a new normalized maximum actually occurs.
        const double scaled=static_cast<double>(ns)*1000.;
        const bool newWorst=worst_!=std::numeric_limits<std::uint32_t>::max()&&scaled>budget*worst_;
        if(newWorst){
            const double ratio=scaled/budget;
            constexpr auto ceiling=std::numeric_limits<std::uint32_t>::max();
            worst_=ratio>=ceiling?ceiling:static_cast<std::uint32_t>(std::ceil(ratio));
        }
        // First miss is visible immediately; sustained overload cannot
        // force a cache-line publication on every single invocation.
        if((over&&over_==1)||newWorst||calls_==1||(calls_&31u)==0){
            publishedWorst_.store(worst_,std::memory_order_relaxed);
            publishedOver_.store(over_,std::memory_order_relaxed);
            publishedCalls_.store(calls_,std::memory_order_release);
        }
    }
    void observeSilence(bool inputAndOutputSilent) noexcept {
        if(!validRate_)return;
        ++silenceObserved_;
        if(inputAndOutputSilent)++silentInOut_;
        publishedSilentInOut_.store(silentInOut_,std::memory_order_relaxed);
        publishedSilence_.store(silenceObserved_,std::memory_order_release);
    }
    Snapshot snapshot() const noexcept {
        // Approximate lifetime counters; a final quiet batch may lag <=31
        // calls. Never combine this with another lease or device rate.
        Snapshot s;
        s.validRate=publishedValidRate_.load(std::memory_order_acquire);
        s.calls=publishedCalls_.load(std::memory_order_acquire);
        s.overBudget=publishedOver_.load(std::memory_order_relaxed);
        s.worstPermille=publishedWorst_.load(std::memory_order_relaxed);
        s.silenceObserved=publishedSilence_.load(std::memory_order_acquire);
        s.silentInOut=publishedSilentInOut_.load(std::memory_order_relaxed);
        const auto packed=publishedWindow_.load(std::memory_order_acquire);
        s.lastWindow={bool(packed>>63),static_cast<unsigned>(packed&0xfffffu),
            static_cast<unsigned>((packed>>20)&0xfffffu),static_cast<unsigned>((packed>>40)&63u)};
        return s;
    }
private:
    static unsigned windowRatio(double ns,double budget) noexcept {
        constexpr unsigned maximum=0xfffffu;
        const double ratio=ns*1000./budget;
        return ratio>=maximum?maximum:static_cast<unsigned>(std::ceil(ratio));
    }
    unsigned windowCount_=0,windowOver_=0;
    double windowNs_=0.,windowBudget_=0.,windowPeakNs_=0.,windowPeakBudget_=1.;
    std::atomic<std::uint64_t> publishedWindow_{0};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    bool validRate_=false; // AUDIO owner, configured while stopped
    std::atomic<bool> publishedValidRate_{false};
    double nsPerFrame_=0.;
    std::uint64_t calls_=0,over_=0,silenceObserved_=0,silentInOut_=0;
    std::uint32_t worst_=0;
    std::atomic<std::uint64_t> publishedCalls_{0},publishedOver_{0},publishedWorst_{0};
    std::atomic<std::uint64_t> publishedSilence_{0},publishedSilentInOut_{0};
};
} // namespace armix::rt
