// P5: user-reported disappearing Mix and airless wet signal.
// Numerical gates: dry onset, 50% constant-power law, wet existence,
// mono fold, controlled shelf frequency selectivity and sweep continuity.
// Sound quality still needs real user listening and host validation.
#include <arverb/Reverb.hpp>
#include "../src/dsp/Primitives.hpp"
#include "../../../engine/builtin_fx/ReverbSisFactoryPrograms.h"
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
static double wetTone(int mode, float hz, float highCut,
                      bool musical=false,bool matchTone=false) {
    arverb::Engine e;
    e.setAlgorithm(static_cast<arverb::Algorithm>(mode));
    e.setParameter(arverb::Parameter::Mix, 1.0f);
    e.setParameter(arverb::Parameter::PreDelayMs, 0.0f);
    e.setParameter(arverb::Parameter::DecaySeconds, 2.3f);
    e.setParameter(arverb::Parameter::DampingHz, 16500.0f);
    e.setParameter(arverb::Parameter::HighCutHz, highCut);
    e.setParameter(arverb::Parameter::DuckAmount, 0.0f);
    e.setParameter(arverb::Parameter::ModDepthMs, 0.0f);
    if(matchTone) {
        const float coeff=arverb::detail::musicalWetLowCutCoeff(150.f,48000.f);
        const float equivalent=-std::log1p(-coeff)*48000.f/(2.f*arverb::detail::kPi);
        e.setParameter(arverb::Parameter::LowCutHz,musical?150.f:equivalent);
        e.setParameter(arverb::Parameter::Diffusion,.62f);
    }
    e.prepare(48000.0,musical);
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
    engine.setParameter(arverb::Parameter::HighCutHz,5500.f); // isolate HP knee, no air shelf
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
// Compare two identical musical tanks with only Duck changed. The
// reference runs in parallel, so decay and modulation cannot masquerade
// as recovery. Voice-presence excitation ends at 1.2s; measure the same
// wet tail during the pause, not a normalized synthetic gain envelope.
static void phraseDucking(double fs,int mode) {
    arverb::Engine raw, duck;
    for(auto* e:{&raw,&duck}) {
        e->loadPreset(arverb::Preset::VocalPlateClean);
        e->setAlgorithm(static_cast<arverb::Algorithm>(mode));
        e->setParameter(arverb::Parameter::Mix,1.f);
        e->setParameter(arverb::Parameter::DecaySeconds,3.f);
        e->setParameter(arverb::Parameter::DuckAmount,e==&raw?0.f:1.f);
        e->prepare(fs,true);
    }
    constexpr int n=128;
    std::array<float,n> l{},r{},a{},b{},c{},d{};
    double rawActive=0,duckActive=0,rawGap=0,duckGap=0;
    const int blocks=static_cast<int>(fs*1.9/n);
    for(int block=0;block<blocks;++block) {
        for(int i=0;i<n;++i) {
            const double time=(block*n+i)/fs;
            const double gate=std::clamp((1.2-time)*100.0,0.0,1.0);
            l[i]=r[i]=static_cast<float>(gate*(.28*std::sin(6.283185307179586*2500*time)
                +.10*std::sin(6.283185307179586*180*time)));
        }
        raw.process(l.data(),r.data(),a.data(),b.data(),n);
        duck.process(l.data(),r.data(),c.data(),d.data(),n);
        for(int i=0;i<n;++i) {
            const double time=(block*n+i)/fs;
            check(std::isfinite(c[i])&&std::isfinite(d[i]),"finite dynamic wet");
            const double x=double(a[i])*a[i]+double(b[i])*b[i];
            const double y=double(c[i])*c[i]+double(d[i])*d[i];
            if(time>.7&&time<1.15){rawActive+=x;duckActive+=y;}
            if(time>1.65&&time<1.85){rawGap+=x;duckGap+=y;}
        }
    }
    check(rawActive>1.e-6&&rawGap>1.e-8,"reference phrase and tail measurable");
    const double active=std::sqrt(duckActive/rawActive),gap=std::sqrt(duckGap/rawGap);
    std::printf("Reverb-Sis dynamics fs=%.0f mode=%d active=%.3f gap=%.3f\n",fs,mode,active,gap);
    check(active>.35&&active<.90,"full Duck creates clearance without erasing wet");
    check(gap>.90&&gap<1.05&&gap>active+.08,"tail blooms back after phrase");
}

// Direct test of the existing wet shaper, with constant detector source
// and independent wet probes: selective presence vs body preservation.
static double selectiveGain(float fs,float frequency,bool musical) {
    arverb::detail::PresenceDucker shaper;
    shaper.prepare(fs,musical);
    double input=0,output=0;
    for(int i=0;i<static_cast<int>(fs*.6f);++i) {
        const float dry=.35f*std::sin(2.0f*arverb::detail::kPi*2500.f*i/fs);
        const float tone=.2f*std::sin(2.0f*arverb::detail::kPi*frequency*i/fs);
        float l=tone,r=tone;
        shaper.process(dry,dry,l,r,musical?1.5f:.6f);
        check(l==r&&std::isfinite(l),"selective stereo linked");
        if(i>fs*.3f){input+=double(tone)*tone;output+=double(l)*l;}
    }
    return std::sqrt(output/input);
}
static double albumToneGain(float fs,float frequency,float detector,float duck,float signature=0.f) {
    arverb::detail::AlbumLateTone tone;tone.prepare(fs);
    double input=0,output=0;
    for(int i=0;i<int(fs*.5f);++i) {
        const float x=.2f*std::sin(2.f*arverb::detail::kPi*frequency*i/fs);
        const float dry=.5f*std::sin(2.f*arverb::detail::kPi*detector*i/fs);
        float l=x,r=x;tone.process(dry,-dry,l,r,duck,1.f,signature);
        check(std::isfinite(l)&&l==r,"Album linked tone finite");
        if(i>fs*.25f){input+=double(x)*x;output+=double(l)*l;}
    }
    return std::sqrt(output/input);
}
static void albumEngineProbe(double fs,int mode) {
    arverb::Engine e;e.loadPreset(arverb::Preset::VocalPlateClean);
    e.setAlgorithm(static_cast<arverb::Algorithm>(mode));
    e.setAlbumReady(true);e.setParameter(arverb::Parameter::Mix,0.f);e.prepare(fs,true);
    std::array<float,128> l{},r{},a{},b{};
    for(int block=0;block<800;++block) {
        if(block==20)e.setParameter(arverb::Parameter::Mix,1.f);
        if(block==180)e.setAlbumReady(false);
        if(block==210)e.setAlbumReady(true);
        if(block==250)e.setVoicing(2);
        if(block==380)e.setVoicing(1);
        if(block==410)e.setVoicing(2);
        for(int i=0;i<128;++i) {
            const double t=(block*128+i)/fs;
            l[i]=r[i]=block<350?float(.16*std::sin(691.15*t)+.06*std::sin(16022.1*t)):0.f;
        }
        e.process(l.data(),r.data(),a.data(),b.data(),128);
        for(int i=0;i<128;++i) {
            check(std::isfinite(a[i])&&std::isfinite(b[i])&&std::abs(a[i])<2.f&&std::abs(b[i])<2.f,
                  "Album live profile morph bounded");
            if(block<20)check(std::abs(a[i]-l[i])<1.e-6f&&std::abs(b[i]-r[i])<1.e-6f,"Album dry endpoint");
        }
    }
}
// Retargeting mid-morph must be equivalent to letting the original
// morph finish and then applying only the latest request. A 1-frame host
// makes the transition endpoint independently observable. Compare full PCM,
// not just bounded peaks (a click may be finite and still audible).
static void signatureRetargetProbe(int fs,unsigned initial) {
    using P=arverb::Parameter;
    arverb::Engine burst,serial;
    for(auto* e:{&burst,&serial}) {
        e->loadPreset(arverb::Preset::VocalPlateClean);
        e->setAlgorithm(static_cast<arverb::Algorithm>(initial));
        e->setVoicing(2);
        e->setParameter(P::Mix,.65f);
        e->prepare(fs,true);
    }
    const unsigned first=(initial+1)%4,discarded=(initial+2)%4,last=(initial+3)%4;
    const float step=1.f/std::max(1.f,float(fs)*.100f);
    float blend=0.f;
    int duration=0;
    while(blend<1.f){blend=std::min(1.f,blend+step);++duration;}
    const int start=fs/5;
    double signalEnergy=0.;
    for(int i=0;i<start+2*duration+fs/20;++i) {
        if(i==start)for(auto* e:{&burst,&serial})e->setAlgorithm(static_cast<arverb::Algorithm>(first));
        if(i==start+duration/4)burst.setAlgorithm(static_cast<arverb::Algorithm>(discarded));
        if(i==start+duration/2)burst.setAlgorithm(static_cast<arverb::Algorithm>(last));
        if(i==start+duration)serial.setAlgorithm(static_cast<arverb::Algorithm>(last));
        const float l=float(.22*std::sin(2.*3.141592653589793*233.*i/fs)+.09*std::sin(2.*3.141592653589793*1700.*i/fs));
        const float r=float(.18*std::sin(2.*3.141592653589793*311.*i/fs));
        float a,b,c,d;
        burst.process(&l,&r,&a,&b,1);serial.process(&l,&r,&c,&d,1);
        check(std::isfinite(a)&&std::isfinite(b)&&a==c&&b==d,
              "Signature interrupted matrix morph equals serialized latest-target reference");
        signalEnergy+=double(a)*a+double(b)*b;
    }
    check(signalEnergy>1.,"Signature retarget exercised non-silent PCM");
}
static void vocalAnchorQualification() {
    using P=arverb::Parameter;
    using namespace armix::builtin_fx;
    constexpr int n=128;
    for(int fs:{44100,48000,96000})for(std::size_t program=0;program<reverbSisFactoryPrograms.size();++program) {
        const auto& preset=reverbSisFactoryPrograms[program];
        for(std::size_t i=0;i<preset.values.size();++i) {
            const auto& info=arverb::parameterInfo(static_cast<P>(i));
            check(preset.values[i]>=info.minimum&&preset.values[i]<=info.maximum,"valid factory program");
        }
        check(preset.values[0]<=.28f&&preset.values[10]<=1.20f,"intentional factory mix and width bounds");
        for(float mix:{0.f,.25f,.35f,.50f,.75f,1.f}) {
            arverb::Engine old,anchor;
            for(auto* e:{&old,&anchor}) {
                for(std::size_t i=0;i<preset.values.size();++i)e->setParameter(static_cast<P>(i),preset.values[i]);
                e->setAlgorithm(static_cast<arverb::Algorithm>(preset.algorithm));
                e->setParameter(P::Mix,mix);
                e->setVoicing(e==&old?2u:3u);e->prepare(fs,true);
            }
            const double expected=1.-double(mix)*mix*mix-std::cos(1.5707963267948966*mix);
            std::array<float,n> l{},r{},a{},b{},c{},d{};
            double inEnergy=0.,oldEnergy=0.,newEnergy=0.,wetEnergy=0.;
            float peak=0.f;
            const int blocks=fs/n;
            for(int block=0;block<blocks;++block) {
                for(int i=0;i<n;++i) {
                    const double t=double(block*n+i)/fs;
                    const double env=.65+.35*std::sin(6.283185307179586*3.*t);
                    l[i]=r[i]=float(env*(.18*std::sin(6.283185307179586*173.*t)+.07*std::sin(6.283185307179586*519.*t)+.035*std::sin(6.283185307179586*1903.*t)));
                }
                old.process(l.data(),r.data(),a.data(),b.data(),n);
                anchor.process(l.data(),r.data(),c.data(),d.data(),n);
                for(int i=0;i<n;++i) {
                    check(std::isfinite(c[i])&&std::isfinite(d[i]),"Anchor finite");
                    check(std::abs((c[i]-a[i])-expected*l[i])<2.e-6&&std::abs((d[i]-b[i])-expected*r[i])<2.e-6,"Anchor difference is direct only");
                    if(mix==0.f||mix==1.f)check(a[i]==c[i]&&b[i]==d[i],"Anchor exact endpoint PCM");
                    peak=std::max(peak,std::max(std::abs(c[i]),std::abs(d[i])));
                    if(block>blocks/2) {
                        const double x=l[i],y=.5*(a[i]+b[i]),z=.5*(c[i]+d[i]);
                        inEnergy+=x*x;oldEnergy+=y*y;newEnergy+=z*z;
                        const double wet=z-(1.-double(mix)*mix*mix)*x;wetEnergy+=wet*wet;
                    }
                }
            }
            check(peak<1.f,"Anchor nominal voiced proxy headroom");
            if(mix>.25f&&mix<.76f)check(newEnergy>oldEnergy,"Anchor improves measured mono voiced energy");
            if(mix==1.f)check(wetEnergy>1.e-8,"factory pure wet is not silent");
            if(fs==48000&&mix==.5f)std::printf("Anchor factory %zu mix50 oldRMS=%.4f newRMS=%.4f peak=%.4f\n",program,std::sqrt(oldEnergy/inEnergy),std::sqrt(newEnergy/inEnergy),peak);
        }
    }
    // Profile changes at unchanged Mix must still update target with 25ms
    // smoothing; use long predelay to independently observe dry-only onset.
    arverb::Engine e;e.setVoicing(2);e.setParameter(P::Mix,.5f);
    e.setParameter(P::PreDelayMs,250.f);e.prepare(48000.,true);
    float in=.2f,l=0,r=0;e.process(&in,&in,&l,&r,1);
    const float before=l;e.setVoicing(3);e.process(&in,&in,&l,&r,1);
    check(l>before&&l-before<.001f,"Anchor recall is smoothed at unchanged Mix");
    for(int i=0;i<8000;++i)e.process(&in,&in,&l,&r,1);
    check(std::abs(l-.175f)<.0001f,"Anchor recall reaches new dry target");
    std::puts("PASS Vocal Anchor: 288 factory/rate/mix PCM cases, exact wet endpoints, smoothed profile recall");
}
// Full actual wet PCM, not just a detached EQ response. Compare identical
// factory values, same tank and input, including early reflections.
static void studioFinishQualification() {
    using P=arverb::Parameter;
    using namespace armix::builtin_fx;
    constexpr int n=128;
    for(int fs:{44100,48000,96000})for(std::size_t program=0;program<reverbSisFactoryPrograms.size();++program) {
        const auto& preset=reverbSisFactoryPrograms[program];
        // All 16 actual presets at 48k; four representative sources at other rates.
        if(fs!=48000&&program!=0&&program!=4&&program!=7&&program!=8)continue;
        for(float hz:{180.f,400.f,950.f,2500.f,9000.f}) {
            arverb::Engine old,finish;
            for(auto* e:{&old,&finish}) {
                for(std::size_t i=0;i<preset.values.size();++i)e->setParameter(static_cast<P>(i),preset.values[i]);
                e->setAlgorithm(static_cast<arverb::Algorithm>(preset.algorithm));
                e->setParameter(P::Mix,1.f);
                e->setVoicing(e==&old?3u:4u);e->prepare(fs,true);
            }
            std::array<float,n> l{},r{},a{},b{},c{},d{};
            double before=0.,after=0.,fold=0.;
            const int blocks=int(1.5*fs/n);
            for(int block=0;block<blocks;++block) {
                for(int i=0;i<n;++i)l[i]=r[i]=float(.18*std::sin(6.283185307179586*hz*(block*n+i)/fs));
                old.process(l.data(),r.data(),a.data(),b.data(),n);
                finish.process(l.data(),r.data(),c.data(),d.data(),n);
                for(int i=0;i<n;++i) {
                    check(std::isfinite(c[i])&&std::isfinite(d[i])&&std::abs(c[i])<2.f&&std::abs(d[i])<2.f,"Studio Finish bounded PCM");
                    if(block>blocks*2/3) {
                        before+=double(a[i])*a[i]+double(b[i])*b[i];
                        after+=double(c[i])*c[i]+double(d[i])*d[i];
                        const double m=.5*(c[i]+d[i]);fold+=m*m;
                    }
                }
            }
            check(before>1.e-9&&after>1.e-9&&fold>1.e-10,"Studio Finish wet stereo and mono measurable");
            const double db=10.*std::log10(after/before);
            if(hz==180.f)check(db>.35&&db<1.5,"Studio Finish body lift");
            if(hz==950.f)check(db<-1.0&&db>-2.4,"Studio Finish nasal mid relief");
            if(hz==9000.f)check(std::abs(db)<.35,"Studio Finish air is not overboosted");
            std::printf("Studio Finish program=%zu rate=%d hz=%.0f deltaDb=%.3f\n",program,fs,hz,db);
        }
    }
    // Dry endpoint remains exactly the previous Anchor at every sample.
    arverb::Engine old,finish;
    for(auto* e:{&old,&finish}) {
        e->setParameter(P::Mix,0.f);e->setVoicing(e==&old?3u:4u);e->prepare(48000.,true);
    }
    for(int i=0;i<48000;++i) {
        const float x=.3f*std::sin(i*.037f);float a,b,c,d;
        old.process(&x,&x,&a,&b,1);finish.process(&x,&x,&c,&d,1);
        check(a==c&&b==d,"Studio Finish exact dry endpoint");
    }
    std::puts("PASS Studio Finish: 120 actual factory/rate/wet band PCM cases and dry endpoint");
}
int main() {
    studioFinishQualification();
    vocalAnchorQualification();
    for(int fs:{44100,48000,96000})for(unsigned mode=0;mode<4;++mode)signatureRetargetProbe(fs,mode);
    std::puts("PASS Signature latest-target matrix continuity: 12 exact PCM comparisons");
    for(float fs:{44100.f,48000.f,96000.f}) {
        const double body=albumToneGain(fs,150.f,2500.f,0.f);
        const double mud=albumToneGain(fs,380.f,2500.f,0.f);
        const double mid=albumToneGain(fs,1450.f,2500.f,0.f);
        const double air=albumToneGain(fs,10000.f,2500.f,0.f);
        const double ess=albumToneGain(fs,7000.f,7000.f,1.f)/albumToneGain(fs,7000.f,7000.f,0.f);
        std::printf("Reverb-Sis album fs=%.0f body=%.3f mud=%.3f mid=%.3f air=%.3f ess=%.3f\n",fs,body,mud,mid,air,ess);
        check(body>.90&&mud<.80&&mid<.80&&air>.96,"Album static contour preserves body and air");
        check(ess<.93&&ess>.50,"Album sibilance detector attenuates wet band");
        const double signatureBody=albumToneGain(fs,170.f,2500.f,0.f,1.f)/albumToneGain(fs,170.f,2500.f,0.f);
        const double signatureMud=albumToneGain(fs,380.f,2500.f,0.f,1.f)/mud;
        const double signatureEss=albumToneGain(fs,7000.f,7000.f,1.f,1.f)/albumToneGain(fs,7000.f,7000.f,0.f,1.f);
        std::printf("Signature filter fs=%.0f bodyDelta=%.3f mudDelta=%.3f ess=%.3f\n",fs,signatureBody,signatureMud,signatureEss);
        check(signatureBody>1.17&&signatureBody<1.21&&signatureMud<1.06&&signatureEss<.93,
              "Signature broad body lift preserves mud and de-ess");
        for(int mode=0;mode<4;++mode)albumEngineProbe(fs,mode);
    }
    std::puts("PASS Reverb-Sis Album Ready contour, de-ess and live profile morph");
    // Exact fractional tap output vs independent original implementation,
    // including ring wrap, fractional values and both clamp boundaries.
    for(float fs:{8000.f,44100.f,48000.f,96000.f,192000.f}) {
        arverb::detail::Delay ring;
        ring.prepare(static_cast<std::size_t>(.132f*fs)+8);
        const std::array<float,6> delays{{0.f,1.f,4.73f*.001f*fs,
            14.9f*.001f*fs,126.1f*.001f*fs,1.e9f}};
        std::array<arverb::detail::Delay::FractionalTap,6> taps{};
        for(std::size_t k=0;k<taps.size();++k)taps[k]=ring.prepareTap(delays[k]);
        for(std::size_t i=0;i<ring.data.size()*2+17;++i) {
            for(std::size_t k=0;k<taps.size();++k) {
                const float s=std::clamp(delays[k],1.f,static_cast<float>(ring.data.size()-4));
                const auto integer=static_cast<std::size_t>(s);
                const float fraction=s-static_cast<float>(integer);
                const auto a=(ring.write-integer)&ring.mask,b=(a-1)&ring.mask;
                const float expected=ring.data[a]+(ring.data[b]-ring.data[a])*fraction;
                check(ring.readPrepared(taps[k])==expected,"prepared tap preserves fractional PCM");
            }
            ring.push(.2f*std::sin(static_cast<float>(i)*.071f));
        }
    }
    std::puts("PASS Reverb-Sis prepared stationary fractional taps (5 rates, ring wrap)");
    // Matched wet HP and unchanged tank for non-Classic modes. Normalize
    // 3x full-wet gain: only the musical shelf differs at open High Cut.
    for(int mode:{1,2,3}) {
        const double dark=wetTone(mode,9800.f,4000.f,true,true)/
                         (3.0*wetTone(mode,9800.f,4000.f,false,true));
        const double body=wetTone(mode,500.f,14500.f,true,true)/
                         (3.0*wetTone(mode,500.f,14500.f,false,true));
        const double air=wetTone(mode,9800.f,14500.f,true,true)/
                        (3.0*wetTone(mode,9800.f,14500.f,false,true));
        std::printf("Reverb-Sis fresh air mode=%d dark=%.4f body=%.4f air=%.4f\n",mode,dark,body,air);
        check(std::abs(dark-1.0)<.015&&std::abs(body-1.0)<.015,
              "dark and body remain calibrated");
        check(air>body*1.035&&air<1.20,"open high-cut adds bounded sparkle");
    }
    std::puts("PASS Reverb-Sis fresh air selective wet PCM");
    for(float fs:{44100.f,48000.f,96000.f}) {
        const double body=selectiveGain(fs,180.f,true);
        const double presence=selectiveGain(fs,2500.f,true);
        const double previous=selectiveGain(fs,2500.f,false);
        std::printf("Reverb-Sis de-mask fs=%.0f body=%.3f presence=%.3f old=%.3f\n",
                    fs,body,presence,previous);
        check(body>.96&&presence<previous-.05&&presence<body-.10,
              "stronger selective clearance preserves vocal body");
        for(int mode=0;mode<4;++mode)phraseDucking(fs,mode);
    }
    std::puts("PASS Reverb-Sis selective vocal dynamics and phrase bloom");
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
