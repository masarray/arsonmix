// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <algorithm>
namespace armix::rt {
// Single audio owner (FxInstance processing gate). Measure sparse actual
// processor invocations, not skipped bypass blocks or whole host callbacks.
// Jitter the interval to reduce aliasing with periodic DSP/guide workloads.
// All control readbacks are deliberately approximate, lifetime-of-lease.
class FxTimingSampler final {
public:
    static constexpr unsigned minimumGap=24,maximumGap=40;
    struct Snapshot {
        std::uint64_t samples=0,frames=0,totalNs=0,maxNs=0,lastNs=0,fullyBypassed=0;
    };
    // CONTROL before lease publication. No reset of live audio-owned data.
    void seed(std::uint32_t value) noexcept {
        rng_=value?value:0x9e3779b9u;
        countdown_=1u+(nextRandom()%maximumGap);
    }
    bool shouldSample() noexcept {
        if(--countdown_!=0)return false;
        countdown_=minimumGap+nextRandom()%(maximumGap-minimumGap+1);
        return true;
    }
    void record(std::uint64_t elapsedNs,unsigned frames,bool fullyBypassed) noexcept {
        // One producer, no CAS retry or RMW contention on the callback.
        last_.store(elapsedNs,std::memory_order_relaxed);
        max_.store(std::max(max_.load(std::memory_order_relaxed),elapsedNs),std::memory_order_relaxed);
        total_.store(total_.load(std::memory_order_relaxed)+elapsedNs,std::memory_order_relaxed);
        frames_.store(frames_.load(std::memory_order_relaxed)+frames,std::memory_order_relaxed);
        if(fullyBypassed)bypassed_.store(bypassed_.load(std::memory_order_relaxed)+1,std::memory_order_relaxed);
        samples_.store(samples_.load(std::memory_order_relaxed)+1,std::memory_order_release);
    }
    Snapshot snapshot()const noexcept {
        Snapshot s;
        s.samples=samples_.load(std::memory_order_acquire);
        s.frames=frames_.load(std::memory_order_relaxed);
        s.totalNs=total_.load(std::memory_order_relaxed);
        s.maxNs=max_.load(std::memory_order_relaxed);
        s.lastNs=last_.load(std::memory_order_relaxed);
        s.fullyBypassed=bypassed_.load(std::memory_order_relaxed);
        return s;
    }
private:
    std::uint32_t nextRandom() noexcept {
        rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;return rng_;
    }
    std::uint32_t rng_=0x9e3779b9u,countdown_=1;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
        "64-bit native audio target requires lock-free timing counters");
    std::atomic<std::uint64_t> samples_{0},frames_{0},total_{0},max_{0},last_{0},bypassed_{0};
};
} // namespace armix::rt
