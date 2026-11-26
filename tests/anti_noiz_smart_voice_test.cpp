// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
static void check(bool x,const char* why){if(!x){std::cerr<<why<<'\n';std::abort();}}
int main(){
 for(int rate:{44100,48000,88200,96000})for(int ch:{1,2}){
  ManualExpander detector;detector.prepare(rate,ch,rate>48000?512:256);
  detector.beginReference();
  for(int i=0;i<rate/2;++i)detector.observeReference(.004f*std::sin(2*std::numbers::pi_v<float>*500*i/rate),0);
  detector.checkpointReference(true);
  for(int i=0;i<rate/2;++i)detector.observeReference(.8f*std::sin(2*std::numbers::pi_v<float>*500*i/rate),0);
  detector.checkpointReference(false);detector.finishReference(true);
  check(detector.referenceValid()&&detector.noisePeak()<.01f,"discarded window cannot contaminate reference");
  Core c;Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=0;s.silenceDb=0;s.manual.enabled=true;s.manual.gate=true;
  s.manual.thresholdDb=-60;s.manual.smartVoice=true;s.manual.smartMarginDb=9;s.manual.smartMaxLiftDb=24;s.manual.rangeDb=96;
  check(c.prepare(rate,ch)&&c.setSettings(s),"prepare");
  check(!c.sidechainReferenceValid()&&std::abs(c.sidechainThreshold()-.001f)<1e-8,"manual fallback");
  check(c.beginLearn(),"learn");
  std::mt19937 gen(421);std::uniform_real_distribution<float> noise(-.006f,.006f);
  std::vector<float> l(rate*3),r(l.size());for(auto& x:l)x=noise(gen);for(auto& x:r)x=noise(gen);
  c.process(l.data(),ch==2?r.data():nullptr,int(l.size()));
  check(c.hasProfile()&&c.sidechainReferenceValid(),"learned sidechain");
  const float learned=c.sidechainNoisePeak(),threshold=c.sidechainThreshold();
  check(learned>0&&threshold>.001f&&threshold<=.01585f&&threshold>=learned,"bounded lift");
  auto reference=c;
  for(int i=0;i<rate*3;++i){l[i]=noise(gen);r[i]=noise(gen);}
  auto a=l,b=r;
  c.process(l.data(),ch==2?r.data():nullptr,int(l.size()));
  for(int pos=0;pos<int(a.size());pos+=127){const int n=std::min(127,int(a.size())-pos);reference.process(a.data()+pos,ch==2?b.data()+pos:nullptr,n);}
  check(a==l&&(ch==1||b==r),"partition");
  check(c.residualGain()==0,"smart idle mute");
  for(int i=0;i<rate;++i){l[i]=ch==1?.15f*std::sin(2*std::numbers::pi_v<float>*700*i/rate):0;r[i]=.15f*std::sin(2*std::numbers::pi_v<float>*700*i/rate);}
  c.process(l.data(),ch==2?r.data():nullptr,rate);check(c.residualGain()==1,"voice reopen");
  s.manual.smartMaxLiftDb=0;check(c.setSettings(s)&&std::abs(c.sidechainThreshold()-.001f)<1e-8,"zero lift");
  s.manual.smartMaxLiftDb=24;s.manual.smartVoice=false;
  check(c.setSettings(s)&&std::abs(c.sidechainThreshold()-.001f)<1e-8,"manual authority");
  s.manual.smartVoice=true;check(c.setSettings(s)&&c.sidechainThreshold()==threshold,"restore smart");
  check(c.beginLearn(),"relearn");std::fill(l.begin(),l.end(),0);std::fill(r.begin(),r.end(),0);
  c.process(l.data(),ch==2?r.data():nullptr,int(l.size()));check(c.sidechainReferenceValid()&&c.sidechainNoisePeak()==learned,"rejected learn preserves reference");
  s.manual.hpHz=300;check(c.setSettings(s)&&!c.sidechainReferenceValid()&&std::abs(c.sidechainThreshold()-.001f)<1e-8,"filter change invalidates");
  check(c.clearProfile()&&!c.sidechainReferenceValid(),"reset clears");
  s.manual.smartMarginDb=NAN;check(!c.setSettings(s),"invalid settings");
  std::cout<<rate<<" "<<ch<<"ch Smart Voice fallback/learn/lift/mute/reopen/partition/invalidation PASS\n";
 }
}
