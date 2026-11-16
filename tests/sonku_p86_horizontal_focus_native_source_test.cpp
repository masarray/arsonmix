// SPDX-License-Identifier: AGPL-3.0-only
// Actual P8.6 native DSP: P8.5 direct/side unchanged, wet room focuses
// at the listener's horizontal speaker plane. No subjective claims.
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
using armix::sonku::Settings;
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuEarlyReflections;
using armix::sonku::SonKuRoomGeometry;
using armix::sonku::SonKuReflectionProfile;
struct Audio {std::vector<float> l,r;};
static Audio render(int mode,int rate,bool room,int block,float az,float yaw,
                    bool antiphase=false) {
 constexpr int frames=12288;
 auto core=std::make_unique<SonKuSpatialCore>();
 assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
 Settings s{};
 s.renderMode=mode;s.musicCompatibleLowFrequency=false;
 s.moving=false;s.startAzimuthDegrees=az;s.headYawDegrees=yaw;
 s.stereoSpreadDegrees=60.f;s.spatialMix=1.f;s.outputTrim=.75f;
 s.sourceDistanceMeters=1.6f;s.spacePreset=room?1:0;
 s.roomAmount=1.f;s.ambience=.47f;s.directElevationDegrees=0.f;
 assert(core->setSettings(s));
 Audio a{std::vector<float>(frames),std::vector<float>(frames)};
 for(int i=0;i<frames;++i) {
  const float t=static_cast<float>(i)/rate;
  const float bass=.14f*std::sin(6.283185307179586f*81.f*t);
  const float vocal=.06f*std::sin(6.283185307179586f*347.f*t);
  const float side=.046f*std::sin(6.283185307179586f*2111.f*t);
  const float transient=i==24?.12f:0.f;
  a.l[i]=bass+vocal+side+transient;
  a.r[i]=antiphase?(-bass-vocal-side-transient):bass+vocal-side+transient;
 }
 for(int i=0;i<frames;i+=block)
  core->process(a.l.data()+i,a.r.data()+i,std::min(block,frames-i));
 return a;
}
static double energy(const Audio& a) {
 double v=0.;
 for(std::size_t i=0;i<a.l.size();++i) {
  const double l=a.l[i],r=a.r[i];
  assert(std::isfinite(l)&&std::isfinite(r));
  assert(std::abs(l)<.99&&std::abs(r)<.99);
  v+=l*l+r*r;
 }
 return v;
}
static double diff(const Audio& a,const Audio& b) {
 double v=0.;
 for(std::size_t i=0;i<a.l.size();++i) {
  const double l=a.l[i]-b.l[i],r=a.r[i]-b.r[i];
  v+=l*l+r*r;
 }
 return v;
}
static double wall(int rate,int wallIndex,bool focus) {
 SonKuEarlyReflections room;
 assert(room.prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
 assert(room.setResearchWallMask(static_cast<std::uint8_t>(1u<<wallIndex)));
 SonKuRoomGeometry g{};g.widthMeters=7.2f;g.depthMeters=7.2f;
 g.distanceMeters=1.6f;g.heightMeters=3.2f;g.sourceHeightMeters=1.45f;
 g.listenerHeightMeters=1.45f;
 assert(room.setRealtimeRoom(g,1.f,true));
 double sum=0.;
 for(int i=0;i<12000;++i) {
  const float x=(i==20?.4f:0.f)+
      .09f*std::sin(6.283185307179586f*311.f*static_cast<float>(i)/rate);
  const auto y=room.process(x,x,0.,60.f,0.f,focus);
  assert(std::isfinite(y.left)&&std::isfinite(y.right));
  sum+=static_cast<double>(y.left)*y.left+
       static_cast<double>(y.right)*y.right;
 }
 return sum;
}
int main() {
 for(int rate:{44100,48000}) {
  for(float az:{0.f,90.f,180.f})for(float yaw:{0.f,60.f,180.f}) {
   for(bool anti:{false,true}){
    const auto baseline=render(4,rate,false,128,az,yaw,anti);
    const auto focus=render(5,rate,false,128,az,yaw,anti);
    const auto sliced=render(5,rate,false,64,az,yaw,anti);
    assert(energy(baseline)>0.01);
    assert(diff(baseline,focus)==0.); // EXACT DRY musical and heading parity
    assert(diff(focus,sliced)==0.);
   }
  }
  // Isolation: horizontal wall IR is identical; floor and ceiling retain
  // non-zero physical image paths at exact bounded ratios, not muted.
  for(int w=0;w<6;++w) {
   const double p85=wall(rate,w,false),p86=wall(rate,w,true);
   assert(p85>1.e-8 && p86>1.e-10);
   const double expected=w==4?.24* .24:w==5?.15*.15:1.;
   const double ratio=p86/p85;
   std::printf("P86_WALL,rate=%d,wall=%d,ratio=%.5f,expected=%.5f\n",
      rate,w,ratio,expected);
   assert(std::abs(ratio-expected)<0.002);
  }
  for(bool anti:{false,true}){
   const auto old=render(4,rate,true,128,90.f,60.f,anti);
   const auto focused=render(5,rate,true,128,90.f,60.f,anti);
   const auto sliced=render(5,rate,true,64,90.f,60.f,anti);
   assert(diff(focused,sliced)==0.);
   assert(diff(old,focused)>1.e-6); // real PCM wet difference
   const double delta=10.*std::log10(energy(focused)/energy(old));
   assert(std::isfinite(delta));
   std::printf("P86_SCENE,rate=%d,antiphase=%d,total_delta_db=%.4f,pcm_difference=%.7f\n",
      rate,anti?1:0,delta,diff(old,focused));
   // Wet level reduction is intentional; the direct-only sound is exact.
   assert(delta>-5. && delta<2.);
  }
 }
 std::puts("PASS P86 HORIZONTAL FOCUS: measured wall IR ratios, unchanged direct music and block parity");
}
