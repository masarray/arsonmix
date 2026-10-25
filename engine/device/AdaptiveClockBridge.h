// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace armix::device
{
// Streaming, bounded, single producer/single consumer capture -> render bridge.
// The render clock owns the output timeline; source clock can be independent.
// Prepare/reset are CONTROL-THREAD operations with both streams STOPPED.
// push()/pull() never allocate, lock, log, wait or call JUCE.
// Not yet wired to JUCE joined WASAPI callback: true independent device
// adapter is required to make this useful on unrelated physical clocks.
class AdaptiveClockBridge final
{
public:
    static constexpr int taps = 48;
    static constexpr int phases = 1024;
    struct Status
    {
        std::uint64_t pushedFrames = 0, renderedFrames = 0;
        std::uint64_t overflowFrames = 0, underflowFrames = 0;
        std::uint64_t primingFrames = 0;
        std::uint64_t nonFiniteInputSamples = 0;
        std::uint64_t fillFrames = 0;
        double correctionPpm = 0, nominalInputPerOutput = 0;
    };

    bool prepare (double captureRate, double renderRate, int numChannels,
                  int bufferCapacityFrames = 32768, double targetLatencyMs = 50.)
    {
        if (!std::isfinite (captureRate) || !std::isfinite (renderRate)
            || captureRate < 8000 || renderRate < 8000
            || captureRate > 384000 || renderRate > 384000
            || numChannels < 1 || numChannels > 16
            || bufferCapacityFrames < 256 || bufferCapacityFrames > (1 << 18)
            || (bufferCapacityFrames & (bufferCapacityFrames - 1)) != 0
            || !std::isfinite (targetLatencyMs)
            || targetLatencyMs < 5. || targetLatencyMs > 200.) return false;

        channels_ = numChannels;
        mask_ = static_cast<std::uint64_t> (bufferCapacityFrames - 1);
        capacity_ = bufferCapacityFrames;
        inputPerOutput_ = captureRate / renderRate;
        renderRate_ = renderRate;
        targetFill_ = std::clamp (static_cast<std::uint64_t> (captureRate * targetLatencyMs / 1000.),
                                  static_cast<std::uint64_t> (taps * 2),
                                  static_cast<std::uint64_t> (capacity_ / 2));
        if (targetFill_ + taps * 2 >= static_cast<std::uint64_t> (capacity_)) return false;

        // Band-limited polyphase windowed-sinc FIR, normalized to DC unity.
        // Anti-alias cutoff leaves margin for bounded ppm rate corrections.
        const double cutoff = 0.94 * std::min (1., 1. / inputPerOutput_);
        const double pi = 3.1415926535897932384626433832795;
        coefficients_.resize (static_cast<std::size_t> (phases * taps));
        for (int p = 0; p < phases; ++p)
        {
            const double frac = static_cast<double> (p) / phases;
            double sum = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                const double t = static_cast<double> (k - (taps / 2 - 1)) - frac;
                const double x = pi * t * cutoff;
                const double sinc = std::abs (x) < 1e-10 ? cutoff
                    : cutoff * std::sin (x) / x;
                // Blackman window; expensive trig only in non-RT prepare().
                const double w = 0.42 - 0.5 * std::cos (2. * pi * k / (taps - 1))
                                       + 0.08 * std::cos (4. * pi * k / (taps - 1));
                const float c = static_cast<float> (sinc * w);
                coefficients_[static_cast<std::size_t> (p * taps + k)] = c;
                sum += c;
            }
            if (!(sum > 0.0)) return false;
            for (int k = 0; k < taps; ++k)
                coefficients_[static_cast<std::size_t> (p * taps + k)]
                    /= static_cast<float> (sum);
        }
        ring_.assign (static_cast<std::size_t> (capacity_) * channels_, 0.f);
        resetStopped();
        return true;
    }

    void resetStopped() noexcept
    {
        write_.store (0, std::memory_order_relaxed);
        read_.store (0, std::memory_order_relaxed);
        lostInput_.store (0, std::memory_order_relaxed);
        missingOutput_.store (0, std::memory_order_relaxed);
        primeOutput_.store (0, std::memory_order_relaxed);
        nonFiniteInput_.store (0, std::memory_order_relaxed);
        outputCount_.store (0, std::memory_order_relaxed);
        correctionSnapshot_.store (0., std::memory_order_relaxed);
        cursor_ = 0.; integratorPpm_ = 0.; correctionPpm_ = 0.;
        primed_ = false;
        gain_ = 0.;
        for (auto& x : previous_) x = 0.f;
    }

    // SPSC producer. Never overwrite unconsumed frames: overflow drops newest
    // producer frames and counts the exact number lost.
    int push (const float* const* capture, int sourceChannels, int frames) noexcept
    {
        if (capacity_ == 0 || frames <= 0 || sourceChannels < 1 || capture == nullptr) return 0;
        auto w = write_.load (std::memory_order_relaxed);
        const auto r = read_.load (std::memory_order_acquire);
        const auto used = std::min<std::uint64_t> (w - r, capacity_);
        const int accepted = std::min (frames, capacity_ - static_cast<int> (used));
        std::uint64_t sanitized = 0;
        for (int i = 0; i < accepted; ++i)
        {
            const auto base = static_cast<std::size_t> (((w + i) & mask_) * channels_);
            for (int c = 0; c < channels_; ++c)
            {
                const float* channel = capture[std::min (c, sourceChannels - 1)];
                // Sanitize only at the capture ingress boundary. Without it,
                // one invalid virtual-driver sample poisons FIR and VST3 DSP.
                const float value = channel != nullptr ? channel[i] : 0.f;
                const bool finite = std::isfinite (value);
                ring_[base + c] = finite ? value : 0.f;
                sanitized += static_cast<std::uint64_t> (!finite);
            }
        }
        write_.store (w + static_cast<std::uint64_t> (accepted), std::memory_order_release);
        if (sanitized != 0) // one atomic increment per anomalous block
            nonFiniteInput_.fetch_add (sanitized, std::memory_order_relaxed);
        if (accepted < frames)
            lostInput_.fetch_add (static_cast<std::uint64_t> (frames - accepted), std::memory_order_relaxed);
        return accepted;
    }

    // Render-clock consumer, sample-accurate bounded streaming polyphase FIR.
    void pull (float* const* render, int outputChannels, int frames) noexcept
    {
        if (frames <= 0 || render == nullptr || outputChannels < 1) return;
        if (capacity_ == 0)
        {
            for (int c = 0; c < outputChannels; ++c)
                if (render[c]) std::fill_n (render[c], frames, 0.f);
            return;
        }
        const auto w = write_.load (std::memory_order_acquire);
        auto r = read_.load (std::memory_order_relaxed);
        const auto fill = w - r;
        if (!primed_ && fill >= targetFill_ + taps)
        {
            // Prime near target occupancy; after starvation never replay
            // a large stale segment of the input FIFO.
            const auto freshStart = w > targetFill_
                ? w - targetFill_ + taps / 2 : r + taps / 2;
            cursor_ = static_cast<double> (std::max (r + taps / 2, freshStart));
            primed_ = true;
            gain_ = 0.;
            integratorPpm_ = 0.;
        }
        if (primed_)
        {
            const double error = static_cast<double> (fill) - static_cast<double> (targetFill_);
            // PI loop: 0.4 ppm/frame proportional + 1.5 ppm/frame/sec integral.
            // These initial limits are lab parameters, not hardware-calibrated.
            const double seconds = static_cast<double> (frames) / renderRate_;
            const double proposal = integratorPpm_ + error * seconds * 1.5;
            const double proportional = error * 0.4;
            const double proposed = proportional + proposal;
            correctionPpm_ = std::clamp (proposed, -2000., 2000.);
            if (proposed == correctionPpm_) integratorPpm_ = proposal; // anti-windup
            correctionSnapshot_.store (correctionPpm_, std::memory_order_relaxed);
        }
        const double step = inputPerOutput_ * (1. + correctionPpm_ * 1e-6);
        std::uint64_t missing = 0, priming = 0;
        for (int i = 0; i < frames; ++i)
        {
            const auto base = static_cast<std::uint64_t> (cursor_);
            const bool have = primed_ && base >= static_cast<std::uint64_t> (taps / 2)
                            && base + taps / 2 + 2 < w;
            if (!have)
            {
                if (primed_) ++missing;
                else ++priming;
            }
            if (have) gain_ = std::min (1., gain_ + 1. / 64.);
            else gain_ = std::max (0., gain_ - 1. / 64.);
            const int phase = have ? std::min (phases - 1,
                static_cast<int> ((cursor_ - static_cast<double> (base)) * phases)) : 0;
            const float* kernel = coefficients_.data() + phase * taps;
            for (int c = 0; c < outputChannels; ++c)
            {
                if (render[c] == nullptr) continue;
                const int sourceC = std::min (c, channels_ - 1);
                float sample = 0.f;
                if (have)
                {
                    const std::uint64_t first = base - static_cast<std::uint64_t> (taps / 2 - 1);
                    double acc = 0.;
                    for (int k = 0; k < taps; ++k)
                    {
                        const auto idx = static_cast<std::size_t> (((first + k) & mask_) * channels_ + sourceC);
                        acc += static_cast<double> (ring_[idx]) * kernel[k];
                    }
                    sample = static_cast<float> (acc);
                    // Smooth start; 64-sample ramp prevents hard jumps on prime.
                    sample *= static_cast<float> (gain_);
                    previous_[static_cast<std::size_t> (sourceC)] = sample;
                }
                else
                {
                    sample = previous_[static_cast<std::size_t> (sourceC)] * static_cast<float> (gain_);
                }
                render[c][i] = sample;
            }
            if (have) cursor_ += step;
            else if (primed_)
            {
                // On starvation re-prime deliberately. Do not endlessly read stale
                // memory, invent source samples or let a PI integral wind up.
                primed_ = false; integratorPpm_ = 0.;
            }
        }
        // Publish consumed frames even if a block starves half-way through.
        // Otherwise a stale read cursor can permanently fill producer FIFO.
        {
            const auto retire = static_cast<std::uint64_t> (cursor_) > taps
                    ? static_cast<std::uint64_t> (cursor_) - taps : 0;
            r = std::max (r, std::min (retire, w));
            read_.store (r, std::memory_order_release);
        }
        if (missing) missingOutput_.fetch_add (missing, std::memory_order_relaxed);
        if (priming) primeOutput_.fetch_add (priming, std::memory_order_relaxed);
        outputCount_.fetch_add (static_cast<std::uint64_t> (frames), std::memory_order_relaxed);
    }

    Status status() const noexcept
    {
        Status s;
        const auto w = write_.load (std::memory_order_acquire);
        const auto r = read_.load (std::memory_order_acquire);
        s.pushedFrames = w;
        s.renderedFrames = outputCount_.load (std::memory_order_relaxed);
        s.overflowFrames = lostInput_.load (std::memory_order_relaxed);
        s.underflowFrames = missingOutput_.load (std::memory_order_relaxed);
        s.primingFrames = primeOutput_.load (std::memory_order_relaxed);
        s.nonFiniteInputSamples = nonFiniteInput_.load (std::memory_order_relaxed);
        // UI snapshots may read write/read cursors from different instants.
        // Never report an enormous wrapped fill on a harmless cross-read race.
        s.fillFrames = w >= r ? w - r : 0;
        s.nominalInputPerOutput = inputPerOutput_;
        s.correctionPpm = correctionSnapshot_.load (std::memory_order_relaxed);
        return s;
    }

private:
    int channels_ = 0, capacity_ = 0;
    std::uint64_t mask_ = 0, targetFill_ = 0;
    double inputPerOutput_ = 1., renderRate_ = 48000.;
    double cursor_ = 0., correctionPpm_ = 0., integratorPpm_ = 0., gain_ = 0.;
    bool primed_ = false;
    std::vector<float> ring_, coefficients_; // allocated ONLY in prepare()
    std::array<float, 16> previous_ {};
    alignas(64) std::atomic<std::uint64_t> write_ {0};
    alignas(64) std::atomic<std::uint64_t> read_ {0};
    std::atomic<std::uint64_t> lostInput_ {0}, missingOutput_ {0}, primeOutput_ {0}, outputCount_ {0};
    std::atomic<std::uint64_t> nonFiniteInput_ {0};
    std::atomic<double> correctionSnapshot_ {0.};
};
} // namespace armix::device
