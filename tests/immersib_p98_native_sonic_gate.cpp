// SPDX-License-Identifier: AGPL-3.0-only
// ImmerSIB P9.7 Venue golden PCM and P9.8 typed scene diversification.
// No claim of actual source/stem separation; synthetic PCM verifies physical
// stage retention, stereo non-null, finite peaks, and distinct sonic programs.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P98_SCENE_VOICING_RESEARCH) || !SONKU_P98_SCENE_VOICING_RESEARCH
#error This test must compile the REAL P9.8 world-engine path
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include <algorithm>
#include <array>
#include <bit>
#include <numeric> // test-only energy aggregation
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
using armix::sonku::Settings;
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::builtin_fx::imersipP92Factory;
using armix::builtin_fx::imersipP92Presets;
constexpr int n=16384;
constexpr double pi=3.1415926535897932384626433832795;
struct Result {std::uint64_t hash;double power,side;float peak;};
static Result render(int rate,const Settings& s,bool phaseReverse=false){
    auto dsp=std::make_unique<SonKuSpatialCore>();
    assert(dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    assert(armix::sonku::valid(s) && dsp->setSettings(s));
    std::array<std::vector<float>,2> pcm{std::vector<float>(n),std::vector<float>(n)};
    for(int i=0;i<n;++i){
        const double t=double(i)/double(rate);
        const float mid=float(.072*std::sin(2*pi*86*t)+
                              .060*std::sin(2*pi*117*t)+
                              .045*std::sin(2*pi*2210*t)+
                              .028*std::sin(2*pi*7400*t));
        const float side=float(.048*std::sin(2*pi*380*t)+
                               .027*std::sin(2*pi*3150*t));
        pcm[0][i]=mid+side;
        pcm[1][i]=phaseReverse?-(mid+side):mid-side;
    }
    for(int i=0;i<n;i+=128)dsp->process(pcm[0].data()+i,pcm[1].data()+i,128);
    Result result{14695981039346656037ull,0.,0.,0.f};
    for(int i=0;i<n;++i){
        const float l=pcm[0][i],r=pcm[1][i];
        assert(std::isfinite(l)&&std::isfinite(r));
        result.peak=std::max({result.peak,std::abs(l),std::abs(r)});
        assert(result.peak<=.9805f);
        if(i>=4096){
            result.power+=double(l)*l+double(r)*r;
            result.side+=double(l-r)*double(l-r);
        }
        for(float value:{l,r}){
            result.hash^=std::bit_cast<std::uint32_t>(value);
            result.hash*=1099511628211ull;
        }
    }
    assert(result.power>1.e-4 && result.side>1.e-7);
#if defined(SONKU_IMMERSIB_PCM_DIAGNOSTICS) && SONKU_IMMERSIB_PCM_DIAGNOSTICS
    const auto diag=dsp->dynamicsTrace();
    assert(diag.samples==n && diag.limitedSamples<=diag.samples);
    assert(diag.minimumLimiterGain>0.f && diag.minimumLimiterGain<=1.f);
    assert(diag.makeupGainSum>=double(diag.samples));
    if(s.sourceDistanceMeters==1.f && s.stereoSpreadDegrees==147.f &&
       s.sceneVoicing==4) {
        const double meanMakeup=diag.makeupGainSum/diag.samples;
        const double limitedPct=100.*diag.limitedSamples/diag.samples;
        std::fprintf(stderr,
            "IMMERSIB_DYNAMICS,rate=%d,makeup=%.5f,limited=%.2f%%,minLimiter=%.5f,meanReduction=%.5f\\n",
            rate,meanMakeup,limitedPct,double(diag.minimumLimiterGain),
            diag.limiterReductionSum/diag.samples);
        // Real PCM adaptive makeup bound, NOT a claim of perceptual no-pumping.
        assert(meanMakeup>=1. && meanMakeup<=1.101);
    }
#endif
    return result;
}
// Whole-engine tone transfer, including HRTF/world reflections, P9.7
// stage, P9.8 voicing, P9.5 makeup and peak guard. Real PCM, not just a
// narrow EQ coefficient guess. Short deterministic steady-state fixture.
static double acousticTonePower(int rate,const Settings& scene,
                                double hz,bool mid) {
    auto dsp=std::make_unique<SonKuSpatialCore>();
    assert(dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    assert(dsp->setSettings(scene));
    std::array<std::vector<float>,2> pcm{
        std::vector<float>(n),std::vector<float>(n)};
    for(int i=0;i<n;++i) {
        const float a=static_cast<float>(
            .065*std::sin(2*pi*hz*i/static_cast<double>(rate)));
        pcm[0][i]=a;
        pcm[1][i]=mid?a:-a;
    }
    for(int i=0;i<n;i+=128)
        dsp->process(pcm[0].data()+i,pcm[1].data()+i,128);
    double energy=0.;
    for(int i=4096;i<n;++i){
        const float l=pcm[0][i],r=pcm[1][i];
        assert(std::isfinite(l)&&std::isfinite(r));
        assert(std::abs(l)<.981f&&std::abs(r)<.981f);
        energy+=double(l)*l+double(r)*r;
    }
    assert(energy>1.e-7);
    return energy;
}
static Settings ownerVenue(){
    Settings s{};
    s.renderMode=9;
    s.moving=false;
    s.startAzimuthDegrees=0.f;
    s.spacePreset=3;               // Large
    s.sourceDistanceMeters=10.f;   // actual approved Venue 10m
    s.stereoSpreadDegrees=106.f;
    s.roomAmount=1.f;
    s.ambience=1.f;
    s.spatialMix=1.f;
    s.outputTrim=1.8f;
    s.sceneVoicing=0;              // exact P9.7, no P9.8 tonal overlay
    return s;
}
int main(){
    const auto venue=armix::builtin_fx::imersipP97VenueGoldenScene();
    const auto current=imersipP92Factory(2);
    const auto golden=ownerVenue();
    assert(venue.renderMode==9 && !venue.moving && venue.sceneVoicing==0);
    assert(venue.sourceDistanceMeters==10.f && venue.stereoSpreadDegrees==106.f);
    assert(venue.roomAmount==1.f && venue.ambience==1.f);
    assert(venue.outputTrim==1.8f && venue.spatialMix==1.f);
    assert(venue.startAzimuthDegrees==0.f && venue.spacePreset==3);
    assert(imersipP92Presets.size()==7);
    const auto ownerPreset=imersipP92Factory(8);
    assert(ownerPreset.spacePreset==3 && ownerPreset.stereoSpreadDegrees==147.f);
    assert(ownerPreset.sourceDistanceMeters==1.f && ownerPreset.roomAmount==1.8f);
    assert(ownerPreset.ambience==.60f && ownerPreset.spatialMix==1.f);
    assert(ownerPreset.headYawDegrees==0.f && ownerPreset.hrtfElevationTrimDegrees==0.f);
    assert(ownerPreset.sceneVoicing==4);
    assert(std::abs(ownerPreset.outputTrim-imersipP92DbToLinear(2.5f))<1.e-6f);
    assert(armix::builtin_fx::validImerSipP92Scene(ownerPreset));
    // Nearfield source projection now supports 1m in the physical XYZ path.
    armix::sonku::xyz::Placement projected{};
    assert(armix::sonku::xyz::project(1.f,0.f,1.45f,7.2f,projected));
    assert(std::abs(projected.horizontalMeters-1.f)<1.e-6f);
    assert(!armix::sonku::xyz::project(.9f,0.f,1.45f,7.2f,projected));
    assert(current.sourceDistanceMeters==3.f &&
           current.roomAmount>1.f && current.sceneVoicing==4);
    assert(armix::builtin_fx::validImerSipP92Scene(current));
    for(const auto& preset:imersipP92Presets) {
        const auto s=imersipP92Factory(preset.id);
        assert(s.sourceDistanceMeters>=1.f && s.sourceDistanceMeters<=4.f);
        assert(s.roomAmount>1.f && s.roomAmount<=1.8f);
    }
    assert(armix::sonku::p98::SceneVoicing::valid(4));
    assert(armix::sonku::p98::SceneVoicing::valid(5));
    assert(!armix::sonku::p98::SceneVoicing::valid(6));
    // Drive the upstream compensation detector into a low-output state.
    // The ImmerSIB cap must reduce modulation range, while the legacy cap
    // remains available for the immutable Venue and other native voicings.
    for(int sr:{44100,48000}) {
        armix::sonku::p95::MusicalPresence bounded,legacy;
        assert(bounded.prepare(sr) && legacy.prepare(sr));
        for(int i=0;i<sr*2;++i) {
            (void)bounded.process(.25f,.25f,.012f,.012f,1.10f);
            (void)legacy.process(.25f,.25f,.012f,.012f);
        }
        assert(bounded.gain()>=1.f && bounded.gain()<=1.1001f);
        assert(legacy.gain()>1.20f && legacy.gain()<=1.6501f);
    }
    // Product scene classification: preserve original Venue golden; new
    // detail and impact voicings must NEVER silently affect its PCM.
    constexpr int expectedProfiles[]{4,4,5,4,4,3,4};
    for(std::size_t i=0;i<imersipP92Presets.size();++i)
        assert(imersipP92Presets[i].voicing==expectedProfiles[i]);
    for(int rate:{44100,48000}){
        // Verify frequency-selective stereo SIDE masking relief BEFORE
        // the measured KEMAR renderer. No assumed or fake vocal stems.
        auto spectrum=[](int sr,int scene,int hz,bool mono)->double {
            armix::sonku::p98::SceneVoicing fx;
            assert(fx.prepare(sr));
            double power=0.;
            for(int i=0;i<12000;++i){
                const float v=.10f*std::sin(2*pi*hz*i/double(sr));
                const auto out=fx.source(v,mono?v:-v,scene,true);
                assert(std::isfinite(out.left)&&std::isfinite(out.right));
                if(mono)assert(std::abs(out.left-out.right)<1.e-7f);
                if(i>=7000){
                    const double side=.5*(out.left-out.right);
                    power+=side*side;
                }
            }
            return power;
        };
        const double baselinePresence=spectrum(rate,0,2050,false);
        const double protectedPresence=spectrum(rate,4,2050,false);
        const double baselineEdge=spectrum(rate,0,3450,false);
        const double protectedEdge=spectrum(rate,4,3450,false);
        const double baselineAir=spectrum(rate,0,7400,false);
        const double lateralAir=spectrum(rate,4,7400,false);
        assert(protectedPresence<baselinePresence*.93);
        // Wider side-HF emphasis previously made concert cymbals harsh.
        // Now 3.45k masking is reduced but true >7k stereo is retained.
        assert(protectedEdge<baselineEdge*.85);
        assert(lateralAir>baselineAir*.75 &&
               lateralAir<baselineAir*1.10);
        (void)spectrum(rate,4,2050,true); // mono side remains zero
        auto centeredEnergy=[](int sr,int scene,int hz,bool final)->double{
            armix::sonku::p98::SceneVoicing fx;
            assert(fx.prepare(sr));
            double energy=0.;
            for(int i=0;i<12000;++i){
                const float v=.10f*std::sin(2*pi*hz*i/double(sr));
                auto out=fx.source(v,v,scene,true);
                if(final)out=fx.finish(out.left,out.right);
                assert(std::isfinite(out.left)&&std::isfinite(out.right));
                assert(std::abs(out.left-out.right)<1.e-7f);
                if(i>=7000){
                    const double mid=.5*(out.left+out.right);
                    energy+=mid*mid;
                }
            }
            return energy;
        };
        const double bodyReference=centeredEnergy(rate,0,285,false);
        const double bodyFull=centeredEnergy(rate,4,285,false);
        const double noseReference=centeredEnergy(rate,0,2050,false);
        const double noseFull=centeredEnergy(rate,4,2050,false);
        const double harshReference=centeredEnergy(rate,0,3450,true);
        const double harshControlled=centeredEnergy(rate,4,3450,true);
        // Genuine center body improvement, not a 2k-only megaphone:
        assert(bodyFull>bodyReference*1.20);
        assert(noseFull<noseReference*1.32);
        // Ear-side upper-mid attenuation survives the finish stage.
        assert(harshControlled<harshReference*.99);
        std::fprintf(stderr,
            "VOCAL_TONE_GATE,rate=%d,body=%.4f,baseline=%.4f,2k=%.4f,2kBase=%.4f,edge=%.4f,edgeBase=%.4f,side3k=%.4f,sideBase=%.4f\n",
            rate,bodyFull,bodyReference,noseFull,noseReference,
            harshControlled,harshReference,protectedEdge,baselineEdge);

        // Confirm an actual audible-transfer direction after the ENTIRE
        // binaural chain, not merely in standalone P9.8 filter output.
        // Compare identical room/distance/trim, changing ONLY voicing.
        for(int id:{2,4}) {
            auto warm=imersipP92Factory(id);
            auto neutral=warm;
            neutral.sceneVoicing=0;
            const double warmBody=acousticTonePower(rate,warm,285.,true);
            const double baseBody=acousticTonePower(rate,neutral,285.,true);
            const double warmEdge=acousticTonePower(rate,warm,3450.,false);
            const double baseEdge=acousticTonePower(rate,neutral,3450.,false);
            const double warmAir=acousticTonePower(rate,warm,7400.,false);
            const double baseAir=acousticTonePower(rate,neutral,7400.,false);
            const double bodyRatio=warmBody/baseBody;
            const double edgeRatio=warmEdge/baseEdge;
            const double airRatio=warmAir/baseAir;
            std::fprintf(stderr,
                "VOCAL_WHOLE_PCM,%d,id=%d,body=%.4f,edge=%.4f,air=%.4f\n",
                rate,id,bodyRatio,edgeRatio,airRatio);
            assert(bodyRatio>1.08);
            assert(edgeRatio<1.05);
            assert(airRatio>.65);
        }
        const auto base=render(rate,golden);
        // Baseline acoustic PCM hashes captured BEFORE any ImmerSIB
        // nearfield or M/S tuning. Real regression, not self-comparison.
        assert(base.hash==(rate==44100?0x4759c0c588e0e421ull:
                                      0x13a8e95368edcc04ull));
        const auto migrated=render(rate,venue);
        // The operator's approved Venue is a bit-exact audio checkpoint.
        assert(base.hash==migrated.hash);
        std::array<std::uint64_t,7> uniquePcm{};
        for(const auto& preset:imersipP92Presets){
            const auto s=imersipP92Factory(preset.id);
            assert(s.renderMode==9 && !s.moving);
            assert(s.spacePreset==3 && s.sourceDistanceMeters<=4.f);
            const auto heard=render(rate,s);
            const auto inverse=render(rate,s,true);
            assert(inverse.side>1.e-7); // no catastrophic antiphase null
            assert(heard.hash!=base.hash);
            // Always print candidate evidence BEFORE its quality floor;
            // std::cout/stdout may be buffered and lost by abort().
            std::fprintf(stderr,"NEARFIELD_DIAG,rate=%d,id=%d,meters=%.2f,early=%.2f,energy=%.6f,venue=%.6f,side=%.6f,peak=%.5f\n",
                rate,preset.id,s.sourceDistanceMeters,s.roomAmount,
                heard.power,base.power,heard.side,double(heard.peak));
            // Safety floor only: older .85 electrical power gate
            // rewarded treble-heavy sound even when the owner heard
            // shrill, thin vocals. The independent WHOLE-PCM 285Hz/3.45k
            // tonal tests above replace it with a physically appropriate
            // transfer quality criterion. Still forbid silent presets.
            assert(heard.power>base.power*.70);
            uniquePcm[static_cast<std::size_t>(preset.id-2)]=heard.hash;
            std::printf("IMMERSIB_P98_SCENE,%d,%d,%s,%d,%.6f,%.6f,%.6f,%016llx\n",
                rate,preset.id,preset.label,s.sceneVoicing,
                heard.power,heard.side,double(heard.peak),
                static_cast<unsigned long long>(heard.hash));
        }
        // Every user-facing scene is a physically distinct PCM program.
        for(std::size_t i=0;i<uniquePcm.size();++i)
            for(std::size_t j=i+1;j<uniquePcm.size();++j)
                assert(uniquePcm[i]!=uniquePcm[j]);
    }
    std::puts("PASS IMMERSIB NEARFIELD 1-4M + EARLY 180 + VENUE GOLDEN PCM");
}
