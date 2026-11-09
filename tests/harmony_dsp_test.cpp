#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <array>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <memory>
using namespace armix::harmony;
static void need(bool value,const char* why){if(!value){std::cerr<<why<<'\n';std::exit(2);}}
int main(){
  // Each Engine owns fixed ring buffers. Allocate offline fixtures on heap:
  // many simultaneous stack-local instances exceed the Windows CTest stack.
  static_assert(sizeof(Engine)>64*1024,"verify test heap-fixture rationale");
  Settings bad;need(valid(bad),"defaults invalid");
  bad.key=12;need(!valid(bad),"invalid key admitted");bad.key=0;
  bad.voices=6;need(!valid(bad),"invalid voice count admitted");
  bad.voices=5;need(valid(bad),"five-voice DSP rejected");
  bad.voices=3;bad.wet=std::nanf("");need(!valid(bad),"NaN accepted");
  need(noteAt(nearestDegree(69.f,0,0),0,0)==69,"A major note mismatch");
  need(noteAt(nearestDegree(69.f,0,1),0,1)==68,"A minor note mismatch");
  for(double sr:{32000.,48000.,96000.,192000.}){
    auto e=std::make_unique<Engine>();need(e->prepare(sr),"prepare failed");
    need(e->latencySamples()>=512,"missing DSP latency declaration");
    Settings s;s.voices=static_cast<int>(sr==192000.?5:4);s.space=1.f;s.color=0.7f;s.wet=0.55f;
    need(e->setSettings(s),"valid edit rejected");
    constexpr int frames=16384;
    std::vector<float> l(frames),r(frames);
    for(int i=0;i<frames;++i)l[i]=r[i]=0.2f*std::sin(6.28318530718*440*i/sr);
    for(int i=0;i<frames;i+=127)e->process(l.data()+i,r.data()+i,std::min(127,frames-i));
    for(int i=0;i<frames;++i)
       need(std::isfinite(l[i])&&std::isfinite(r[i])&&std::abs(l[i])<=4&&std::abs(r[i])<=4,
            "nonfinite or unbounded PCM");
  }
  auto e=std::make_unique<Engine>();need(e->prepare(48000),"48k prepare failed");
  Settings s;s.wet=0.6f;need(e->setSettings(s),"setting rejected");
  constexpr int count=48000;
  std::vector<float> l(count),r(count);
  for(int i=0;i<count;++i)l[i]=r[i]=0.2f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<count;i+=128)e->process(l.data()+i,r.data()+i,128);
  need(e->voiced(),"voiced A4 not recognized");
  need(e->observedMidiNote()>=68&&e->observedMidiNote()<=70,"period/octave tracking error");
  double stereo=0,energy=0;
  for(int i=20000;i<count;++i){stereo+=std::abs(l[i]-r[i]);energy+=l[i]*l[i]+r[i]*r[i];}
  need(energy>1.0,"harmonized signal silent");
  need(stereo>1.0,"space did not produce stereo spread");
  Settings dry;dry.wet=0.f;need(e->setSettings(dry),"dry settings rejected");
  need(!e->setSettings(bad),"invalid runtime control admitted");
  auto silent=std::make_unique<Engine>();need(silent->prepare(48000),"silent prepare failed");
  std::vector<float> zero(4096,0.f),other(4096,0.f);
  silent->process(zero.data(),other.data(),4096);
  need(!silent->voiced(),"silence detected as voiced");
  // A silent-noise floor must not be transformed into fake pitched backing voices.
  auto quiet=std::make_unique<Engine>();need(quiet->prepare(48000),"quiet prepare failed");
  std::vector<float> ql(48000),qr(48000);
  for(int i=0;i<48000;++i)ql[i]=qr[i]=0.0001f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<48000;i+=128)quiet->process(ql.data()+i,qr.data()+i,128);
  need(!quiet->voiced(),"noise-floor pitch misclassified as vocal");
  // After a live pitch change, the confidence/hold gate must eventually
  // follow the new diatonic note, then release on silence.
  auto changing=std::make_unique<Engine>();need(changing->prepare(48000),"changing prepare failed");
  std::vector<float> sl(48000),sr(48000);
  auto feed=[&](double hz,double amp){
    for(int i=0;i<48000;++i)
      sl[i]=sr[i]=static_cast<float>(amp*std::sin(6.28318530718*hz*i/48000));
    for(int i=0;i<48000;i+=128)
      changing->process(sl.data()+i,sr.data()+i,128);
  };
  feed(440.,.20);need(changing->voiced()&&changing->observedMidiNote()==69,
                      "sustained A4 tracking unstable");
  feed(523.2511306,.20);need(changing->voiced()&&changing->observedMidiNote()==72,
                             "sung-note change did not follow C5");
  feed(440.,0.);need(!changing->voiced(),"silence did not gate harmony");
  // Wet=0 is an exact declared-latency pass-through, including across
  // irregular callback chunk boundaries (important for dry/wet PDC).
  auto dryEngine=std::make_unique<Engine>();need(dryEngine->prepare(48000),"dry prepare failed");
  Settings dryOnly;dryOnly.wet=0.f;need(dryEngine->setSettings(dryOnly),
                                      "dry-only setting rejected");
  const int n=dryEngine->latencySamples()+1024;
  std::vector<float> il(n,0.f),ir(n,0.f);il[0]=.5f;ir[0]=-.25f;
  for(int pos=0;pos<n;pos+=127)
    dryEngine->process(il.data()+pos,ir.data()+pos,std::min(127,n-pos));
  for(int i=0;i<n;++i){
    const float a=i==dryEngine->latencySamples()?.5f:0.f;
    const float b=i==dryEngine->latencySamples()?-.25f:0.f;
    need(std::abs(il[i]-a)<1.e-5f&&std::abs(ir[i]-b)<1.e-5f,
         "dry pass-through did not match declared sample delay");
  }
  // AUDIO callback receives only precomputed control packets. A 24 dB
  // gain change must ramp rather than step at an arbitrary block boundary.
  auto automated=std::make_unique<Engine>();need(automated->prepare(48000),"parameter engine prepare failed");
  Settings neutral;neutral.wet=0.f;
  need(automated->setSettings(neutral),"neutral settings failed");
  std::vector<float> al(4096,.25f),ar(4096,.25f);
  for(int i=0;i<4096;i+=128)automated->process(al.data()+i,ar.data()+i,128);
  Settings quieter=neutral;quieter.gainDb=-24.f;
  Engine::PreparedSettings prepared;
  need(Engine::prepareSettings(quieter,prepared),"prepared settings rejected");
  need(automated->applyPreparedSettings(prepared),"prepared DSP edit failed");
  float firstL=.25f,firstR=.25f;
  automated->process(&firstL,&firstR,1);
  need(std::abs(firstL-.25f)<.015f&&std::abs(firstR-.25f)<.015f,
       "gain edit produced an instantaneous click");
  std::vector<float> tailL(4096,.25f),tailR(4096,.25f);
  for(int i=0;i<4096;i+=113)
    automated->process(tailL.data()+i,tailR.data()+i,std::min(113,4096-i));
  const float expected=.25f*std::pow(10.f,-24.f/20.f);
  need(std::abs(tailL.back()-expected)<.002f&&
       std::abs(tailR.back()-expected)<.002f,
       "smoothed output gain did not settle at accepted value");
  Settings rejected=quieter;rejected.space=std::nanf("");
  need(!Engine::prepareSettings(rejected,prepared),
       "invalid realtime edit was prepared");
  // Runtime voice-count, stereo-space and wet changes are executed while
  // pitch-shifted voices are active-> This also exercises released voice tails.
  auto active=std::make_unique<Engine>();need(active->prepare(48000),"automation voice engine failed");
  Settings initial;initial.wet=.5f;
  need(active->setSettings(initial),"initial voices rejected");
  std::vector<float> vl(48000),vr(48000);
  for(int i=0;i<48000;++i)
    vl[i]=vr[i]=.2f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<48000;i+=128)active->process(vl.data()+i,vr.data()+i,128);
  for(int edit=0;edit<40;++edit){
    Settings next=initial;next.voices=2+edit%4;
    next.wet=(edit%2)?1.f:0.f;
    next.space=(edit%3)?1.f:0.f;
    next.color=(edit%2)?0.f:1.f;
    next.gainDb=(edit%2)?-9.f:0.f;
    next.correction=(edit%2)!=0;
    Engine::PreparedSettings packet;
    need(Engine::prepareSettings(next,packet)&&active->applyPreparedSettings(packet),
         "prepared live automation failed");
    float lbuf[257],rbuf[257];
    for(int j=0;j<257;++j)
      lbuf[j]=rbuf[j]=.2f*std::sin(6.28318530718*440*(edit*257+j)/48000);
    active->process(lbuf,rbuf,257);
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
    const float other=p<=.5f?.5f-p:1.5f-p;
    need(w>=0.f&&w<=1.f&&
         std::abs(w+smoothGrainBlend(other)-1.f)<1.e-5f,
         "grain crossfade invalid complementary weight");
    need(std::abs(w-smoothGrainBlend(1.f-p))<1.e-5f,
         "grain crossfade phase asymmetry");
  }
  need(smoothGrainBlend(.001f)<2.e-5f&&
       (1.f-smoothGrainBlend(.499f))<2.e-5f,
       "grain seam has a sharp first derivative");
  // Male low register and high soprano note tracking at two device rates.
  // Earlier 850 Hz search cap could not classify C6 (1046.5 Hz).
  const std::array<std::pair<double,int>,2> singerNotes{{
    {82.406889,40},{1046.502261,84}}};
  for(double rate:{48000.,96000.}){
    for(const auto& item:singerNotes){
      auto probe=std::make_unique<Engine>();need(probe->prepare(rate),"vocal register prepare failed");
      const int count=static_cast<int>(rate*.35);
      std::vector<float> pl(static_cast<std::size_t>(count));
      std::vector<float> pr(static_cast<std::size_t>(count));
      for(int i=0;i<count;++i)
        pl[static_cast<std::size_t>(i)]=pr[static_cast<std::size_t>(i)]=
          .2f*std::sin(6.28318530718*item.first*i/rate);
      for(int i=0;i<count;i+=127)
        probe->process(pl.data()+i,pr.data()+i,std::min(127,count-i));
      need(probe->voiced(),"vocal register not voiced");
      need(std::abs(probe->observedMidiNote()-item.second)<=1,
           "vocal register octave/scale misclassification");
    }
  }
  // Pitch hysteresis: a sung vibrato around E/F midpoint must not alternate
  // two harmony notes every analysis hop; a real note change must still pass.
  need(!pitchChangeHasMargin(64,65,64.55f),"boundary vibrato incorrectly revoiced");
  need(pitchChangeHasMargin(64,65,64.78f),"valid melodic transition blocked");
  need(pitchChangeHasMargin(-1,65,64.5f),"phrase onset cannot acquire note");
  // Re-key while live: previous key's quantizer must not leak into a new scale.
  auto rekey=std::make_unique<Engine>();
  need(rekey->prepare(48000),"re-key engine prepare failed");
  std::vector<float> rkL(48000),rkR(48000);
  for(int i=0;i<48000;++i)
    rkL[i]=rkR[i]=.2f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<48000;i+=128)rekey->process(rkL.data()+i,rkR.data()+i,128);
  need(rekey->voiced(),"re-key baseline pitch not acquired");
  Settings keyEdit;keyEdit.key=5; // F major: A remains in scale
  Engine::PreparedSettings keyPacket;
  need(Engine::prepareSettings(keyEdit,keyPacket)&&
       rekey->applyPreparedSettings(keyPacket),"key edit was rejected");
  need(!rekey->voiced()&&rekey->observedMidiNote()==-1,
       "key edit reused a stale note from previous scale");
  for(int i=0;i<48000;++i)
    rkL[i]=rkR[i]=.2f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<48000;i+=128)rekey->process(rkL.data()+i,rkR.data()+i,128);
  need(rekey->voiced()&&rekey->observedMidiNote()==69,
       "fresh key did not reacquire stable A4");
  // Fixed-size reduced audio history must survive repeated wraparound at
  // the highest supported device rate without changing declared PDC.
  auto wrap=std::make_unique<Engine>();
  need(wrap->prepare(192000),"192 kHz ring prepare failed");
  Settings fullDry;fullDry.wet=0.f;
  need(wrap->setSettings(fullDry),"192 kHz dry setup failed");
  constexpr int wraps=65536;
  std::vector<float> wl(wraps,0.f),wr(wraps,0.f);
  for(int i=0;i<wraps;i+=16384){wl[i]=.50f;wr[i]=-.25f;}
  for(int pos=0;pos<wraps;pos+=137)
    wrap->process(wl.data()+pos,wr.data()+pos,std::min(137,wraps-pos));
  for(int i=0;i<wraps;++i){
    const bool impulse=i>=wrap->latencySamples() &&
      ((i-wrap->latencySamples())%16384==0);
    need(std::abs(wl[i]-(impulse?.50f:0.f))<1.e-5f &&
         std::abs(wr[i]-(impulse?-.25f:0.f))<1.e-5f,
         "reduced 192 kHz history corrupted a wrapped dry impulse");
  }
  // Regression for sliding lag-energy normalization: harmonic-rich A2
  // vocals must remain a stable note at small, irregular ASIO block sizes.
  for(int block:{64,128,257}){
    auto pitched=std::make_unique<Engine>();
    need(pitched->prepare(48000.),"harmonic tracking prepare failed");
    constexpr int samples=24000;
    std::vector<float> hl(samples),hr(samples);
    for(int i=0;i<samples;++i){
      const double t=static_cast<double>(i)/48000.;
      hl[i]=hr[i]=static_cast<float>(
          .17*std::sin(6.283185307179586*110.*t)+
          .04*std::sin(6.283185307179586*220.*t)+
          .02*std::sin(6.283185307179586*330.*t));
    }
    for(int i=0;i<samples;i+=block)
      pitched->process(hl.data()+i,hr.data()+i,std::min(block,samples-i));
    need(pitched->voiced()&&pitched->observedMidiNote()==45,
         "rolling pitch energy altered singer-like A2 classification");
  }
  // P0: alternate ASIO block sizes must produce identical output PCM.
  // Sliced analysis operates per sample, never depends on callback framing.
  std::vector<float> referenceL,referenceR;
  for(int block:{64,127,256}){
    auto bounded=std::make_unique<Engine>();
    need(bounded->prepare(48000.),"P0 bounded engine prepare failed");
    Settings rich;rich.voices=5;rich.wet=.7f;rich.correction=true;
    need(bounded->setSettings(rich),"P0 quality setup failed");
    constexpr int frames=48000;
    std::vector<float> left(frames),right(frames);
    for(int i=0;i<frames;++i){
      const double phase=6.283185307179586*220.*i/48000.;
      left[i]=static_cast<float>(.18*std::sin(phase)+.06*std::sin(2*phase));
      right[i]=static_cast<float>(.16*std::sin(phase)+.04*std::sin(3*phase));
    }
    for(int i=0;i<frames;i+=block)
      bounded->process(left.data()+i,right.data()+i,std::min(block,frames-i));
    need(bounded->peakPitchLagWork()>0&&
         bounded->peakPitchLagWork()<=Engine::maxPitchLagsPerSlice,
         "P0 pitch analysis overran per-sample lag budget");
    need(bounded->voiced()&&bounded->observedMidiNote()==57,
         "P0 bounded singer tone detection failed");
    if(referenceL.empty()){
      referenceL=left;referenceR=right;
    }else for(int i=0;i<frames;++i)
      need(std::abs(left[i]-referenceL[i])<1.e-6f&&
           std::abs(right[i]-referenceR[i])<1.e-6f,
           "P0 pitch output depends on ASIO callback block size");
  }
  // P1: real opposite-phase vocal PCM must not become silent harmony.
  for(double rate:{48000.,96000.}){
    auto opposite=std::make_unique<Engine>();
    need(opposite->prepare(rate),"antiphase singer prepare failed");
    Settings wet;wet.voices=5;wet.wet=.85f;wet.correction=true;
    need(opposite->setSettings(wet),"antiphase singer settings failed");
    const int count=static_cast<int>(rate*.65);
    std::vector<float> left(static_cast<std::size_t>(count));
    std::vector<float> right(static_cast<std::size_t>(count));
    for(int i=0;i<count;++i){
      const float x=static_cast<float>(
          .19*std::sin(6.283185307179586*220.*i/rate)+
          .05*std::sin(6.283185307179586*440.*i/rate));
      left[static_cast<std::size_t>(i)]=x;
      right[static_cast<std::size_t>(i)]=-x;
    }
    for(int i=0;i<count;i+=257)
      opposite->process(left.data()+i,right.data()+i,std::min(257,count-i));
    need(opposite->voiced()&&opposite->observedMidiNote()==57,
         "phase-opposed singer lost stable A3 pitch");
    double harmonized=0.;
    for(int i=count/2;i<count;++i){
      const float l=left[static_cast<std::size_t>(i)];
      const float r=right[static_cast<std::size_t>(i)];
      need(std::isfinite(l)&&std::isfinite(r),"phase-safe PCM not finite");
      const double recovered=.5*(static_cast<double>(l)+r);
      harmonized+=recovered*recovered;
    }
    need(harmonized>0.005,"opposed stereo produced no harmonized audio");
  }
  // Declared 48k wet/dry sample PDC must remain 2*16ms even after slicing.
  need(e->latencySamples()==1536,"P0 changed live session PDC without proof");
  std::cout<<"Native Harmony DSP: deterministic PCM, 2-5 voices, octave/scale, stereo, silence and 32-192k PASS\n";
}
