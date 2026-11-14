// SPDX-License-Identifier: AGPL-3.0-only
// P7.2 pure C++ realtime edit contract, same native core as JUCE callback.
// 44.1/48k PCM, measured direct XYZ, phase-preserving accepted edits.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) || !SONKU_P7_MEASURED_ELEVATION_RESEARCH || !defined(SONKU_P71_DIRECT_XYZ_RESEARCH) || !SONKU_P71_DIRECT_XYZ_RESEARCH || !defined(SONKU_P72_NATIVE_XYZ_RT) || !SONKU_P72_NATIVE_XYZ_RT
#error The P7.2 source test requires exactly the full experimental contract
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::Settings;
int main(){
 constexpr double pi=3.14159265358979323846;
 constexpr int N=16384;
 for(double rate:{44100.,48000.}){
   for(float spread:{0.f,60.f}){
     auto changed=std::make_unique<SonKuSpatialCore>();
     auto chunks=std::make_unique<SonKuSpatialCore>();
     auto flat=std::make_unique<SonKuSpatialCore>();
     for(auto* dsp:{changed.get(),chunks.get(),flat.get()})
        assert(dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
     Settings scene{};
     scene.spacePreset=1;scene.roomAmount=.58f;scene.ambience=.2f;
     scene.outputTrim=.55f;scene.stereoSpreadDegrees=spread;
     scene.startAzimuthDegrees=35.f;scene.revolutionSeconds=3.7f;
     scene.directElevationDegrees=0.f;
     assert(armix::sonku::valid(scene));
     for(auto* dsp:{changed.get(),chunks.get(),flat.get()})
        assert(dsp->setSettings(scene));
     std::array<float,N> l{},r{},cl{},cr{},fl{},fr{};
     for(int n=0;n<N;++n){
       const float t=static_cast<float>(n/rate);
       const float bass=.13f*std::sin(static_cast<float>(2*pi*220*t));
       const float treble=.05f*std::sin(static_cast<float>(2*pi*3200*t));
       const float voice=.03f*std::sin(static_cast<float>(2*pi*740*t));
       l[n]=cl[n]=fl[n]=bass+treble+voice;
       r[n]=cr[n]=fr[n]=bass-treble+voice*.8f;
     }
     // Before any realtime edit, same P7.1 0-degree native behavior.
     const int boundary=4096;
     changed->process(l.data(),r.data(),boundary);
     chunks->process(cl.data(),cr.data(),boundary);
     flat->process(fl.data(),fr.data(),boundary);
     for(int i=0;i<boundary;++i) {
       assert(l[i]==cl[i]&&r[i]==cr[i]);
       assert(l[i]==fl[i]&&r[i]==fr[i]);
     }
     const double phase=changed->azimuthDegrees();
     auto bad=scene;
     bad.directElevationDegrees=65.f;
     bad.startAzimuthDegrees=179.f;
     assert(!armix::sonku::valid(bad));
     assert(!changed->setSettings(bad));
     bad.directElevationDegrees=std::numeric_limits<float>::quiet_NaN();
     assert(!armix::sonku::valid(bad) && !changed->setSettings(bad));
     bad.directElevationDegrees=45.f;
     bad.sourceDistanceMeters=8.f; // impossible 3.2m room
     assert(!armix::sonku::valid(bad) && !changed->setSettings(bad));
     assert(changed->settings().directElevationDegrees==0.f);
     assert(changed->azimuthDegrees()==phase); // rejected edit cannot move orbit

     scene.directElevationDegrees=25.f;
     assert(armix::sonku::valid(scene));
     assert(changed->setSettings(scene)&&chunks->setSettings(scene));
     assert(changed->settings().directElevationDegrees==25.f);
     // Live edit must keep sample clock, ring buffers and continuous orbit.
     assert(changed->azimuthDegrees()==phase);
     changed->process(l.data()+boundary,r.data()+boundary,N-boundary);
     for(int at=boundary;at<N;){
       const int width=std::min((at%2)?257:64,N-at);
       chunks->process(cl.data()+at,cr.data()+at,width);
       at+=width;
     }
     flat->process(fl.data()+boundary,fr.data()+boundary,N-boundary);
     double delta=0.,energy=0.;
     for(int n=0;n<N;++n){
       assert(std::isfinite(l[n])&&std::isfinite(r[n]));
       assert(std::abs(l[n])<1.f&&std::abs(r[n])<1.f);
       assert(std::abs(l[n]-cl[n])<2.e-6f);
       assert(std::abs(r[n]-cr[n])<2.e-6f);
       if(n>boundary+2048)
          delta+=std::abs(l[n]-fl[n])+std::abs(r[n]-fr[n]);
       energy+=l[n]*l[n]+r[n]*r[n];
     }
     assert(energy>.001&&delta>.05);
     assert(std::abs(changed->azimuthDegrees()-chunks->azimuthDegrees())<1.e-7);
     // Accept a second elevated edit (same PCM instance, no new graph).
     scene.directElevationDegrees=-25.f;
     assert(changed->setSettings(scene));
     assert(changed->settings().directElevationDegrees==-25.f);
     // Device restart resynthesizes approved coordinates, not stale 0 degree.
     assert(changed->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
     assert(changed->settings().directElevationDegrees==-25.f);
     assert(changed->researchDirectElevation()==-25.f);
     // A P6 downgrade must not smuggle unsupported 3D coordinates.
     assert(changed->prepare(rate,SonKuReflectionProfile::DirectionalKemar));
     assert(changed->settings().directElevationDegrees==0.f);
     assert(!changed->setSettings(scene));
   }
 }
 std::puts("PASS SonKu P7.2 atomic realtime XYZ edit, causal KEMAR PCM, phase stability, 44.1/48k, restart/downgrade");
}
