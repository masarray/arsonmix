// P9 regression: bounded callback coefficient math, artifact-free fixed-tap
// transitions and rapid preset automation in the actual reusable DSP core.
#include <arverb/Reverb.hpp>
#include "../src/dsp/Primitives.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char* why) {
    if (!condition) { std::fprintf(stderr, "P9 PRESET FAIL: %s\n", why); std::abort(); }
}
void init(arverb::Engine& e) {
    e.setParameter(arverb::Parameter::Mix, 1.0f);
    e.setParameter(arverb::Parameter::PreDelayMs, 0.0f);
    e.setParameter(arverb::Parameter::Size, 0.80f);
    e.setParameter(arverb::Parameter::DecaySeconds, 5.0f);
    e.setParameter(arverb::Parameter::Diffusion, 0.80f);
    e.prepare(48000.0);
}
float sample(int n) noexcept {
    return .19f * std::sin(6.283185307179586f * 997.0f * n / 48000.0f)
         + .045f * std::sin(6.283185307179586f * 3327.0f * n / 48000.0f);
}
void prime(arverb::Engine& e) {
    std::array<float,64> in{}, a{}, b{};
    for (int base=0; base<24000; base+=64) {
        for (int i=0;i<64;i++) in[i]=sample(base+i);
        e.process(in.data(),in.data(),a.data(),b.data(),64);
    }
}
} // namespace
int main() {
    // Libm replacement must preserve gain/filter response to < -110dB.
    for (int i=0;i<=8000;i++) {
        const float x = float(i) * 0.001f;
        require(std::abs(arverb::detail::fastExpNeg(x) - std::exp(-x)) <= 2.5e-6f,
                "fast exp accuracy over entire supported domain");
    }
    for (float fs : {8000.0f, 44100.0f, 48000.0f, 96000.0f, 192000.0f}) {
        for (float rate : {0.04f, 0.3f, 1.5f, 2.98f}) {
            arverb::detail::Oscillator oscillator;
            oscillator.setFrequency(rate,fs);
            const float phase=2.0f*arverb::detail::kPi*rate/fs;
            require(std::abs(oscillator.sinStep-std::sin(phase))<2e-7f
                && std::abs(oscillator.cosStep-std::cos(phase))<2e-7f,
                "small-angle LFO recurrence step accuracy");
        }
    }
    // Both instances have identical state. On the first frame after a delay
    // target switch, the moving-tap implementation must still read the OLD
    // tap (blend begins at zero), not teleport to the new read position.
    for (const auto parameter : {arverb::Parameter::PreDelayMs, arverb::Parameter::Size}) {
        arverb::Engine oldEngine, switched;
        init(oldEngine);
        init(switched);
        prime(oldEngine);
        prime(switched);
        switched.setParameter(parameter, parameter==arverb::Parameter::Size ? 2.0f : 210.0f);
        float in=sample(24000), oldL=0,oldR=0,newL=0,newR=0;
        oldEngine.process(&in,&in,&oldL,&oldR,1);
        switched.process(&in,&in,&newL,&newR,1);
        require(std::isfinite(newL) && std::isfinite(newR),
                "first switched-tap frame must remain finite");
        require(std::abs(oldL-newL)<8e-4f && std::abs(oldR-newR)<8e-4f,
                "preset delay/size must not jump on first ASIO sample");
        std::array<float,64> inputs{},outL{},outR{};
        for(int block=0;block<120;block++){
            for(int i=0;i<64;i++)inputs[i]=sample(24001+block*64+i);
            switched.process(inputs.data(),inputs.data(),outL.data(),outR.data(),64);
            for(int i=0;i<64;i++)
                require(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                        "70ms tap morph must never produce NaN/Inf");
        }
    }
    // Last-target wins while several changes arrive before the 70ms fade
    // finishes. Exercise all four modes and seven possible DSP buffer sizes.
    arverb::Engine switcher;
    init(switcher);
    prime(switcher);
    constexpr std::array<int,7> sizes{{1,3,17,32,64,128,256}};
    std::array<float,256> in{},outL{},outR{};
    int position=24000;
    for(int b=0;b<900;b++){
        const int n=sizes[static_cast<std::size_t>(b)%sizes.size()];
        if((b%2)==0){
            const auto preset=static_cast<arverb::Preset>((b*13)%int(arverb::presetCount));
            switcher.loadPreset(preset);
            switcher.setAlgorithm(arverb::presetAlgorithm(preset));
        }
        for(int i=0;i<n;i++)in[i]=sample(position+i);
        switcher.process(in.data(),in.data(),outL.data(),outR.data(),static_cast<std::size_t>(n));
        for(int i=0;i<n;i++){
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                    "rapid musical preset switching must stay finite");
            require(std::abs(outL[i])<32.0f && std::abs(outR[i])<32.0f,
                    "rapid musical preset switching must remain bounded");
        }
        position+=n;
    }
    std::puts("PASS P9: fast coefficients + first-frame continuity + rapid presets");
}
