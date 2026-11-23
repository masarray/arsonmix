// P5: user-reported disappearing Mix and airless wet signal.
// Numerical gates: dry onset, 50% constant-power law, wet existence,
// mono fold, controlled shelf frequency selectivity and sweep continuity.
// Sound quality still needs real user listening and host validation.
#include <arverb/Reverb.hpp>
#include "../src/dsp/Primitives.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void check(bool ok,const char* why) {
    if (!ok) {std::fprintf(stderr,"MIX/AIR FAILURE: %s\n",why);std::abort();}
}
static double shelfGain(float hz,float air,float fs) {
    arverb::detail::AirShelf shelf;
    const float coefficient=arverb::detail::lowPassCoeff(7800.0f,fs);
    double before=0,after=0;
    for(int i=0;i<48000;++i) {
        float s=0.3f*std::sin(2.0f*arverb::detail::kPi*hz*i/fs);
        float out=shelf.process(s,coefficient,air);
        if(i>24000) {before+=s*s;after+=out*out;}
    }
    return std::sqrt(after/before);
}
static double render(int mode,float mix,bool mono) {
    arverb::Engine e;
    e.loadPreset(arverb::Preset::VocalPlate);
    e.setAlgorithm(static_cast<arverb::Algorithm>(mode));
    e.setParameter(arverb::Parameter::Mix,mix);
    e.setParameter(arverb::Parameter::DuckAmount,0.0f);
    e.prepare(48000.0);
    std::array<float,128> l{},r{},a{},b{};
    double sum=0.0;
    for(int block=0;block<600;++block) {
        for(int i=0;i<128;++i) {
            float t=static_cast<float>(block*128+i);
            l[i]=0.25f*std::sin(t*0.061f)+0.09f*std::sin(t*0.72f);
            r[i]=mono?l[i]:0.22f*std::sin(t*0.041f+0.7f);
        }
        e.process(l.data(),r.data(),a.data(),b.data(),128);
        for(int i=0;i<128;++i) {
            check(std::isfinite(a[i])&&std::isfinite(b[i]),"finite all mixes/modes");
            if(block>200) {
                const float collapsed=0.5f*(a[i]+b[i]);
                sum+=static_cast<double>(collapsed)*collapsed;
            }
        }
    }
    return std::sqrt(sum/(399.0*128.0));
}
// P8 real-engine tone probes: gate the final wet spectrum, not only a
// disconnected shelf primitive. Measurements are RMS, not listening scores.
static double wetTone(int mode, float hz, float highCut) {
    arverb::Engine e;
    e.setAlgorithm(static_cast<arverb::Algorithm>(mode));
    e.setParameter(arverb::Parameter::Mix, 1.0f);
    e.setParameter(arverb::Parameter::PreDelayMs, 0.0f);
    e.setParameter(arverb::Parameter::DecaySeconds, 2.3f);
    e.setParameter(arverb::Parameter::DampingHz, 16500.0f);
    e.setParameter(arverb::Parameter::HighCutHz, highCut);
    e.setParameter(arverb::Parameter::DuckAmount, 0.0f);
    e.setParameter(arverb::Parameter::ModDepthMs, 0.0f);
    e.prepare(48000.0);
    std::array<float,128> l{},r{},a{},b{};
    double energy=0.0;
    for(int block=0;block<360;++block) {
        for(int j=0;j<128;++j) {
            const float phase=2.0f*arverb::detail::kPi*hz*
                static_cast<float>(block*128+j)/48000.0f;
            l[j]=r[j]=0.18f*std::sin(phase);
        }
        e.process(l.data(),r.data(),a.data(),b.data(),128);
        if (block>=160) for(int j=0;j<128;++j)
            energy+=static_cast<double>(a[j])*a[j]+static_cast<double>(b[j])*b[j];
    }
    return std::sqrt(energy/(200.0*128.0*2.0));
}

int main() {
    // These must use the exact gain law; with 36ms predelay, the first
    // sample after prepare is dry only, not phase-cancelled wet energy.
    for(float mix:{0.f,0.5f,1.f}) {
        arverb::Engine e;
        e.setParameter(arverb::Parameter::Mix,mix);
        e.setParameter(arverb::Parameter::PreDelayMs,60.f);
        e.prepare(48000.0);
        float l=0.5f,r=0.3f,a=0,b=0;
        e.process(&l,&r,&a,&b,1);
        const float expected=mix==0.f?1.f:mix==0.5f?0.70710678f:0.f;
        check(std::abs(a-0.5f*expected)<0.00005f,
              "50% Mix must retain -3dB dry, not old -6dB dip");
        check(std::abs(b-0.3f*expected)<0.00005f,
              "Mix gain matched L and R at wet onset");
    }
    check(shelfGain(10500.f,0.5f,48000.f)>
          shelfGain(250.f,0.5f,48000.f)+0.12,
          "wet air shelf lifts high frequency more than bass");
    check(std::abs(shelfGain(8000.f,0.f,48000.f)-1.0)<1e-8,
          "air shelf at zero lift exactly transparent");

    for(int mode=0;mode<4;++mode) {
        const double dry=render(mode,0.f,false);
        check(dry>0.07,"healthy stereo dry benchmark");
        double prev=render(mode,0.f,true);
        for(float mix:{0.25f,0.5f,0.75f,1.f}) {
            const double rms=render(mode,mix,true);
            std::printf("mode=%d mix=%.2f collapsed_mono_rms=%.6f dry=%.6f\n",
                        mode,mix,rms,dry);
            check(rms>0.014,"wet-heavy Mix should not go near silence in mono");
            check(rms<dry*3.4,"wet-heavy Mix must avoid excessive output gain");
            check(std::fabs(rms-prev)<dry*1.5,
                  "Mix sweep no extreme abrupt level jump");
            prev=rms;
        }
    }
    // P8: prepare() must prime coefficients, not defer expensive
    // initialization to the very first small buffer; with static targets,
    // samplewise and large-block engines must generate the same output.
    {
        arverb::Engine full, split;
        full.setAlgorithm(arverb::Algorithm::Plate);
        split.setAlgorithm(arverb::Algorithm::Plate);
        full.setParameter(arverb::Parameter::PreDelayMs,0.0f);
        split.setParameter(arverb::Parameter::PreDelayMs,0.0f);
        full.prepare(48000.0);
        split.prepare(48000.0);
        std::array<float,2048> il{},ir{},fl{},fr{},sl{},sr{};
        for(std::size_t i=0;i<il.size();++i) {
            il[i]=0.15f*std::sin(static_cast<float>(i)*0.037f);
            ir[i]=-0.12f*std::sin(static_cast<float>(i)*0.043f);
        }
        full.process(il.data(),ir.data(),fl.data(),fr.data(),il.size());
        for(std::size_t i=0;i<il.size();++i)
            split.process(&il[i],&ir[i],&sl[i],&sr[i],1);
        for(std::size_t i=0;i<il.size();++i)
            check(std::abs(fl[i]-sl[i])<1e-6f &&
                  std::abs(fr[i]-sr[i])<1e-6f,
                  "cold-start one-sample versus 2048-sample buffers diverged");
    }
    // HighCut should determine whether top air exists. No free-running
    // noise, dry boost, pseudo-shimmer or bypass of the user's dark cutoff.
    for(int mode : {0,1,3}) {
        const double highDark=wetTone(mode,9800.0f,4000.0f);
        const double highOpen=wetTone(mode,9800.0f,14500.0f);
        const double lowDark=wetTone(mode,500.0f,4000.0f);
        const double lowOpen=wetTone(mode,500.0f,14500.0f);
        check(highDark>1e-8 && lowDark>1e-8, "measurable wet frequency probe");
        const double highRatio=highOpen/highDark;
        const double lowRatio=lowOpen/lowDark;
        std::printf("P8 mode=%d wet air highRatio=%.3f lowRatio=%.3f\\n",
                    mode,highRatio,lowRatio);
        check(highRatio>lowRatio*1.12,
              "brightening open HighCut must favor top air over body");
        check(highOpen<2.5, "wet top-air output must remain bounded");
    }
    std::puts("PASS P8: equal-power Mix, cold-block invariance, wet top-air selectivity");
}
