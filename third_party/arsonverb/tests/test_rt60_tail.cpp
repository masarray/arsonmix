// P7 actual-engine sonic regression, NOT a listening-quality certificate.
// Reuse existing 8 FDN LP filters and prove high-frequency tail direction
// across all 4 algorithms, 2 room sizes and 2 decay times.
#include <arverb/Reverb.hpp>
#include "../src/dsp/Primitives.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool good, const char* detail) {
    if (!good) { std::fprintf(stderr, "P7 TAIL FAILURE: %s\n", detail); std::abort(); }
}
struct SpectralBand {
    arverb::detail::LowPass low[2], high[2];
    float a=0.0f,b=0.0f;
    explicit SpectralBand(float fs) {
        a=arverb::detail::lowPassCoeff(2600.0f,fs);
        b=arverb::detail::lowPassCoeff(6200.0f,fs);
    }
    double add(float l,float r) {
        const float x=high[0].process(l,b)-low[0].process(l,a);
        const float y=high[1].process(r,b)-low[1].process(r,a);
        return double(x)*x+double(y)*y;
    }
};
double tailEnergy(unsigned mode,float decay,float size,float damping, int block) {
    constexpr int fs=48000;
    constexpr int total=fs*3;
    arverb::Engine engine;
    engine.loadPreset(arverb::Preset::VocalPlateClean);
    engine.setAlgorithm(static_cast<arverb::Algorithm>(mode));
    engine.setParameter(arverb::Parameter::Mix,1.0f);
    engine.setParameter(arverb::Parameter::DuckAmount,0.0f);
    engine.setParameter(arverb::Parameter::DecaySeconds,decay);
    engine.setParameter(arverb::Parameter::Size,size);
    engine.setParameter(arverb::Parameter::DampingHz,damping);
    engine.setParameter(arverb::Parameter::HighCutHz,20000.0f);
    engine.setParameter(arverb::Parameter::LowCutHz,30.0f);
    engine.setParameter(arverb::Parameter::ModDepthMs,0.1f);
    engine.prepare(fs);
    std::array<float,257> inL{},inR{},outL{},outR{};
    SpectralBand band{static_cast<float>(fs)};
    double energy=0.0;
    for(int pos=0;pos<total;pos+=block) {
        const auto n=static_cast<std::size_t>(std::min(block,total-pos));
        inL.fill(0.0f);inR.fill(0.0f);
        if(pos==0) {inL[0]=0.65f;inR[0]=0.47f;}
        engine.process(inL.data(),inR.data(),outL.data(),outR.data(),n);
        for(std::size_t i=0;i<n;++i) {
            check(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                  "finite audio all algorithms, decay and damping");
            check(std::abs(outL[i])<4.0f && std::abs(outR[i])<4.0f,
                  "bounded output under full wet impulse");
            const double sampleEnergy=band.add(outL[i],outR[i]);
            const int t=pos+static_cast<int>(i);
            if(t>=fs*3/4 && t<fs*7/4) energy+=sampleEnergy;
        }
    }
    return energy;
}
}
int main() {
    for(unsigned mode=0;mode<4;++mode) {
        for(float size:{0.85f,1.65f}) {
            for(float decay:{1.4f,5.0f}) {
                const double dark=tailEnergy(mode,decay,size,1300.0f,127);
                const double airy=tailEnergy(mode,decay,size,16500.0f,127);
                std::printf("P7 mode=%u size=%.2f decay=%.2f darkHF=%.9g brightHF=%.9g ratio=%.3f\n",
                            mode,size,decay,dark,airy,airy/(dark+1.e-30));
                check(std::isfinite(dark) && std::isfinite(airy) && airy>1.e-17,
                      "meaningful high-band tail exists");
                check(airy>dark*1.01,
                      "brighter damping must retain more high-frequency late energy");
            }
        }
    }
    // Even the largest room and slowest tail must remain bounded.
    for(unsigned mode=0;mode<4;++mode) {
        const double longTail=tailEnergy(mode,12.0f,2.0f,18000.0f,257);
        check(std::isfinite(longTail) && longTail<30.0,
              "12-second longest room finite and stable");
    }
    std::puts("PASS P7: 4 modes, 2 sizes, 2 decays, HF damping order, maximum tail stability");
}
