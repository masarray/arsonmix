// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) || !SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
#error "The private Orbit reference requires active P9.9 native DSP"
#endif
#include "../engine/builtin_fx/sonku/SonKuP99DefaultOrbitStage.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::Settings;
using armix::sonku::p99::DefaultOrbitStage;

int main() {
    const auto reference=DefaultOrbitStage::settings();
    assert(armix::sonku::valid(reference));
    assert(reference.renderMode==9 && reference.sceneVoicing==1);
    assert(reference.moving && reference.clockwise);
    assert(reference.startAzimuthDegrees==0.f);
    assert(reference.revolutionSeconds==12.f);
    assert(reference.stereoSpreadDegrees==127.f);
    assert(reference.spatialMix==1.f);
    assert(reference.outputTrim==1.8f);
    assert(reference.spacePreset==3);
    assert(reference.sourceDistanceMeters==2.7f);
    assert(reference.roomAmount==1.f && reference.ambience==.77f);
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
    assert(reference.directElevationDegrees==0.f);
#endif
    for(const int rate:{44100,48000}){
        auto engine=std::make_unique<SonKuSpatialCore>();
        assert(engine->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
        assert(engine->setSettings(reference));
        assert(engine->ready());
        constexpr double pi=3.1415926535897932384626433832795;
        double energy=0.;
        float peak=0.f;
        constexpr int block=128;
        std::array<float,block> l{},r{};
        for(int n=0;n<rate;n+=block){
            const int frames=std::min(block,rate-n);
            for(int i=0;i<frames;++i){
                const double t=double(n+i)/rate;
                const float mid=.064f*std::sin(2.*pi*320.*t)+
                                .031f*std::sin(2.*pi*2050.*t)+
                                .026f*std::sin(2.*pi*86.*t);
                const float side=.021f*std::sin(2.*pi*3831.*t);
                l[i]=mid+side;
                r[i]=mid-side;
            }
            engine->process(l.data(),r.data(),frames);
            for(int i=0;i<frames;++i){
                assert(std::isfinite(l[i])&&std::isfinite(r[i]));
                const float mag=std::max(std::abs(l[i]),std::abs(r[i]));
                peak=std::max(peak,mag);
                assert(mag<=.9805f);
                energy+=double(l[i])*l[i]+double(r[i])*r[i];
            }
        }
        // EXACT native rotation: full revolution 12s, one sec = +30°,
        // no GUI velocity mock or second spatial PCM authority.
        assert(std::abs(engine->azimuthDegrees()-30.)<.01);
        assert(energy>.01 && peak>.01f);
        std::printf("P99_REFERENCE_ORBIT_NATIVE,%d,az=%.5f,peak=%.6f,energy=%.6f\n",
                    rate,engine->azimuthDegrees(),peak,energy);
        // A saved user scene can still switch the same core to static.
        auto staticScene=reference;
        staticScene.moving=false;
        assert(engine->setSettings(staticScene));
        const double before=engine->azimuthDegrees();
        l.fill(.05f);r.fill(.05f);
        engine->process(l.data(),r.data(),block);
        assert(engine->azimuthDegrees()==before);
        // The historical approved Venue remains a *different* scene ID.
        staticScene.sceneVoicing=0;
        staticScene.sourceDistanceMeters=10.f;
        staticScene.stereoSpreadDegrees=106.f;
        staticScene.ambience=1.f;
        staticScene.outputTrim=1.8f;
        assert(engine->setSettings(staticScene));
        assert(engine->settings().sceneVoicing==0);
    }
    std::puts("P99_NEW_DEFAULT_ORBIT_NATIVE_ROTATION_AND_BOUNDED_PCM_PASSED");
}
