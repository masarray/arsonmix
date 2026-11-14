// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P7_MEASURED_ELEVATION_RESEARCH) || !defined(SONKU_P71_DIRECT_XYZ_RESEARCH)
#error P7.1 true XYZ measurement must be tested under both research flags
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
 constexpr float pi=3.14159265358979323846f;
 for(double rate:{44100.,48000.}){
   for(float elev:{-25.f,0.f,25.f,45.f}){
     xyz::Placement projected{};
     assert(xyz::project(1.6f,elev,1.45f,3.2f,projected));
     assert(std::abs(std::hypot(projected.horizontalMeters,
                       projected.heightDeltaMeters)-1.6f)<1e-5f);
     assert(std::abs(projected.sourceHeightMeters-
                     (1.45f+projected.heightDeltaMeters))<1e-6f);
     assert(sixsurface::validHeight({3.2f,1.45f,projected.sourceHeightMeters}));
     const float az=75.f;
     const auto floor=sixsurface::image(
         {3.2f,1.45f,projected.sourceHeightMeters},projected.horizontalMeters,
         az,sixsurface::Boundary::Floor,static_cast<float>(rate));
     const auto ceil=sixsurface::image(
         {3.2f,1.45f,projected.sourceHeightMeters},projected.horizontalMeters,
         az,sixsurface::Boundary::Ceiling,static_cast<float>(rate));
     assert(floor.elevation<0.f&&ceil.elevation>0.f);
     assert(floor.delaySamples>0.f&&ceil.delaySamples>0.f);
     assert(floor.pathMeters>1.6f&&ceil.pathMeters>1.6f);
     assert(sixsurface::fitsDelayRing(
         {3.2f,1.45f,projected.sourceHeightMeters},projected.horizontalMeters,
         static_cast<float>(rate),8192,140));
   }
   xyz::Placement rejected{};
   for(float invalid:{-40.f,60.f,75.f}) {
      assert(!xyz::project(1.6f,invalid,1.45f,3.2f,rejected));
   }
   assert(!xyz::project(1.6f,NAN,1.45f,3.2f,rejected));
   assert(!xyz::project(10.f,25.f,1.45f,3.2f,rejected));
   assert(!xyz::project(1.6f,25.f,1.45f,2.f,rejected));
   // Real measured direct HRTF, not synthetic equalization; ear mirror.
   for(float elevation:{-25.f,25.f,45.f}){
     hrir3d::EarBank left{},right{},horizontal{};
     hrir3d::interpolate(73.f,elevation,rate==44100.,left);
     hrir3d::interpolate(-73.f,elevation,rate==44100.,right);
     hrir3d::interpolate(73.f,0.f,rate==44100.,horizontal);
     double difference=0.;
     for(int ear=0;ear<2;++ear)
       for(int k=0;k<hrir::taps;++k){
          assert(std::abs(left[ear][k]-right[1-ear][k])<1e-6f);
          difference+=std::abs(left[ear][k]-horizontal[ear][k]);
       }
     assert(difference>.05);
   }
   for(float elev:{-25.f,25.f,45.f}){
     for(float spread:{0.f,60.f}){
       auto base=std::make_unique<SonKuSpatialCore>();
       auto exact=std::make_unique<SonKuSpatialCore>();
       auto chunks=std::make_unique<SonKuSpatialCore>();
       assert(base->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
       assert(exact->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
       assert(chunks->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
       assert(exact->setResearchDirectElevation(elev));
       assert(chunks->setResearchDirectElevation(elev));
       assert(!exact->setResearchDirectElevation(-40.f)); // impossible room
       assert(exact->researchDirectElevation()==elev);
       Settings s{};
       s.startAzimuthDegrees=33.f;s.moving=true;s.revolutionSeconds=3.1f;
       s.stereoSpreadDegrees=spread;s.spacePreset=1;s.roomAmount=.6f;
       s.ambience=.15f;s.sourceDistanceMeters=1.6f;s.outputTrim=.55f;
       assert(base->setSettings(s)&&exact->setSettings(s)&&chunks->setSettings(s));
       constexpr int length=16384;
       std::array<float,length> la{},ra{},lb{},rb{},lc{},rc{};
       for(int i=0;i<length;++i){
         const float t=static_cast<float>(i/rate);
         const float bass=.12f*std::sin(2.f*pi*220.f*t);
         const float treble=.06f*std::sin(2.f*pi*3200.f*t);
         const float voice=.03f*std::sin(2.f*pi*550.f*t);
         la[i]=lb[i]=lc[i]=bass+treble+voice;
         ra[i]=rb[i]=rc[i]=bass-treble+.7f*voice;
       }
       base->process(la.data(),ra.data(),length);
       exact->process(lb.data(),rb.data(),length);
       for(int at=0;at<length;){
         const int n=std::min((at%2)?257:64,length-at);
         chunks->process(lc.data()+at,rc.data()+at,n);at+=n;
       }
       double energy=0.,difference=0.,early=0.;
       for(int i=0;i<length;++i){
         assert(std::isfinite(lb[i])&&std::isfinite(rb[i]));
         assert(std::abs(lb[i])<1.f&&std::abs(rb[i])<1.f);
         assert(std::abs(lb[i]-lc[i])<2e-6f&&std::abs(rb[i]-rc[i])<2e-6f);
         energy+=lb[i]*lb[i]+rb[i]*rb[i];
         difference+=std::abs(lb[i]-la[i])+std::abs(rb[i]-ra[i]);
         if(i>=48 && i<400)early+=std::abs(lb[i]-la[i])+std::abs(rb[i]-ra[i]);
       }
       assert(energy>.01&&difference>.1&&early>1e-5);
       assert(!exact->setResearchDirectElevation(0.f)); // never mutate running PCM
       exact->reset();
       assert(!exact->setResearchDirectElevation(0.f)); // reset is NOT unpublish
       assert(exact->researchDirectElevation()==elev);
       assert(std::abs(exact->azimuthDegrees()-chunks->azimuthDegrees())<1e-7);
       // Device re-prepare uses the same explicit research placement,
       // but a switch to P6 clears research-only elevation.
       assert(exact->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
       assert(exact->researchDirectElevation()==elev);
       assert(exact->prepare(rate,SonKuReflectionProfile::DirectionalKemar));
       assert(exact->researchDirectElevation()==0.f);
       assert(!exact->setResearchDirectElevation(25.f));
     }
   }
 }
 std::puts("PASS SonKu P7.1 true XYZ: measured direct/room, bounded placement, mirrored HRTF, 44.1/48k, 64/257 PCM, quiescent-only control");
}
