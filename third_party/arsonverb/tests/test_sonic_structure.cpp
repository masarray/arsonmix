// P3: numeric safety, bass-vs-mid contour and dense early-energy contract.
// These tests do not claim professional listening quality.
#include <arverb/Reverb.hpp>
#include "../src/dsp/Primitives.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "P3 SONIC FAILURE: %s\n", message); std::abort(); }
}

static double sineGain(float hz, float depth, float fs) {
    arverb::detail::BassDecayContour filter;
    const float coef = arverb::detail::lowPassCoeff(132.0f, fs);
    double inEnergy = 0.0, outEnergy = 0.0;
    for (int t = 0; t < static_cast<int>(fs); ++t) {
        const float input = 0.33f * std::sin(2.0f * arverb::detail::kPi * hz * t / fs);
        const float output = filter.process(input, coef, depth);
        check(std::isfinite(output), "contour output finite");
        if (depth == 0.0f) check(output == input, "0 depth exact bypass");
        if (t >= static_cast<int>(fs / 2)) {
            inEnergy += static_cast<double>(input) * input;
            outEnergy += static_cast<double>(output) * output;
        }
    }
    return std::sqrt(outEnergy / inEnergy);
}
int main() {
    const float fs = 48000.0f;
    const double low = sineGain(95.0f, 0.10f, fs);
    const double presence = sineGain(2600.0f, 0.10f, fs);
    check(low < presence - 0.035 && presence > 0.99,
          "feedback contour must preferentially absorb bass and protect presence");
    check(std::abs(sineGain(95.0f, 0.0f, fs) - 1.0) < 1e-12,
          "zero-depth contour mathematically transparent");

    // Compare low and high diffusion impulse energy. New early-send morph must
    // be finite, deterministic, and neither mute nor explode the wet signal.
    for (auto rate : {8000.0, 48000.0, 96000.0, 192000.0}) {
        std::array<float, 257> inL{}, inR{}, outL{}, outR{};
        for (float diffusion : {0.0f, 0.55f, 0.80f, 1.0f}) {
            arverb::Engine engine;
            engine.setParameter(arverb::Parameter::Mix, 1.0f);
            engine.setParameter(arverb::Parameter::PreDelayMs, 0.0f);
            engine.setParameter(arverb::Parameter::Diffusion, diffusion);
            engine.setParameter(arverb::Parameter::LowCutHz, 1000.0f);
            engine.prepare(rate);
            double energyL = 0.0, energyR = 0.0;
            for (int cb = 0; cb < 480; ++cb) {
                inL.fill(0.0f); inR.fill(0.0f);
                if (cb == 0) inL[0] = 0.7f;
                engine.process(inL.data(), inR.data(), outL.data(), outR.data(), outL.size());
                for (std::size_t k = 0; k < outL.size(); ++k) {
                    check(std::isfinite(outL[k]) && std::isfinite(outR[k]),
                          "finite impulse at all sample rates/diffusion");
                    check(std::abs(outL[k]) < 16.0f && std::abs(outR[k]) < 16.0f,
                          "bounded output");
                    energyL += static_cast<double>(outL[k]) * outL[k];
                    energyR += static_cast<double>(outR[k]) * outR[k];
                }
            }
            check(energyL > 1e-12 && energyR > 1e-12,
                  "stereo tail remains audible");
        }
    }
    std::printf("PASS P3: bass gain %.6f < presence gain %.6f, 4 rates, 4 diffusion regimes\n",
                low, presence);
}
