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
    return result;
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
    assert(imersipP92Presets.size()==6);
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
    // Product scene classification: preserve original Venue golden; new
    // detail and impact voicings must NEVER silently affect its PCM.
    constexpr int expectedProfiles[]{4,4,5,4,4,3};
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
        const double baselineAir=spectrum(rate,0,7400,false);
        const double lateralAir=spectrum(rate,4,7400,false);
        assert(protectedPresence<baselinePresence*.93);
        assert(lateralAir>baselineAir*1.03);
        (void)spectrum(rate,4,2050,true); // mono side remains zero

        const auto base=render(rate,golden);
        // Baseline acoustic PCM hashes captured BEFORE any ImmerSIB
        // nearfield or M/S tuning. Real regression, not self-comparison.
        assert(base.hash==(rate==44100?0x4759c0c588e0e421ull:
                                      0x13a8e95368edcc04ull));
        const auto migrated=render(rate,venue);
        // The operator's approved Venue is a bit-exact audio checkpoint.
        assert(base.hash==migrated.hash);
        std::array<std::uint64_t,6> uniquePcm{};
        for(const auto& preset:imersipP92Presets){
            const auto s=imersipP92Factory(preset.id);
            assert(s.renderMode==9 && !s.moving);
            assert(s.spacePreset==3 && s.sourceDistanceMeters<=4.f);
            const auto heard=render(rate,s);
            const auto inverse=render(rate,s,true);
            assert(inverse.side>1.e-7); // no catastrophic antiphase null
            assert(heard.hash!=base.hash);
            // A named flagship hall should not collapse to near-dry, low
            // electrical energy compared with the approved Venue fixture.
            // This is a *synthetic PCM floor*, not LUFS or listening proof.
            if(preset.id==3 || preset.id==6)
                assert(heard.power>base.power*.85);
            if(preset.id==7)
                assert(heard.power>base.power*.75);
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
