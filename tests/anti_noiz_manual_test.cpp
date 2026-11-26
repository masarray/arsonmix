// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include "../engine/builtin_fx/anti_noiz/AntiNoizNativeState.h"
#include <iostream>
#include <cstdlib>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
void check(bool x,const char* msg){if(!x){std::cerr<<msg<<'\n';std::abort();}}
int main(){
 for(int rate:{44100,48000,88200,96000})for(int ch:{1,2}){
  ManualExpander e; e.prepare(rate,ch,rate>48000?512:256);
  ManualExpander::Settings s; s.enabled=true;s.gate=true;s.rangeDb=96;s.holdMs=50;s.releaseMs=60;
  check(ManualExpander::valid(s),"valid");e.apply(ManualExpander::prepareSettings(s));
  for(int i=0;i<rate/2;++i)e.step(.003f*std::sin(2*std::numbers::pi_v<float>*30*i/rate),0);
  check(e.gain()==0,"gate exact mute");
  for(int i=0;i<rate/10;++i){float x=.1f*std::sin(2*std::numbers::pi_v<float>*500*i/rate);e.step(ch==1?x:0,ch==2?x:0);}
  check(e.gain()==1,"voice opens either channel");
  for(int i=0;i<rate/50;++i)e.step(0,0);
  check(e.gain()==1,"hold");
  for(int i=0;i<rate/2;++i)e.step(0,0);
  check(e.gain()==0,"close");
  Core::Settings cfg;cfg.mode=Core::Mode::Voice;cfg.cleanDb=0;cfg.manual=s;
  Core core;check(core.prepare(rate,ch)&&core.setSettings(cfg),"core prepare");
  check(core.latencySamples()==(rate>48000?512:256)&&!core.hasProfile(),"PDC and no profile");
  std::vector<float> l(rate*2),r(l.size());
  for(int i=0;i<rate*2;++i){const float noise=.003f*std::sin(2*std::numbers::pi_v<float>*30*i/rate);
    const float voice=i>=rate&&i<rate*3/2?.1f*std::sin(2*std::numbers::pi_v<float>*500*i/rate):0.f;
    l[i]=noise+(ch==1?voice:0);r[i]=noise+voice;}
  auto a=l,b=r;Core other;check(other.prepare(rate,ch)&&other.setSettings(cfg),"partition core");
  core.process(l.data(),ch==2?r.data():nullptr,int(l.size()));
  for(int pos=0;pos<int(a.size());pos+=127){const int n=std::min(127,int(a.size())-pos);other.process(a.data()+pos,ch==2?b.data()+pos:nullptr,n);}
  check(a==l&&(ch==1||b==r),"core partition");
  check(std::all_of(l.begin()+rate/2,l.begin()+rate,[](float v){return v==0;}),"core silent pause without Learn");
  double power=0;for(int i=rate+rate/10;i<rate+rate/4;++i){const float x=ch==1?l[i]:r[i];power+=x*x;}
  check(power>.001*rate*.15,"core voice opens");
  check(core.residualGain()==0,"actual manual GR");
  s.gate=false;s.rangeDb=60;s.ratio=4;e.apply(ManualExpander::prepareSettings(s));
  check(ManualExpander::cut(-50,s)==30&&ManualExpander::cut(-80,s)==60,"static curve");
  s.kneeDb=12;check(std::abs(ManualExpander::cut(-40,s)-4.5f)<1e-6,"knee");
  // A steady detector inside the upper half of the knee must not be forced open.
  e.reset();s.thresholdDb=-40;s.holdMs=0;s.attackMs=1;s.releaseMs=10;
  e.apply(ManualExpander::prepareSettings(s));
  for(int i=0;i<rate/2;++i)e.step(.015f*std::sin(2*std::numbers::pi_v<float>*1000*i/rate),0);
  check(e.gain()<.999f&&e.gain()>.2f,"dynamic knee response");
  s.ratio=1;e.apply(ManualExpander::prepareSettings(s));for(int i=0;i<rate/5;++i)e.step(0,0);check(e.gain()==1,"unity ratio");
  s.ratio=4;s.rangeDb=0;e.apply(ManualExpander::prepareSettings(s));for(int i=0;i<rate/5;++i)e.step(0,0);check(e.gain()==1,"unity range");
  s.thresholdDb=NAN;check(!ManualExpander::valid(s),"nan reject");
  std::cout<<rate<<" "<<ch<<"ch manual DSP PASS\n";
 }
}
