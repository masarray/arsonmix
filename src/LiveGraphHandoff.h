// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>

// P2.5.1 SPSC RT graph handoff. Message thread OWNS all graphs and retires
// only after the audio callback acknowledges a buffer-boundary transition.
// The callback never copies ownership, frees memory, locks, or waits.
namespace armix::rt
{
template <typename Graph>
class LiveGraphHandoff final
{
public:
    struct Cycle { Graph* graph=nullptr; bool fadeOut=false; bool fadeIn=false; };
    void initialize (Graph* graph) noexcept
    {
        active_.store (graph, std::memory_order_release);
        pending_.store (nullptr, std::memory_order_release);
        retired_.store (nullptr, std::memory_order_release);
        phase_.store (0, std::memory_order_release);
        fadeInNext_ = false;
    }
    bool stage (Graph* candidate) noexcept
    {
        if (!candidate) return false;
        int idle = 0;
        if (!phase_.compare_exchange_strong (idle, 1, std::memory_order_acq_rel))
            return false;
        pending_.store (candidate, std::memory_order_release);
        return true;
    }
    bool busy() const noexcept
    {
        return phase_.load (std::memory_order_acquire) != 0;
    }
    Cycle beginCycle() noexcept
    {
        Cycle cycle;
        cycle.graph = active_.load (std::memory_order_acquire);
        cycle.fadeOut = pending_.load (std::memory_order_acquire) != nullptr;
        cycle.fadeIn = fadeInNext_;
        fadeInNext_ = false;
        return cycle;
    }
    void endCycle (const Cycle& cycle) noexcept
    {
        if (!cycle.fadeOut) return;
        // Control thread never cancels a published graph before acknowledgement.
        auto* newGraph = pending_.exchange (nullptr, std::memory_order_acq_rel);
        if (!newGraph) return;
        active_.store (newGraph, std::memory_order_release);
        phase_.store (2, std::memory_order_release);
        retired_.store (cycle.graph, std::memory_order_release);
        fadeInNext_ = true;
    }
    Graph* takeRetired() noexcept
    {
        return retired_.exchange (nullptr, std::memory_order_acq_rel);
    }
    // Called AFTER the message thread safely releases the retired graph.
    void completeRetirement() noexcept
    {
        phase_.store (0, std::memory_order_release);
    }
    Graph* current() const noexcept { return active_.load (std::memory_order_acquire); }
    // Caller must already have synchronously stopped/detached audio callbacks.
    void resetAfterStop (Graph* graph) noexcept { initialize (graph); }
private:
    std::atomic<Graph*> active_ {nullptr};
    std::atomic<Graph*> pending_ {nullptr};
    std::atomic<Graph*> retired_ {nullptr};
    // 0=idle, 1=queued, 2=acknowledged; closes the pending/ack gap.
    std::atomic<int> phase_ {0};
    bool fadeInNext_ = false; // audio thread exclusive
};

inline float linearBufferRamp (int sample, int total, bool fadeOut, bool fadeIn) noexcept
{
    if (total <= 1) return fadeOut ? 0.f : 1.f;
    const auto unit = static_cast<float> (sample) / static_cast<float> (total-1);
    if (fadeOut) return 1.f-unit;
    if (fadeIn) return unit;
    return 1.f;
}
} // namespace armix::rt
