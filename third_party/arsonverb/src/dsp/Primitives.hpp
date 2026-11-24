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

// Two identical one-pole HP stages have a combined -6 dB knee if
// each stage is set to the displayed cutoff. For the musical profile,
// solve their actual discrete-time response for combined -3 dB there.
// HP(z) = pole*(1-z^-1)/(1-pole*z^-1); q=1/pole, y=q-1.
// With d=4*sin(w/2)^2, |HP|^2=d/(y*y+(1+y)*d).
// Set |HP|^2=1/sqrt(2) so the two-stage amplitude is 1/sqrt(2).
// Only prepare or coalesced cutoff automation calls this; no new per-sample
// work, filter state, allocations or feedback changes.
inline float musicalWetLowCutCoeff(float cutoff, float sampleRate) noexcept {
    cutoff = clamp(cutoff, 10.0f, sampleRate * 0.44f);
    const float x = kPi * cutoff / sampleRate;
    // Reverb parameter range 30..1200 Hz, fs >= 8 kHz: x <= .472.
    const float x2 = x*x;
    const float s = x*(1.0f+x2*(-1.0f/6.0f+x2/120.0f));
    const float d = 4.0f*s*s;
    constexpr float k = 0.41421356237309505f; // sqrt(2)-1
    const float y = (std::sqrt(d*d+4.0f*d*k)-d)*0.5f;
    return y/(1.0f+y);
}

// Partial energy compensation for late reverb only. A short exponential
// decay has less integrated energy than a long one at equal excitation.
// Preserve the expressive 1.7..3.5s singing range exactly; recover up to
// +3.52dB for short spaces and gently trim very long washes by <=1.01dB.
// This is not automatic loudness matching, compression or feedback gain.
// Evaluated only on prepare / coalesced decay changes.
inline float musicalLateReturnScale(float decaySeconds) noexcept {
    const float decay = clamp(decaySeconds, .15f, 12.f);
    if (decay < 1.7f)
        return std::min(1.5f, std::sqrt(1.7f / decay));
    if (decay > 3.5f)
        return std::max(.89f, std::sqrt(3.5f / decay));
    return 1.f;
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
    struct FractionalTap {
        std::size_t integer=1;
        float fraction=0.f;
    };
    FractionalTap prepareTap(float samples) const noexcept {
        // Valid only after prepare(), same clamp and rounding as read().
        samples = clamp(samples, 1.0f, static_cast<float>(data.size() - 4));
        const auto integer = static_cast<std::size_t>(samples);
        return {integer,samples-static_cast<float>(integer)};
    }
    float readPrepared(FractionalTap tap) const noexcept {
        const std::size_t a = (write - tap.integer) & mask;
        const std::size_t b = (a - 1) & mask;
        return data[a] + (data[b] - data[a]) * tap.fraction;
    }
    float read(float samples) const noexcept {
        // Moving taps retain exactly the original fractional interpolation.
        return readPrepared(prepareTap(samples));
    }
    // Fixed, prepare-validated tap: no clamp, float conversion or
    // interpolation needed for a stationary early reflection.
    float readFixed(std::size_t samples) const noexcept {
        return data[(write - samples) & mask];
    }
    void push(float x) noexcept {
        data[write] = x;
        write = (write + 1) & mask;
    }
};

struct Allpass {
    Delay delay;
    Delay::FractionalTap tap{};
    void prepare(float ms, float rate) {
        const float samples = std::max(1.0f, ms * 0.001f * rate);
        delay.prepare(static_cast<std::size_t>(samples) + 2);
        tap=delay.prepareTap(samples);
    }
    void reset() noexcept { delay.reset(); }
    float process(float x, float g) noexcept {
        const float d = delay.readPrepared(tap);
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
// Fixed peaking coefficients computed at prepare, shared between channels.
struct AlbumBell {
    float b0=1.f,b1=0.f,b2=0.f,a1=0.f,a2=0.f;
    void prepare(float fs,float hz,float q,float db) noexcept {
        const double w=2.0*3.14159265358979323846*std::min(hz,fs*.4f)/fs;
        const double a=std::pow(10.0,db/40.0),alpha=std::sin(w)/(2.0*q);
        const double norm=1.0/(1.0+alpha/a);
        b0=float((1.0+alpha*a)*norm);b1=float(-2.0*std::cos(w)*norm);
        b2=float((1.0-alpha*a)*norm);a1=b1;a2=float((1.0-alpha/a)*norm);
    }
    struct Memory {
        float z1=0.f,z2=0.f;
        float process(float x,const AlbumBell& c) noexcept {
            const float y=c.b0*x+z1;
            z1=killDenormal(c.b1*x-c.a1*y+z2);
            z2=killDenormal(c.b2*x-c.a2*y);
            return y;
        }
    };
};
// Late-only tone: early reflections and dry bypass this contour.
// Linked detector is polarity-independent; Duck=0 disables de-essing.
struct AlbumLateTone {
    AlbumBell mud,mid,body;
    AlbumBell::Memory mudState[2],midState[2],bodyState[2];
    LowPass dryEssLow[2],dryEssHigh[2],wetEssLow[2],wetEssHigh[2];
    float lo=0.f,hi=0.f,attack=0.f,release=0.f,env=0.f;
    void prepare(float fs) noexcept {
        mud.prepare(fs,380.f,.8f,-2.5f);
        mid.prepare(fs,1450.f,.65f,-2.4f);
        body.prepare(fs,170.f,1.05f,1.5f); // wet body, not sub-bass or feedback boost
        lo=lowPassCoeff(5000.f,fs);hi=lowPassCoeff(10500.f,fs);
        attack=1.f-std::exp(-1.f/(.004f*fs));
        release=1.f-std::exp(-1.f/(.075f*fs));
        reset();
    }
    void reset() noexcept {
        for(int c=0;c<2;++c) {
            mudState[c]={};midState[c]={};bodyState[c]={};
            dryEssLow[c].reset();dryEssHigh[c].reset();
            wetEssLow[c].reset();wetEssHigh[c].reset();
        }
        env=0.f;
    }
    void process(float inL,float inR,float& l,float& r,float duck,float blend,float signature=0.f) noexcept {
        const float dl=dryEssHigh[0].process(inL,hi)-dryEssLow[0].process(inL,lo);
        const float dr=dryEssHigh[1].process(inR,hi)-dryEssLow[1].process(inR,lo);
        const float peak=std::max(std::fabs(dl),std::fabs(dr));
        env=killDenormal(env+(peak>env?attack:release)*(peak-env));
        const float depth=.85f*duck*env/(.035f+env);
        float* out[2]{&l,&r};
        for(int c=0;c<2;++c) {
            const float x=*out[c];
            float y=midState[c].process(mudState[c].process(x,mud),mid);
            if(signature>0.f) {
                const float full=bodyState[c].process(y,body);
                y+=signature*(full-y);
            }
            const float band=wetEssHigh[c].process(y,hi)-wetEssLow[c].process(y,lo);
            y-=depth*band;
            *out[c]=x+blend*(y-x);
        }
    }
};

struct PresenceDucker {
    LowPass dryLow[2], dryHigh[2], wetLow[2], wetHigh[2];
    float lowCoeff = 0.0f, highCoeff = 0.0f;
    float attack = 0.0f, release = 0.0f, envelope = 0.0f;

    void prepare(float fs, bool musical = false) noexcept {
        lowCoeff = lowPassCoeff(1250.0f, fs);
        highCoeff = lowPassCoeff(4200.0f, fs);
        // Fast articulation clearance, followed by a natural phrase bloom.
        // Legacy profile retains its original envelope exactly.
        attack = 1.0f - std::exp(-1.0f / ((musical ? 0.006f : 0.010f) * fs));
        release = 1.0f - std::exp(-1.0f / ((musical ? 0.090f : 0.180f) * fs));
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
        // Caller scales the musical profile to prioritize selective
        // clearance over broadband suppression (effective max 1.5).
        // Depth remains <= .84; 0% Duck = exact bypass.
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
// The user High Cut remains authoritative. A slightly stronger wet-only
// shelf at open settings adds sparkle; closed/dark settings stay unchanged.
inline float wetAirLift(float highCutHz,float dampingHz,bool musical) noexcept {
    const float openness=clamp((highCutHz-5500.f)/8000.f,0.f,1.f);
    const float bright=clamp((dampingHz-800.f)/17200.f,0.f,1.f);
    return (musical?.90f:.72f)*openness*(.72f+.28f*bright);
}

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
