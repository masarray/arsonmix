// SPDX-License-Identifier: AGPL-3.0-only
// Hosted CPU evidence only: NOT a Windows ASIO realtime guarantee.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::Settings;
struct Time {double p50=0.,p95=0.,p99=0.;int late=0;};
static Time run(double rate,int frames,int instances,bool fusion){
 std::array<std::unique_ptr<SonKuSpatialCore>,4> cores{};
 Settings settings{};
 settings.moving=false;settings.spatialMix=1.f;settings.outputTrim=.75f;
 settings.stereoSpreadDegrees=60.f;settings.spacePreset=1;
 settings.sourceDistanceMeters=1.6f;
 settings.roomAmount=.55f;settings.ambience=.3f;
 for(int j=0;j<instances;++j){
  cores[j]=std::make_unique<SonKuSpatialCore>();
  if(!cores[j]->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch) ||
     !cores[j]->setSettings(settings) ||
     !cores[j]->setResearchFusedMusicClarity(fusion))
   std::abort();
 }
 std::array<std::array<float,256>,4> l{},rr{};
 std::vector<double> measurements;measurements.reserve(96);
 for(int frame=0;frame<120;++frame){
  for(int j=0;j<instances;++j)
   for(int i=0;i<frames;++i) {
    const double t=(frame*frames+i)/rate;
    l[j][i]=static_cast<float>(.06*std::sin(2.*3.141592653589793*311*t)+
                              .02*std::sin(2.*3.141592653589793*1211*t));
    rr[j][i]=static_cast<float>(.04*std::sin(2.*3.141592653589793*719*t)-
                               .015*std::sin(2.*3.141592653589793*2833*t));
   }
  const auto start=std::chrono::steady_clock::now();
  for(int j=0;j<instances;++j)
    cores[j]->process(l[j].data(),rr[j].data(),frames);
  const auto end=std::chrono::steady_clock::now();
  if(frame>=24)
   measurements.push_back(std::chrono::duration<double,std::micro>(
     end-start).count());
 }
 std::sort(measurements.begin(),measurements.end());
 const auto q=[&](double v){return measurements[
   static_cast<std::size_t>(v*(measurements.size()-1))];};
 const double deadline=1.e6*frames/rate;
 const int late=static_cast<int>(std::count_if(measurements.begin(),
   measurements.end(),[&](double d){return d>deadline;}));
 return {q(.5),q(.95),q(.99),late};
}
int main(){
 bool withinBound=true;
 for(double rate:{44100.,48000.})
 for(int buffer:{64,128,256})
 for(int inserts:{1,2,4}){
  const Time base=run(rate,buffer,inserts,false);
  const Time fused=run(rate,buffer,inserts,true);
  const double medianRatio=fused.p50/std::max(base.p50,1.);
  std::printf("P84_SINGLE_CPU,rate=%.0f,buffer=%d,inserts=%d,"
    "p82_p50_us=%.2f,p82_p99_us=%.2f,p84_p50_us=%.2f,p84_p95_us=%.2f,"
    "p84_p99_us=%.2f,median_ratio=%.3f,p84_over_deadline=%d\n",
    rate,buffer,inserts,base.p50,base.p99,fused.p50,fused.p95,
    fused.p99,medianRatio,fused.late);
  // One 4-FIR core instead of 2-FIR Music Bed can be slower, but
  // must not grow beyond 2.6x median CPU. p99 & XRUN remain evidence
  // not an actionable hosted-VM failure due to scheduler variance.
  withinBound &= std::isfinite(medianRatio)&&medianRatio<=2.6;
 }
 if(!withinBound) {
  std::fputs("FAIL P84 SINGLE CPU: complexity regression vs P8.2 exceeds 2.6x\n",stderr);
  return 1;
 }
 std::puts("PASS P84 SINGLE CPU: single PCM core P8.2 vs P8.4 44.1/48k "
  "64/128/256 1/2/4 native inserts, median ratio bounded; "
  "real ASIO XRUN and UI restart NOT PROVEN");
}
