// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace armix::diagnostics {

struct PhysicalLoopback final {
    static constexpr int chips = 383;
    static constexpr int chipFrames = 8;
    static constexpr int pulseFrames = chips * chipFrames;
    static constexpr float amplitude = 0.025f; // -32 dBFS peak; line-level cable only

    enum class State { idle, capturing, complete, cancelled };
    static_assert(std::atomic<State>::is_always_lock_free);
    struct Capture {
        int rate = 0;
        int guard = 0;
        int maxDelay = 0;
        std::array<float, pulseFrames> pulse {};
        std::vector<float> received;
    };
    struct Result {
        int frames = 0;
        double confidence = 0;
        const char* error = nullptr;
    };

    // CONTROL allocates and publishes once. The callback is the sole writer
    // until complete/cancelled is released. No callback borrows a worker buffer.
    bool start(int rate, int block, int input, int output, int inputs, int outputs,
               std::uint64_t deviceEpoch)
    {
        if (state() != State::idle || rate < 44100 || rate > 192000 ||
            block < 16 || block > 2048 || input < 0 || input >= inputs ||
            output < 0 || output >= outputs || deviceEpoch == 0)
            return false;
        capture_.rate = rate;
        capture_.guard = rate / 10;
        capture_.maxDelay = rate / 2;
        capture_.received.resize(static_cast<std::size_t>(
            capture_.guard + capture_.maxDelay + pulseFrames));
        std::uint32_t seed = 0x481b2a63;
        for (int chip = 0; chip < chips; ++chip) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            std::fill_n(capture_.pulse.data() + chip * chipFrames, chipFrames,
                        (seed & 1u) ? amplitude : -amplitude);
        }
        block_ = block;
        input_ = input;
        output_ = output;
        inputs_ = inputs;
        outputs_ = outputs;
        deviceEpoch_ = deviceEpoch;
        cursor_ = 0;
        cancellationRequested_.store(false, std::memory_order_relaxed);
        state_.store(State::capturing, std::memory_order_release);
        return true;
    }

    State state() const noexcept { return state_.load(std::memory_order_acquire); }

    void requestCancel() noexcept
    {
        cancellationRequested_.store(true, std::memory_order_release);
    }

    // Callback or serialized device lifecycle only.
    void interrupted() noexcept
    {
        if (state() == State::capturing)
            state_.store(State::cancelled, std::memory_order_release);
    }

    // Read before graph rendering: some drivers alias input/output buffers.
    bool captureInput(const float* const* inputs, int count, int frames) noexcept
    {
        if (state() != State::capturing)
            return false;
        if (!inputs || count != inputs_ || frames != block_ || !inputs[input_]) {
            interrupted();
            return false;
        }
        const auto remaining = static_cast<int>(capture_.received.size()) - cursor_;
        std::copy_n(inputs[input_], std::min(frames, remaining),
                    capture_.received.data() + cursor_);
        return true;
    }

    // Only true when this block replaced physical output with the isolated
    // probe. No graph/stream mutation, analysis, allocation, or audio rearm.
    bool emitProbe(float* const* outputs,
                 int outputCount, int frames, std::uint64_t epoch, bool permitted,
                 const std::array<int, 2>& mainPair) noexcept
    {
        if (state() != State::capturing)
            return false;
        if (cancellationRequested_.load(std::memory_order_acquire) || !permitted ||
            epoch != deviceEpoch_ || frames != block_ ||
            outputCount != outputs_ || !outputs ||
            !outputs[output_] || (output_ != mainPair[0] && output_ != mainPair[1])) {
            interrupted();
            return false;
        }
        const auto captured = static_cast<int>(capture_.received.size());
        const int n = std::min(frames, captured - cursor_);
        for (int channel = 0; channel < outputCount; ++channel)
            if (auto* out = outputs[channel])
                std::fill_n(out, frames, 0.0f);
        const int begin = std::max(cursor_, capture_.guard);
        const int end = std::min(cursor_ + n, capture_.guard + pulseFrames);
        if (begin < end)
            std::copy_n(capture_.pulse.data() + begin - capture_.guard, end - begin,
                        outputs[output_] + begin - cursor_);
        cursor_ += n;
        if (cursor_ == captured)
            state_.store(State::complete, std::memory_order_release);
        return true;
    }

    // CONTROL consumes only after the callback has stopped touching this capture.
    Capture take()
    {
        Capture result = std::move(capture_);
        state_.store(State::idle, std::memory_order_release);
        return result;
    }

    // Worker only. Centered normalized correlation with a quiet pre-roll,
    // clipping/invalid-sample checks, an ambiguity check and a noise margin.
    static Result analyse(const Capture& capture)
    {
        const auto& received = capture.received;
        double noiseEnergy = 0, noiseSum = 0, pulseSum = 0, pulseEnergy = 0;
        for (std::size_t i = 0; i < received.size(); ++i) {
            const double x = received[i];
            if (!std::isfinite(x) || std::abs(x) >= 0.98)
                return {0, 0, "Clipped or invalid line input. Lower the interface gain."};
            if (i < static_cast<std::size_t>(capture.guard)) {
                noiseEnergy += x*x;
                noiseSum += x;
            }
        }
        for (const double x : capture.pulse) {
            pulseSum += x;
            pulseEnergy += x*x;
        }
        pulseEnergy -= pulseSum*pulseSum / pulseFrames;
        const double noiseVariance = std::max(0.0, noiseEnergy/capture.guard -
            std::pow(noiseSum/capture.guard, 2));
        std::vector<double> prefix(received.size() + 1);
        std::vector<double> squares(received.size() + 1);
        for (std::size_t i = 0; i < received.size(); ++i) {
            const double x = received[i];
            prefix[i + 1] = prefix[i] + x;
            squares[i + 1] = squares[i] + x*x;
        }
        std::vector<double> scores(static_cast<std::size_t>(capture.maxDelay + 1));
        int best = 0;
        for (int offset = 0; offset <= capture.maxDelay; ++offset) {
            const int begin = capture.guard + offset;
            const int end = begin + pulseFrames;
            const double sum = prefix[end] - prefix[begin];
            const double energy = squares[end] - squares[begin] - sum*sum/pulseFrames;
            if (energy < pulseFrames * std::max(0.00000004, 16*noiseVariance))
                continue;
            double dot = 0;
            for (int chip = 0; chip < chips; ++chip) {
                const int index = begin + chip * chipFrames;
                dot += capture.pulse[chip * chipFrames] *
                       (prefix[index + chipFrames] - prefix[index]);
            }
            scores[offset] = std::min(1.0, std::abs(dot - pulseSum*sum/pulseFrames) /
                                           std::sqrt(pulseEnergy*energy));
            if (scores[offset] > scores[best])
                best = offset;
        }
        const double confidence = scores[best];
        if (confidence < 0.70 || best < 1 || best == capture.maxDelay)
            return {0, confidence, "No reliable wired return. Check the cable, line input and noise."};
        for (int offset = 0; offset <= capture.maxDelay; ++offset)
            if (std::abs(offset - best) > chipFrames * 2 && scores[offset] >= confidence * 0.85)
                return {0, confidence, "Multiple strong returns. Disable hardware monitoring or feedback."};
        return {best, confidence, nullptr};
    }

private:
    std::atomic<State> state_ { State::idle };
    std::atomic<bool> cancellationRequested_ { false };
    Capture capture_;
    std::uint64_t deviceEpoch_ = 0;
    int input_ = -1, output_ = -1, inputs_ = 0, outputs_ = 0, block_ = 0, cursor_ = 0;
};
} // namespace armix::diagnostics
