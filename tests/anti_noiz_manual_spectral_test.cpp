// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include "../engine/builtin_fx/anti_noiz/AntiNoizNativeState.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
void check(bool x,const char* why){if(!x){std::cerr<<why<<'\n';std::abort();}}
int main(){
 for(float e:{.1f,1.f,2.f,5.f,100.f}){
  float prior=1;
  for(float floor:{1.f,.5f,.1f,.01f}){const float x=ManualSpectral::target(e,1,floor,.5f);check(x<=prior&&x>=floor-1e-6f,"reduction monotonic");prior=x;}
  check(ManualSpectral::target(e,.1f,.01f,0)<=ManualSpectral::target(e,10.f,.01f,0),"threshold monotonic");
  check(ManualSpectral::target(e,1,.01f,1)>=ManualSpectral::target(e,1,.01f,0),"protection monotonic");
 }
 check(ManualSpectral::smooth(.1f,1,1,1)>.1f,"artifact smoothing");
 for(int rate:{44100,48000,88200,96000})for(int channels:{1,2}){
  Core c;Core::Settings s;s.mode=Core::Mode::Voice;s.spectralManual=true;s.cleanDb=24;s.silenceDb=0;s.artifactControl=0;s.vocalProtection=0;
  check(c.prepare(rate,channels)&&c.setSettings(s)&&!c.audition()&&c.beginLearn(),"prepare/learn");
  std::mt19937 rng(19);std::uniform_real_distribution<float> rnd(-.01f,.01f);
  std::vector<float> l(rate*3),r(l.size());for(auto& x:l)x=rnd(rng);r=l;
  c.process(l.data(),channels==2?r.data():nullptr,int(l.size()));check(c.hasProfile(),"learned profile");
  c.resetStreamKeepProfile();
  const auto anchor=c.spectrum().noise;
  Core ref=c;
  std::vector<float> x(rate*3),y(x.size());for(auto& v:x)v=rnd(rng);y=x;
  auto dry=x,wet=x,wetR=y;
  check(c.audition(),"audition admission");
  c.process(x.data(),channels==2?y.data():nullptr,int(x.size()));
  ref.process(wet.data(),channels==2?wetR.data():nullptr,int(wet.size()));
  const int d=c.latencySamples();double err=0,energy=0;
  for(int i=rate/2;i<rate;++i){const double delta=x[i]+wet[i]-dry[i-d];err+=delta*delta;energy+=double(dry[i-d])*dry[i-d];}
  check(err/energy<1e-10,"aligned complement");
  check(!c.auditionActive(),"audition auto expiry");
  for(int i=rate*5/2;i<rate*3;++i)check(x[i]==wet[i],"post audition exact return");
  check(c.spectrum().noise==anchor,"immutable learned anchor");
  // The manual expander owns residual gain, irrespective of the old Silence
  // knob. Auto context must not consume CPU or leak authority into this path.
  Core manualA=c,manualB=c;auto q=s;q.manual.enabled=true;q.manual.rangeDb=0;q.silenceDb=0;
  check(manualA.setSettings(q),"manual authority A");q.silenceDb=48;
  check(manualB.setSettings(q),"manual authority B");auto aL=dry,aR=dry,bL=dry,bR=dry;
  manualA.process(aL.data(),channels==2?aR.data():nullptr,int(aL.size()));
  manualB.process(bL.data(),channels==2?bR.data():nullptr,int(bL.size()));
  check(aL==bL&&aR==bR,"legacy Silence cannot affect manual expander PCM");
  q.manual.enabled=false;check(manualB.setSettings(q),"return automatic dynamics");
  std::fill(bL.begin(),bL.end(),0.f);std::fill(bR.begin(),bR.end(),0.f);
  manualB.process(bL.data(),channels==2?bR.data():nullptr,int(bL.size()));
  check(manualB.residualGain()<.1f&&manualB.spectrum().noise==anchor,"fresh automatic idle evidence and immutable profile");
  // A second Learn must never bypass the trusted manual spectral mask.
  Core capture=c,control=c;check(capture.beginLearn(),"relearn start");
  std::vector<float> captureL(rate/2),captureR(rate/2);
  for(auto& v:captureL)v=rnd(rng);captureR=captureL;
  auto controlL=captureL,controlR=captureR;
  capture.process(captureL.data(),channels==2?captureR.data():nullptr,int(captureL.size()));
  control.process(controlL.data(),channels==2?controlR.data():nullptr,int(controlL.size()));
  check(capture.learning()&&captureL==controlL&&captureR==controlR,"relearn PCM equals live trusted processing");
  capture.cancelNativeLearn();check(!capture.learning()&&capture.spectrum().noise==anchor,"cancel keeps anchor");
  captureL=controlL=dry;captureR=controlR=dry;
  capture.process(captureL.data(),channels==2?captureR.data():nullptr,int(captureL.size()));
  control.process(controlL.data(),channels==2?controlR.data():nullptr,int(controlL.size()));
  check(captureL==controlL&&captureR==controlR,"cancel preserves mask and signal timeline");
  // Direct profile handover: accepted anchor is immediately authoritative
  // for readback, while processing traverses a 100 ms bounded interpolation.
  NoiseProfile<257> profile;const int hop=rate>48000?256:128;
  profile.prepare(rate,hop,channels,hop+1);
  NoiseProfile<257>::Powers powers{};
  for(auto& ch:powers)ch.fill(1.f);
  check(profile.begin(),"first profile");while(profile.learning())profile.observe(powers);
  check(profile.anchor()[0][1]==1.f,"first profile immediate");
  for(auto& ch:powers)ch.fill(4.f);
  check(profile.begin(),"replacement profile");while(profile.learning())profile.observe(powers);
  check(profile.anchor()[0][1]==4.f&&profile.processingAnchor()[0][1]==1.f,"commit isolated from rendering");
  const int frames=int(std::ceil(.1*rate/hop));float previous=1.f;
  for(int i=0;i<frames;++i){profile.advanceHandover();const float value=profile.processingAnchor()[0][1];check(value>=previous&&value<=4.f&&value-previous<.2f,"bounded monotonic handover");previous=value;}
  check(profile.processingAnchor()[0][1]==4.f,"handover endpoint");
  check(c.clearProfile()&&!c.hasProfile()&&!c.audition()&&!c.spectrum().noiseValid,"clear");
  check(!c.setSettings([](Core::Settings q){q.artifactControl=NAN;return q;}(s)),"invalid reject");
  s.spectralEnabled=false;check(c.setSettings(s),"bypass");
  auto state=NativeState::encode(s);Core::Settings restored;check(NativeState::decode(state,restored)&&!restored.spectralEnabled&&restored.artifactControl==0,"state");
  std::cout<<rate<<" "<<channels<<"ch spectral complement/expiry/profile/state PASS\n";
 }
}
