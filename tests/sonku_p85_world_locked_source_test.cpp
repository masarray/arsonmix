// SPDX-License-Identifier: AGPL-3.0-only
// Source-native PCM proof for SonKu P8.5; NOT proof of headphone perception.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::Settings;
struct Audio {std::vector<float> l,r;};
static Audio render(int mode,int rate,bool room,int block,float world,float yaw,bool antiphase=false) {
 constexpr int frames=8192;
 auto core=std::make_unique<SonKuSpatialCore>();
 assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
 Settings s{};s.moving=false;s.startAzimuthDegrees=world;s.headYawDegrees=yaw;
 s.stereoSpreadDegrees=60.f;s.spatialMix=1.f;s.outputTrim=.75f;
 s.spacePreset=room?1:0;s.sourceDistanceMeters=1.6f;s.roomAmount=.55f;s.ambience=.3f;
 s.musicCompatibleLowFrequency=false;s.renderMode=mode;
 assert(core->setSettings(s));
 Audio a{std::vector<float>(frames),std::vector<float>(frames)};
 for(int i=0;i<frames;++i) {
  const float t=static_cast<float>(i)/rate;
  const float mid=.10f*std::sin(6.283185307179586f*83.f*t)
       +.05f*std::sin(6.283185307179586f*460.f*t);
  const float side=.045f*std::sin(6.283185307179586f*1710.f*t);
  const float impulse=(i==20)?.16f:0.f;
  a.l[i]=mid+side+impulse;
  a.r[i]=antiphase?(-mid-side-impulse):mid-side+impulse;
 }
 for(int i=0;i<frames;i+=block)
   core->process(a.l.data()+i,a.r.data()+i,std::min(frames-i,block));
 return a;
}
static double energy(const Audio& a) {
 double e=0;
 for(std::size_t i=0;i<a.l.size();++i) {
  assert(std::isfinite(a.l[i])&&std::isfinite(a.r[i]));
  assert(std::abs(a.l[i])<.99f&&std::abs(a.r[i])<.99f);
  e+=static_cast<double>(a.l[i])*a.l[i]+static_cast<double>(a.r[i])*a.r[i];
 }
 return e;
}
static double diff(const Audio& a,const Audio& b) {
 assert(a.l.size()==b.l.size());
 double e=0;
 for(std::size_t i=0;i<a.l.size();++i) {
  const double dl=a.l[i]-b.l[i],dr=a.r[i]-b.r[i];
  e+=dl*dl+dr*dr;
 }
 return e;
}
int main() {
 for(int rate:{44100,48000}) for(bool room:{false,true}) {
  for(bool antiphase:{false,true}) {
   const auto p84=render(3,rate,room,128,0.f,0.f,antiphase);
   const auto p85=render(4,rate,room,128,0.f,0.f,antiphase);
   const auto sliced=render(4,rate,room,64,0.f,0.f,antiphase);
   assert(energy(p84)>.01);
   assert(diff(p84,p85)==0.); // music must be EXACT at neutral yaw
   assert(diff(p85,sliced)==0.);
  }
  const auto yaw90=render(4,rate,room,128,0.f,90.f);
  const auto yaw90sliced=render(4,rate,room,64,0.f,90.f);
  assert(diff(yaw90,yaw90sliced)==0.);
  assert(energy(yaw90)>.01);
  if(!room) {
   // At world 180 and listener yaw 180 the effective HRTF is FRONT.
   // P8.4 had used world 180 as the MID HRIR (wrong frame).
   const auto front=render(3,rate,false,128,0.f,0.f);
   const auto corrected=render(4,rate,false,128,180.f,180.f);
   const auto legacy=render(3,rate,false,128,180.f,180.f);
   assert(diff(front,corrected)==0.);
   assert(diff(legacy,corrected)>1.e-7);
   std::printf("P85_WORLD_LOCK,rate=%d,wrong_frame_energy=%.9f,corrected=%.9f\n",
      rate,diff(legacy,front),diff(corrected,front));
  }
  std::printf("P85_PCM,rate=%d,room=%d,P84_NEUTRAL_BYTE_PARITY=PASS,64_128=PASS\n",
     rate,room?1:0);
 }
 std::puts("PASS P85 WORLD LOCK: real native MID yaw, P84 neutrality, antiphase and chunk parity");
}
