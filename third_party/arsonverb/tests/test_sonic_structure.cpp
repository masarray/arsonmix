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
// Isolate the first added reflection before the legacy 14.9ms
// early onset and before even the shortest FDN tap at Size=2.
// A real engine impulse proves the new wet energy is not just gain.
static double earlyTextureEnergy(double fs,float diffusion,bool musical) {
    arverb::Engine e;
    e.loadPreset(arverb::Preset::VocalPlateClean);
    e.setParameter(arverb::Parameter::Mix,1.f);
    e.setParameter(arverb::Parameter::PreDelayMs,0.f);
    e.setParameter(arverb::Parameter::Size,2.f);
    e.setParameter(arverb::Parameter::Diffusion,diffusion);
    e.setParameter(arverb::Parameter::EarlyLevel,1.f);
    e.setParameter(arverb::Parameter::DuckAmount,0.f);
    e.prepare(fs,musical);
    std::array<float,127> l{},r{},a{},b{};
    double energy=0.0;
    for(int pos=0;pos<static_cast<int>(.05*fs);pos+=127) {
        l.fill(0.f);r.fill(0.f);
        if(pos==0)l[0]=r[0]=.3f;
        e.process(l.data(),r.data(),a.data(),b.data(),127);
        for(int i=0;i<127;++i) {
            const double ms=1000.0*(pos+i)/fs;
            check(std::isfinite(a[i])&&std::isfinite(b[i]),"finite early texture");
            if(ms<7.0)check(std::abs(a[i])+std::abs(b[i])<1.e-6,
                            "new wet onset must not leak dry");
            if(ms>=7.0&&ms<14.0)energy+=double(a[i])*a[i]+double(b[i])*b[i];
        }
    }
    return energy;
}
int main() {
    for(double rate:{8000.,44100.,48000.,96000.,192000.}) {
        const auto low=earlyTextureEnergy(rate,.62f,true);
        const auto legacy=earlyTextureEnergy(rate,1.f,false);
        const auto middle=earlyTextureEnergy(rate,.81f,true);
        const auto high=earlyTextureEnergy(rate,1.f,true);
        check(low<1.e-12&&legacy<1.e-12,"low/legacy onset unchanged");
        check(middle>1.e-7&&high>middle*3.8&&high<middle*4.2,
              "diffusion smoothly increases real independent early reflection");
        std::printf("Reverb-Sis onset fs=%.0f low=%.9g medium=%.9g high=%.9g\n",
                    rate,low,middle,high);
    }
    // Actual voiced phrase/pause PCM at normal insert Mix, across low
    // and high fundamentals, all algorithms and typical ASIO sample rates.
    // This is numerical headroom/continuity evidence, not a listening score.
    for(double rate:{44100.,48000.,96000.}) for(unsigned mode=0;mode<4;++mode) {
        arverb::Engine e;
        e.loadPreset(arverb::Preset::VocalPlateClean);
        e.setAlgorithm(static_cast<arverb::Algorithm>(mode));
        e.setParameter(arverb::Parameter::Mix,.34f);
        e.setParameter(arverb::Parameter::Diffusion,.94f);
        e.setParameter(arverb::Parameter::LowCutHz,150.f);
        e.setParameter(arverb::Parameter::EarlyLevel,.36f);
        e.setParameter(arverb::Parameter::DuckAmount,.18f);
        e.prepare(rate,true);
        constexpr int n=128;
        std::array<float,n> l{},r{},a{},b{};
        double peak=0.0,tail=0.0,stereo=0.0;
        const int blocks=static_cast<int>(rate*2.5/n);
        for(int block=0;block<blocks;++block) {
            for(int i=0;i<n;++i) {
                const double time=(block*n+i)/rate;
                const double phase=time<.9?time*110.0:.9*110.0+(time-.9)*330.0;
                const double gate=time<1.5?std::min(1.0,std::min(time,1.5-time)*40.0):0.0;
                double x=0;
                for(int h=1;h<=8;++h)
                    x+=(.085/h)*std::sin(6.283185307179586*phase*h+.31*h*h);
                l[i]=r[i]=static_cast<float>(gate*x);
            }
            e.process(l.data(),r.data(),a.data(),b.data(),n);
            for(int i=0;i<n;++i) {
                check(std::isfinite(a[i])&&std::isfinite(b[i]),"finite phrase");
                peak=std::max(peak,std::max(std::abs(double(a[i])),std::abs(double(b[i]))));
                if((block*n+i)/rate>1.7) {
                    const double mid=.5*(a[i]+b[i]),side=.5*(a[i]-b[i]);
                    tail+=mid*mid;stereo+=mid*mid+side*side;
                }
            }
        }
        check(peak<1.0&&tail>1.e-7&&tail>.08*stereo,
              "nominal phrase headroom, audible post-phrase mono tail");
        std::printf("Reverb-Sis phrase fs=%.0f mode=%u peak=%.4f tailMidFraction=%.3f\n",
                    rate,mode,peak,tail/stereo);
    }
    std::puts("PASS Reverb-Sis natural density and vocal phrase headroom");
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
