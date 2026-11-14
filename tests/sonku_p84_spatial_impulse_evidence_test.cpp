// SPDX-License-Identifier: AGPL-3.0-only
// Real one-core native P8.4 spatial impulse diagnostics.
// Distinct measured ITD/ILD, frontal/rear spectral cues and world-yaw room
// tails are NECESSARY source-level evidence, NOT headphone externalization.
// No JUCE, no human perception, no fabricated second PCM engine.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::Settings;
struct Sound {std::vector<float> l,r;};
static void gate(bool v,const char* s) {
 if(!v){std::fprintf(stderr,"FAIL P84 SPATIAL: %s\n",s);std::abort();}
}
static Sound impulse(double rate,float az,float yaw,bool room) {
 auto dsp=std::make_unique<SonKuSpatialCore>();
 gate(dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch),"prepare");
 Settings s{};s.moving=false;s.startAzimuthDegrees=az;
 s.stereoSpreadDegrees=60.f;s.spatialMix=1.f;s.outputTrim=.75f;
 s.sourceDistanceMeters=1.6f;s.spacePreset=room?1:0;
 s.roomAmount=.55f;s.ambience=.30f;
 gate(dsp->setSettings(s),"settings");
 gate(dsp->setResearchHeadYaw(yaw),"yaw");
 gate(dsp->setResearchFusedMusicClarity(true),"fusion admission");
 constexpr int n=8192;
 Sound a{{std::vector<float>(n)},{std::vector<float>(n)}};
 a.l[1024]=.20f;a.r[1024]=.20f; // true mono program: no SIDE rescue
 for(int i=0;i<n;i+=128)dsp->process(a.l.data()+i,a.r.data()+i,128);
 return a;
}
static double e(const Sound& a,int begin,int end,int ear=-1) {
 double sum=0;
 for(int i=begin;i<end;++i){
  if(ear!=1)sum+=static_cast<double>(a.l[i])*a.l[i];
  if(ear!=0)sum+=static_cast<double>(a.r[i])*a.r[i];
 }
 return sum;
}
static double d(const Sound& a,const Sound& b,int begin,int end) {
 double sum=0;
 for(int i=begin;i<end;++i){
  const double l=a.l[i]-b.l[i],r=a.r[i]-b.r[i];
  sum+=l*l+r*r;
 }
 return sum;
}
static double db(double x,double y) {
 return 10.*std::log10((x+1.e-25)/(y+1.e-25));
}
struct Iacc {double value=0.;int lag=0;};
static Iacc iacc(const Sound& a,int begin,int end) {
 const double norm=std::sqrt(e(a,begin,end,0)*e(a,begin,end,1));
 if(norm<1.e-20)return {};
 Iacc out{};
 for(int lag=-40;lag<=40;++lag){
  double sum=0.;
  for(int i=std::max(begin,begin-lag);i<std::min(end,end-lag);++i)
   sum+=static_cast<double>(a.l[i])*a.r[i+lag];
  const double c=std::abs(sum)/norm;
  if(c>out.value)out={std::min(1.,c),lag};
 }
 return out;
}
int main(){
 for(double rate:{44100.,48000.}){
  const auto front=impulse(rate,0.f,0.f,false);
  const auto rear=impulse(rate,180.f,0.f,false);
  const auto left=impulse(rate,-90.f,0.f,false);
  const auto right=impulse(rate,90.f,0.f,false);
  const auto frontRoom=impulse(rate,0.f,0.f,true);
  const auto frontRoomTurned=impulse(rate,0.f,90.f,true);
  const auto frontTurned=impulse(rate,0.f,90.f,false);
  constexpr int start=1024, directEnd=1250;
  const double frontRear=std::sqrt(d(front,rear,start,directEnd)/
                                    (e(front,start,directEnd)+1.e-20));
  const double leftRight=std::sqrt(d(left,right,start,directEnd)/
                                    (e(left,start,directEnd)+1.e-20));
  const double leftIld=db(e(left,start,directEnd,1),
                          e(left,start,directEnd,0));
  const double rightIld=db(e(right,start,directEnd,1),
                           e(right,start,directEnd,0));
  const Iacc l=iacc(left,start,directEnd);
  const Iacc rr=iacc(right,start,directEnd);
  // Early/late acoustic tail is isolated after the direct ~140-tap HRIR
  // and after the 220-Hz mono bass lowpass has decayed.
  constexpr int tailStart=1600,tailEnd=4700;
  const double tailRoom=e(frontRoom,tailStart,tailEnd);
  const double tailDry=e(front,tailStart,tailEnd);
  const double tailMotion=d(frontRoom,frontRoomTurned,tailStart,tailEnd);
  const double motionDirect=d(front,frontTurned,start,directEnd);
  const double approxDrr=db(e(frontRoom,start,directEnd),tailRoom);
  std::printf("P84_SPATIAL,rate=%.0f,front_rear_diff=%.6f,left_right_diff=%.6f,"
    "left_ILD_dB=%.3f,right_ILD_dB=%.3f,left_IACC=%.5f,left_lag=%d,"
    "right_IACC=%.5f,right_lag=%d,room_DRR_window_dB=%.3f,"
    "room_tail=%.9g,dry_tail=%.9g,world_yaw_tail_delta=%.9g,"
    "world_yaw_direct_delta=%.9g\n",
    rate,frontRear,leftRight,leftIld,rightIld,l.value,l.lag,
    rr.value,rr.lag,approxDrr,tailRoom,tailDry,tailMotion,motionDirect);
  gate(std::isfinite(frontRear)&&frontRear>.05,"front/back source HRIR cannot be identical");
  gate(std::isfinite(leftRight)&&leftRight>.10,"left/right source HRIR cannot be identical");
  gate(leftIld<-.3&&rightIld>.3,"signed left-right HRTF ILD must be directional");
  gate(l.value>0.&&l.value<=1.&&rr.value>0.&&rr.value<=1.,"bounded direct IACC");
  gate(tailRoom>1.e-12&&tailRoom>tailDry*1.1,
       "room must produce actual delayed binaural energy");
  gate(tailMotion>1.e-14&&motionDirect>1.e-12,
       "both ROOM and direct must respond to world-fixed head yaw");
 }
 std::puts("PASS P84 SPATIAL SOURCE: measured front/rear and left/right,"
   " ILD/IACC, delayed room energy and room+direct head yaw; "
   "NO HUMAN EXTERNALIZATION OR HEAD TRACKING CLAIM");
}
