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
  // A silent-noise floor must not be transformed into fake pitched backing voices.
  Engine quiet;need(quiet.prepare(48000),"quiet prepare failed");
  std::vector<float> ql(48000),qr(48000);
  for(int i=0;i<48000;++i)ql[i]=qr[i]=0.0001f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<48000;i+=128)quiet.process(ql.data()+i,qr.data()+i,128);
  need(!quiet.voiced(),"noise-floor pitch misclassified as vocal");
  // After a live pitch change, the confidence/hold gate must eventually
  // follow the new diatonic note, then release on silence.
  Engine changing;need(changing.prepare(48000),"changing prepare failed");
  std::vector<float> sl(48000),sr(48000);
  auto feed=[&](double hz,double amp){
    for(int i=0;i<48000;++i)
      sl[i]=sr[i]=static_cast<float>(amp*std::sin(6.28318530718*hz*i/48000));
    for(int i=0;i<48000;i+=128)
      changing.process(sl.data()+i,sr.data()+i,128);
  };
  feed(440.,.20);need(changing.voiced()&&changing.observedMidiNote()==69,
                      "sustained A4 tracking unstable");
  feed(523.2511306,.20);need(changing.voiced()&&changing.observedMidiNote()==72,
                             "sung-note change did not follow C5");
  feed(440.,0.);need(!changing.voiced(),"silence did not gate harmony");
  // Wet=0 is an exact declared-latency pass-through, including across
  // irregular callback chunk boundaries (important for dry/wet PDC).
  Engine dryEngine;need(dryEngine.prepare(48000),"dry prepare failed");
  Settings dryOnly;dryOnly.wet=0.f;need(dryEngine.setSettings(dryOnly),
                                      "dry-only setting rejected");
  const int n=dryEngine.latencySamples()+1024;
  std::vector<float> il(n,0.f),ir(n,0.f);il[0]=.5f;ir[0]=-.25f;
  for(int pos=0;pos<n;pos+=127)
    dryEngine.process(il.data()+pos,ir.data()+pos,std::min(127,n-pos));
  for(int i=0;i<n;++i){
    const float a=i==dryEngine.latencySamples()?.5f:0.f;
    const float b=i==dryEngine.latencySamples()?-.25f:0.f;
    need(std::abs(il[i]-a)<1.e-5f&&std::abs(ir[i]-b)<1.e-5f,
         "dry pass-through did not match declared sample delay");
  }
  std::cout<<"Native Harmony DSP: deterministic PCM, 2-4 voices, octave/scale, stereo, silence and 32-192k PASS\n";
}
