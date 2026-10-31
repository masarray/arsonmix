// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace armix::rt {
// Capture-only evidence, never DSP repair. Some WASAPI backends may prepend
// exact zero samples after an input reservoir shortage; legitimate digital
// silence is possible too. These counters are CANDIDATES, not proven xruns.
// One callback writer; control/diagnostics read atomics. No RT allocations,
// logging, locks, device queries or independent full-buffer second pass.
class CaptureBoundaryEvidence final {
public:
    static constexpr int minZeroHeadFrames = 32;
    static constexpr float tailSignalThreshold = 0.0005f;
    static constexpr int maxObservedPorts = 64; // fixed memory, JUCE callback-ordered active inputs

    void reset() noexcept {
        previousTailActive_ = false;
        candidateBlocks_.store(0, std::memory_order_relaxed);
        candidateFrames_.store(0, std::memory_order_relaxed);
        nonFiniteSamples_.store(0, std::memory_order_relaxed);
        for (auto& p : perPortPeak_) p.store(0.f, std::memory_order_relaxed);
        for (auto& p : perPortSignalBlocks_) p.store(0, std::memory_order_relaxed);
    }

    float observe(const float* const* input, int channels, int frames) noexcept {
        if (!input || channels <= 0 || frames <= 0) {
            previousTailActive_ = false;
            return 0.f;
        }
        channels = std::min(channels, maxObservedPorts);
        float peak = 0.f;
        bool tailActive = false;
        int leadingAllZero = frames;
        int observed = 0;
        std::uint64_t nonFinite = 0;
        // Channel-major scan reuses the existing pre-MUTE peak measurement.
        for (int c = 0; c < channels; ++c) {
            const auto* samples = input[c];
            if (!samples) {
                perPortPeak_[static_cast<std::size_t>(c)].store(0.f, std::memory_order_relaxed);
                continue;
            }
            ++observed;
            float channelPeak = 0.f;
            int leading = frames;
            for (int i = 0; i < frames; ++i) {
                const auto v = samples[i];
                if (!std::isfinite(v)) { ++nonFinite; continue; }
                const auto mag = std::abs(v);
                peak = std::max(peak, mag);
                channelPeak = std::max(channelPeak, mag);
                if (v != 0.f && leading == frames) leading = i;
                if (i >= frames - 16 && mag > tailSignalThreshold) tailActive = true;
            }
            const auto port = static_cast<std::size_t>(c);
            perPortPeak_[port].store(channelPeak, std::memory_order_relaxed);
            if (channelPeak > tailSignalThreshold)
                perPortSignalBlocks_[port].fetch_add(1, std::memory_order_relaxed);
            leadingAllZero = std::min(leadingAllZero, leading);
        }
        if (nonFinite)
            nonFiniteSamples_.fetch_add(nonFinite, std::memory_order_relaxed);
        if (observed && previousTailActive_ &&
            leadingAllZero >= minZeroHeadFrames) {
            candidateBlocks_.fetch_add(1, std::memory_order_relaxed);
            candidateFrames_.fetch_add(static_cast<std::uint64_t>(leadingAllZero),
                                       std::memory_order_relaxed);
        }
        previousTailActive_ = observed > 0 && tailActive;
        return peak;
    }
    // One atomic read per visible hardware pin at GUI telemetry rate. These
    // indices are JUCE callback order, NOT guessed ASIO driver pin names;
    // only the matching epoch's active input mask maps them to labels.
    float portPeak(int callbackPort) const noexcept {
        return callbackPort >= 0 && callbackPort < maxObservedPorts
            ? perPortPeak_[static_cast<std::size_t>(callbackPort)].load(std::memory_order_relaxed)
            : 0.f;
    }
    std::uint64_t portSignalBlocks(int callbackPort) const noexcept {
        return callbackPort >= 0 && callbackPort < maxObservedPorts
            ? perPortSignalBlocks_[static_cast<std::size_t>(callbackPort)].load(std::memory_order_relaxed)
            : 0;
    }
    std::uint64_t candidateBlocks() const noexcept {
        return candidateBlocks_.load(std::memory_order_relaxed);
    }
    std::uint64_t candidateFrames() const noexcept {
        return candidateFrames_.load(std::memory_order_relaxed);
    }
    std::uint64_t nonFiniteSamples() const noexcept {
        return nonFiniteSamples_.load(std::memory_order_relaxed);
    }
    static constexpr const char* provenance() noexcept {
        return "EXACT_ZERO_CAPTURE_BLOCK_HEAD_AFTER_NONZERO_TAIL_HEURISTIC_NOT_DRIVER_XRUN";
    }
private:
    bool previousTailActive_ = false; // only callback owns this state
    std::atomic<std::uint64_t> candidateBlocks_{0}, candidateFrames_{0};
    std::atomic<std::uint64_t> nonFiniteSamples_{0};
    std::array<std::atomic<float>,maxObservedPorts> perPortPeak_{};
    std::array<std::atomic<std::uint64_t>,maxObservedPorts> perPortSignalBlocks_{};
};
}
