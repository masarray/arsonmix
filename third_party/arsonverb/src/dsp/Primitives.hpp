#pragma once
// Private DSP primitives. Non-platform, allocation-free processing interfaces.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace arverb::detail {
constexpr float kPi = 3.14159265358979323846f;
inline float clamp(float x, float lo, float hi) noexcept {
    return std::max(lo, std::min(hi, x));
}

inline float killDenormal(float x) noexcept {
    return (std::fabs(x) < 1.0e-20f) ? 0.0f : x;
}

// Range-reduced 7th-order exponential: strictly bounded positive exponent.
// A float-only Horner polynomial + four squarings, no libm call or lookup
// cache misses when a DAW changes several preset parameters on one ASIO block.
// On [0,8], absolute error vs exp(-x) is below ~3e-7 in float arithmetic.
// The reverb's RT60 exponent magnitude is <= 6.12; filter coeffs <= 2.77.
inline float fastExpNeg(float exponent) noexcept {
    const float y = clamp(exponent, 0.0f, 8.0f) * (1.0f / 16.0f);
    float result = 1.0f + y * (-1.0f + y * (0.5f + y * (-1.0f / 6.0f
                 + y * (1.0f / 24.0f + y * (-1.0f / 120.0f
                 + y * (1.0f / 720.0f - y / 5040.0f))))));
    result *= result;
    result *= result;
    result *= result;
    result *= result;
    return result;
}
inline float lowPassCoeff(float cutoff, float sampleRate) noexcept {
    // One pole: no transcendental libm in audio callback; exact frequency
    // bound remains identical to the previous std::exp implementation.
    cutoff = clamp(cutoff, 10.0f, sampleRate * 0.44f);
    return 1.0f - fastExpNeg(2.0f * kPi * cutoff / sampleRate);
}

struct Delay {
    std::vector<float> data;
    std::size_t mask = 0;
    std::size_t write = 0;

    void prepare(std::size_t maxDelaySamples) {
        std::size_t n = 8;
        while (n < maxDelaySamples + 8) n <<= 1;
        data.assign(n, 0.0f);
        mask = n - 1;
        write = 0;
    }
    void reset() noexcept {
        std::fill(data.begin(), data.end(), 0.0f);
        write = 0;
    }
    float read(float samples) const noexcept {
        // read-before-push: delay 1.0 means previous sample; min requested >=1.
        samples = clamp(samples, 1.0f, static_cast<float>(data.size() - 4));
        const auto integer = static_cast<std::size_t>(samples);
        const float fraction = samples - static_cast<float>(integer);
        const std::size_t a = (write - integer) & mask;
        const std::size_t b = (a - 1) & mask;
        return data[a] + (data[b] - data[a]) * fraction;
    }
    void push(float x) noexcept {
        data[write] = x;
        write = (write + 1) & mask;
    }
};

struct Allpass {
    Delay delay;
    float samples = 1.0f;
    void prepare(float ms, float rate) {
        samples = std::max(1.0f, ms * 0.001f * rate);
        delay.prepare(static_cast<std::size_t>(samples) + 2);
    }
    void reset() noexcept { delay.reset(); }
    float process(float x, float g) noexcept {
        const float d = delay.read(samples);
        const float y = d - g * x;
        delay.push(killDenormal(x + g * y));
        return y;
    }
};

struct LowPass {
    float z = 0.0f;
    float process(float x, float coeff) noexcept {
        z = killDenormal(z + coeff * (x - z));
        return z;
    }
    void reset() noexcept { z = 0.0f; }
};

struct HighPass {
    LowPass low;
    float process(float x, float coeff) noexcept {
        return x - low.process(x, coeff);
    }
    void reset() noexcept { low.reset(); }
};

// Stereo-linked, input-conditioned wet presence attenuation. This is an
// economical perceptual starting point, not speech/AI analysis or a new mode.
// The FDN feedback and all dry samples remain completely untouched.
struct PresenceDucker {
    LowPass dryLow[2], dryHigh[2], wetLow[2], wetHigh[2];
    float lowCoeff = 0.0f, highCoeff = 0.0f;
    float attack = 0.0f, release = 0.0f, envelope = 0.0f;

    void prepare(float fs) noexcept {
        lowCoeff = lowPassCoeff(1250.0f, fs);
        highCoeff = lowPassCoeff(4200.0f, fs);
        attack = 1.0f - std::exp(-1.0f / (0.010f * fs));
        release = 1.0f - std::exp(-1.0f / (0.180f * fs));
        reset();
    }
    void reset() noexcept {
        for (auto& f : dryLow) f.reset();
        for (auto& f : dryHigh) f.reset();
        for (auto& f : wetLow) f.reset();
        for (auto& f : wetHigh) f.reset();
        envelope = 0.0f;
    }
    void process(float inL, float inR, float& wetL, float& wetR,
                 float duckAmount) noexcept {
        const float dryBandL = dryHigh[0].process(inL, highCoeff)
                             - dryLow[0].process(inL, lowCoeff);
        const float dryBandR = dryHigh[1].process(inR, highCoeff)
                             - dryLow[1].process(inR, lowCoeff);
        const float peak = std::max(std::fabs(dryBandL), std::fabs(dryBandR));
        envelope = killDenormal(envelope + (peak > envelope ? attack : release)
                                * (peak - envelope));
        // Always advance history: raising Duck during an existing reverb tail
        // must not start fresh filter state and create a discontinuity.
        const float wetBandL = wetHigh[0].process(wetL, highCoeff)
                             - wetLow[0].process(wetL, lowCoeff);
        const float wetBandR = wetHigh[1].process(wetR, highCoeff)
                             - wetLow[1].process(wetR, lowCoeff);
        // Bounded, smooth saturating subtraction; 0% Duck = exact bypass.
        const float depth = 0.56f * duckAmount * envelope / (0.085f + envelope);
        wetL -= depth * wetBandL;
        wetR -= depth * wetBandR;
    }
};

// Frequency-selective low/low-mid decay contour in the feedback tank.
// One pole only; optional zero-depth is *exactly* transparent at the output.
// State is always advanced to support fast automation without filter startup.
// One-pole wet high shelf; air is a *spectral tilt*, not a wideband
// gain boost, pitch shift, noise generator or extra source of sibilance.
// Caller determines bounded lift from high-cut and existing wet color.
struct AirShelf {
    LowPass low;
    void reset() noexcept { low.reset(); }
    float process(float wet, float coeff, float lift) noexcept {
        const float body = low.process(wet, coeff);
        return wet + lift * (wet - body);
    }
};

struct BassDecayContour {
    LowPass low;
    void reset() noexcept { low.reset(); }
    float process(float x, float coefficient, float depth) noexcept {
        const float bass = low.process(x, coefficient);
        return x - depth * bass;
    }
};

struct Oscillator {
    float sinPhase = 0.0f;
    float cosPhase = 1.0f;
    float sinStep = 0.0f;
    float cosStep = 1.0f;
    unsigned count = 0;
    void setFrequency(float hertz, float rate) noexcept {
        // 8kHz minimum fs and <=2Hz * 1.49 LFO ratio bound
        // |omega| <= .00235 rad: fifth-order sine / fourth-order cosine
        // approximate std::sin/cos to within float precision. Avoid
        // 16 libm calls in first preset-switch callback.
        const float omega = 2.0f * kPi * hertz / rate;
        const float square = omega * omega;
        sinStep = omega * (1.0f - square * (1.0f / 6.0f
                                         - square * (1.0f / 120.0f)));
        cosStep = 1.0f - square * (0.5f - square * (1.0f / 24.0f));
    }
    void reset(float phase) noexcept {
        sinPhase = std::sin(phase);
        cosPhase = std::cos(phase);
        count = 0;
    }
    void tick() noexcept {
        const float s = sinPhase * cosStep + cosPhase * sinStep;
        const float c = cosPhase * cosStep - sinPhase * sinStep;
        sinPhase = s;
        cosPhase = c;
        // Prevent recursive-oscillator amplitude drift. Rarely in the hot path.
        if ((++count & 8191u) == 0u) {
            const float norm = 1.0f / std::sqrt(s * s + c * c + 1.0e-20f);
            sinPhase *= norm;
            cosPhase *= norm;
        }
    }
};

}  // namespace arverb::detail
