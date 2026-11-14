// SPDX-License-Identifier: AGPL-3.0-only
// P8.4 real-time renderMode=3: SAME native SonKuSpatialCore as JUCE.
// Gate the source-mode path, not only the earlier pre-PCM research setter.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::Settings;
using armix::sonku::SonKuReflectionProfile;
struct Audio {std::vector<float> l,r;};
static Audio render(bool fused,int rate,bool room,int block) {
 const int N=16384;
 Audio a{{std::vector<float>(N)},{std::vector<float>(N)}};
 auto core=std::make_unique<SonKuSpatialCore>();
 assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
 Settings s{};s.moving=false;s.startAzimuthDegrees=0.f;
 s.stereoSpreadDegrees=60.f;s.spatialMix=1.f;s.outputTrim=.75f;
 s.spacePreset=room?1:0;s.sourceDistanceMeters=1.6f;
 s.roomAmount=.55f;s.ambience=.30f;
 s.renderMode=fused?3:0;s.musicCompatibleLowFrequency=false;
 assert(core->setSettings(s));
 for(int i=0;i<N;++i) {
  const double t=i/static_cast<double>(rate);
  const float m=static_cast<float>(.12*std::sin(2*3.141592653589793*83*t)
      +.06*std::sin(2*3.141592653589793*461*t));
  const float side=static_cast<float>(.055*std::sin(2*3.141592653589793*1703*t));
  a.l[i]=m+side;
  a.r[i]=m-side;
 }
 for(int i=0;i<N;i+=block)
  core->process(a.l.data()+i,a.r.data()+i,std::min(block,N-i));
 return a;
}
static double energy(const Audio& x) {
 double sum=0;
 for(std::size_t i=0;i<x.l.size();++i){
  assert(std::isfinite(x.l[i])&&std::isfinite(x.r[i]));
  assert(std::abs(x.l[i])<1.f&&std::abs(x.r[i])<1.f);
  sum+=static_cast<double>(x.l[i])*x.l[i]+static_cast<double>(x.r[i])*x.r[i];
 }
 return sum;
}
static double difference(const Audio& x,const Audio& y) {
 double e=0;
 for(std::size_t i=0;i<x.l.size();++i){
  const double l=x.l[i]-y.l[i],r=x.r[i]-y.r[i];
  e+=l*l+r*r;
 }
 return e;
}
int main(){
 for(int rate:{44100,48000}) for(bool room:{false,true}){
  const auto golden=render(false,rate,room,128);
  const auto music=render(true,rate,room,128);
  const auto sliced=render(true,rate,room,64);
  const double baseline=energy(golden),candidate=energy(music);
  const double relative=10.*std::log10(candidate/baseline);
  const double contrast=std::sqrt(difference(golden,music)/baseline);
  std::printf("P84_V5_SOURCE,rate=%d,room=%d,delta_db=%.3f,normalized_contrast=%.5f\n",
              rate,room,relative,contrast);
  assert(difference(music,sliced)==0);
  assert(std::isfinite(relative)&&std::abs(relative)<1.5);
  assert(contrast>.004 && contrast<.5);
 }
 std::puts("PASS P84 V5 NATIVE SOURCE: real render mode 3, golden A/B, 44.1/48k room OFF/ON PCM 64/128 exact parity");
}
