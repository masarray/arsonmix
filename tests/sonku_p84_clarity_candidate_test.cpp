// SPDX-License-Identifier: AGPL-3.0-only
// P8.4 fidelity-first independent OFFLINE SonKu PCM tests; no JUCE graph change.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuP84ClarityAnchor.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
using armix::sonku::Settings;
using armix::sonku::p83::Mode;
using armix::sonku::p84::ClarityAnchor;
struct Sound {std::vector<float> l,r;};
enum class Program {Mono,AntiPhase,Left,Right,Partial,Transient,Impulse};
static constexpr double pi=3.14159265358979323846;
static void require(bool yes,const char* msg){
 if(!yes){std::fprintf(stderr,"FAIL P84: %s\n",msg);std::abort();}
}
static const char* label(Program p) {
 switch(p){case Program::Mono:return "mono";
 case Program::AntiPhase:return "antiphase";
 case Program::Left:return "left";
 case Program::Right:return "right";
 case Program::Partial:return "partial";
 case Program::Transient:return "transient";
 case Program::Impulse:return "impulse";}
 return "unknown";
}
static Sound music(double rate,Program kind,int frames){
 Sound a{{std::vector<float>(frames)},{std::vector<float>(frames)}};
 const double hz[]={83,157,311,461,719,1139,1703,2891,5387,9301};
 const double amp[]={.056,.026,.021,.038,.022,.015,.032,.012,.019,.008};
 for(int i=0;i<frames;++i) {
  const double t=i/rate;
  float x=0,y=0;
  for(int k=0;k<10;++k){
   x+=static_cast<float>(amp[k]*std::sin(2*pi*hz[k]*t+.15*k));
   y+=static_cast<float>(amp[k]*std::sin(2*pi*hz[k]*t+.11*k+.30));
  }
  if(kind==Program::Transient){
   const int age=i%2304;
   const float strike=age<128?
       static_cast<float>(.18*std::exp(-age*.055)*std::sin(2*pi*3000*t)):0.f;
   x+=strike;y-=.7f*strike;
  }
  switch(kind) {
  case Program::Mono:a.l[i]=a.r[i]=x;break;
  case Program::AntiPhase:a.l[i]=x;a.r[i]=-x;break;
  case Program::Left:a.l[i]=x;break;
  case Program::Right:a.r[i]=x;break;
  case Program::Partial:a.l[i]=x;a.r[i]=.7f*x+.3f*y;break;
  case Program::Transient:a.l[i]=x;a.r[i]=y;break;
  case Program::Impulse:a.l[i]=a.r[i]=i==2048?.18f:0.f;break;
  }
 }
 return a;
}
static Sound native(double rate,bool room,Program kind,int block,
                    Mode mode,float yaw=0.f) {
 auto dsp=std::make_unique<SonKuSpatialCore>();
 require(dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch),"prepare");
 Settings s{};
 s.moving=false;s.startAzimuthDegrees=0.f;s.stereoSpreadDegrees=60.f;
 s.directElevationDegrees=0.f;s.spatialMix=1.f;s.outputTrim=.75f;
 s.spacePreset=room?1:0;s.sourceDistanceMeters=1.6f;
 s.roomAmount=room?.55f:0.f;s.ambience=room?.30f:0.f;
 require(dsp->setSettings(s),"settings");
 require(dsp->setResearchRenderMode(mode),"mode");
 require(dsp->setResearchHeadYaw(yaw),"yaw");
 Sound out=music(rate,kind,room?16384:8192);
 const int frames=static_cast<int>(out.l.size());
 for(int i=0;i<frames;i+=block)
   dsp->process(out.l.data()+i,out.r.data()+i,std::min(block,frames-i));
 return out;
}
static Sound blend(double rate,const Sound& anchor,const Sound& physical,
                   float amount,int block){
 require(anchor.l.size()==physical.l.size(),"same PCM timeline");
 const int n=static_cast<int>(anchor.l.size());
 ClarityAnchor clarity{};
 require(clarity.prepare(rate,amount),"clarity prepare");
 Sound o{{std::vector<float>(n)},{std::vector<float>(n)}};
 for(int i=0;i<n;i+=block)
  for(int j=i;j<std::min(n,i+block);++j)
   clarity.process(anchor.l[j],anchor.r[j],physical.l[j],physical.r[j],
                   o.l[j],o.r[j]);
 return o;
}
static double energy(const Sound& a){
 double e=0;
 for(std::size_t i=0;i<a.l.size();++i)
   e+=static_cast<double>(a.l[i])*a.l[i]+
      static_cast<double>(a.r[i])*a.r[i];
 return e;
}
static double difference(const Sound& a,const Sound& b){
 require(a.l.size()==b.l.size(),"matched");
 double e=0;
 for(std::size_t i=0;i<a.l.size();++i){
  const double dl=a.l[i]-b.l[i],dr=a.r[i]-b.r[i];
  e+=dl*dl+dr*dr;
 }
 return e;
}
static double relativeDb(double after,double before){
 return 10.*std::log10((after+1.e-20)/(before+1.e-20));
}
static double probe(const Sound& a,double rate,double hz){
 const std::complex<double> rot(std::cos(2*pi*hz/rate),
                                -std::sin(2*pi*hz/rate));
 std::complex<double> phase{1,0},L{},R{};
 for(std::size_t i=0;i<a.l.size();++i){
  if(i>=1024){L+=static_cast<double>(a.l[i])*phase;
              R+=static_cast<double>(a.r[i])*phase;}
  phase*=rot;
 }
 return std::norm(L)+std::norm(R);
}
int main(){
 std::setvbuf(stdout,nullptr,_IONBF,0); // retain diagnostic evidence on gate abort
 double broadMax=0,bandMax=0;
 bool musicGate=true;
 for(double rate:{44100.,48000.})
 for(bool room:{false,true})
 for(Program input:{Program::Mono,Program::AntiPhase,Program::Left,
                    Program::Right,Program::Partial,Program::Transient,
                    Program::Impulse}){
  const auto anchor=native(rate,room,input,128,Mode::MusicBedP82);
  const auto physical=native(rate,room,input,128,Mode::SpeakerMonitor);
  const auto disabled=blend(rate,anchor,physical,0.f,128);
  require(difference(anchor,disabled)==0,"zero influence MUST preserve P82 PCM exactly");
  const auto candidate=blend(rate,anchor,physical,.12f,128);
  const auto smaller=blend(rate,anchor,physical,.12f,64);
  require(difference(candidate,smaller)==0,"buffer64/128 parity");
  const double gain=relativeDb(energy(candidate),energy(anchor));
  broadMax=std::max(broadMax,std::abs(gain));
  std::printf("P84_PROGRAM,%.0f,room=%d,%s,physical_dB=%.3f,clarity_dB=%.3f\n",
     rate,room,label(input),relativeDb(energy(physical),energy(anchor)),gain);
  musicGate &= std::isfinite(gain)&&std::abs(gain)<=1.5;
  for(double hz:{83.,157.,311.,461.,719.,1139.,1703.,2891.,5387.,9301.}){
   const double before=probe(anchor,rate,hz);
   if(before<1.e-9)continue;
   const double db=relativeDb(probe(candidate,rate,hz),before);
   bandMax=std::max(bandMax,std::abs(db));
   std::printf("P84_PROBE,%.0f,room=%d,%s,%.0fHz,delta_dB=%.3f\n",
      rate,room,label(input),hz,db);
   musicGate &= std::isfinite(db)&&std::abs(db)<=2.5;
  }
  // Room impulse spectral-response guard. Probes average over each
  // 1/3-octave band so narrow reference notches cannot be selectively
  // hidden or magnified. This checks *all* audible octave regions, not
  // the ten tone frequencies used for the music fixtures.
  if(input==Program::Impulse){
   for(double center=90.;center<=12000.;center*=std::pow(2.,1./3.)){
    double referencePower=0.,candidatePower=0.;
    for(int step=-2;step<=2;++step){
     const double hz=center*std::pow(2.,step/12.);
     referencePower+=probe(anchor,rate,hz);
     candidatePower+=probe(candidate,rate,hz);
    }
    const double color=relativeDb(candidatePower,referencePower);
    bandMax=std::max(bandMax,std::abs(color));
    std::printf("P84_BROADBAND_IR,%.0f,room=%d,center=%.1fHz,"
                "delta_dB=%.3f\n",rate,room,center,color);
    musicGate &= std::isfinite(color)&&std::abs(color)<=2.5;
   }
  }
  for(std::size_t i=0;i<candidate.l.size();++i)
   require(std::isfinite(candidate.l[i])&&std::isfinite(candidate.r[i])&&
           std::abs(candidate.l[i])<1.f&&std::abs(candidate.r[i])<1.f,
           "finite, unclipped native audio");
  if(input==Program::Left&&!room){
   const auto rotated=native(rate,false,input,128,Mode::SpeakerMonitor,60.f);
   const auto changed=blend(rate,anchor,rotated,.18f,128);
   const double cue=std::sqrt(difference(changed,candidate)/(energy(anchor)+1.e-20));
   std::printf("P84_YAW,%.0f,head_turned=60,relative_pcm_difference=%.6f\n",rate,cue);
   require(cue>.005&&cue<.5,"world yaw positional variation bounded and nonzero");
  }
 }
 require(musicGate,"music fidelity FAILED: per-band or RMS deviation exceeds P8.2 quality guard");
 std::printf("PASS P84 RESEARCH: 44.1/48 kHz P82 byte bypass, seven programs including impulse, "
             "room ON/OFF, 10 frequencies, chunk and yaw; worst_broad=%.3fdB "
             "worst_frequency=%.3fdB; listening and ASIO NOT PROVEN\n",
              broadMax,bandMax);
}
