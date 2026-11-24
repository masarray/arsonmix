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
static double render(int mode,float mix,bool mono,bool musical=false,float duck=0.f,
                     bool broadband=false,float width=-1.f) {
    arverb::Engine e;
    e.loadPreset(arverb::Preset::VocalPlate);
    e.setAlgorithm(static_cast<arverb::Algorithm>(mode));
    e.setParameter(arverb::Parameter::Mix,mix);
    e.setParameter(arverb::Parameter::DuckAmount,duck);
    if(width>=0.f) e.setParameter(arverb::Parameter::Width,width);
    e.prepare(48000.0,musical);
    std::array<float,128> l{},r{},a{},b{};
    double sum=0.0;
    for(int block=0;block<600;++block) {
        for(int i=0;i<128;++i) {
            float t=static_cast<float>(block*128+i);
            l[i]=0.25f*std::sin(t*0.061f)+0.09f*std::sin(t*0.72f);
            if(broadband) {
                // Deterministic harmonic-rich voiced proxy; not a human
                // listening test, and no external/licensed audio fixture.
                float voice=0.f;
                for(int h=1;h<=24;++h)
                    voice+=(0.23f/static_cast<float>(h))*
                        std::sin(t*0.0242f*h + 0.37f*h*h);
                l[i]=voice;
            }
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

// Independent complex magnitude formula for the real HP primitive.
// A pair of equal stages has amplitude equal to the single-stage power.
static double cascadeAmplitude(double coeff,double hz,double fs) {
    const double pole=1.0-coeff;
    const double s=std::sin(3.14159265358979323846*hz/fs);
    const double d=4.0*s*s;
    return pole*pole*d/(coeff*coeff+pole*d);
}
static double bodyTone(int mode,double fs,float cutoff,bool musical) {
    arverb::Engine engine;
    engine.loadPreset(arverb::Preset::VocalPlate);
    engine.setAlgorithm(static_cast<arverb::Algorithm>(mode));
    engine.setParameter(arverb::Parameter::LowCutHz,cutoff);
    // Isolate filter transfer: at <=.62 both profiles use identical
    // diffusion/injection; high-density texture has its own PCM test.
    engine.setParameter(arverb::Parameter::Diffusion,.62f);
    engine.setParameter(arverb::Parameter::Mix,1.f);
    engine.setParameter(arverb::Parameter::DuckAmount,0.f);
    engine.setParameter(arverb::Parameter::ModDepthMs,0.f);
    engine.prepare(fs,musical);
    std::array<float,128> l{},r{},a{},b{};
    const int blocks=static_cast<int>(3.0*fs/128);
    double energy=0.0;
    for(int block=0;block<blocks;++block) {
        for(int j=0;j<128;++j)
            l[j]=r[j]=static_cast<float>(.12*std::sin(
                6.283185307179586*cutoff*(block*128+j)/fs));
        engine.process(l.data(),r.data(),a.data(),b.data(),128);
        if(block>=blocks*2/3) for(int j=0;j<128;++j) {
            check(std::isfinite(a[j])&&std::isfinite(b[j]),"finite body tone");
            energy+=double(a[j])*a[j]+double(b[j])*b[j];
        }
    }
    return std::sqrt(energy/(2.0*128*(blocks-blocks*2/3)));
}
int main() {
    // Verify the actual discrete-time knee, not an analog approximation.
    // Preserve sub-cutoff rejection and a monotonic high-pass response.
    for(float fs:{8000.f,44100.f,48000.f,96000.f,192000.f}) {
        for(float cutoff:{30.f,150.f,260.f,1200.f}) {
            const float c=arverb::detail::musicalWetLowCutCoeff(cutoff,fs);
            const double knee=cascadeAmplitude(c,cutoff,fs);
            check(c>0.f&&c<1.f&&std::abs(knee-.70710678118)<.0003,
                  "musical low-cut displayed frequency is actual -3dB knee");
            // At the extreme 8k/1200Hz setting the digital knee is near
            // Nyquist relative to a vocal cutoff; use its computed bound.
            const double quarterLimit=fs==8000.f&&cutoff==1200.f?.35:.18;
            check(cascadeAmplitude(c,cutoff*.25,fs)<quarterLimit &&
                  cascadeAmplitude(c,cutoff*2.0,fs)>knee,
                  "retain low-frequency rejection and monotonic response");
        }
    }
    // At the cutoff itself, compare real wet engine PCM with independent
    // legacy profile, removing its known 3x full-wet gain difference.
    // Identical tank and zero duck/modulation isolate the filter response.
    for(double fs:{44100.0,48000.0,96000.0}) for(int mode=0;mode<4;++mode) {
        constexpr float cutoff=150.f;
        const double legacy=bodyTone(mode,fs,cutoff,false);
        const double musical=bodyTone(mode,fs,cutoff,true);
        const double expected=.70710678118/cascadeAmplitude(
            arverb::detail::lowPassCoeff(cutoff,static_cast<float>(fs)),cutoff,fs);
        check(legacy>1.e-6&&musical>1.e-6,"measurable vocal body at cutoff");
        const double ratio=musical/(3.0*legacy);
        std::printf("Reverb-Sis body fs=%.0f mode=%d normalized=%.3f expected=%.3f\n",
                    fs,mode,ratio,expected);
        check(std::abs(ratio/expected-1.0)<.04,
              "actual musical wet body follows calibrated knee");
    }
    std::puts("PASS Reverb-Sis body calibration: discrete knee and actual 4-mode wet PCM");
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
    // Musical profile: actual mono-fold output, with nonzero ducking.
    // Full-wet profile retains the 3x gain law, calibrated low-cut
    // and independent high-diffusion texture. Cross-profile RMS is not
    // a pure scalar; preserve the measured level floor and headroom.
    // The exact live Mix sum is still independently tested below.
    for(int mode=0;mode<4;++mode) {
        const double oldWet=render(mode,1.f,true,false,0.f);
        const double newWet=render(mode,1.f,true,true,0.f);
        const double sung=render(mode,1.f,true,true,.22f);
        const double dry=render(mode,0.f,true,true,0.f);
        std::printf("Reverb-Sis mode=%d dry=%.6f oldWet=%.6f wet=%.6f ducked=%.6f\n",
                    mode,dry,oldWet,newWet,sung);
        check(newWet/oldWet>2.95&&newWet/oldWet<6.0,
              "full-wet return retains gain improvement and bounded vocal body");
        check(sung>newWet*.65,"normal duck cannot erase the wet return");
        check(sung>dry*.12,"audible wet-heavy mono fold with real duck");
        const double voiceDry=render(mode,0.f,true,true,.22f,true);
        const double voiceWet=render(mode,1.f,true,true,.22f,true);
        const double voiceLegacy=render(mode,1.f,true,false,.43f,true);
        std::printf("Reverb-Sis mode=%d harmonic RMS wet/dry=%.3f new/old=%.3f\n",
                    mode,voiceWet/voiceDry,voiceWet/voiceLegacy);
        check(voiceWet>voiceLegacy*1.7,"harmonic sustained source improves versus old ducked return");
        // Verified e00e96d1 baseline: .543/.882/.453/.339. Preserve
        // the .30 fixture regression floor across the spectral refinement;
        // this is NOT a universal loudness-match target.
        check(voiceWet>voiceDry*.30 && voiceWet<voiceDry*3.4,
              "harmonic sustained source must preserve measured wet energy in mono");
        // Width must change the side component only; neither mono playback
        // nor a stereo-to-mono downstream sink may lose the mid return.
        const double narrow=render(mode,1.f,true,true,.22f,true,0.f);
        const double wide=render(mode,1.f,true,true,.22f,true,1.6f);
        std::printf("Reverb-Sis mode=%d mono width0=%.6f width1.6=%.6f\n",
                    mode,narrow,wide);
        check(narrow>voiceDry*.30 && std::abs(wide-narrow)<voiceDry*1e-5,
              "width must preserve actual mono wet RMS at both endpoints");
        for(float mix:{.25f,.5f,.75f,1.f}) {
            const double rms=render(mode,mix,true,true,.22f);
            check(rms>dry*.12 && rms<dry*3.4,"musical Mix no severe sustained level collapse");
        }
    }
    // Exercise actual PCM during live Mix changes, not only freshly
    // prepared steady states. Independent pure-wet musical tank is the
    // reference (same calibrated cutoff, excitation, algorithm and Duck=0).
    // Normalize by its 1.5x endpoint wet gain; the exact dry/wet sum test
    // remains independent of the tank's spectral color. This
    // verifies the whole dry/wet sum, including no hidden dry at 100%.
    for(const double fs : {44100.0,48000.0,96000.0}) {
        for(int mode=0;mode<4;++mode) {
            arverb::Engine reference, musical;
            for(auto* engine : {&reference,&musical}) {
                engine->loadPreset(arverb::Preset::VocalPlate);
                engine->setAlgorithm(static_cast<arverb::Algorithm>(mode));
                engine->setParameter(arverb::Parameter::DuckAmount,0.f);
                engine->setParameter(arverb::Parameter::Mix,engine==&reference?1.f:.28f);
            }
            reference.prepare(fs,true);
            musical.prepare(fs,true);
            constexpr int n=128;
            std::array<float,n> l{},r{},a{},b{},c{},d{};
            const auto gain=[](float mix) {
                const double x=std::clamp(2.0*mix-1.0,0.0,1.0);
                return std::sin(1.5707963267948966*mix)*(1.0+0.5*x*x*(3.0-2.0*x));
            };
            const double alpha=1.0-std::exp(-1.0/(.025*fs));
            double dryGain=std::cos(1.5707963267948966*.28f),wetGain=gain(.28f);
            double dryTarget=dryGain,wetTarget=wetGain;
            const int stage=static_cast<int>(fs*.6/n);
            for(int block=0;block<4*stage;++block) {
                if(block%stage==0) {
                    const float mix=block==0?.28f:block==stage?.75f:block==2*stage?1.f:0.f;
                    musical.setParameter(arverb::Parameter::Mix,mix);
                    dryTarget=std::cos(1.5707963267948966*mix);
                    wetTarget=gain(mix);
                }
                for(int i=0;i<n;++i) {
                    const double time=(block*n+i)/fs;
                    // Independent stereo harmonic inputs with a common body.
                    l[i]=static_cast<float>(.16*std::sin(691.15*time)+.07*std::sin(2073.45*time));
                    r[i]=static_cast<float>(.14*std::sin(691.15*time)+.06*std::sin(2638.94*time+.7));
                }
                reference.process(l.data(),r.data(),a.data(),b.data(),n);
                musical.process(l.data(),r.data(),c.data(),d.data(),n);
                for(int i=0;i<n;++i) {
                    dryGain+=alpha*(dryTarget-dryGain);
                    wetGain+=alpha*(wetTarget-wetGain);
                    check(std::isfinite(c[i])&&std::isfinite(d[i]),"live Mix finite");
                    check(std::abs(c[i]-(dryGain*l[i]+wetGain*a[i]/1.5))<.0004 &&
                          std::abs(d[i]-(dryGain*r[i]+wetGain*b[i]/1.5))<.0004,
                          "live Mix gain law, no phase inversion or hidden dry");
                    if(block>=3*stage-4&&block<3*stage)
                        check(std::abs(c[i]-a[i])<.0004 &&
                              std::abs(d[i]-b[i])<.0004,
                              "settled full wet contains only calibrated wet");
                }
            }
        }
    }
    std::puts("PASS Reverb-Sis live Mix: all algorithms at 44.1/48/96 kHz");
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
