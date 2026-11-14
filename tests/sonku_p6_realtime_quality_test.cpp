// SPDX-License-Identifier: AGPL-3.0-only
// P6 opt-in, prepared native ROOM renderer: keep full music and physical
// room paths on one PCM instance. No runtime graph switch or user ABI.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

int main(){
    using namespace armix::sonku;
    constexpr double pi=3.14159265358979323846;
    constexpr int frames=12288;
    for(const double rate:{44100.,48000.}){
        auto reject=std::make_unique<SonKuSpatialCore>();
        assert(!reject->prepare(rate,static_cast<SonKuReflectionProfile>(255)));
        auto light=std::make_unique<SonKuSpatialCore>();
        auto full=std::make_unique<SonKuSpatialCore>();
        auto split=std::make_unique<SonKuSpatialCore>();
        assert(light->prepare(rate));
        assert(full->prepare(rate,SonKuReflectionProfile::DirectionalKemar));
        assert(split->prepare(rate,SonKuReflectionProfile::DirectionalKemar));
        assert(light->preparedReflectionProfile()==SonKuReflectionProfile::Lightweight);
        assert(full->preparedReflectionProfile()==SonKuReflectionProfile::DirectionalKemar);
        // Prepared production-profile kernels are immutable; offline-only
        // mutators must not silently disable the real-time renderer.
        assert(!full->setDirectionalEarlyReflectionsHrir(false));
        assert(!full->setDirectionalZeroSpreadFusion(false));
        assert(!full->setDirectionalContiguousHrir(false));
        Settings scene{};
        scene.startAzimuthDegrees=40.f;scene.revolutionSeconds=3.f;
        scene.moving=true;scene.stereoSpreadDegrees=60.f;
        scene.spacePreset=1;scene.sourceDistanceMeters=1.6f;
        scene.spatialMix=1.f;scene.outputTrim=.6f;
        scene.roomAmount=.70f;scene.ambience=.22f;
        assert(light->setSettings(scene));
        assert(full->setSettings(scene));
        assert(split->setSettings(scene));
        std::array<float,frames> l{},r{},sl{},sr{},ll{},lr{};
        for(int n=0;n<frames;++n){
            const float t=static_cast<float>(n/rate);
            const float bass=.12f*std::sin(static_cast<float>(2.*pi*220.*t));
            const float treb=.06f*std::sin(static_cast<float>(2.*pi*3200.*t));
            const float side=.035f*std::sin(static_cast<float>(2.*pi*470.*t));
            l[n]=sl[n]=ll[n]=bass+treb+side;
            r[n]=sr[n]=lr[n]=bass-treb-side;
        }
        full->process(l.data(),r.data(),frames);
        light->process(ll.data(),lr.data(),frames);
        for(int at=0;at<frames;){
            const int n=std::min((at%3)==0?64:((at%3)==1?128:257),frames-at);
            split->process(sl.data()+at,sr.data()+at,n);
            at+=n;
        }
        double difference=0.,energy=0.;
        for(int n=0;n<frames;++n){
            assert(std::isfinite(l[n])&&std::isfinite(r[n]));
            assert(std::abs(l[n])<1.f&&std::abs(r[n])<1.f);
            assert(std::abs(l[n]-sl[n])<2.e-6f);
            assert(std::abs(r[n]-sr[n])<2.e-6f);
            if(n<96){ // no reflected image source reaches the listener yet
                assert(l[n]==ll[n]&&r[n]==lr[n]);
            }
            if(n>2500)difference+=std::abs(l[n]-ll[n])+std::abs(r[n]-lr[n]);
            energy+=l[n]*l[n]+r[n]*r[n];
        }
        assert(energy>0.01 && difference>1.e-4);
        assert(std::abs(full->azimuthDegrees()-split->azimuthDegrees())<1.e-7);
        // On device restart the accepted scene must republish with the
        // explicitly requested profile, not retain an old hidden mode.
        assert(full->prepare(rate,SonKuReflectionProfile::DirectionalKemar));
        assert(full->preparedReflectionProfile()==SonKuReflectionProfile::DirectionalKemar);
        assert(full->settings().spacePreset==1);
        assert(full->prepare(rate));
        assert(full->preparedReflectionProfile()==SonKuReflectionProfile::Lightweight);
        assert(full->settings().spacePreset==1);
    }
    std::puts("PASS P6 native ROOM profile, 44.1/48k, accepted scene restart, full-KEMAR music and chunk parity");
}
