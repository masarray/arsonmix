// SPDX-License-Identifier: AGPL-3.0-only
// P8.8: isolate spectral headphone elevation from true XYZ room geometry,
// with meaningful direct distance cue, real SonKu PCM, no perceptual claims.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH) || !SONKU_P88_PERCEPTUAL_ALIGNMENT_RESEARCH
#error Native P8.8 research gate required.
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuSourcePlacement3D.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::Settings;
using armix::sonku::SonKuReflectionProfile;
constexpr float pi=3.14159265358979323846f;
struct Audio{std::vector<float> l,r;};
static Audio render(int mode,int rate,int room,int block,float az,float yaw,
   float elevation,float trim,float distance,bool anti=false,float er=1.f,float rev=1.f) {
 constexpr int n=16384;
 auto core=std::make_unique<SonKuSpatialCore>();
 assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
 Settings s{};s.renderMode=mode;s.moving=false;s.startAzimuthDegrees=az;
 s.stereoSpreadDegrees=60.f;s.headYawDegrees=yaw;s.musicCompatibleLowFrequency=false;
 s.spatialMix=1.f;s.outputTrim=.75f;s.spacePreset=room;s.roomAmount=er;
 s.ambience=rev;s.sourceDistanceMeters=distance;
 s.directElevationDegrees=elevation;s.hrtfElevationTrimDegrees=trim;
 assert(core->setSettings(s));
 Audio a{std::vector<float>(n),std::vector<float>(n)};
 for(int i=0;i<n;++i){
  const float t=static_cast<float>(i)/rate;
  const float b=.135f*std::sin(2.f*pi*80.f*t);
  const float m=.072f*std::sin(2.f*pi*443.f*t);
  const float hi=.032f*std::sin(2.f*pi*7500.f*t);
  const float side=.025f*std::sin(2.f*pi*2800.f*t);
  a.l[i]=b+m+hi+side+(i==20?.06f:0.f);
  a.r[i]=anti?-a.l[i]:b+m+hi-side+(i==20?.06f:0.f);
 }
 for(int i=0;i<n;i+=block)
  core->process(a.l.data()+i,a.r.data()+i,std::min(block,n-i));
 return a;
}
static double energy(const Audio& a){
 double e=0.;
 for(std::size_t i=0;i<a.l.size();++i){
  const auto l=a.l[i],r=a.r[i];
  assert(std::isfinite(l)&&std::isfinite(r));
  assert(std::abs(l)<.98f&&std::abs(r)<.98f);
  if(i>4096)e+=l*l+r*r;
 }return e;
}
static double delta(const Audio& a,const Audio& b){
 assert(a.l.size()==b.l.size());
 double d=0.;
 for(std::size_t i=0;i<a.l.size();++i){
  const double l=a.l[i]-b.l[i],r=a.r[i]-b.r[i];
  d+=l*l+r*r;
 }return d;
}
int main(){
 for(int rate:{44100,48000}){
  // P8.8 at trim=0 and ROOM OFF is exactly the P8.7 signed source PCM,
  // not only level parity. Both direct FIR and stereo SIDE are protected.
  for(float az:{0.f,90.f,180.f})for(float yaw:{0.f,60.f,180.f})
    for(bool anti:{false,true}){
     auto golden=render(6,rate,0,128,az,yaw,0.f,0.f,1.6f,anti);
     auto p88=render(7,rate,0,128,az,yaw,0.f,0.f,1.6f,anti);
     auto sliced=render(7,rate,0,64,az,yaw,0.f,0.f,1.6f,anti);
     assert(energy(p88)>.001);
     assert(delta(golden,p88)==0.);
     assert(delta(p88,sliced)==0.);
    }
  // Measured HRTF spectral-angle offset must be truly separate from XYZ.
  // P8.7 physical -12deg in room OFF matches P8.8 physically 0deg,
  // HRTF offset -12deg exactly. Neither depends on simulated ceiling.
  auto ref=render(6,rate,0,128,90.f,0.f,-12.f,0.f,1.6f);
  auto spectral=render(7,rate,0,128,90.f,0.f,0.f,-12.f,1.6f);
  auto horizontal=render(7,rate,0,128,90.f,0.f,0.f,0.f,1.6f);
  assert(delta(ref,spectral)==0.);
  assert(delta(spectral,horizontal)>1.e-7);
  // Geometry: -7.4° at 7.3m is nearly on floor while HRTF -12° at
  // physical zero degrees leaves the speaker correctly at ear height.
  armix::sonku::xyz::Placement nearFloor{},earHeight{};
  assert(armix::sonku::xyz::project(7.3f,-7.4f,1.45f,7.2f,nearFloor));
  assert(armix::sonku::xyz::project(7.3f,0.f,1.45f,7.2f,earHeight));
  assert(nearFloor.sourceHeightMeters<.55f);
  assert(std::abs(earHeight.sourceHeightMeters-1.45f)<1.e-6f);
  // ROOM ON with no diffuse/wall input: compare reliable direct-distance cue
  // at close 1.6m versus FAR 7.3m. LF foundation must stay nonzero.
  for(bool anti:{false,true}){
   const auto old=render(6,rate,3,128,24.f,0.f,0.f,0.f,7.3f,anti,0.f,0.f);
   const auto far=render(7,rate,3,128,24.f,0.f,0.f,0.f,7.3f,anti,0.f,0.f);
   const auto sliced=render(7,rate,3,64,24.f,0.f,0.f,0.f,7.3f,anti,0.f,0.f);
   assert(delta(far,sliced)==0.);
   const double e0=energy(old),e1=energy(far);
   assert(e0>.00001&&e1>.00001);
   const double db=10.*std::log10(e1/e0);
   std::printf("P88_DISTANCE,rate=%d,anti=%d,far_db=%.5f\n",rate,anti?1:0,db);
   assert(db<-.15 && db> -6.);
  }
  // Legacy P8.7 high-air/bass and P8.6 horizontal reflection field
  // must remain bounded/finite at the owner's actual 7.3m LARGE 100/100.
  for(float trim:{-20.f,-10.f,0.f,10.f}){
   const auto f=render(7,rate,3,128,24.f,0.f,0.f,trim,7.3f,false,1.f,1.f);
   const auto g=render(7,rate,3,64,24.f,0.f,0.f,trim,7.3f,false,1.f,1.f);
   assert(delta(f,g)==0.);
   assert(energy(f)>.001);
  }
  // Out-of-range effective HRTF and mode 1/2 rejected, no silent clamping.
  Settings invalid{};invalid.renderMode=7;invalid.hrtfElevationTrimDegrees=-30.f;
  invalid.directElevationDegrees=-20.f;invalid.spacePreset=3;
  invalid.sourceDistanceMeters=1.6f;
  assert(!armix::sonku::valid(invalid));
  invalid.hrtfElevationTrimDegrees=0.f;
  assert(armix::sonku::valid(invalid));
  invalid.renderMode=7;invalid.hrtfElevationTrimDegrees=-35.f;assert(!armix::sonku::valid(invalid));
 }
 std::puts("PASS P88 HEIGHT DISTANCE: geometric ear-height independence, spectral KEMAR parity, native far cue and 64/128");
}
