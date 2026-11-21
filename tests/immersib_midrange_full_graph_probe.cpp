// SPDX-License-Identifier: AGPL-3.0-only
// Diagnostic only: actual ImmerSIB SonKu P9 2x2 graph, finished L/R mono
// and correlated pan tones. Separate ROOM ON/OFF from P90/P94 and limiter.
// NO claim of audible root cause from narrow tone measurement alone.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
using armix::builtin_fx::imersipP92Factory;
constexpr double pi=3.141592653589793238462643383279502884;
struct ProbeResult {double db=0., peak=0., cap=0., makeup=0.;};
static ProbeResult probe(int sr,int scene,int hz,bool candidate,bool stage,
                         bool room,bool pan) {
  auto dsp=std::make_unique<SonKuSpatialCore>();
  dsp->optInImmerSIBVocalRoom();
  if(candidate)dsp->optInImmerSIBConcertCandidate(stage);
  assert(dsp->prepare(sr,SonKuReflectionProfile::SixSurfaceKemarResearch));
  auto s=imersipP92Factory(scene);
  if(!room) {s.spacePreset=0;s.roomAmount=0.f;s.ambience=0.f;}
  assert(dsp->setSettings(s));
  constexpr int total=32768,analysis=8192;
  constexpr double amplitude=.025;
  std::array<std::vector<float>,2> x{
      std::vector<float>(total),std::vector<float>(total)};
  for(int i=0;i<total;++i) {
    const float v=float(amplitude*std::sin(2*pi*double(hz)*i/sr));
    x[0][i]=v;
    x[1][i]=pan?.25f*v:v;
  }
  for(int i=0;i<total;i+=256)
    dsp->process(x[0].data()+i,x[1].data()+i,std::min(256,total-i));
  double cs[2]{},sn[2]{},pk=0.;
  for(int i=total-analysis;i<total;++i) {
    const double phase=2*pi*double(hz)*i/sr;
    for(int ch=0;ch<2;++ch) {
      const double v=x[ch][i];
      assert(std::isfinite(v));
      pk=std::max(pk,std::abs(v));
      cs[ch]+=v*std::cos(phase);
      sn[ch]+=v*std::sin(phase);
    }
  }
  const double v0=2.*std::hypot(cs[0],sn[0])/analysis;
  const double v1=2.*std::hypot(cs[1],sn[1])/analysis;
  const double a=std::hypot(v0,v1)/(amplitude*std::sqrt(2));
  const auto stats=dsp->dynamicsTrace();
  assert(std::isfinite(a)&&a>1.e-6&&pk<.981);
  assert(stats.samples==total);
  return {20.*std::log10(a),pk,
          double(stats.limitedSamples)/double(stats.samples),
          stats.makeupGainSum/double(stats.samples)};
}
int main(){
  for(int sr:{44100,48000}){
    for(int scene:{2,4}){
      for(bool room:{false,true}){
        for(bool pan:{false,true}){
          for(int hz:{285,950,1350,1700,1950,2050,2400,2900,4500}){
            const auto neutral=probe(sr,scene,hz,false,true,room,pan);
            const auto v4=probe(sr,scene,hz,true,false,room,pan);
            const auto v5=probe(sr,scene,hz,true,true,room,pan);
            assert(std::isfinite(neutral.db)&&std::isfinite(v4.db)&&std::isfinite(v5.db));
            std::printf("IMMERSIB_MID_PROBE,sr=%d,scene=%d,room=%d,pan=%d,hz=%d,legacyDb=%.3f,v4Db=%.3f,v5Db=%.3f,diffV5V4=%.3f,limiter=%.5f,makeup=%.4f\n",
               sr,scene,int(room),int(pan),hz,
               neutral.db,v4.db,v5.db,v5.db-v4.db,v5.cap,v5.makeup);
          }
        }
      }
    }
  }
  std::puts("IMMERSIB_MID_PROBE_PASS: P9 actual chain diagnostic, not acoustic acceptance");
}
