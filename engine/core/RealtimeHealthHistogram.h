// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace armix::rt
{
// Approximate callback budget distribution. Exactly one audio producer.
// Relaxed atomics permit a nonblocking, slightly non-transactional UI snapshot.
// Bucket upper bounds are fractions of the negotiated callback deadline.
class RealtimeHealthHistogram final
{
public:
    static constexpr std::array<double, 12> bounds {
        0.01,0.02,0.04,0.08,0.12,0.20,0.30,0.40,0.55,0.70,0.85,1.0
    };
    struct Snapshot
    {
        std::array<std::uint64_t,13> counts {};
        std::uint64_t total = 0;
        double p95Ceiling = 0.0, p99Ceiling = 0.0;
    };
    void observe (std::uint64_t elapsedMicroseconds, double periodMicroseconds) noexcept
    {
        if (!(periodMicroseconds > 0.0) || !std::isfinite (periodMicroseconds)) return;
        const double fraction = elapsedMicroseconds / periodMicroseconds;
        unsigned i = 0;
        for (; i < bounds.size() && fraction > bounds[i]; ++i) {}
        counts_[i].fetch_add (1, std::memory_order_relaxed);
    }
    Snapshot snapshot() const noexcept
    {
        Snapshot v;
        for (unsigned i = 0; i < v.counts.size(); ++i)
        {
            v.counts[i] = counts_[i].load (std::memory_order_relaxed);
            v.total += v.counts[i];
        }
        auto percentile = [&](std::uint64_t numerator) noexcept {
            if (!v.total) return 0.0;
            const auto target = (v.total * numerator + 99) / 100;
            std::uint64_t acc = 0;
            for (unsigned i = 0; i < v.counts.size(); ++i)
            {
                acc += v.counts[i];
                if (acc >= target) return i < bounds.size() ? bounds[i] : -1.0; // sentinel: unbounded > 100%
            }
            return -1.0;
        };
        v.p95Ceiling = percentile(95);
        v.p99Ceiling = percentile(99);
        return v;
    }
private:
    std::array<std::atomic<std::uint64_t>,13> counts_ {};
};
} // namespace armix::rt
