#include "arverb/Reverb.hpp"
#include "arverb/arverb_c.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

// Test-only allocation interception: deliberately fails the hot-path contract if
// Engine::process() ever gains a dynamic allocation in a future refactor.
static std::atomic<unsigned> gAllocations{0};
void* operator new(std::size_t n) {
    gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static void require(bool ok, const char* reason) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", reason); std::abort(); }
}

int main() {
    using namespace arverb;
    constexpr int fs = 48000;
    constexpr std::size_t frames = fs * 3;
    std::vector<float> left(frames, 0.0f), right(frames, 0.0f);
    left[0] = 0.9f;
    std::vector<float> outL(frames), outR(frames), partL(frames), partR(frames);

    // Identical output regardless of audio block segmentation (fixed parameters).
    Engine full, split;
    full.setParameter(Parameter::Mix, 1.0f);
    split.setParameter(Parameter::Mix, 1.0f);
    full.prepare(fs);
    split.prepare(fs);
    full.process(left.data(), right.data(), outL.data(), outR.data(), frames);
    std::size_t processed = 0;
    while (processed < frames) {
        std::size_t n = std::min<std::size_t>(137, frames - processed);
        split.process(left.data() + processed, right.data() + processed,
                      partL.data() + processed, partR.data() + processed, n);
        processed += n;
    }
    double energyL = 0.0, energyR = 0.0, maxDiff = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        require(std::isfinite(outL[i]) && std::isfinite(outR[i]), "IR finite");
        maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(partL[i] - outL[i])));
        maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(partR[i] - outR[i])));
        energyL += outL[i] * outL[i];
        energyR += outR[i] * outR[i];
    }
    require(maxDiff < 1e-6, "block partition invariance");
    require(energyL > 1e-6 && energyR > 1e-6, "cross-stereo wet tail present");

    // Silence after reset, including oscillator/diffusion/feedback history.
    full.reset();
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    full.process(left.data(), right.data(), outL.data(), outR.data(), frames);
    for (std::size_t i = 0; i < frames; ++i)
        require(std::fabs(outL[i]) + std::fabs(outR[i]) < 1e-12f, "reset removes tail");

    // In-place processing must match out-of-place processing.
    Engine a, b;
    a.prepare(fs); b.prepare(fs);
    constexpr std::size_t n = 2048;
    left.resize(n); right.resize(n);
    outL.resize(n); outR.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = 0.2f * std::sin(2.0f * 3.14159265f * 220.0f * i / fs);
        right[i] = 0.2f * std::sin(2.0f * 3.14159265f * 293.66f * i / fs);
    }
    a.process(left.data(), right.data(), outL.data(), outR.data(), n);
    b.process(left.data(), right.data(), left.data(), right.data(), n);
    for (std::size_t i = 0; i < n; ++i) {
        require(std::fabs(left[i] - outL[i]) < 1e-6f, "inplace L");
        require(std::fabs(right[i] - outR[i]) < 1e-6f, "inplace R");
    }

    // Parameter bounds and sample-rate support.
    Engine rate;
    rate.setParameter(Parameter::DecaySeconds, 1000.f);
    require(rate.getParameter(Parameter::DecaySeconds) == 12.0f, "parameter upper bound");
    for (double sampleRate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        rate.prepare(sampleRate);
        rate.loadPreset(Preset::AiryBallad);
        std::fill(left.begin(), left.end(), 0.0f);
        left[0] = 0.5f;
        rate.process(left.data(), right.data(), outL.data(), outR.data(), n);
        for (std::size_t i = 0; i < n; ++i)
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]), "finite sample-rate sweep");
    }

    // No allocations in process, including tiny (one-frame) host callbacks.
    const unsigned allocationsBefore = gAllocations.load(std::memory_order_relaxed);
    rate.process(left.data(), right.data(), outL.data(), outR.data(), n);
    for (std::size_t i = 0; i < n; ++i)
        rate.process(left.data() + i, right.data() + i, outL.data() + i, outR.data() + i, 1);
    require(gAllocations.load(std::memory_order_relaxed) == allocationsBefore,
            "no audio-thread heap allocation");

    // Changing parameters repeatedly must not produce non-finite output.
    rate.prepare(48000);
    for (int block = 0; block < 80; ++block) {
        rate.setParameter(Parameter::DecaySeconds, block % 2 ? 0.15f : 12.0f);
        rate.setParameter(Parameter::Size, block % 2 ? 0.65f : 2.0f);
        rate.setParameter(Parameter::ModDepthMs, block % 2 ? 0.0f : 1.2f);
        rate.setParameter(Parameter::LowCutHz, block % 2 ? 30.0f : 1200.0f);
        rate.process(left.data(), right.data(), outL.data(), outR.data(), n);
        for (std::size_t i = 0; i < n; ++i)
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]), "finite under parameter extremes");
    }

    // C ABI works without requiring plugin SDK or C++ app host.
    auto* h = arverb_create();
    require(h != nullptr, "C ABI create");
    require(arverb_prepare(h, 48000.0) == 1, "C ABI prepare");
    require(arverb_load_preset(h, ARVERB_VOCAL_PLATE) == 1, "C ABI preset");
    require(arverb_set_parameter(h, ARVERB_MIX, 1.0f) == 1, "C ABI parameter");
    require(arverb_set_parameter(h, 9999, 1.0f) == 0, "C ABI invalid parameter");
    require(arverb_set_parameter(h, ARVERB_MIX, std::nanf("")) == 0, "C ABI rejects NaN parameters");
    arverb_process(h, left.data(), right.data(), outL.data(), outR.data(), n);
    arverb_destroy(h);

    std::printf("PASS: stereo, finite, reset, 8-192kHz, C ABI, inplace, partition; maxDiff=%.9g, wetR_energy=%g\n", maxDiff, energyR);
    return 0;
}
