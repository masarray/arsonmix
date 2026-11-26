// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace armix::rt {
// Vendor-independent evidence, never an automatic bypass/sleep authority.
// One audio writer under the existing FX lease gate. Exact counts published
// in batches, plus immediate publication on a new worst or deadline miss.
// An invocation budget is frames/rate, NOT an actual hardware callback xrun.
class FxWorkloadEvidence final {
public:
    struct Snapshot {
        bool validRate=false;
        std::uint64_t calls=0,overBudget=0,worstPermille=0;
    };
    void prepare(double rate) noexcept {
        validRate_=std::isfinite(rate)&&rate>=8000.&&rate<=384000.;
        nsPerFrame_=validRate_?1.e9/rate:0.;
    }
    void observe(std::uint64_t ns,int frames) noexcept {
        if(!validRate_||frames<=0)return;
        const double budget=nsPerFrame_*frames;
        ++calls_;
        const bool over=static_cast<double>(ns)>budget;
        if(over)++over_;
        // Compare without dividing on the ordinary hot path. Divide only
        // when a new normalized maximum actually occurs.
        const double scaled=static_cast<double>(ns)*1000.;
        bool newWorst=scaled>budget*worst_;
        if(newWorst){
            const double ratio=scaled/budget;
            constexpr auto ceiling=std::numeric_limits<std::uint32_t>::max();
            worst_=ratio>=ceiling?ceiling:static_cast<std::uint32_t>(std::ceil(ratio));
        }
        if(over||newWorst||(calls_&31u)==0){
            publishedWorst_.store(worst_,std::memory_order_relaxed);
            publishedOver_.store(over_,std::memory_order_relaxed);
            publishedCalls_.store(calls_,std::memory_order_release);
        }
    }
    Snapshot snapshot() const noexcept {
        // Approximate lifetime counters; a final quiet batch may lag <=31
        // calls. Never combine this with another lease or device rate.
        Snapshot s;
        s.validRate=validRate_;
        s.calls=publishedCalls_.load(std::memory_order_acquire);
        s.overBudget=publishedOver_.load(std::memory_order_relaxed);
        s.worstPermille=publishedWorst_.load(std::memory_order_relaxed);
        return s;
    }
private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    bool validRate_=false;
    double nsPerFrame_=0.;
    std::uint64_t calls_=0,over_=0;
    std::uint32_t worst_=0;
    std::atomic<std::uint64_t> publishedCalls_{0},publishedOver_{0},publishedWorst_{0};
};
} // namespace armix::rt
