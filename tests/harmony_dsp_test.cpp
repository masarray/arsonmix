#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <array>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <memory>
#include <limits>
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
  need(rekey->voiced()&&rekey->observedMidiNote()==69,
       "key edit interrupted a sustained voiced singer");
  for(int i=0;i<48000;++i)
    rkL[i]=rkR[i]=.2f*std::sin(6.28318530718*440*i/48000);
  for(int i=0;i<48000;i+=128)rekey->process(rkL.data()+i,rkR.data()+i,128);
  need(rekey->voiced()&&rekey->observedMidiNote()==69,
       "fresh key did not reacquire stable A4");

  // A change of guide key and fallback MUST NOT suppress active vocal
  // analysis/room/grains. The former setGuideKey() immediately unvoiced a
  // singer, making Music mode audibly sputter on uncertain key boundaries.
  auto guided=std::make_unique<Engine>();
  need(guided->prepare(48000),"guided vocal prepare failed");
  Settings guideSettings;guideSettings.key=0;guideSettings.scale=0;
  guideSettings.guideMode=1;guideSettings.guideChannelId=6001;
  guideSettings.voices=5;guideSettings.wet=1.f;
  need(guided->setSettings(guideSettings),"guided settings failed");
  std::vector<float> gL(48000),gR(48000);
  const auto feedGuided=[&](int count){
    for(int i=0;i<count;++i)
      gL[i]=gR[i]=static_cast<float>(.20*std::sin(
          6.283185307179586*440.*i/48000.));
    for(int i=0;i<count;i+=64)
      guided->process(gL.data()+i,gR.data()+i,std::min(64,count-i));
  };
  feedGuided(48000);
  need(guided->voiced()&&guided->observedMidiNote()==69,
       "guide singer baseline lost stable voice");
  for(int cycle=0;cycle<8;++cycle){
    if(cycle%2==0)guided->setGuideKey(0,1); // C minor excludes sung A
    else guided->setGuideKey(-1,-1);         // explicit Manual fallback
    need(guided->voiced()&&guided->observedMidiNote()>=0,
         "guide key event hard-gated sustained vocal");
    feedGuided(4096);
    need(guided->voiced()&&guided->observedMidiNote()>=0,
         "singer failed to continue across guide key transition");
    if(cycle%2==0)
      need(guided->observedMidiNote()!=69,
           "old C-major pitch was stuck after C-minor transition");
    else
      need(guided->observedMidiNote()==69,
           "manual fallback failed to retune singing A");
  }
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
    need(bounded->completedPitchAnalyses()>100&&
         bounded->abandonedPitchAnalyses()==0,
         "P0 slices failed to finish before the next analysis hop");
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

  // P1: the approved 48 kHz Color law must be preserved. At every other
  // supported sample rate, equal physical time must imply equal one-pole
  // attenuation; no pow/log calls can occur inside the audio callback.
  for(double rate:{32000.,44100.,48000.,96000.,192000.}){
    RateStableColor table;
    need(table.prepare(rate),"color filter prepare rejected supported rate");
    for(float color:{0.f,.08f,.25f,.5f,.75f,.91f,1.f}){
      const float actual=table.coefficient(color);
      const float legacy=.07f+.77f*color;
      need(std::isfinite(actual)&&actual>0.f&&actual<1.f,
           "rate-adjusted Color coefficient invalid");
      if(rate==48000.)
        need(std::abs(actual-legacy)<1.e-6f,
             "48 kHz Color legacy response changed");
      // 125 microseconds is six samples at 48k. A 10ms measurement
      // would hide every wrong Color coefficient: these filters all
      // decay to ~zero well before 10ms.
      const double oldDecay=std::pow(1.-static_cast<double>(legacy),6.);
      const double newDecay=std::pow(1.-static_cast<double>(actual),
                                    rate*.000125);
      need(std::abs(oldDecay-newDecay)<1.e-4,
           "Color attenuation changes at identical physical time");
    }
  }
  // P2: compare every supported key/scale/degree against the old per-voice
  // pow(2,(targetMidi-observedMidi)/12) reference before accepting a LUT.
  PreparedPitchIntervals intervals;
  intervals.prepare();
  for(int key=0;key<12;++key)
    for(int scale=0;scale<2;++scale)
      for(int degree=-5;degree<=65;++degree){
        const int note=noteAt(degree,key,scale);
        constexpr std::array<int,5> offsets{{2,4,-2,6,-4}};
        for(float detune:{-.45f,-.17f,0.f,.23f,.49f}){
          const float midi=static_cast<float>(note)+detune;
          const float base=std::pow(2.f,(note-midi)/12.f);
          for(const int offset:offsets){
            const int target=noteAt(degree+offset,key,scale);
            need(std::abs(target-note)<=PreparedPitchIntervals::maximumSemitones,
                 "harmonic interval outside precomputed LUT");
            const float old=std::clamp(std::pow(2.f,(target-midi)/12.f),
                                       0.5f,2.f);
            const float next=std::clamp(base*intervals.ratio(target-note),
                                        0.5f,2.f);
            need(std::abs(old-next)<1.e-6f,
                 "precomputed harmony interval changed output pitch ratio");
          }
        }
      }
  // P0: rescued phase-opposed ANALYSIS must never modify DRY output.
  // The player, direct hardware monitor and graph still own stereo polarity.
  for(double rate:{48000.,192000.}){
    auto dry=std::make_unique<Engine>();
    need(dry->prepare(rate),"dry antiphase sample-rate prepare failed");
    Settings silentRack;silentRack.wet=0.f;silentRack.correction=false;
    need(dry->setSettings(silentRack),"dry antiphase packet failed");
    const int latency=dry->latencySamples();
    const int count=latency*3+217;
    std::vector<float> original(static_cast<std::size_t>(count));
    std::vector<float> left(static_cast<std::size_t>(count));
    std::vector<float> right(static_cast<std::size_t>(count));
    for(int i=0;i<count;++i){
      const float x=static_cast<float>(
          .21*std::sin(6.283185307179586*140.*i/rate)+
          .06*std::sin(6.283185307179586*280.*i/rate));
      original[static_cast<std::size_t>(i)]=x;
      left[static_cast<std::size_t>(i)]=x;
      right[static_cast<std::size_t>(i)]=-x;
    }
    for(int i=0;i<count;i+=127)
      dry->process(left.data()+i,right.data()+i,std::min(127,count-i));
    for(int i=0;i<count;++i){
      const float expected=i<latency?0.f:
         original[static_cast<std::size_t>(i-latency)];
      need(std::abs(left[static_cast<std::size_t>(i)]-expected)<1.e-6f&&
           std::abs(right[static_cast<std::size_t>(i)]+expected)<1.e-6f,
           "P0 polarity rescue corrupted dry stereo or exact PDC");
    }
  }

  // P0: all supported device rates and irregular ASIO-shaped periods must
  // complete the full frozen correlation search WITHOUT dropped windows.
  // This prevents a naive 32->8-lag reduction from silently truncating E2-C6.
  for(const double rate:{32000.,44100.,48000.,96000.,192000.}){
    for(int block:{64,127,256}){
      auto sliced=std::make_unique<Engine>();
      need(sliced->prepare(rate),"P0 multirate slice prepare failed");
      const int samples=static_cast<int>(rate*.30);
      std::vector<float> l(static_cast<std::size_t>(samples));
      std::vector<float> r(static_cast<std::size_t>(samples));
      for(int i=0;i<samples;++i){
        const float value=static_cast<float>(
            .16*std::sin(6.283185307179586*220.*i/rate)+
            .04*std::sin(6.283185307179586*440.*i/rate));
        l[static_cast<std::size_t>(i)]=value;
        r[static_cast<std::size_t>(i)]=value;
      }
      for(int i=0;i<samples;i+=block)
        sliced->process(l.data()+i,r.data()+i,std::min(block,samples-i));
      need(sliced->peakPitchLagWork()<=8&&
           sliced->peakPitchLagWork()>0,
           "P0 worst-case lag slice exceeded eight");
      need(sliced->completedPitchAnalyses()>=20 &&
           sliced->abandonedPitchAnalyses()==0,
           "P0 correlation hop overwritten on supported device");
      need(sliced->voiced() && sliced->observedMidiNote()==57,
           "P0 sliced note lost in a supported rate/block configuration");
    }
  }
  // P0 defensive PCM: no allocation/crash/NaN from extreme finite,
  // infinity or NaN, including during the silence->singer transition.
  for(int voiceCount:{2,5}){
    auto overflow=std::make_unique<Engine>();
    need(overflow->prepare(48000.),"P0 extreme PCM prepare failed");
    Settings full;full.wet=1.f;full.voices=voiceCount;full.correction=true;
    need(overflow->setSettings(full),"P0 extreme PCM settings rejected");
    constexpr int frames=8192;
    std::vector<float> l(frames),r(frames);
    for(int i=0;i<frames;++i){
      const auto j=i%8;
      const float raw=j==0?std::numeric_limits<float>::max():
                       j==1?-std::numeric_limits<float>::max():
                       j==2?std::numeric_limits<float>::infinity():
                       j==3?-std::numeric_limits<float>::infinity():
                       j==4?std::numeric_limits<float>::quiet_NaN():
                       j==5?4.5f:j==6?-4.5f:0.f;
      l[static_cast<std::size_t>(i)]=raw;
      r[static_cast<std::size_t>(i)]=-raw;
    }
    for(int i=0;i<frames;i+=64)
      overflow->process(l.data()+i,r.data()+i,std::min(64,frames-i));
    for(int i=0;i<frames;++i)
      need(std::isfinite(l[static_cast<std::size_t>(i)]) &&
           std::isfinite(r[static_cast<std::size_t>(i)]) &&
           std::abs(l[static_cast<std::size_t>(i)])<=4.f &&
           std::abs(r[static_cast<std::size_t>(i)])<=4.f,
           "P0 extreme PCM yielded invalid or out-of-bounds output");
  }
  // Direct analysis-only stereo fold must never produce NaN/inf even if a
  // connected host provides a finite float close to FLT_MAX.
  StereoPhaseSafeFold guard;
  need(guard.prepare(48000.),"P0 stereo analysis fold prepare failed");
  for(int i=0;i<4096;++i){
    const float huge=std::numeric_limits<float>::max();
    const float y=guard.process(huge,huge);
    need(std::isfinite(y)&&std::abs(y)<=4.f,
         "P0 guide PCM overflowed in stereo analysis fold");
  }

  // P1 natural ensemble: each singer owns a distinct stable arrival time,
  // vibrato phase/rate and early-reflection history. No heap or random PCM
  // work is introduced, and reflections must not bleed between singers.
  for(double rate:{32000.,48000.,96000.,192000.}){
    auto ensemble=std::make_unique<EnsembleSpace>();
    need(ensemble->prepare(rate),"ensemble prepare rejected supported rate");
    need(ensemble->arrivalSamples(0)>0.f&&
         ensemble->arrivalSamples(2)>ensemble->arrivalSamples(1),
         "independent singer onset offsets not prepared");
    need(ensemble->reflectionSamples(4)<static_cast<int>(
             EnsembleSpace::reflectionCapacity),
         "highest-rate reflection would read outside fixed history");
    const int delay=ensemble->reflectionSamples(0);
    for(int i=0;i<=delay+5;++i){
      for(int v=0;v<EnsembleSpace::voices;++v){
        const float input=(i==0&&v==0)?1.f:0.f;
        const float out=ensemble->exchangeReflection(v,input);
        const float expected=(i==delay&&v==0)?1.f:0.f;
        need(std::abs(out-expected)<1.e-6f,
             "ensemble reflection delay or channel isolation broken");
      }
      ensemble->advance(true);
    }
    float minRatio=2.f,maxRatio=0.f;
    const int sustain=static_cast<int>(rate*.60);
    for(int i=0;i<sustain;++i){
      const float a=ensemble->pitchRatio(0);
      const float b=ensemble->pitchRatio(1);
      need(std::isfinite(a)&&std::isfinite(b)&&a>.996f&&a<1.004f&&
           b>.996f&&b<1.004f,
           "ensemble vibrato/detune exceeds transparent musical bounds");
      minRatio=std::min(minRatio,a);maxRatio=std::max(maxRatio,a);
      ensemble->advance(true);
    }
    need(maxRatio-minRatio>0.0004f,
         "ensemble vocalist lacks independent sustain modulation");
    ensemble->advance(false);
  }
  // At Space=0 mono input stays mono with no reflected crossfeed. At
  // Space=1, three distinct backing singers must form true stereo PCM,
  // while the singer's dry path keeps its declared delay and center.
  for(double rate:{48000.,96000.}){
    constexpr int samples=48000;
    std::vector<float> zeroL(samples),zeroR(samples);
    std::vector<float> wideL(samples),wideR(samples);
    auto center=std::make_unique<Engine>();
    auto wide=std::make_unique<Engine>();
    need(center->prepare(rate)&&wide->prepare(rate),
         "ensemble space engine prepare failed");
    Settings narrow; narrow.voices=3;narrow.wet=.6f;narrow.space=0.f;
    Settings spread=narrow;spread.space=1.f;
    need(center->setSettings(narrow)&&wide->setSettings(spread),
         "ensemble controls not accepted");
    for(int i=0;i<samples;++i){
      const float x=static_cast<float>(.16*std::sin(
          6.283185307179586*220.*i/rate)+.05*std::sin(
          6.283185307179586*440.*i/rate));
      zeroL[static_cast<std::size_t>(i)]=zeroR[static_cast<std::size_t>(i)]=x;
      wideL[static_cast<std::size_t>(i)]=wideR[static_cast<std::size_t>(i)]=x;
    }
    for(int i=0;i<samples;i+=64){
      const auto len=std::min(64,samples-i);
      center->process(zeroL.data()+i,zeroR.data()+i,len);
      wide->process(wideL.data()+i,wideR.data()+i,len);
    }
    double centerDifference=0.,spreadDifference=0.,wideEnergy=0.,foldEnergy=0.;
    for(int i=samples/2;i<samples;++i){
      centerDifference+=std::abs(zeroL[static_cast<std::size_t>(i)]-
                                 zeroR[static_cast<std::size_t>(i)]);
      spreadDifference+=std::abs(wideL[static_cast<std::size_t>(i)]-
                                 wideR[static_cast<std::size_t>(i)]);
      const double x=.5*(wideL[static_cast<std::size_t>(i)]+
                           wideR[static_cast<std::size_t>(i)]);
      wideEnergy+=wideL[static_cast<std::size_t>(i)]*
                  wideL[static_cast<std::size_t>(i)]+
                  wideR[static_cast<std::size_t>(i)]*
                  wideR[static_cast<std::size_t>(i)];
      foldEnergy+=x*x;
    }
    need(centerDifference<.0001,
         "Space zero leaked phase-widened stereo or unbalanced rooms");
    need(spreadDifference>0.5&&wideEnergy>1.&&foldEnergy>.1,
         "ensemble room has no stereo depth or loses mono compatibility");
  }

  // Field feedback: full Wet, 4 voices, Space 60% still sounded thin.
  // Presence curve must lift BACKING only, with sqrt voice count normalization
  // and exact Wet0 silence; all scalar math is prepared on control thread.
  for(int voices=2;voices<=Engine::maxVoices;++voices){
    float previous=0.f;
    for(float wet:{0.f,.15f,.45f,.60f,.85f,1.f}){
      const float modern=Engine::normalizedBackingWetAmplitude(wet,voices);
      const float legacy=Engine::normalizedBackingWetAmplitude(wet,voices,false);
      need(std::isfinite(modern)&&modern>=previous&&
           modern<=1.151f/std::sqrt(static_cast<float>(voices)),
           "Harmony wet presence nonmonotone or unbounded");
      if(wet==0.f)need(modern==0.f&&legacy==0.f,"Wet0 leaked harmony");
      if(wet==1.f)need(modern>legacy*1.73f&&modern<legacy*1.80f,
                       "Wet100 failed to raise backing presence");
      previous=modern;
    }
    Settings settings;settings.voices=voices;settings.wet=1.f;settings.space=.6f;
    Engine::PreparedSettings modern{},legacy{};
    need(Engine::prepareSettings(settings,modern)&&
         Engine::prepareSettings(settings,legacy,false),
         "prepared Harmony wet staging rejected");
    for(int voice=0;voice<voices;++voice){
      const float level=std::hypot(modern.mixL[voice],modern.mixR[voice]);
      const float prior=std::hypot(legacy.mixL[voice],legacy.mixR[voice]);
      need(std::abs(level-Engine::normalizedBackingWetAmplitude(1.f,voices))<1.e-5f&&
           level>prior*1.73f,"prepared Harmony pan lost presence or normalization");
    }
  }
  // P0 lead preservation: backing Wet must never change the lead amplitude.
  // The *only* intentional lead replacement is Pitch Tune's explicit blend.
  for(float dry:{-.9f,-.3f,0.f,.27f,.9f}){
    need(Engine::leadPath(dry,.5f,0.f)==dry,
         "wet-independent original lead unexpectedly ducked");
    need(std::abs(Engine::leadPath(dry,.5f,.5f)-(.5f*dry+.25f))<1.e-6f,
         "explicit Pitch Tune blend changed");
  }

  // P1 diffuse shared BACKING room: no immediate output, fixed delay at
  // all accepted rates, decaying rather than metallic or unstable feedback.
  for(double rate:{32000.,44100.,48000.,96000.,192000.}){
    auto room=std::make_unique<BackingDepthRoom>();
    need(room->prepare(rate),"backing room rejected supported sample rate");
    for(std::size_t v=0;v<BackingDepthRoom::branches;++v)
      need(room->delaySamples(v)>0&&
           room->delaySamples(v)<BackingDepthRoom::capacity&&
           room->feedbackGain(v)>0.f&&room->feedbackGain(v)<1.f,
           "backing FDN missing bounded delays or stable feedback");
    const int total=static_cast<int>(rate*1.8);
    double first=0.,late=0.,last=0.,mono=0.;
    for(int i=0;i<total;++i){
      const auto o=room->process(i==0?1.f:0.f,i==0?1.f:0.f);
      need(std::isfinite(o.left)&&std::isfinite(o.right)&&
           std::abs(o.left)<2.f&&std::abs(o.right)<2.f,
           "backing room blew up or returned non-finite PCM");
      const double energy=o.left*o.left+o.right*o.right;
      if(i<static_cast<int>(rate*.02))
        need(energy<1.e-18,"diffuse field leaked before its first tap");
      if(i<static_cast<int>(rate*.11))first+=energy;
      else if(i<static_cast<int>(rate*.35))late+=energy;
      if(i>static_cast<int>(rate*1.5))last+=energy;
      mono+=(.5*(o.left+o.right))*(.5*(o.left+o.right));
    }
    need(first>1.e-5&&late>1.e-9&&last<late*.01,
         "diffuse field lacks early energy or bounded late decay");
    need(mono>1.e-5,"room anti-phase cancelled in mono");
    need(room->prepare(rate),"device restart failed to reset room");
    for(int i=0;i<512;++i){
      const auto o=room->process(0.f,0.f);
      need(o.left==0.f&&o.right==0.f,
           "room restart retained stale output from previous session");
    }
  }
  // P1 stereo depth changes must remain audible in mono, but lead should
  // stay transparent regardless of Space and Wet when no Pitch Tune.
  for(float depth:{0.f,.5f,1.f}){
    EnsembleSpace singer;
    need(singer.prepare(48000.),"backing HF shader prepare failed");
    double inputEnergy=0.,outputEnergy=0.;
    for(int i=0;i<3000;++i){
      const float pulse=(i%7)<3?1.f:-1.f;
      const float shaded=singer.shadeBacking(0,pulse,depth);
      inputEnergy+=pulse*pulse;
      outputEnergy+=shaded*shaded;
      singer.advance(true);
    }
    need(outputEnergy>0.&&outputEnergy<=inputEnergy*1.01,
         "backing distance contour amplified or removed singer");
    if(depth==0.f)need(std::abs(outputEnergy-inputEnergy)<1.e-6,
                       "Space zero failed to preserve direct backing");
  }
  // Declared 48k wet/dry sample PDC must remain 2*16ms even after slicing.
  need(e->latencySamples()==1536,"P0 changed live session PDC without proof");
  std::cout<<"Native Harmony DSP: deterministic PCM, 2-5 voices, octave/scale, stereo, silence and 32-192k PASS\n";
}
