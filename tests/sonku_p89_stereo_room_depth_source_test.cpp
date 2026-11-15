// SPDX-License-Identifier: AGPL-3.0-only
// P8.9 P8.8 AB: no head-locked stereo collapse, no muted music, no reverb-only fake stage.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P89_DEPTH_COHERENCE_RESEARCH) || !SONKU_P89_DEPTH_COHERENCE_RESEARCH
#error P8.9 real native source build gate required
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
struct Stereo{std::vector<float> left,right;};
static constexpr float pi=3.14159265358979323846f;
static Stereo render(int mode,int rate,int chunk,int room,float distance,
                     float early,float tail,float yaw,bool anti=false,
                     float spectralTrim=-10.f,float mix=1.f){
 constexpr int frames=16384;
 auto fx=std::make_unique<SonKuSpatialCore>();
 assert(fx->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
 Settings s{};s.renderMode=mode;s.spacePreset=room;
 s.sourceDistanceMeters=distance;s.roomAmount=early;s.ambience=tail;
 s.moving=false;s.startAzimuthDegrees=24.f;s.stereoSpreadDegrees=60.f;
 s.headYawDegrees=yaw;s.directElevationDegrees=0.f;
 s.hrtfElevationTrimDegrees=spectralTrim;s.spatialMix=mix;s.outputTrim=.75f;
 assert(fx->setSettings(s));
 Stereo out{std::vector<float>(frames),std::vector<float>(frames)};
 for(int i=0;i<frames;++i){
  const float t=static_cast<float>(i)/static_cast<float>(rate);
  const float mid=.17f*std::sin(2*pi*87.f*t)+.11f*std::sin(2*pi*640.f*t)
                 +.05f*std::sin(2*pi*4800.f*t);
  const float side=.07f*std::sin(2*pi*1700.f*t);
  out.left[i]=mid+side+(i==31?.09f:0.f);
  out.right[i]=anti?-out.left[i]:mid-side+(i==31?.09f:0.f);
 }
 for(int i=0;i<frames;i+=chunk)
  fx->process(out.left.data()+i,out.right.data()+i,std::min(chunk,frames-i));
 return out;
}
static double power(const Stereo& s,int first=4096){
 double result=0;
 for(std::size_t i=0;i<s.left.size();++i){
  assert(std::isfinite(s.left[i])&&std::isfinite(s.right[i]));
  assert(std::abs(s.left[i])<.981f&&std::abs(s.right[i])<.981f);
  if(i>=static_cast<std::size_t>(first))
   result+=double(s.left[i])*s.left[i]+double(s.right[i])*s.right[i];
 }
 return result;
}
static double difference(const Stereo& x,const Stereo& y){
 assert(x.left.size()==y.left.size());double d=0;
 for(std::size_t i=0;i<x.left.size();++i){
  const double l=double(x.left[i])-y.left[i],r=double(x.right[i])-y.right[i];
  d+=l*l+r*r;
 }
 return d;
}
static double earlyDifferencePower(const Stereo& wet,const Stereo& noEr){
 assert(wet.left.size()==noEr.left.size());double e=0;
 for(std::size_t i=1200;i<11500;++i){
  const double l=double(wet.left[i])-noEr.left[i];
  const double r=double(wet.right[i])-noEr.right[i];
  e+=l*l+r*r;
 }return e;
}
int main(){
 for(int rate:{44100,48000}){
  // P8.8 PCM is an immutable reference; P8.9 ROOM OFF must be bit-identical
  // for correlated and 180-degree antiphase music at all head orientations.
  for(float yaw:{0.f,60.f,180.f})for(bool anti:{false,true}){
   auto old=render(7,rate,128,0,1.6f,1.f,1.f,yaw,anti);
   auto now=render(8,rate,128,0,1.6f,1.f,1.f,yaw,anti);
   auto sliced=render(8,rate,64,0,1.6f,1.f,1.f,yaw,anti);
   assert(difference(old,now)==0.);
   assert(difference(now,sliced)==0.);
   assert(power(now)>.002);
  }
  // NO arbitrary added coloration at the approved close 1.6m, even with
  // measured wall HRIR and FDN live. Only farther listening changes balance.
  for(bool anti:{false,true}){
   auto old=render(7,rate,128,3,1.6f,1.f,1.f,0.f,anti);
   auto now=render(8,rate,128,3,1.6f,1.f,1.f,0.f,anti);
   assert(difference(old,now)==0.);
  }
  // At 7.3m direct only, full antiphase music SIDE remains strongly audible,
  // but headlocked SIDE becomes less dominant. Bass is retained by P8.8 LF.
  auto oldDirect=render(7,rate,128,3,7.3f,0.f,0.f,0.f,true);
  auto newDirect=render(8,rate,128,3,7.3f,0.f,0.f,0.f,true);
  assert(power(oldDirect)>.001&&power(newDirect)>.001);
  const double ratio=power(newDirect)/power(oldDirect);
  assert(ratio>.42&&ratio<.99);
  // Horizontal measured early wall reflections gain relevance relative to
  // the headphone SIDE. Test the real difference, not unrendered coefficients.
  const auto p88Wet=render(7,rate,128,3,7.3f,1.f,0.f,0.f,false);
  const auto p88Dry=render(7,rate,128,3,7.3f,0.f,0.f,0.f,false);
  const auto p89Wet=render(8,rate,128,3,7.3f,1.f,0.f,0.f,false);
  const auto p89Dry=render(8,rate,128,3,7.3f,0.f,0.f,0.f,false);
  const double refEr=earlyDifferencePower(p88Wet,p88Dry);
  const double focusedEr=earlyDifferencePower(p89Wet,p89Dry);
  assert(refEr>1.e-10&&focusedEr>refEr*1.01);
  // Owner stress: 100% early and reverb, measured HRTF, 7.3m LARGE;
  // chunk independence, no zero output, no NaN, no output hard clipping.
  for(bool anti:{false,true}){
   const auto stressed=render(8,rate,128,3,7.3f,1.f,1.f,0.f,anti);
   const auto smaller=render(8,rate,64,3,7.3f,1.f,1.f,0.f,anti);
   assert(difference(stressed,smaller)==0.);
   assert(power(stressed)>.005);
  }
  // Legacy/offline dry mix still bit-identical and independent of P8.9.
  auto dry=render(8,rate,128,3,7.3f,1.f,1.f,0.f,false,-10.f,0.f);
  auto dryP88=render(7,rate,128,3,7.3f,1.f,1.f,0.f,false,-10.f,0.f);
  assert(difference(dry,dryP88)==0.);
  Settings invalid{};invalid.renderMode=8;invalid.spacePreset=3;
  invalid.sourceDistanceMeters=7.3f;invalid.hrtfElevationTrimDegrees=-10.f;
  assert(armix::sonku::valid(invalid));
  invalid.hrtfElevationTrimDegrees=-35.f;
  assert(!armix::sonku::valid(invalid));
  std::printf("P89_DEPTH_RATE,%d,anti_side_ratio=%.6f,ER_ratio=%.6f\n",
              rate,ratio,focusedEr/refEr);
 }
 std::puts("PASS P89 ROOM ANCHOR: 44.1/48k exact P88 OFF and close-field, live far directional ER vs headphone SIDE, phase-safe MUSIC, 64/128");
}
