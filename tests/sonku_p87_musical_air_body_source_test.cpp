// SPDX-License-Identifier: AGPL-3.0-only
// P8.7 measured actual native PCM. Objective phase/energy only;
// no false claim that synthetic tests equal headphone user FAT.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P87_MUSIC_BODY_RESEARCH) || !SONKU_P87_MUSIC_BODY_RESEARCH
#error Must compile explicit opt-in P8.7 source engine
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuP87MusicalAirBody.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using armix::sonku::Settings;
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
constexpr float pi=3.14159265358979323846f;
struct Audio{std::vector<float> l,r;};
static Audio scene(int mode,int rate,int room,int block,float az,float yaw,
                   bool anti=false,float distance=1.6f){
 constexpr int frames=12288;
 auto core=std::make_unique<SonKuSpatialCore>();
 assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
 Settings s{};s.renderMode=mode;s.moving=false;s.musicCompatibleLowFrequency=false;
 s.startAzimuthDegrees=az;s.headYawDegrees=yaw;s.stereoSpreadDegrees=60.f;
 s.spatialMix=1.f;s.outputTrim=.75f;s.spacePreset=room;
 s.sourceDistanceMeters=distance;s.roomAmount=1.f;s.ambience=.47f;
 assert(core->setSettings(s));
 Audio a{{std::vector<float>(frames)},{std::vector<float>(frames)}};
 for(int i=0;i<frames;++i){
  const float t=static_cast<float>(i)/rate;
  const float mid=.11f*std::sin(2.f*pi*83.f*t)+.047f*std::sin(2.f*pi*444.f*t);
  const float side=.037f*std::sin(2.f*pi*4700.f*t)+.016f*std::sin(2.f*pi*8760.f*t);
  const float transient=i==20?.09f:0.f;
  a.l[i]=mid+side+transient;
  a.r[i]=anti?(-mid-side-transient):mid-side+transient;
 }
 for(int i=0;i<frames;i+=block)
  core->process(a.l.data()+i,a.r.data()+i,std::min(block,frames-i));
 return a;
}
static double energy(const Audio& a){
 double v=0.;
 for(unsigned i=0;i<a.l.size();++i){
  const double l=a.l[i],r=a.r[i];
  assert(std::isfinite(l)&&std::isfinite(r));
  assert(std::abs(l)<.98&&std::abs(r)<.98);
  v+=l*l+r*r;
 }return v;
}
static double diff(const Audio& a,const Audio& b){
 assert(a.l.size()==b.l.size());
 double v=0.;
 for(unsigned i=0;i<a.l.size();++i){
  const double dl=a.l[i]-b.l[i],dr=a.r[i]-b.r[i];
  v+=dl*dl+dr*dr;
 }return v;
}
static double toneResponse(int rate,float hz,bool direct){
 armix::sonku::p87::MusicalAirBody filter;
 assert(filter.prepare(rate));
 constexpr int n=16384;
 double baseline=0,after=0;
 for(int i=0;i<n;++i){
  const float x=.15f*std::sin(2.f*pi*hz*i/rate);
  float l=x,r=x;
  if(direct)filter.enhanceDirect(l,r);
  else {const auto b=filter.diffuseLowBody(l,r);l+=b.left;r+=b.right;}
  if(i>n/2){baseline+=x*x;after+=l*l;}
 }
 return std::sqrt(after/baseline);
}
int main(){
 for(int rate:{44100,48000}){
  // Direct high-air is bounded and monotonically grows without a narrow Q.
  const double air1=toneResponse(rate,1000.f,true);
  const double air8=toneResponse(rate,8000.f,true);
  const double air12=toneResponse(rate,12000.f,true);
  const double body85=toneResponse(rate,85.f,false);
  const double body8k=toneResponse(rate,8000.f,false);
  std::printf("P87_FILTER,rate=%d,air1k=%.5f,air8k=%.5f,air12k=%.5f,body85=%.5f,body8k=%.5f\n",
   rate,air1,air8,air12,body85,body8k);
  assert(air1>1.0&&air1<1.16);
  assert(air8>1.13&&air8<1.28);
  assert(air12>air8&&air12<1.29);
  assert(body85>1.30&&body85<1.46);
  assert(body8k>1.0&&body8k<1.05);
  // Safety-critical: selecting P8.7 while ROOM OFF MUST NOT alter
  // any P8.6 direct, bass or side PCM. 64/128 block sizes are exact.
  for(float az:{0.f,90.f,180.f})for(float yaw:{0.f,60.f,180.f})
   for(bool anti:{false,true}){
    const auto p86=scene(5,rate,0,128,az,yaw,anti);
    const auto p87=scene(6,rate,0,128,az,yaw,anti);
    const auto sliced=scene(6,rate,0,64,az,yaw,anti);
    assert(energy(p86)>.01);
    assert(diff(p86,p87)==0.);
    assert(diff(p87,sliced)==0.);
   }
  // ROOM ON: bounded improvement in wet tonal spectrum plus intact focus.
  // Run both near 1.6 m and a larger 28.8 m scene at owner-like distance.
  for(int room:{1,3})for(bool anti:{false,true}){
   const float distance=room==3?6.1f:1.6f;
   const auto p86=scene(5,rate,room,128,90.f,60.f,anti,distance);
   const auto p87=scene(6,rate,room,128,90.f,60.f,anti,distance);
   const auto sliced=scene(6,rate,room,64,90.f,60.f,anti,distance);
   const double e86=energy(p86),e87=energy(p87);
   const double d=diff(p86,p87);
   assert(diff(p87,sliced)==0.);
   assert(d>1.e-7 && e86>.01 && e87>.01);
   const double db=10.*std::log10(e87/e86);
   std::printf("P87_NATIVE_SCENE,rate=%d,room=%d,anti=%d,db=%.4f,pcm_delta=%.8f\n",
      rate,room,anti?1:0,db,d);
   assert(std::isfinite(db)&&db>-3.&&db<3.);
  }
 }
 std::puts("PASS P87 MUSICAL AIR BODY: direct PCM parity, bounded HF and FDN LF gains, anti-phase and block parity");
}
