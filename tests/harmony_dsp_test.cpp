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
  // AUDIO callback receives only precomputed control packets. A 24 dB
  // gain change must ramp rather than step at an arbitrary block boundary.
  Engine automated;need(automated.prepare(48000),"parameter engine prepare failed");
  Settings neutral;neutral.wet=0.f;
  need(automated.setSettings(neutral),"neutral settings failed");
  std::vector<float> al(4096,.25f),ar(4096,.25f);
  for(int i=0;i<4096;i+=128)automated.process(al.data()+i,ar.data()+i,128);
  Settings quieter=neutral;quieter.gainDb=-24.f;
  Engine::PreparedSettings prepared;
  need(Engine::prepareSettings(quieter,prepared),"prepared settings rejected");
  need(automated.applyPreparedSettings(prepared),"prepared DSP edit failed");
  float firstL=.25f,firstR=.25f;
  automated.process(&firstL,&firstR,1);
  need(std::abs(firstL-.25f)<.015f&&std::abs(firstR-.25f)<.015f,
       "gain edit produced an instantaneous click");
  std::vector<float> tailL(4096,.25f),tailR(4096,.25f);
  for(int i=0;i<4096;i+=113)
    automated.process(tailL.data()+i,tailR.data()+i,std::min(113,4096-i));
  const float expected=.25f*std::pow(10.f,-24.f/20.f);
  need(std::abs(tailL.back()-expected)<.002f&&
       std::abs(tailR.back()-expected)<.002f,
       "smoothed output gain did not settle at accepted value");
  Settings rejected=quieter;rejected.space=std::nanf("");
  need(!Engine::prepareSettings(rejected,prepared),
       "invalid realtime edit was prepared");
  // Runtime voice-count, stereo-space and wet changes are executed while
  // pitch-shifted voices are active. This also exercises released voice tails.
  Engine active;need(active.prepare(48000),"automation voice engine failed");
  Settings initial;initial.wet=.5f;
  need(active.setSettings(initial),"initial voices rejected");
  std::vector<float> vl(48000),vr(48000);
  for(int i=0;i<48000;++i)
    vl[i]=vr[i]=.2f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<48000;i+=128)active.process(vl.data()+i,vr.data()+i,128);
  for(int edit=0;edit<36;++edit){
    Settings next=initial;next.voices=2+edit%3;
    next.wet=(edit%2)?1.f:0.f;
    next.space=(edit%3)?1.f:0.f;
    next.color=(edit%2)?0.f:1.f;
    next.gainDb=(edit%2)?-9.f:0.f;
    next.correction=(edit%2)!=0;
    Engine::PreparedSettings packet;
    need(Engine::prepareSettings(next,packet)&&active.applyPreparedSettings(packet),
         "prepared live automation failed");
    float lbuf[257],rbuf[257];
    for(int j=0;j<257;++j)
      lbuf[j]=rbuf[j]=.2f*std::sin(6.28318530718*440*(edit*257+j)/48000);
    active.process(lbuf,rbuf,257);
    for(int j=0;j<257;++j)
      need(std::isfinite(lbuf[j])&&std::isfinite(rbuf[j])&&
           std::abs(lbuf[j])<=4.f&&std::abs(rbuf[j])<=4.f,
           "live control ramp produced invalid PCM");
  }
  // Smooth complementary grain crossfade: seam and center values, symmetry,
  // unity-sum weights and bounded slope at reset/half-grain transitions.
  need(std::abs(smoothGrainBlend(0.f))<1.e-7f&&
       std::abs(smoothGrainBlend(.5f)-1.f)<1.e-7f&&
       std::abs(smoothGrainBlend(1.f))<1.e-7f,"bad grain seam endpoints");
  for(int i=0;i<=100;++i){
    const float p=static_cast<float>(i)/100.f;
    const float w=smoothGrainBlend(p);
    need(w>=0.f&&w<=1.f&&std::abs(w+smoothGrainBlend(.5f-p)-1.f)<1.e-5f,
         "grain crossfade invalid weight");
    need(std::abs(w-smoothGrainBlend(1.f-p))<1.e-5f,
         "grain crossfade phase asymmetry");
  }
  need(smoothGrainBlend(.001f)<2.e-5f&&
       (1.f-smoothGrainBlend(.499f))<2.e-5f,
       "grain seam has a sharp first derivative");
  // Male low register and high soprano note tracking at two device rates.
  // Earlier 850 Hz search cap could not classify C6 (1046.5 Hz).
  for(double rate:{48000.,96000.}){
    for(const auto item:{{82.406889,40},{1046.502261,84}}){
      Engine probe;need(probe.prepare(rate),"vocal register prepare failed");
      const int count=static_cast<int>(rate*.35);
      std::vector<float> pl(static_cast<std::size_t>(count));
      std::vector<float> pr(static_cast<std::size_t>(count));
      for(int i=0;i<count;++i)
        pl[static_cast<std::size_t>(i)]=pr[static_cast<std::size_t>(i)]=
          .2f*std::sin(6.28318530718*item.first*i/rate);
      for(int i=0;i<count;i+=127)
        probe.process(pl.data()+i,pr.data()+i,std::min(127,count-i));
      need(probe.voiced(),"vocal register not voiced");
      need(std::abs(probe.observedMidiNote()-item.second)<=1,
           "vocal register octave/scale misclassification");
    }
  }
  std::cout<<"Native Harmony DSP: deterministic PCM, 2-4 voices, octave/scale, stereo, silence and 32-192k PASS\n";
}
