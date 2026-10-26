// SPDX-License-Identifier: AGPL-3.0-only
// Hardware-free, deterministic independent clock simulation. This is an
// inspectable stress/quality/performance baseline, NOT physical hardware FAT.
// No sleeps, thread scheduling, OS audio devices, random_device or fake SRC.
#include "../engine/device/AdaptiveClockBridge.h"
#include "../engine/device/ClockBridgeSizing.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using Bridge = armix::device::AdaptiveClockBridge;
constexpr double pi = 3.1415926535897932384626433832795;
static void require(bool ok, const char* why)
{
    if (!ok) { std::fprintf(stderr, "ASRC SOAK FAIL: %s\n", why); std::abort(); }
}

struct Scenario {
    const char* name;
    double captureHz;
    double renderHz;
    int driftPpm;
    bool inject30msStalls;
};

static void run(const Scenario& sc)
{
    using Clock = std::chrono::steady_clock;
    constexpr int simulatedMilliseconds = 8000;
    constexpr int ringFrames = 32768;
    constexpr double targetMs = 70.;
    const auto sizing = armix::device::ClockBridgeSizing::choose(
        sc.captureHz, sc.renderHz, 480, 512, Bridge::taps, ringFrames, targetMs);
    require(sizing.valid, "negotiated bridge sizing rejected bounded scenario");
    Bridge bridge;
    require(bridge.prepare(sc.captureHz, sc.renderHz, 2, ringFrames, sizing.targetMs),
            "failed to prepare 2-channel polyphase FIR");
    std::array<float, 512> captureL{}, captureR{}, renderL{}, renderR{};
    const float* input[2] {captureL.data(), captureR.data()};
    float* output[2] {renderL.data(), renderR.data()};
    constexpr std::array<int, 5> outputQuanta {128, 480, 64, 256, 512};
    // Timed callback budget observations are informational, NEVER a source
    // for inferred render underruns or a host-independent CI CPU pass/fail.
    std::array<double, 8192> budgetPercent {};
    std::size_t measurements = 0;
    std::uint64_t captureIndex = 0, outputFrames = 0;
    double nextRenderMs = 0., leftPower = 0., rightPower = 0., crossPower = 0.;
    double maxStep = 0.;
    float previous = 0.f;
    bool hasPrevious = false;
    std::uint64_t checkedSamples = 0;
    std::uint64_t steadyPriming = 0, steadyUnderflow = 0;
    std::size_t quantumIndex = 0;
    std::uint32_t seededJitter = 0x315A7289u;
    std::size_t callbackCount = 0;
    const auto begin = Clock::now();

    for (int ms = 0; ms < simulatedMilliseconds; ++ms)
    {
        // Independent capture oscillator: + or -300 ppm relative to render.
        // Periodically block capture delivery, NOT source production, so
        // successful recovery must catch up with exactly the missing frames.
        seededJitter = seededJitter * 1664525u + 1013904223u;
        const bool shortJitter = seededJitter % 257u < 4u;
        const bool burst = sc.inject30msStalls && ms % 1700 >= 405
                            && ms % 1700 < 435;
        if (!shortJitter && !burst)
        {
            const auto due = static_cast<std::uint64_t>(std::floor(
                sc.captureHz * (1. + sc.driftPpm * 1.e-6) * ms / 1000.));
            require(due >= captureIndex, "capture oscillator went backward");
            while (captureIndex < due)
            {
                const auto n = static_cast<int>(std::min<std::uint64_t>(
                    captureL.size(), due - captureIndex));
                for (int i = 0; i < n; ++i)
                {
                    const auto frame = captureIndex + static_cast<std::uint64_t>(i);
                    captureL[static_cast<std::size_t>(i)] = static_cast<float>(
                        .37 * std::sin(2.*pi*997.*static_cast<double>(frame)/sc.captureHz));
                    captureR[static_cast<std::size_t>(i)] = static_cast<float>(
                        .21 * std::sin(2.*pi*1741.*static_cast<double>(frame)/sc.captureHz));
                    // Verify sanitization remains bounded and channel isolated.
                    if (frame == 113)
                        captureL[static_cast<std::size_t>(i)] =
                            std::numeric_limits<float>::quiet_NaN();
                }
                require(bridge.push(input, 2, n) == n,
                        "unexpected producer overflow after recoverable stall");
                captureIndex += static_cast<std::uint64_t>(n);
            }
        }
        while (nextRenderMs <= static_cast<double>(ms))
        {
            const int frames = outputQuanta[quantumIndex++ % outputQuanta.size()];
            const auto start = Clock::now();
            bridge.pull(output, 2, frames);
            const auto end = Clock::now();
            const double callbackMs = 1000. * frames / sc.renderHz;
            nextRenderMs += callbackMs;
            outputFrames += static_cast<std::uint64_t>(frames);
            ++callbackCount;
            require(measurements < budgetPercent.size(), "bounded metric buffer overflow");
            budgetPercent[measurements++] = 100. * std::chrono::duration<double,
                std::milli>(end-start).count() / callbackMs;
            for (int i = 0; i < frames; ++i)
            {
                const float l = renderL[static_cast<std::size_t>(i)];
                const float r = renderR[static_cast<std::size_t>(i)];
                require(std::isfinite(l) && std::isfinite(r), "non-finite output");
                require(std::abs(l) < .65f && std::abs(r) < .4f,
                        "unbounded peak/overshoot");
                if (ms > 1500)
                {
                    leftPower += static_cast<double>(l)*l;
                    rightPower += static_cast<double>(r)*r;
                    crossPower += static_cast<double>(l)*r;
                    if (hasPrevious)
                        maxStep = std::max(maxStep, std::abs(double(l-previous)));
                    previous = l;
                    hasPrevious = true;
                    ++checkedSamples;
                }
            }
        }
        if (ms == 1500)
        {
            const auto state = bridge.status();
            steadyPriming = state.primingFrames;
            steadyUnderflow = state.underflowFrames;
        }
        const auto status = bridge.status();
        require(status.fillFrames <= ringFrames, "FIFO occupancy above capacity");
        require(std::isfinite(status.correctionPpm)
                && std::abs(status.correctionPpm) <= 2000.,
                "PI drift correction outside bounded ppm");
    }

    const auto end = Clock::now();
    const auto st = bridge.status();
    require(st.renderedFrames == outputFrames, "render accounting lost frames");
    require(st.pushedFrames == captureIndex, "capture accounting lost frames");
    require(st.nonFiniteInputSamples == 1, "invalid capture sample not sanitized");
    require(st.overflowFrames == 0, "healthy scheduler overflowed capture ring");
    require(st.primingFrames == steadyPriming
            && st.underflowFrames == steadyUnderflow,
            "healthy drift/jitter after warmup caused missing output PCM");
    require(checkedSamples > 100000, "insufficient steady-state evidence");
    const double lRms = std::sqrt(leftPower / checkedSamples);
    const double rRms = std::sqrt(rightPower / checkedSamples);
    const double crossMean = crossPower / checkedSamples;
    // Independent tones guarantee that left/right channel data have not been
    // duplicated, swapped or corrupted. Avoid fragile exact FFT phase tests.
    require(lRms > .24 && lRms < .29, "left channel fidelity energy outside tolerance");
    require(rRms > .13 && rRms < .17, "right channel fidelity energy outside tolerance");
    require(std::abs(crossMean) < .008, "unexpected left/right crosstalk");
    require(maxStep < .19, "audible-scale render discontinuity under healthy jitter");
    std::sort(budgetPercent.begin(), budgetPercent.begin() + measurements);
    const auto percentile = [&](double p) {
        const auto rank = static_cast<std::size_t>(
            std::floor((measurements-1) * p));
        return budgetPercent[rank];
    };
    const double executionMs = std::chrono::duration<double,std::milli>(end-begin).count();
    std::printf("ASRC_SOAK %s virtual=%.1fs elapsed=%.1fms callbacks=%zu "
                "p95Budget=%.2f%% p99Budget=%.2f%% p999Budget=%.2f%% "
                "leftRms=%.5f rightRms=%.5f maxStep=%.5f "
                "fill=%llu correctionPpm=%.2f underflow=%llu overflow=%llu\n",
                sc.name, simulatedMilliseconds/1000., executionMs,
                callbackCount, percentile(.95), percentile(.99), percentile(.999),
                lRms, rRms, maxStep,
                static_cast<unsigned long long>(st.fillFrames), st.correctionPpm,
                static_cast<unsigned long long>(st.underflowFrames),
                static_cast<unsigned long long>(st.overflowFrames));
}

} // namespace

int main()
{
    for (const auto& scenario : std::array<Scenario,6> {{
        {"44k1_to_48k_plus300",44100.,48000.,+300,false},
        {"48k_to_44k1_minus300",48000.,44100.,-300,false},
        {"48k_to_48k_plus100",48000.,48000.,+100,true},
        {"48k_to_48k_minus100",48000.,48000.,-100,true},
        {"96k_to_48k_minus300",96000.,48000.,-300,true},
        {"48k_to_96k_plus300",48000.,96000.,+300,true}
    }})
        run(scenario);
    return 0;
}
