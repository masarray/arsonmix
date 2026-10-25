// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace armix::device
{
// Bounded event counters for independent source/render streams, independent
// of backend-specific driver xrun semantics. These are bridge frame counts,
// NEVER a substitute for hardware-render underrun evidence.
struct SplitStreamCounters final
{
    std::atomic<std::uint64_t> captureCallbacks {0}, renderCallbacks {0};
    std::atomic<std::uint64_t> captureFrames {0}, renderFrames {0};
    std::atomic<std::uint64_t> captureDropFrames {0}, renderMissingFrames {0};
    std::atomic<std::uint64_t> invalidBufferCallbacks {0};
    std::atomic<std::uint64_t> sourceGeneration {0}, renderGeneration {0};
};
struct SplitStreamSnapshot
{
    std::uint64_t captureCallbacks = 0, renderCallbacks = 0;
    std::uint64_t captureFrames = 0, renderFrames = 0;
    std::uint64_t captureDropFrames = 0, renderMissingFrames = 0;
    std::uint64_t invalidBufferCallbacks = 0;
    std::uint64_t sourceGeneration = 0, renderGeneration = 0;
};
// Control-plane observer for split device adapters. The realtime threads only
// perform bounded atomic increments; snapshots are gathered on UI thread.
class SplitStreamTelemetry final
{
public:
    void sourceStarted() noexcept { counters_.sourceGeneration.fetch_add(1,std::memory_order_relaxed); }
    void renderStarted() noexcept { counters_.renderGeneration.fetch_add(1,std::memory_order_relaxed); }
    void captured (int asked,int accepted) noexcept
    {
        counters_.captureCallbacks.fetch_add(1,std::memory_order_relaxed);
        if(asked<0 || accepted<0 || accepted>asked)
        {
            counters_.invalidBufferCallbacks.fetch_add(1,std::memory_order_relaxed);
            return;
        }
        counters_.captureFrames.fetch_add(static_cast<std::uint64_t>(accepted),std::memory_order_relaxed);
        counters_.captureDropFrames.fetch_add(static_cast<std::uint64_t>(asked-accepted),std::memory_order_relaxed);
    }
    void rendered(int asked,int missing) noexcept
    {
        counters_.renderCallbacks.fetch_add(1,std::memory_order_relaxed);
        if(asked<0 || missing<0 || missing>asked)
        {
            counters_.invalidBufferCallbacks.fetch_add(1,std::memory_order_relaxed);
            return;
        }
        counters_.renderFrames.fetch_add(static_cast<std::uint64_t>(asked),std::memory_order_relaxed);
        counters_.renderMissingFrames.fetch_add(static_cast<std::uint64_t>(missing),std::memory_order_relaxed);
    }
    SplitStreamSnapshot snapshot() const noexcept
    {
        return {counters_.captureCallbacks.load(std::memory_order_relaxed),
                counters_.renderCallbacks.load(std::memory_order_relaxed),
                counters_.captureFrames.load(std::memory_order_relaxed),
                counters_.renderFrames.load(std::memory_order_relaxed),
                counters_.captureDropFrames.load(std::memory_order_relaxed),
                counters_.renderMissingFrames.load(std::memory_order_relaxed),
                counters_.invalidBufferCallbacks.load(std::memory_order_relaxed),
                counters_.sourceGeneration.load(std::memory_order_relaxed),
                counters_.renderGeneration.load(std::memory_order_relaxed)};
    }
private:
    SplitStreamCounters counters_;
};
} // namespace armix::device
