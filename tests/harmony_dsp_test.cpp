#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace armix::harmony;
static void need(bool value,const char* why){if(!value){std::cerr<<why<<'\n';std::exit(2);}}
int main(){
  Settings bad;need(valid(bad),"defaults invalid");
  bad.key=12;need(!valid(bad),"invalid key admitted");bad.key=0;
  bad.voices=5;need(!valid(bad),"invalid voice count admitted");
  bad.voices=3;bad.wet=std::nanf("");need(!valid(bad),"NaN accepted");
  need(noteAt(nearestDegree(69.f,0,0),0,0)==69,"A major note mismatch");
  need(noteAt(nearestDegree(69.f,0,1),0,1)==68,"A minor note mismatch");
  for(double sr:{32000.,48000.,96000.,192000.}){
    Engine e;need(e.prepare(sr),"prepare failed");
    need(e.latencySamples()>=512,"missing DSP latency declaration");
    Settings s;s.voices=4;s.space=1.f;s.color=0.7f;s.wet=0.55f;
    need(e.setSettings(s),"valid edit rejected");
    constexpr int frames=16384;
    std::vector<float> l(frames),r(frames);
    for(int i=0;i<frames;++i)l[i]=r[i]=0.2f*std::sin(6.28318530718*440*i/sr);
    for(int i=0;i<frames;i+=127)e.process(l.data()+i,r.data()+i,std::min(127,frames-i));
    for(int i=0;i<frames;++i)
       need(std::isfinite(l[i])&&std::isfinite(r[i])&&std::abs(l[i])<=4&&std::abs(r[i])<=4,
            "nonfinite or unbounded PCM");
  }
  Engine e;need(e.prepare(48000),"48k prepare failed");
  Settings s;s.wet=0.6f;need(e.setSettings(s),"setting rejected");
  constexpr int count=48000;
  std::vector<float> l(count),r(count);
  for(int i=0;i<count;++i)l[i]=r[i]=0.2f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<count;i+=128)e.process(l.data()+i,r.data()+i,128);
  need(e.voiced(),"voiced A4 not recognized");
  need(e.observedMidiNote()>=68&&e.observedMidiNote()<=70,"period/octave tracking error");
  double stereo=0,energy=0;
  for(int i=20000;i<count;++i){stereo+=std::abs(l[i]-r[i]);energy+=l[i]*l[i]+r[i]*r[i];}
  need(energy>1.0,"harmonized signal silent");
  need(stereo>1.0,"space did not produce stereo spread");
  Settings dry;dry.wet=0.f;need(e.setSettings(dry),"dry settings rejected");
  need(!e.setSettings(bad),"invalid runtime control admitted");
  Engine silent;need(silent.prepare(48000),"silent prepare failed");
  std::vector<float> zero(4096,0.f),other(4096,0.f);
  silent.process(zero.data(),other.data(),4096);
  need(!silent.voiced(),"silence detected as voiced");
  std::cout<<"Native Harmony DSP: deterministic PCM, 2-4 voices, octave/scale, stereo, silence and 32-192k PASS\n";
}
