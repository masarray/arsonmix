// Broad measurable audio invariants. These are NOT subjective listening tests.
#include <arverb/Reverb.hpp>
#include "../src/dsp/Primitives.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace {
void require(bool ok, const char* reason) {
    if (!ok) { std::fprintf(stderr, "AUDIO REGRESSION FAILURE: %s\n", reason); std::abort(); }
}
struct Window {
    double left = 0, right = 0, cross = 0;
    void add(float l, float r) {
        require(std::isfinite(l) && std::isfinite(r), "finite output");
        left += static_cast<double>(l) * l;
        right += static_cast<double>(r) * r;
        cross += static_cast<double>(l) * r;
    }
};
}
int main() {
    constexpr int fs = 48000;
    constexpr int n = 256;
    constexpr int seconds = 5;
    std::array<float, n> inL{}, inR{}, outL{}, outR{};
    for (const auto preset : {arverb::Preset::VocalPlate, arverb::Preset::VocalChamber,
                              arverb::Preset::MusicHall, arverb::Preset::AiryBallad}) {
        arverb::Engine engine;
        engine.loadPreset(preset);
        engine.setParameter(arverb::Parameter::Mix, 1.0f);
        engine.prepare(fs);
        std::array<Window, seconds> windows{};
        double earlyEnergy = 0.0;
        for (int frame = 0; frame < fs * seconds; frame += n) {
            inL.fill(0); inR.fill(0);
            if (frame == 0) inL[0] = 1.0f;
            const auto count = static_cast<std::size_t>(std::min(n, fs * seconds - frame));
            engine.process(inL.data(), inR.data(), outL.data(), outR.data(), count);
            for (std::size_t i = 0; i < count; ++i) {
                const int pos = frame + static_cast<int>(i);
                windows[static_cast<std::size_t>(pos / fs)].add(outL[i], outR[i]);
                if (pos < fs / 100) earlyEnergy += outL[i] * outL[i] + outR[i] * outR[i];
            }
        }
        require(earlyEnergy < 1e-15, "wet-only predelay should suppress first 10 ms");
        require(windows[0].left > 1e-7 && windows[0].right > 1e-7,
                "wet stereo signal exists in first second");
        require(windows[1].left + windows[1].right > 1e-10, "reverb tail beyond one second");
        // Reject physically implausible exponential growth/feedback runaway.
        require(windows[4].left + windows[4].right <
                4.0 * (windows[0].left + windows[0].right), "feedback instability");
        const double e0 = windows[0].left + windows[0].right;
        const double e4 = windows[4].left + windows[4].right;
        std::printf("preset=%u total_energy_0to1s=%.8g energy_4to5s=%.8g\n",
                    static_cast<unsigned>(preset), e0, e4);
    }
    // A damaged host sample must not poison the recursive network permanently.
    arverb::Engine robust;
    robust.setParameter(arverb::Parameter::Mix, 1.0f);
    robust.prepare(fs);
    inL.fill(0); inR.fill(0);
    inL[0] = std::numeric_limits<float>::max();
    inR[1] = std::numeric_limits<float>::infinity();
    inL[2] = std::numeric_limits<float>::quiet_NaN();
    for (int block = 0; block < 300; ++block) {
        robust.process(inL.data(), inR.data(), outL.data(), outR.data(), n);
        for (std::size_t i = 0; i < n; ++i)
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]), "host corruption recovery");
        inL.fill(0); inR.fill(0);
    }

    // Deterministic frequency-selective Duck regression independent of the
    // FDN topology: wet presence must recede more than low-frequency energy.
    // An identical stereo excitation must preserve stereo symmetry.
    const auto shaperGain = [](float hz, float amount) {
        arverb::detail::PresenceDucker shaper;
        shaper.prepare(48000.0f);
        double inputSq = 0.0, wetSq = 0.0;
        for (int i = 0; i < 48000; ++i) {
            const float signal = 0.35f * std::sin(
                6.283185307179586f * hz * static_cast<float>(i) / 48000.0f);
            float wetL = signal, wetR = signal;
            shaper.process(signal, signal, wetL, wetR, amount);
            require(std::isfinite(wetL) && std::isfinite(wetR)
                    && std::abs(wetL - wetR) < 1e-6f,
                    "presence-shaper must be finite, stereo-linked");
            if (amount == 0.0f)
                require(wetL == signal, "zero Duck leaves wet signal unchanged");
            if (i >= 24000) {
                inputSq += static_cast<double>(signal) * signal;
                wetSq += static_cast<double>(wetL) * wetL;
            }
        }
        return std::sqrt(wetSq / inputSq);
    };
    const double presenceGain = shaperGain(2500.0f, 1.0f);
    const double lowGain = shaperGain(230.0f, 1.0f);
    require(presenceGain < lowGain - 0.07 && lowGain > 0.83,
            "voice presence should be attenuated more than low band");
    require(std::abs(shaperGain(2500.0f, 0.0f) - 1.0) < 1e-10,
            "zero Duck must be exactly spectrally transparent");
    std::printf("presence shaping gains 2.5k=%.3f, 230Hz=%.3f\\n",
                presenceGain, lowGain);
    std::puts("PASS: impulse onset, wet stereo, decay stability, finite hostile input, selective presence duck");
}
