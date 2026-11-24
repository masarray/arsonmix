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
#include <vector>

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
// Compare the actual late-only PCM to the legacy tank with identical
// wet HP coefficients. Low diffusion disables the extra early texture,
// and modes 1..3 do not use the Classic modal bass contour, so matching
// the coefficients isolates the new output scale without changing the tank.
static void lateReturnReference(double fs,unsigned mode,float decay) {
    arverb::Engine legacy,musical;
    const float rate=static_cast<float>(fs);
    constexpr float cutoff=150.f;
    const float coeff=arverb::detail::musicalWetLowCutCoeff(cutoff,rate);
    const float equivalent=-std::log1p(-coeff)*rate/(2.f*arverb::detail::kPi);
    for(auto* e:{&legacy,&musical}) {
        e->loadPreset(arverb::Preset::VocalPlate);
        e->setAlgorithm(static_cast<arverb::Algorithm>(mode));
        e->setParameter(arverb::Parameter::Mix,1.f);
        e->setParameter(arverb::Parameter::PreDelayMs,0.f);
        e->setParameter(arverb::Parameter::DecaySeconds,decay);
        e->setParameter(arverb::Parameter::Diffusion,.62f);
        e->setParameter(arverb::Parameter::LowCutHz,e==&legacy?equivalent:cutoff);
        e->setParameter(arverb::Parameter::HighCutHz,5500.f); // isolate late gain from musical air
        e->setParameter(arverb::Parameter::EarlyLevel,0.f);
        e->setParameter(arverb::Parameter::DuckAmount,0.f);
        e->setParameter(arverb::Parameter::ModDepthMs,0.f);
        e->prepare(fs,e==&musical);
    }
    std::array<float,127> l{},r{},a{},b{},c{},d{};
    double base=0,actual=0;
    for(int pos=0;pos<static_cast<int>(2*fs);pos+=127) {
        l.fill(0.f);r.fill(0.f);
        if(pos==0){l[0]=.4f;r[0]=.3f;}
        legacy.process(l.data(),r.data(),a.data(),b.data(),127);
        musical.process(l.data(),r.data(),c.data(),d.data(),127);
        for(int i=0;i<127;++i) {
            check(std::isfinite(c[i])&&std::isfinite(d[i]),"finite calibrated late tail");
            base+=double(a[i])*a[i]+double(b[i])*b[i];
            actual+=double(c[i])*c[i]+double(d[i])*d[i];
        }
    }
    const double expected=decay<1.7f?std::min(1.5,std::sqrt(1.7/decay)):
        decay>3.5f?std::max(.89,std::sqrt(3.5/decay)):1.0;
    check(base>1.e-10,"reference late energy measurable");
    const double ratio=std::sqrt(actual/base)/3.0;
    std::printf("Reverb-Sis late fs=%.0f mode=%u decay=%.2f gain=%.4f expected=%.4f\n",
                fs,mode,decay,ratio,expected);
    check(std::abs(ratio-expected)<.015,"late output matches calibrated gain independent of tank");
}

// Offline Schroeder energy decay curve (stereo total energy), not a
// subjective score or an ISO room measurement. Estimate T20 by the
// -5/-25dB crossing difference after the onset. Use a sufficiently long
// capture to avoid truncating the longer decay's integration.
static double estimateT20(double fs,unsigned mode,float decay,float size) {
    arverb::Engine e;
    e.loadPreset(arverb::Preset::VocalPlateClean);
    e.setAlgorithm(static_cast<arverb::Algorithm>(mode));
    e.setParameter(arverb::Parameter::Mix,1.f);
    e.setParameter(arverb::Parameter::PreDelayMs,0.f);
    e.setParameter(arverb::Parameter::DecaySeconds,decay);
    e.setParameter(arverb::Parameter::Size,size);
    e.setParameter(arverb::Parameter::Diffusion,.94f);
    e.setParameter(arverb::Parameter::EarlyLevel,0.f);
    e.setParameter(arverb::Parameter::DuckAmount,0.f);
    e.prepare(fs,true);
    const int total=static_cast<int>(fs*5.0);
    std::vector<double> energy(static_cast<std::size_t>(total));
    std::array<float,257> l{},r{},a{},b{};
    for(int pos=0;pos<total;pos+=257) {
        l.fill(0.f);r.fill(0.f);
        if(pos==0){l[0]=.4f;r[0]=.3f;}
        const int n=std::min(257,total-pos);
        e.process(l.data(),r.data(),a.data(),b.data(),static_cast<std::size_t>(n));
        for(int i=0;i<n;++i) {
            check(std::isfinite(a[i])&&std::isfinite(b[i]),"finite measured EDC");
            energy[static_cast<std::size_t>(pos+i)]=double(a[i])*a[i]+double(b[i])*b[i];
        }
    }
    for(int i=total-2;i>=0;--i)
        energy[static_cast<std::size_t>(i)]+=energy[static_cast<std::size_t>(i+1)];
    check(energy[0]>1.e-9,"measurable integrated impulse response");
    const auto crossing=[&](double db) {
        const double threshold=energy[0]*std::pow(10.0,db/10.0);
        for(int i=0;i<total;++i)if(energy[static_cast<std::size_t>(i)]<=threshold)return i/fs;
        return -1.0;
    };
    const double a5=crossing(-5),a25=crossing(-25);
    check(a5>=0&&a25>a5,"valid energy decay crossings");
    return 3.0*(a25-a5);
}
int main() {
    check(std::abs(arverb::detail::musicalLateReturnScale(.15f)-1.5f)<1.e-6f &&
          arverb::detail::musicalLateReturnScale(1.7f)==1.f &&
          arverb::detail::musicalLateReturnScale(3.5f)==1.f &&
          std::abs(arverb::detail::musicalLateReturnScale(12.f)-.89f)<1.e-6f,
          "bounded late calibration and preserved vocal decay window");
    for(double fs:{44100.,48000.,96000.})
        for(unsigned mode=1;mode<4;++mode)
            for(float decay:{.65f,1.7f,2.45f,6.f})
                lateReturnReference(fs,mode,decay);
    for(double fs:{44100.,48000.,96000.}) for(unsigned mode=0;mode<4;++mode)
        for(float size:{.85f,1.65f}) {
            const double shortT=estimateT20(fs,mode,.9f,size);
            const double longT=estimateT20(fs,mode,3.2f,size);
            std::printf("Reverb-Sis T20-extrap fs=%.0f mode=%u size=%.2f short=%.3f long=%.3f\n",
                        fs,mode,size,shortT,longT);
            check(shortT>.05&&longT>shortT*1.4&&longT<9.6,
                  "actual tail decay grows with decay control");
        }
    std::puts("PASS Reverb-Sis late gain calibration and measured energy decay ordering");
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
