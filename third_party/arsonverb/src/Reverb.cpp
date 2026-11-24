#include "arverb/Reverb.hpp"
#include "dsp/Primitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace arverb {
namespace {
constexpr float kLn001 = 6.907755278982137f;  // ln(1000), for RT60
constexpr std::size_t kLines = 8;
// Bound expensive control-coefficient work under tiny ASIO buffer automation.
// The last host target wins; audio coefficients remain sample-smoothed.
constexpr unsigned kControlQuantumSamples = 128;
constexpr float kDelayMorphSeconds = 0.070f; // clickless dual-read tap crossfade

// Audio-thread-safe sine gain polynomial on [0,pi/2]. At x=pi/2,
// 11th-order Taylor error < 6e-8 in double; no transcendental library
// calls when Mix changes as part of a 14-parameter host preset.
inline float sineQuarterWave(float normalized) noexcept {
    const float x = detail::clamp(normalized, 0.0f, 1.0f) * 1.57079632679489661923f;
    const float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f
            + x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f
            - x2 / 39916800.0f)))));
}

// Reverb-Sis insert/send level: keep the established <=50% musical
// balance, then smoothly recover up to +3.52 dB at the pure-wet endpoint.
// This is a deterministic gain law, NOT a feedback boost, AGC, limiter,
// dry leak, or stereo-dependent correction. Called only on Mix targets.
inline float wetMixTarget(float mix, bool musicalReturn) noexcept {
    const float base = sineQuarterWave(mix);
    if (!musicalReturn) return base;
    const float x = detail::clamp(2.0f * mix - 1.0f, 0.0f, 1.0f);
    const float curve = x*x*(3.0f-2.0f*x);
    return base * (1.0f + 0.5f*curve);
}

inline void algorithmMatrix (unsigned mode, const float* input, float* output) noexcept {
    // All three *new* feedback families are orthogonal and do not add delay
    // buffers. Plate: eight-line 3-stage normalized Hadamard, O(N log N).
    if (mode == 1u) {
        float a[8], b[8];
        for (int i=0;i<8;i+=2) {
            a[i]=input[i]+input[i+1]; a[i+1]=input[i]-input[i+1];
        }
        for (int i=0;i<8;i+=4) {
            b[i]=a[i]+a[i+2]; b[i+2]=a[i]-a[i+2];
            b[i+1]=a[i+1]+a[i+3]; b[i+3]=a[i+1]-a[i+3];
        }
        constexpr float invSqrt8=0.353553390593273762f;
        for (int i=0;i<4;++i) {
            output[i]=invSqrt8*(b[i]+b[i+4]);
            output[i+4]=invSqrt8*(b[i]-b[i+4]);
        }
    } else if (mode == 2u) {
        // Chamber: four interleaved pairwise Givens rotations; intentionally
        // sparser, with less cross-coupling than a hall/plate feedback field.
        constexpr int pairs[4][2] {{0,5},{1,6},{2,7},{3,4}};
        for (const auto& p:pairs) {
            const int i=p[0],j=p[1];
            output[i]=0.8f*input[i]+0.6f*input[j];
            output[j]=-0.6f*input[i]+0.8f*input[j];
        }
    } else {
        // Hall: distinct Householder eigenvector from legacy Classic.
        constexpr float sign[8] {1.f,1.f,-1.f,-1.f,-1.f,-1.f,1.f,1.f};
        float projection=0.0f;
        for (int i=0;i<8;++i) projection+=sign[i]*input[i];
        projection*=0.25f;
        for (int i=0;i<8;++i) output[i]=input[i]-sign[i]*projection;
    }
}


constexpr ParameterInfo kParameterInfo[parameterCount] = {
    {"mix",       "Mix",         "",    0.0f,    1.0f,    0.19f},
    {"predelay",  "Pre-delay",   "ms",  0.0f,  250.0f,   36.0f},
    {"decay",     "Decay RT60",  "s",   0.15f,  12.0f,    2.00f},
    {"size",      "Size",        "",    0.65f,   2.0f,    1.0f},
    {"diffusion", "Diffusion",   "",    0.0f,    1.0f,    0.66f},
    {"damping",   "HF damping",  "Hz", 800.0f,18000.0f, 6300.0f},
    {"lowcut",    "Wet low cut", "Hz",  30.0f, 1200.0f,  220.0f},
    {"highcut",   "Wet high cut","Hz",1000.0f,20000.0f,10000.0f},
    {"moddepth",  "Mod depth",   "ms",  0.0f,    1.2f,    0.18f},
    {"modrate",   "Mod rate",    "Hz",  0.05f,   2.0f,    0.32f},
    {"width",     "Wet width",   "",    0.0f,    1.6f,    1.08f},
    {"early",     "Early level", "",    0.0f,    1.0f,    0.30f},
    {"duck",      "Duck amount", "",    0.0f,    1.0f,    0.33f}
};

constexpr float kDelayMs[kLines] = {30.13f, 34.37f, 39.19f, 43.73f,
                                   47.53f, 52.87f, 59.17f, 66.41f};
constexpr float kLfoRatio[kLines] = {0.77f, 1.12f, 0.69f, 1.31f,
                                    0.94f, 1.49f, 0.83f, 1.24f};
// Fixed orthogonal-ish injection and readout patterns, distinct per channel.
constexpr float kInjectL[kLines] = {1, 1, 1, 1,-1,-1,-1,-1};
constexpr float kInjectR[kLines] = {1, 1,-1,-1, 1, 1,-1,-1};
constexpr float kReadL[kLines]   = {1,-1, 1, 1,-1, 1,-1,-1};
constexpr float kReadR[kLines]   = {1, 1,-1, 1,-1,-1, 1,-1};
constexpr float kReflect[kLines] = {1,-1, 1,-1, 1,-1, 1,-1};
constexpr float kDiffuseMs[2][4] = {
    {4.73f, 8.81f, 13.17f, 20.93f},
    {5.27f, 9.41f, 14.63f, 22.19f}
};
constexpr float kEarlyMs[6] = {14.9f, 23.3f, 37.7f, 55.1f, 83.3f, 126.1f};
constexpr float kEarlyGain[6] = {0.34f, 0.29f, 0.23f, 0.18f, 0.14f, 0.12f};
// Original non-uniform feed-forward reflections, interleaved with the
// existing six. All fit in the existing 132ms early buffer. They fill
// the sparse onset without another recirculating comb/allpass network.
constexpr std::size_t kTextureTaps = 4;
constexpr float kTextureMs[kTextureTaps] = {8.3f, 31.1f, 67.9f, 104.7f};
constexpr float kTextureGain[kTextureTaps] = {0.23f, 0.18f, 0.14f, 0.10f};

// P7: derive the representative tank traversal from the ORIGINAL FDN
// geometry. All four matrices share these 8 delay lengths; this must not
// introduce another tank or another feedback filter.
constexpr float representativeDelayMs() noexcept {
    float total=0.0f;
    for (float ms : kDelayMs) total += ms;
    return total / static_cast<float>(kLines);
}

// High-band feedback attenuation is calculated from a target *relative*
// RT60, not a fixed loss per feedback iteration. This avoids over-damping
// long decays and under-damping short rooms. A single expm1 is computed
// only when size/decay/damping changes (never per audio sample).
// The existing 8 feedback LPs perform the actual spectral shaping.
inline float calibratedHFLoss(float decaySeconds, float roomSize,
                              float dampingHz) noexcept {
    const float bright = detail::clamp((dampingHz - 800.0f) / 17200.0f, 0.0f, 1.0f);
    const float highRT60Fraction = 0.58f + 0.40f * bright; // 58%-98% of mid RT60
    const float traversalSeconds = representativeDelayMs() * 0.001f * roomSize;
    const float excessLogLoss = -kLn001 * traversalSeconds / decaySeconds *
                                (1.0f / highRT60Fraction - 1.0f);
    // Finite bounded exponent: use the same fast polynomial as the gain and
    // filter coefficients; this is evaluated on preset automation callbacks.
    return detail::clamp(1.0f - detail::fastExpNeg(-excessLogLoss), 0.0f, 0.92f);
}

// Top-air is a bounded wet-only spectral tilt, never a noise generator.
// A closed high-cut gate remains dark; an open high-cut and low HF absorption
// permit more sheen without raising the dry path, tail feedback or output trim.

// Additional low-amplitude, irregularly spaced *early energy* taps. This is
// one original density layer, not a ray-traced room model or Valhalla code.
// The old 6-tap sound remains untouched below Diffusion=0.62. Delay storage
// stays inside the existing 132ms early buffers; no new allocations.
constexpr float bassDecayCutoff(float lowCutHz) noexcept {
    // Subtle recirculating low-frequency absorption, NOT an extra wet HPF.
    return 85.0f + 0.14f * lowCutHz;
}
constexpr float bassDecayDepth(float lowCutHz) noexcept {
    // 3..10% low-band reduction per pass; small at normal vocal settings.
    return 0.03f + 0.07f * (lowCutHz / 1200.0f);
}

using detail::Delay;
using detail::Allpass;
using detail::LowPass;
using detail::HighPass;
using detail::Oscillator;
using detail::killDenormal;
using detail::clamp;
using detail::lowPassCoeff;

}  // namespace

const ParameterInfo& parameterInfo(Parameter parameter) noexcept {
    const auto i = static_cast<std::size_t>(parameter);
    return kParameterInfo[i < parameterCount ? i : 0];
}

struct Engine::State {
    float sampleRate = 0.0f;
    bool musicalReturn = false;
    float wetReturnTrim = 1.0f;
    float presenceDuckScale = 1.0f;
    float smoother = 0.0f;
    float envelopeAttack = 0.0f;
    float envelopeRelease = 0.0f;
    Delay pre[2];
    Delay early[2];
    Allpass diffuser[2][4];
    Delay lines[kLines];
    // Immutable with respect to each prepare() call: avoid invariant arithmetic
    // in the sample loop without introducing per-parameter cache invalidation.
    Delay::FractionalTap earlyTapL[6]{},earlyTapR[6]{};
    std::size_t textureL[kTextureTaps]{}, textureR[kTextureTaps]{};
    float fdnBaseSamples[kLines]{};
    float millisecondsToSamples = 0.0f;
    Oscillator lfo[kLines];
    LowPass feedbackLP[kLines];
    // Bass absorption only along the Householder reflection eigenmode:
    // one shared one-pole state, not 8 costly per-line filters.
    detail::BassDecayContour feedbackBass;
    HighPass wetHP[2][2];
    LowPass wetLP[2][2];
    detail::AirShelf airShelf[2];
    float airCoeff = 0.0f;
    float wetHighSlope = 0.0f, targetHighSlope = 0.0f;
    float airLift = 0.0f, targetAirLift = 0.0f;
    float hfDampingBlend = 1.0f, targetHFDampingBlend = 1.0f;
    // Sine/cosine gain targets calculated only when the host Mix changes.
    // Two native gain smoothers keep the audio callback free of any trig,
    // lookup or interpolation per sample.
    float currentDryGain=1.0f, currentWetGain=0.0f;
    float targetDryGain=1.0f, targetWetGain=0.0f;
    // Fold late-space calibration into the existing output multiplier.
    // Separate from early level, feedback gain and dry/wet Mix.
    float lateReturn=.34f, targetLateReturn=.34f;
    float cachedMix=-1.0f;
    detail::PresenceDucker presence;
    detail::AlbumLateTone albumTone;
    LowPass albumBass[kLines];
    float albumBlend=0.f,signatureBlend=0.f,albumStep=0.f,albumBassCoeff=0.f;
    float albumLoss=0.f,targetAlbumLoss=0.f;
    std::array<float, parameterCount> smooth{};
    float gains[kLines]{};
    float targetGains[kLines]{};
    float cachedDecay = -1.0f;
    float cachedSize = -1.0f;
    float cachedRate = -1.0f;
    float cachedDamping = -1.0f;
    float cachedLowCut = -1.0f;
    float cachedHighCut = -1.0f;
    unsigned coefficientCooldownSamples = 0;
    // Block targets can move hundreds of milliseconds at preset switches.
    // Smoothing delay *length* moves the playback head and can produce a
    // fast pitch sweep / alias pop. Morph two fixed read taps instead.
    // If a new target arrives mid-transition, keep the current transition
    // continuous, then use the latest target (last-wins, no RT allocations).
    float preTapCurrent = 0.0f, preTapNext = 0.0f, preTapTarget = 0.0f;
    float preTapBlend = 1.0f;
    float sizeTapCurrent = 1.0f, sizeTapNext = 1.0f, sizeTapTarget = 1.0f;
    float sizeTapBlend = 1.0f;
    float delayMorphStep = 0.0f;
    float targetHF = 0.0f;
    float targetLow = 0.0f;
    float targetHigh = 0.0f;
    float targetBassCoeff = 0.0f;
    float targetBassDepth = 0.0f;
    float bassCoeff = 0.0f;
    float bassDepth = 0.0f;
    float duckEnvelope = 0.0f;
    float hfFeedbackCoeff = 0.0f;
    float wetHighCoeff = 0.0f;
    float wetLowCoeff = 0.0f;
    unsigned fromAlgorithm=0, toAlgorithm=0;
    float algorithmBlend=1.0f, algorithmStep=0.0f;

    void reset() noexcept {
        for (auto& p : pre) p.reset();
        for (auto& e : early) e.reset();
        for (auto& ch : diffuser) for (auto& d : ch) d.reset();
        for (auto& l : lines) l.reset();
        for (auto& f : feedbackLP) f.reset();
        feedbackBass.reset();
        for (auto& ch : wetHP) for (auto& f : ch) f.reset();
        for (auto& ch : wetLP) for (auto& f : ch) f.reset();
        for (auto& f : airShelf) f.reset();
        presence.reset();
        albumTone.reset();
        for(auto& f:albumBass)f.reset();
        for (std::size_t i = 0; i < kLines; ++i) {
            lfo[i].reset(static_cast<float>(i) * 1.7718f + 0.39f);
        }
        duckEnvelope = 0.0f;
    }
};

Engine::Engine() : state_(std::make_unique<State>()) {
    for (std::size_t i = 0; i < parameterCount; ++i)
        parameters_[i].store(kParameterInfo[i].defaultValue, std::memory_order_relaxed);
}

Engine::~Engine() = default;

bool Engine::prepared() const noexcept { return state_->sampleRate > 0.0f; }

void Engine::prepare(double sampleRate) { prepare(sampleRate,false); }

void Engine::prepare(double sampleRate, bool musicalReturn) {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0)
        throw std::invalid_argument("arverb sample rate must be 8000..192000 Hz");

    State& s = *state_;
    // Allocation is allowed here only, when the host has suspended processing.
    s.sampleRate = static_cast<float>(sampleRate);
    s.musicalReturn = musicalReturn;
    // +6.02 dB wet return only. Feedback/injection/RT60 remain unchanged.
    s.wetReturnTrim = musicalReturn ? 2.0f : 1.0f;
    s.presenceDuckScale = musicalReturn ? 1.50f : 1.0f;
    const float fs = s.sampleRate;
    s.millisecondsToSamples = fs * 0.001f;
    for (std::size_t i=0;i<kTextureTaps;++i) {
        s.textureL[i] = static_cast<std::size_t>(std::lround(
            kTextureMs[i]*s.millisecondsToSamples));
        s.textureR[i] = static_cast<std::size_t>(std::lround(
            (kTextureMs[i]+(i%2==0?0.79f:1.37f))*s.millisecondsToSamples));
    }
    for (std::size_t i = 0; i < kLines; ++i)
        s.fdnBaseSamples[i] = kDelayMs[i] * s.millisecondsToSamples;
    const auto forMs = [fs](float ms) -> std::size_t {
        return static_cast<std::size_t>(std::ceil(ms * 0.001f * fs)) + 8;
    };
    for (auto& p : s.pre) p.prepare(forMs(252.0f));
    for (auto& e : s.early) e.prepare(forMs(132.0f));
    for(int t=0;t<6;++t) {
        const float left=kEarlyMs[t]*s.millisecondsToSamples;
        s.earlyTapL[t]=s.early[0].prepareTap(left);
        s.earlyTapR[t]=s.early[1].prepareTap(left*(1.0f+0.019f*(t%3)));
    }
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < 4; ++i)
            s.diffuser[ch][i].prepare(kDiffuseMs[ch][i], fs);
    }
    for (std::size_t i = 0; i < kLines; ++i)
        s.lines[i].prepare(forMs(kDelayMs[i] * 2.0f + 3.0f));

    s.smoother = 1.0f - std::exp(-1.0f / (0.025f * fs));
    s.envelopeAttack = 1.0f - std::exp(-1.0f / ((musicalReturn ? 0.008f : 0.012f) * fs));
    s.envelopeRelease = 1.0f - std::exp(-1.0f / ((musicalReturn ? 0.16f : 0.34f) * fs));
    s.presence.prepare(fs,musicalReturn);
    s.albumTone.prepare(fs);
    s.albumStep=1.f/(.045f*fs);
    s.albumBassCoeff=lowPassCoeff(300.f,fs);
    s.airCoeff = lowPassCoeff(7800.0f, fs);
    s.algorithmStep = 1.0f / std::max(1.0f, fs * 0.100f); // 100ms morph
    s.fromAlgorithm = s.toAlgorithm = algorithm_.load(std::memory_order_relaxed);
    s.algorithmBlend=1.0f;
    for (std::size_t i = 0; i < parameterCount; ++i)
        s.smooth[i] = parameters_[i].load(std::memory_order_relaxed);
    s.cachedMix = s.smooth[static_cast<unsigned>(Parameter::Mix)];
    s.currentDryGain = s.targetDryGain = sineQuarterWave(1.0f - s.cachedMix);
    s.currentWetGain = s.targetWetGain = wetMixTarget(s.cachedMix, s.musicalReturn);
    // Freeze initial taps at the host's actual restored preset.
    s.preTapCurrent = s.preTapNext = s.preTapTarget =
        s.smooth[static_cast<unsigned>(Parameter::PreDelayMs)]
        * s.millisecondsToSamples;
    s.sizeTapCurrent = s.sizeTapNext = s.sizeTapTarget =
        s.smooth[static_cast<unsigned>(Parameter::Size)];
    s.preTapBlend = s.sizeTapBlend = 1.0f;
    s.delayMorphStep = 1.0f / std::max(1.0f, fs * kDelayMorphSeconds);

    const float decay = s.smooth[static_cast<unsigned>(Parameter::DecaySeconds)];
    const float size = s.smooth[static_cast<unsigned>(Parameter::Size)];
    s.lateReturn = s.targetLateReturn = .34f *
        (musicalReturn ? detail::musicalLateReturnScale(decay) : 1.f);
    for (std::size_t i = 0; i < kLines; ++i) {
        const float lengthSeconds = kDelayMs[i] * 0.001f * size;
        s.gains[i] = detail::fastExpNeg(kLn001 * lengthSeconds / decay);
        s.targetGains[i] = s.gains[i];
    }
    // P8: all invariant/exp/trig work belongs in prepare(), not the first
    // time-critical processBlock after the host starts its ASIO stream.
    s.albumLoss=s.targetAlbumLoss=clamp(1.f-detail::fastExpNeg(
        kLn001*representativeDelayMs()*.001f*size/decay*.55f),0.f,.30f);
    s.cachedDecay = decay;
    s.cachedSize = size;
    s.cachedRate = s.smooth[static_cast<unsigned>(Parameter::ModRateHz)];
    for (std::size_t i = 0; i < kLines; ++i)
        s.lfo[i].setFrequency(s.cachedRate * kLfoRatio[i], fs);
    s.cachedDamping = s.smooth[static_cast<unsigned>(Parameter::DampingHz)];
    s.cachedLowCut = s.smooth[static_cast<unsigned>(Parameter::LowCutHz)];
    s.cachedHighCut = s.smooth[static_cast<unsigned>(Parameter::HighCutHz)];
    s.coefficientCooldownSamples = 0;
    s.hfFeedbackCoeff = s.targetHF = lowPassCoeff(s.cachedDamping, fs);
    s.wetLowCoeff = s.targetLow = musicalReturn
        ? detail::musicalWetLowCutCoeff(s.cachedLowCut, fs)
        : lowPassCoeff(s.cachedLowCut, fs);
    const float highCut = s.smooth[static_cast<unsigned>(Parameter::HighCutHz)];
    s.wetHighCoeff = s.targetHigh = lowPassCoeff(highCut, fs);
    // Bright settings transition from a 12dB/oct to a gentler 6dB/oct
    // wet post-filter, with a small controlled air shelf above ~6.5k.
    s.wetHighSlope = s.targetHighSlope =
        clamp((highCut - 5500.0f) / 5500.0f, 0.0f, 1.0f);
    const float dampingCut = s.cachedDamping;
    s.airLift = s.targetAirLift = detail::wetAirLift(highCut,dampingCut,musicalReturn);
    s.hfDampingBlend = s.targetHFDampingBlend = calibratedHFLoss(
        decay, size, dampingCut);
    const float initialLowCut = s.cachedLowCut;
    s.bassCoeff = s.targetBassCoeff = lowPassCoeff(bassDecayCutoff(initialLowCut), fs);
    s.bassDepth = s.targetBassDepth = bassDecayDepth(initialLowCut);
    reset();
}

void Engine::reset() noexcept {
    state_->reset();
    State& s=*state_;
    s.fromAlgorithm=s.toAlgorithm=algorithm_.load(std::memory_order_relaxed);
    s.algorithmBlend=1.0f;
    const auto profile=s.musicalReturn?voicing_.load(std::memory_order_relaxed):0u;
    s.albumBlend=profile!=0u?1.f:0.f;
    s.signatureBlend=profile==2u?1.f:0.f;
    s.coefficientCooldownSamples=0;
    // Transport resets must not re-open an old half-finished tap transition.
    s.preTapCurrent = s.preTapNext;
    s.preTapBlend = 1.0f;
    s.sizeTapCurrent = s.sizeTapNext;
    s.sizeTapBlend = 1.0f;
}
void Engine::setAlbumReady(bool enabled) noexcept {
    setVoicing(enabled?1u:0u);
}
void Engine::setVoicing(unsigned profile) noexcept {
    if(profile<=2u)voicing_.store(profile,std::memory_order_relaxed);
}
void Engine::setAlgorithm(Algorithm value) noexcept {
    const auto v=static_cast<unsigned>(value);
    if (v<algorithmCount) algorithm_.store(v,std::memory_order_relaxed);
}
Algorithm Engine::getAlgorithm() const noexcept {
    return static_cast<Algorithm>(algorithm_.load(std::memory_order_relaxed));
}
const char* algorithmName(Algorithm value) noexcept {
    switch(value) {
        case Algorithm::Classic: return "Classic";
        case Algorithm::Plate: return "Plate";
        case Algorithm::Chamber: return "Chamber";
        case Algorithm::Hall: return "Hall";
    }
    return "Classic";
}

void Engine::setParameter(Parameter parameter, float value) noexcept {
    const auto i = static_cast<std::size_t>(parameter);
    if (i >= parameterCount || !std::isfinite(value)) return;
    const auto& info = kParameterInfo[i];
    parameters_[i].store(clamp(value, info.minimum, info.maximum), std::memory_order_relaxed);
}

float Engine::getParameter(Parameter parameter) const noexcept {
    const auto i = static_cast<std::size_t>(parameter);
    return i < parameterCount ? parameters_[i].load(std::memory_order_relaxed) : 0.0f;
}

void Engine::process(const float* inputLeft, const float* inputRight,
                     float* outputLeft, float* outputRight,
                     std::size_t frames) noexcept {
    State& s = *state_;
    if (!s.sampleRate || !frames || !inputLeft || !inputRight || !outputLeft || !outputRight) return;
    float target[parameterCount];
    for (std::size_t i = 0; i < parameterCount; ++i)
        target[i] = parameters_[i].load(std::memory_order_relaxed);
    const auto idx = [](Parameter p) -> unsigned { return static_cast<unsigned>(p); };
    // Atomic host target captured once per block. The entire 100ms
    // transition uses the SAME tank; no two-engine tail CPU/memory cost.
    const float requestedMix = target[idx(Parameter::Mix)];
    if (requestedMix != s.cachedMix) {
        s.cachedMix = requestedMix;
        s.targetDryGain = sineQuarterWave(1.0f - requestedMix);
        s.targetWetGain = wetMixTarget(requestedMix, s.musicalReturn);
    }
    const auto profile=s.musicalReturn?voicing_.load(std::memory_order_relaxed):0u;
    const float albumTarget=profile!=0u?1.f:0.f;
    const float signatureTarget=profile==2u?1.f:0.f;
    bool profileMoving=s.albumBlend!=albumTarget||s.signatureBlend!=signatureTarget;
    const unsigned requested = algorithm_.load(std::memory_order_relaxed);
    if (requested != s.toAlgorithm) {
        s.fromAlgorithm = (s.algorithmBlend < 0.5f)
            ? s.fromAlgorithm : s.toAlgorithm;
        s.toAlgorithm = requested;
        s.algorithmBlend = s.fromAlgorithm == s.toAlgorithm ? 1.0f : 0.0f;
    }

    // Coalesce fast automation to one coefficient update per 128 processed
    // samples. Mix + topology still receive every block; the seven continuous
    // audio controls and all filter/gain coefficients still smooth per sample.
    // Avoid 8 exp + 16 trig and several exp filters on EVERY 32-sample block.
    // Always use the latest atomic targets; never queue stale control events.
    if (s.coefficientCooldownSamples == 0u) {
        bool anyCoefficientChanged = false;
        const bool roomChanged = target[idx(Parameter::Size)] != s.cachedSize ||
                                 target[idx(Parameter::DecaySeconds)] != s.cachedDecay;
        if (roomChanged) {
            s.cachedSize = target[idx(Parameter::Size)];
            const float nextDecay = target[idx(Parameter::DecaySeconds)];
            if (nextDecay != s.cachedDecay && s.musicalReturn)
                s.targetLateReturn = .34f * detail::musicalLateReturnScale(nextDecay);
            s.cachedDecay = nextDecay;
            s.targetAlbumLoss=clamp(1.f-detail::fastExpNeg(kLn001*representativeDelayMs()*.001f*
                s.cachedSize/s.cachedDecay*.55f),0.f,.30f);
            for (std::size_t i = 0; i < kLines; ++i) {
                const float delaySec = kDelayMs[i] * s.cachedSize * 0.001f;
                s.targetGains[i] = detail::fastExpNeg(
                    kLn001 * delaySec / s.cachedDecay);
            }
            anyCoefficientChanged = true;
        }
        if (target[idx(Parameter::ModRateHz)] != s.cachedRate) {
            s.cachedRate = target[idx(Parameter::ModRateHz)];
            for (std::size_t i = 0; i < kLines; ++i)
                s.lfo[i].setFrequency(s.cachedRate * kLfoRatio[i], s.sampleRate);
            anyCoefficientChanged = true;
        }
        const bool dampingChanged = target[idx(Parameter::DampingHz)] != s.cachedDamping;
        if (dampingChanged) {
            s.cachedDamping = target[idx(Parameter::DampingHz)];
            s.targetHF = lowPassCoeff(s.cachedDamping, s.sampleRate);
            anyCoefficientChanged = true;
        }
        if (roomChanged || dampingChanged)
            s.targetHFDampingBlend = calibratedHFLoss(
                s.cachedDecay, s.cachedSize, s.cachedDamping);
        if (target[idx(Parameter::LowCutHz)] != s.cachedLowCut) {
            s.cachedLowCut = target[idx(Parameter::LowCutHz)];
            s.targetLow = s.musicalReturn
                ? detail::musicalWetLowCutCoeff(s.cachedLowCut, s.sampleRate)
                : lowPassCoeff(s.cachedLowCut, s.sampleRate);
            s.targetBassCoeff = lowPassCoeff(bassDecayCutoff(s.cachedLowCut), s.sampleRate);
            s.targetBassDepth = bassDecayDepth(s.cachedLowCut);
            anyCoefficientChanged = true;
        }
        const bool highCutChanged = target[idx(Parameter::HighCutHz)] != s.cachedHighCut;
        if (highCutChanged) {
            s.cachedHighCut = target[idx(Parameter::HighCutHz)];
            s.targetHigh = lowPassCoeff(s.cachedHighCut, s.sampleRate);
            s.targetHighSlope = clamp(
                (s.cachedHighCut - 5500.0f) / 5500.0f, 0.0f, 1.0f);
            anyCoefficientChanged = true;
        }
        if (highCutChanged || dampingChanged)
            s.targetAirLift = detail::wetAirLift(s.cachedHighCut,s.cachedDamping,s.musicalReturn);
        if (anyCoefficientChanged)
            s.coefficientCooldownSamples = kControlQuantumSamples;
    }
    // Decrement by actual processed frames, not callback count: block sizes
    // are variable, including 1 and 2048 samples; no clock/thread required.
    if (frames >= s.coefficientCooldownSamples)
        s.coefficientCooldownSamples = 0;
    else
        s.coefficientCooldownSamples -= static_cast<unsigned>(frames);

    // Seven smoothed scalar controls remain: Mix is smoothed directly in
    // gain space, avoiding LUT/index/lerp arithmetic on every sample.
    // Decay, modulation rate and the three EQ frequency parameters instead
    // drive independent, correctly smoothed feedback/filter coefficients.
    // Integrating those five unused sample histories cost CPU but had no
    // influence on the audio output. Keep the original audible update order.
    constexpr std::array<unsigned, 5> kSmoothedAudioControls {{
        static_cast<unsigned>(Parameter::Diffusion),
        static_cast<unsigned>(Parameter::ModDepthMs),
        static_cast<unsigned>(Parameter::Width),
        static_cast<unsigned>(Parameter::EarlyLevel),
        static_cast<unsigned>(Parameter::DuckAmount)
    }};
    // Publish only the latest requested tap locations; avoid repeatedly
    // interrupting an in-flight morph and introducing discontinuities.
    s.preTapTarget = target[idx(Parameter::PreDelayMs)] * s.millisecondsToSamples;
    s.sizeTapTarget = target[idx(Parameter::Size)];
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (s.preTapBlend >= 1.0f && s.preTapCurrent != s.preTapTarget) {
            s.preTapNext = s.preTapTarget;
            s.preTapBlend = 0.0f;
        }
        if (s.sizeTapBlend >= 1.0f && s.sizeTapCurrent != s.sizeTapTarget) {
            s.sizeTapNext = s.sizeTapTarget;
            s.sizeTapBlend = 0.0f;
        }
        for (const unsigned p : kSmoothedAudioControls)
            s.smooth[p] += s.smoother * (target[p] - s.smooth[p]);
        s.currentDryGain += s.smoother*(s.targetDryGain-s.currentDryGain);
        s.currentWetGain += s.smoother*(s.targetWetGain-s.currentWetGain);
        s.lateReturn += s.smoother*(s.targetLateReturn-s.lateReturn);
        // Fixed profiles skip morph updates; legacy preset recall stays clickless.
        if(profileMoving) {
            const auto step=[&](float value,float target) {
                return target>value?std::min(1.f,value+s.albumStep):
                    target<value?std::max(0.f,value-s.albumStep):value;
            };
            const float previousAlbum=s.albumBlend,previousSignature=s.signatureBlend;
            s.albumBlend=step(s.albumBlend,albumTarget);
            s.signatureBlend=step(s.signatureBlend,signatureTarget);
            if(previousAlbum>0.f&&s.albumBlend==0.f) {
                s.albumTone.reset();for(auto& f:s.albumBass)f.reset();
            } else if(previousSignature>0.f&&s.signatureBlend==0.f) {
                for(auto& memory:s.albumTone.bodyState)memory={};
            }
            profileMoving=s.albumBlend!=albumTarget||s.signatureBlend!=signatureTarget;
        }
        const float album=s.albumBlend,signature=s.signatureBlend;
        if(album>0.f)s.albumLoss+=s.smoother*(s.targetAlbumLoss-s.albumLoss);
        // Hoist per-frame coefficient outside all eight FDN lines.
        // Signature retains 55% of extra LF absorption, never boosts feedback.
        const float albumFeedbackLoss=album*s.albumLoss*(signature>0.f?1.f-.45f*signature:1.f);
        const float airLift=signature>0.f?s.airLift*(1.f+.12f*signature):s.airLift;
        const float dryGain=s.currentDryGain;
        const float wetGain=s.currentWetGain;
        const float preSamples = s.preTapCurrent;
        const float size = s.sizeTapCurrent;
        const float diffusion = s.smooth[idx(Parameter::Diffusion)];
        const float modDepth = s.smooth[idx(Parameter::ModDepthMs)] * s.millisecondsToSamples;
        const float width = s.smooth[idx(Parameter::Width)];
        const float earlyAmount = s.smooth[idx(Parameter::EarlyLevel)];
        const float duckAmount = s.smooth[idx(Parameter::DuckAmount)];
        // More echo density need not mean more short allpass ringing.
        // At high Diffusion trade a portion of the allpass coefficient
        // for independent feed-forward early texture. Smooth input
        // parameter drives both; low Diffusion / legacy remains exact.
        const float density = clamp((diffusion - 0.62f) / 0.38f, 0.0f, 1.0f);
        const float texture = s.musicalReturn ? density : 0.0f;
        const float g = 0.15f + diffusion * 0.60f - 0.13f*texture;
        s.hfFeedbackCoeff += s.smoother * (s.targetHF - s.hfFeedbackCoeff);
        s.wetLowCoeff += s.smoother * (s.targetLow - s.wetLowCoeff);
        s.wetHighCoeff += s.smoother * (s.targetHigh - s.wetHighCoeff);
        s.hfDampingBlend += s.smoother * (s.targetHFDampingBlend - s.hfDampingBlend);
        s.wetHighSlope += s.smoother * (s.targetHighSlope - s.wetHighSlope);
        s.airLift += s.smoother * (s.targetAirLift - s.airLift);
        s.bassCoeff += s.smoother * (s.targetBassCoeff - s.bassCoeff);
        s.bassDepth += s.smoother * (s.targetBassDepth - s.bassDepth);
        // Contain invalid upstream samples before they enter a recursive network.
        // Sanitise malformed host buffers, including finite values close to FLT_MAX.
        // +24 dBFS and beyond is valid; 32.0f preserves substantial headroom
        // while preventing the recursive matrix from reaching inf/NaN.
        const float inL = std::isfinite(inputLeft[frame])
            ? clamp(inputLeft[frame], -32.0f, 32.0f) : 0.0f;
        const float inR = std::isfinite(inputRight[frame])
            ? clamp(inputRight[frame], -32.0f, 32.0f) : 0.0f;
        float preL = inL;
        float preR = inR;
        // Zero-delay does not access an unwritten ring slot.
        if (preSamples >= 1.0f) {
            preL = s.pre[0].read(preSamples);
            preR = s.pre[1].read(preSamples);
        } else if (preSamples > 0.0f) {
            preL = inL * (1.0f - preSamples) + s.pre[0].read(1.0f) * preSamples;
            preR = inR * (1.0f - preSamples) + s.pre[1].read(1.0f) * preSamples;
        }
        if (s.preTapBlend < 1.0f) {
            const float next = s.preTapNext;
            float nextL = inL, nextR = inR;
            if (next >= 1.0f) {
                nextL = s.pre[0].read(next);
                nextR = s.pre[1].read(next);
            } else if (next > 0.0f) {
                nextL = inL * (1.0f - next) + s.pre[0].read(1.0f) * next;
                nextR = inR * (1.0f - next) + s.pre[1].read(1.0f) * next;
            }
            preL += s.preTapBlend * (nextL - preL);
            preR += s.preTapBlend * (nextR - preR);
        }
        s.pre[0].push(inL);
        s.pre[1].push(inR);

        // A musical, continuous Early Send morph: increase spatial early
        // crossfeed and feed its already-computed energy into the diffuser
        // at high Diffusion. Six existing taps only, zero extra Delay::read.
        // Diffusion <=0.62 preserves the original early stereo/coupling.
        const float earlyCrossfeed = 0.10f + density * 0.08f;
        float earlyL = 0.0f, earlyR = 0.0f;
        for (int t = 0; t < 6; ++t) {
            const float a = s.early[0].readPrepared(s.earlyTapL[t]);
            const float b = s.early[1].readPrepared(s.earlyTapR[t]);
            earlyL += kEarlyGain[t] * (a + earlyCrossfeed * b);
            earlyR += kEarlyGain[t] * (b + earlyCrossfeed * a);
        }
        if (texture > 0.0f) {
            float extraL=0.0f,extraR=0.0f;
            for (std::size_t t=0;t<kTextureTaps;++t) {
                extraL += kTextureGain[t]*s.early[0].readFixed(s.textureL[t]);
                extraR += kTextureGain[t]*s.early[1].readFixed(s.textureR[t]);
            }
            // Linked, symmetric crossfeed; never inject an inverted mono
            // copy. No new delay storage or feedback gain.
            earlyL += texture*(extraL+0.14f*extraR);
            earlyR += texture*(extraR+0.14f*extraL);
        }
        s.early[0].push(preL);
        s.early[1].push(preR);

        const float earlySend = 0.28f + 0.12f * density;
        const float directSend = 1.0f - earlySend;
        float diffL = preL * directSend + earlyL * earlySend;
        float diffR = preR * directSend + earlyR * earlySend;
        for (int i = 0; i < 4; ++i) {
            diffL = s.diffuser[0][i].process(diffL, g);
            diffR = s.diffuser[1][i].process(diffR, g);
        }

        float taps[kLines];
        float sumHouseholder = 0.0f;
        float tailL = 0.0f, tailR = 0.0f;
        for (std::size_t i = 0; i < kLines; ++i) {
            const float base = s.fdnBaseSamples[i] * size;
            const float modulation = modDepth * s.lfo[i].sinPhase;
            taps[i] = s.lines[i].read(base + modulation);
            if (s.sizeTapBlend < 1.0f) {
                const float nextTap = s.lines[i].read(
                    s.fdnBaseSamples[i] * s.sizeTapNext + modulation);
                taps[i] += s.sizeTapBlend * (nextTap - taps[i]);
            }
            sumHouseholder += kReflect[i] * taps[i];
            tailL += kReadL[i] * taps[i];
            tailR += kReadR[i] * taps[i];
            s.lfo[i].tick();
        }
        // H = I - 2vv'/8. Orthogonal Householder feedback: energy preserving
        // before per-line RT60 and damping; O(N), not an O(N^2) matrix multiply.
        const float householderProjection = 0.25f * sumHouseholder;
        // One modal absorption path preserves Householder O(N) feedback and
        // avoids the 8 extra filters that cost ~31% in the initial P3 trial.
        // The selected eigenmode loses low-frequency energy gradually;
        // other eigenmodes, stereo injection and dry remain untouched.
        const float bassShapedProjection = s.feedbackBass.process(
            householderProjection, s.bassCoeff, s.bassDepth);
        // Legacy Classic remains exactly the original Householder DSP.
        // Non-Classic modes use real, distinct feedback transform families.
        float feedbackA[8], feedbackB[8], feedback[8];
        if (s.fromAlgorithm==0u) {
            for (int i=0;i<8;++i)
                feedbackA[i]=taps[i]-bassShapedProjection*kReflect[i];
        } else algorithmMatrix(s.fromAlgorithm,taps,feedbackA);
        if (s.algorithmBlend<1.0f) {
            if (s.toAlgorithm==0u) {
                for (int i=0;i<8;++i)
                    feedbackB[i]=taps[i]-bassShapedProjection*kReflect[i];
            } else algorithmMatrix(s.toAlgorithm,taps,feedbackB);
            const float blend=s.algorithmBlend;
            for (int i=0;i<8;++i)
                feedback[i]=feedbackA[i]+blend*(feedbackB[i]-feedbackA[i]);
            s.algorithmBlend=std::min(1.0f,blend+s.algorithmStep);
            if (s.algorithmBlend>=1.0f) s.fromAlgorithm=s.toAlgorithm;
        } else {
            for (int i=0;i<8;++i) feedback[i]=feedbackA[i];
        }
        for (std::size_t i = 0; i < kLines; ++i) {
            s.gains[i] += s.smoother * (s.targetGains[i] - s.gains[i]);
            const float mixed = feedback[i];
            const float filtered = s.feedbackLP[i].process(mixed, s.hfFeedbackCoeff);
            // P7 calibrated, smoothed, convex high-frequency absorption.
            // Stable 0..1 loss, preserving midband FDN decay gain and all
            // orthogonal algorithm matrices without new DSP primitives.
            float damped = mixed + s.hfDampingBlend * (filtered - mixed);
            // Convex blend of identity and one-pole HP: bounded loss,
            // no positive feedback boost. Original path unchanged when off.
            if(album>0.f)damped-=albumFeedbackLoss*s.albumBass[i].process(damped,s.albumBassCoeff);
            const float injected = 0.30f * (kInjectL[i] * diffL + kInjectR[i] * diffR);
            s.lines[i].push(killDenormal(s.gains[i] * damped + injected));
        }

        // Recover audible late-space level without brightening the dry
        // channel or altering the feedback loop's internal stability.
        if(album>0.f)s.albumTone.process(inL,inR,tailL,tailR,duckAmount,album,signature);
        float wetL = tailL * s.lateReturn + earlyL * (0.42f * earlyAmount);
        float wetR = tailR * s.lateReturn + earlyR * (0.42f * earlyAmount);
        wetL = s.wetHP[0][1].process(s.wetHP[0][0].process(wetL, s.wetLowCoeff), s.wetLowCoeff);
        wetR = s.wetHP[1][1].process(s.wetHP[1][0].process(wetR, s.wetLowCoeff), s.wetLowCoeff);
        const float lpL1 = s.wetLP[0][0].process(wetL, s.wetHighCoeff);
        const float lpR1 = s.wetLP[1][0].process(wetR, s.wetHighCoeff);
        const float lpL2 = s.wetLP[0][1].process(lpL1, s.wetHighCoeff);
        const float lpR2 = s.wetLP[1][1].process(lpR1, s.wetHighCoeff);
        // Dark cutoff retains the older steep roll-off. At high cutoff,
        // recover air and sheen with smooth post-filter slope morph.
        wetL = s.airShelf[0].process(
            lpL2 + s.wetHighSlope * (lpL1 - lpL2), s.airCoeff, airLift);
        wetR = s.airShelf[1].process(
            lpR2 + s.wetHighSlope * (lpR1 - lpR2), s.airCoeff, airLift);

        // Presence shaping is a wet-only complement to existing broadband
        // ducking; no lookahead, new parameter, or FDN topology change.
        if(album>0.f)
            s.presence.process(inL,inR,wetL,wetR,duckAmount*(s.presenceDuckScale+.65f*album));
        else s.presence.process(inL, inR, wetL, wetR, duckAmount * s.presenceDuckScale);

        const float mid = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR) * width;
        const float loudness = std::max(std::fabs(inL), std::fabs(inR));
        const float envCoef = loudness > s.duckEnvelope ? s.envelopeAttack : s.envelopeRelease;
        s.duckEnvelope += envCoef * (loudness - s.duckEnvelope);
        // Reverb-Sis: let the frequency-selective presence shaper create
        // articulation space. Broadband ducking is lighter (max -3.74dB)
        // so warmth/air survive; the faster release blooms between phrases.
        // The smooth knee is level-dependent, never a gate or hidden dry.
        const float duckFraction=s.musicalReturn?s.duckEnvelope/(0.12f+s.duckEnvelope):0.f;
        const float duckGain = s.musicalReturn
            ? 1.0f - 0.35f * duckAmount * duckFraction
            : 1.0f / (1.0f + 6.0f * duckAmount * s.duckEnvelope);
        const float returnGain = wetGain * s.wetReturnTrim * (album>0.f?
            duckGain+.10f*album*duckAmount*duckFraction:duckGain);
        // Exact sine/cos dry and base wet law at stable settings, plus
        // bounded musical high-Mix compensation folded into the existing
        // wet gain target. No new sample-loop operation or feedback change.
        // 50% retains ~-3dB direct signal; 100% is still pure wet/send.
        outputLeft[frame] = dryGain * inL + returnGain * (mid + side);
        outputRight[frame] = dryGain * inR + returnGain * (mid - side);
        if (s.preTapBlend < 1.0f) {
            s.preTapBlend = std::min(1.0f, s.preTapBlend + s.delayMorphStep);
            if (s.preTapBlend >= 1.0f) s.preTapCurrent = s.preTapNext;
        }
        if (s.sizeTapBlend < 1.0f) {
            s.sizeTapBlend = std::min(1.0f, s.sizeTapBlend + s.delayMorphStep);
            if (s.sizeTapBlend >= 1.0f) s.sizeTapCurrent = s.sizeTapNext;
        }
    }
}

}  // namespace arverb
