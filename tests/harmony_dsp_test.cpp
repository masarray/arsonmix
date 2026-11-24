#include "../engine/builtin_fx/HarmonyDsp.h"
#include "../engine/builtin_fx/HarmonyMusicKey.h"
#include <algorithm>
#include <array>
#include <utility>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <memory>
#include <limits>
#include <chrono>
#include <iomanip>
using namespace armix::harmony;
static void need(bool value,const char* why){if(!value){std::cerr<<why<<'\n';std::exit(2);}}
// The fixture has the same fundamental in both channels and different
// harmonic/phase content. A mono replacement loses the stereo residual.
struct LeadFixture {std::vector<float> left,right;};
static LeadFixture leadFixture(int rate,double noteHz,float cents,float vibrato){
  LeadFixture out{std::vector<float>(2*rate),std::vector<float>(2*rate)};
  constexpr double tau=6.283185307179586;
  double phase=0.;
  for(int i=0;i<2*rate;++i){
    const double modulation=vibrato*std::sin(tau*5.*i/rate);
    phase+=tau*noteHz*std::exp2((cents+modulation)/1200.)/rate;
    out.left[i]=static_cast<float>(.17*std::sin(phase)+.045*std::sin(2*phase));
    out.right[i]=static_cast<float>(.11*std::sin(phase+.38)+.04*std::sin(2*phase+1.2));
  }
  return out;
}
static LeadFixture tune(LeadFixture signal,int rate,bool correction,
                        int block=127,float color=.5f,int releaseAt=-1){
  auto engine=std::make_unique<Engine>();
  need(engine->prepare(rate)&&engine->latencySamples()==std::lround(.024*rate),
       "Pitch Tune changed PDC");
  Settings s;s.wet=0.f;s.correction=correction;s.color=color;
  need(engine->setSettings(s),"lead-only settings rejected");
  const int size=static_cast<int>(signal.left.size());
  for(int i=0;i<size;){
    if(i==releaseAt){
      s.correction=false;
      need(engine->setSettings(s),"Pitch Tune release rejected");
    }
    const int end=i<releaseAt?releaseAt:size;
    const int n=std::min(block,end-i);
    engine->process(signal.left.data()+i,signal.right.data()+i,n);
    i+=n;
  }
  return signal;
}
static std::vector<double> centsByCycle(const LeadFixture& signal,int rate,double target){
  std::vector<double> cents;
  const auto& x=signal.left;
  double previous=-1.;
  for(int i=rate;i<static_cast<int>(x.size())-1;++i){
    if(x[i]>=0.f||x[i+1]<0.f)continue;
    const double crossing=i-x[i]/(x[i+1]-x[i]);
    if(previous>=0.)cents.push_back(1200.*std::log2(rate/(crossing-previous)/target));
    previous=crossing;
  }
  std::sort(cents.begin(),cents.end());
  need(cents.size()>50,"lead fixture lacks steady pitch");
  return cents;
}
static double stereoResidual(const LeadFixture& signal,int rate){
  double side=0.,mid=0.;
  for(std::size_t i=static_cast<std::size_t>(rate);i<signal.left.size();++i){
    const double l=signal.left[i],r=signal.right[i];
    side+=(l-r)*(l-r);mid+=(l+r)*(l+r);
  }
  return side/mid;
}
static void testLeadTune(){
  for(int rate:{48000,96000}){
    for(double hz:{82.406889,220.,523.251131,1046.502261}){
      const auto inTune=leadFixture(rate,hz,0.f,0.f);
      const auto dry=tune(inTune,rate,false);
      const auto unity=tune(inTune,rate,true);
      need(unity.left==dry.left&&unity.right==dry.right,
           "steady pitch inside the deadband altered stereo PCM");
      const auto sharp=leadFixture(rate,hz,35.f,0.f);
      const auto corrected=tune(sharp,rate,true);
      const auto cents=centsByCycle(corrected,rate,hz);
      const auto center=cents[cents.size()/2];
      need(center>=5.&&center<=27.,"lead correction failed to reduce pitch error gently");
      const auto sharpDry=tune(sharp,rate,false);
      need(std::abs(stereoResidual(corrected,rate)/stereoResidual(sharpDry,rate)-1.)<.10,
           "correction collapsed or inflated the stereo lead");
      const auto warm=tune(sharp,rate,true,64,0.f);
      need(warm.left==corrected.left&&warm.right==corrected.right,
           "lead correction changed with block size or backing COLOR");
    }
    const auto vibrato=leadFixture(rate,220.,0.f,55.f);
    const auto tuned=tune(vibrato,rate,true);
    const auto cycles=centsByCycle(tuned,rate,220.);
    const auto original=centsByCycle(tune(vibrato,rate,false),rate,220.);
    const auto span=[](const auto& values){
      return values[9*values.size()/10]-values[values.size()/10];
    };
    need(span(cycles)>.85*span(original)&&span(cycles)<1.15*span(original),
         "wide vibrato changed excessively");
    auto mono=leadFixture(rate,220.,-35.f,0.f);
    mono.right=mono.left;
    const auto on=tune(mono,rate,true);
    if(on.left!=on.right){
      float peak=0.f;int first=-1;
      for(int i=0;i<2*rate;++i){
        const float delta=std::abs(on.left[i]-on.right[i]);
        if(on.left[i]!=on.right[i]&&first<0)first=i;
        peak=std::max(peak,delta);
      }
      std::cerr<<"H1_MONO rate="<<rate<<" first="<<first
               <<" maxSideDelta="<<peak<<'\n';
    }
    need(on.left==on.right,"mono lead acquired a stereo discrepancy");
    const auto dry=tune(mono,rate,false);
    const auto disabled=tune(mono,rate,true,127,.5f,rate);
    need(std::equal(disabled.left.begin()+rate+rate/10,disabled.left.end(),
                    dry.left.begin()+rate+rate/10),
         "disable did not return to sample-exact delayed lead");
    for(int i=rate;i<rate+rate/10;++i)
      need(std::abs(disabled.left[i]-disabled.left[i-1])<.06f,
           "Pitch Tune toggle caused a PCM step");
  }
}
static void testVoicePlanner(){
  for(const auto& scale:{major,minor}){
    for(int key=0;key<12;++key){
      VoicePlanner planner;
      for(int melody=48;melody<=84;++melody){
        if(std::find(scale.begin(),scale.end(),(melody-key)%12)==scale.end())continue;
        planner.plan(melody,key,scale);
        for(std::size_t i=0;i<singers.size();++i){
          const int note=planner.target(i);
          const auto& role=singers[i];
          need(note>=melody+role.minInterval&&note<=melody+role.maxInterval,
               "diatonic singer crossed its register band");
          need(std::find(scale.begin(),scale.end(),(note-key)%12)!=scale.end(),
               "diatonic singer left the selected key");
        }
      }
    }
  }
  VoicePlanner planner;
  planner.plan(69,0,major);
  const int high=planner.target(2);
  planner.plan(67,0,major);
  need(planner.target(2)==high,"HIGH missed a stable common tone");
  planner.reset();
  planner.plan(67,0,major);
  need(planner.target(2)!=high,"reset retained previous phrase voicing");
}
// H4 independent expression is sample-clock owned, not callback-block
// owned. Common tones keep their individual vibrato age during a lead
// note change; only moved singers start a new settle/bloom.
static void testIndependentSingerExpression(){
  for(int rate:{32000,48000,96000,192000}){
    auto e=std::make_unique<EnsembleSpace>();
    need(e->prepare(rate),"H4 singer prepare");
    e->beginNote();
    const float oldAlpha=.00045f;
    for(int i=0;i<rate/2;++i){
      for(int v=0;v<5;++v){
        const float ratio=e->pitchRatio(v,1.f);
        need(ratio>.993f&&ratio<1.007f,"H4 expression out of cents bound");
      }
      e->advance(true);
    }
    const auto heldAge=e->noteAgeForOfflineTests(0);
    need(e->expressionForOfflineTests(0)>.99f&&
         e->expressionForOfflineTests(2)>.99f,"H4 sustain failed to bloom");
    // Unconfirmed melody candidate must not deflate a held backing note.
    for(int i=0;i<rate/100;++i){
      for(int v=0;v<5;++v)e->pitchRatio(v,1.f);
      e->advance(false,true);
    }
    need(e->expressionForOfflineTests(0)>.99f,"H4 pending note killed common tone");
    const auto ageAfterCandidate=e->noteAgeForOfflineTests(0);
    need(ageAfterCandidate>heldAge,"H4 common tone clock paused");
    e->beginNote(1u<<2);
    need(e->noteAgeForOfflineTests(0)==ageAfterCandidate&&
         e->noteAgeForOfflineTests(2)==0,"H4 common tone reset");
    need(e->ratioSmoothing(0,oldAlpha,1.f)==oldAlpha&&
         e->ratioSmoothing(2,oldAlpha,1.f)>oldAlpha*(48000.f/rate),
         "H4 pitch settle not role-specific");
    for(int i=0;i<rate/20;++i){
      for(int v=0;v<5;++v)e->pitchRatio(v,1.f);
      e->advance(true);
    }
    need(e->expressionForOfflineTests(0)>.99f&&
         e->expressionForOfflineTests(2)<.25f,"H4 moved and held voices bloom together");
    for(int i=0;i<rate/6;++i){
      for(int v=0;v<5;++v)e->pitchRatio(v,1.f);
      e->advance(false);
    }
    need(e->expressionForOfflineTests(0)<.001f&&
         e->expressionForOfflineTests(2)<.001f,"H4 expression retained through release");
  }
}
// Exercise the real F0 -> accepted note -> chord planner -> singer mask
// path, not merely manually calling the expression helper.
static void testCommonToneEngineTransition(){
  // G3 -> F3 has actual held LOW/HIGH under the H3 chord planner.
  // A3 -> its nearby notes does not guarantee any retained role.
  constexpr int first=55;
  int second=-1,held=-1,moved=-1;
  for(int candidate:{53,52,57,59,60}){
    VoicePlanner plan;plan.plan(first,0,major,5);
    std::array<int,5> before{};
    for(int v=0;v<5;++v)before[v]=plan.target(v);
    plan.plan(candidate,0,major,5);
    int h=-1,m=-1;
    for(int v=0;v<5;++v){
      if(plan.target(v)==before[v])h=v;
      else m=v;
    }
    if(h>=0&&m>=0){second=candidate;held=h;moved=m;break;}
  }
  need(second>=0,"H4 common-tone fixture unavailable");
  for(int rate:{48000,96000}){
    auto e=std::make_unique<Engine>();
    Settings s;s.voices=5;s.wet=.8f;
    need(e->prepare(rate)&&e->setSettings(s),"H4 phrase engine prepare");
    double phase=0.;
    auto note=[&](int midi,int frames){
      const double step=6.283185307179586*440.*std::exp2((midi-69)/12.)/rate;
      std::array<float,127> l{},r{};
      for(int pos=0;pos<frames;pos+=127){
        const int n=std::min(127,frames-pos);
        for(int i=0;i<n;++i){
          l[i]=r[i]=static_cast<float>(.16*std::sin(phase)+.04*std::sin(2.*phase+.2));
          phase+=step;
        }
        e->process(l.data(),r.data(),n);
      }
    };
    note(first,rate/2);
    need(e->observedMidiNote()==first,"H4 first phrase note not accepted");
    const int heldNote=e->singerTargetForOfflineTests(held);
    const int movedNote=e->singerTargetForOfflineTests(moved);
    const auto oldAge=e->singerAgeForOfflineTests(held);
    note(second,rate/4);
    need(e->observedMidiNote()==second,"H4 second phrase note not accepted");
    need(e->singerTargetForOfflineTests(held)==heldNote&&
         e->singerTargetForOfflineTests(moved)!=movedNote,
         "H4 actual planner did not retain intended common tone");
    need(e->singerAgeForOfflineTests(held)==oldAge+static_cast<std::uint64_t>(rate/4)&&
         e->singerAgeForOfflineTests(moved)<static_cast<std::uint64_t>(rate/4),
         "H4 actual singer ages restarted together");
    std::cout<<"H4_PHRASE rate="<<rate<<" held="<<held
             <<" moved="<<moved<<" heldAge="<<e->singerAgeForOfflineTests(held)
             <<" movedAge="<<e->singerAgeForOfflineTests(moved)<<std::endl;
  }
}
// H5: measurable broad spectral identity and fallback-only guard.
// Do not confuse this with true vowel/formant reconstruction.
static void testReducedLatency(){
  using Renderer=PitchSynchronousVoice<16384,5>;
  for(int rate:{32000,44100,48000,96000,192000}){
    const float period=static_cast<float>(rate/65.);
    const float live=static_cast<float>(std::lround(rate*.024));
    need(Renderer::completeEpochWindow(period,1.f,live),
         "H7 legal bass lead causal bound");
    need(!Renderer::completeEpochWindow(period,.5f,live),
         "H7 insufficient epoch lookback accepted");
    need(Renderer::completeEpochWindow(period,.59f,live+rate*.015f),
         "H7 bass support full epoch bound");
    auto previous=std::make_unique<Engine>(),candidate=std::make_unique<Engine>();
    need(previous->prepare(rate,true)&&candidate->prepare(rate),"H7 prepare");
    need(previous->latencySamples()==2*std::lround(rate*.016)&&
         candidate->latencySamples()==std::lround(rate*.024),"H7 declared delays");
    Settings settings;settings.wet=0.f;
    need(previous->setSettings(settings)&&candidate->setSettings(settings),"H7 dry settings");
    const int count=previous->latencySamples()+256;
    std::vector<float> l(count,0.f),r(count,0.f),oldL,oldR;
    l[0]=.6f;r[0]=-.25f;oldL=l;oldR=r;
    for(int at=0;at<count;at+=127){
      const int n=std::min(127,count-at);
      previous->process(oldL.data()+at,oldR.data()+at,n);
      candidate->process(l.data()+at,r.data()+at,n);
    }
    for(int i=0;i<count;++i){
      const bool hit=i==candidate->latencySamples(),oldHit=i==previous->latencySamples();
      need(l[i]==(hit?.6f:0.f)&&r[i]==(hit?-.25f:0.f)&&
           oldL[i]==(oldHit?.6f:0.f)&&oldR[i]==(oldHit?-.25f:0.f),
           "H7 exact dry impulse offset");
    }
    std::cout<<"H7_LATENCY rate="<<rate<<" old="<<previous->latencySamples()
             <<" live="<<candidate->latencySamples()<<std::endl;
  }
  // Compare aligned backing-only energy with the original 32ms renderer
  // on low bass through soprano harmonic-rich sustained notes.
  for(int rate:{48000,96000})for(double hz:{82.406889,220.,440.,880.}){
    const auto input=leadFixture(rate,hz,0.f,0.f);
    auto run=[&](bool legacy,bool wet,int block){
      auto e=std::make_unique<Engine>();Settings s;s.voices=5;s.wet=wet?1.f:0.f;
      s.color=1.f;s.space=.6f;
      need(e->prepare(rate,legacy)&&e->setSettings(s),"H7 A/B setup");
      auto out=input;
      for(int i=0;i<2*rate;i+=block)
        e->process(out.left.data()+i,out.right.data()+i,std::min(block,2*rate-i));
      return out;
    };
    const auto old=run(true,true,127),oldDry=run(true,false,127);
    const auto now=run(false,true,127),dry=run(false,false,127);
    const auto split=run(false,true,64);
    need(now.left==split.left&&now.right==split.right,"H7 block parity");
    double a=0.,b=0.;
    for(int i=rate;i<2*rate;++i){
      const double ol=old.left[i]-oldDry.left[i],orr=old.right[i]-oldDry.right[i];
      const double nl=now.left[i]-dry.left[i],nr=now.right[i]-dry.right[i];
      need(std::isfinite(nl)&&std::isfinite(nr),"H7 nonfinite PCM");
      a+=ol*ol+orr*orr;b+=nl*nl+nr*nr;
    }
    const double db=10.*std::log10(b/a);
    std::cout<<"H7_AB rate="<<rate<<" hz="<<hz<<" backing_dB="<<db<<std::endl;
    need(a>1.&&b>1.&&std::abs(db)<2.,"H7 backing energy regression");
  }
}
static void testResonanceEngine(){
  constexpr int rate=48000;
  auto input=leadFixture(rate,220.,0.f,0.f);input.right=input.left;
  auto render=[&](int voices,bool wet,bool resonance,int block){
    auto e=std::make_unique<Engine>();Settings s;
    s.voices=voices;s.wet=wet?1.f:0.f;s.color=1.f;s.space=.6f;
    need(e->prepare(rate)&&e->setResonanceForOfflineReference(resonance)&&
         e->setSettings(s),"H8 engine setup");
    auto out=input;
    for(int at=0;at<2*rate;at+=block)
      e->process(out.left.data()+at,out.right.data()+at,std::min(block,2*rate-at));
    return out;
  };
  const auto old=render(5,true,false,127),now=render(5,true,true,127);
  const auto split=render(5,true,true,64);
  const auto dry=render(5,false,true,127);
  need(now.left==split.left&&now.right==split.right,"H8 block parity");
  need(dry.left==render(5,false,false,64).left,"H8 lead changed");
  need(render(2,true,false,127).left==render(2,true,true,64).left,"H8 Duo changed");
  double a=0.,b=0.,delta=0.;
  for(int i=rate;i<2*rate;++i){
    const double x=old.left[i]-dry.left[i],y=now.left[i]-dry.left[i];
    a+=x*x;b+=y*y;delta+=(x-y)*(x-y);
    need(std::isfinite(y),"H8 engine nonfinite");
  }
  std::cout<<"H8_ENGINE gain_dB="<<10.*std::log10(b/a)
           <<" residual_dB="<<10.*std::log10(delta/a)<<std::endl;
  need(a>1.&&b>1.&&std::abs(10.*std::log10(b/a))<1.5&&delta/a>1.e-5,
       "H8 actual engine resonance absent or excessive");
}
// H10: compare both accumulation orders on identical input in the same
// process. Wall clock is diagnostic, not a machine-dependent pass gate.
// Interleave reference/candidate blocks and alternate measurement order.
static void testCorrelationPerformance(){
  using Clock=std::chrono::steady_clock;
  for(int rate:{48000,96000,192000})for(int block:{64,128}){
    const auto input=leadFixture(rate,rate==192000?880.:220.,35.f,55.f);
    auto x=input,y=input;
    // Identical deterministic noisy interruptions to exercise low
    // confidence, release and reacquisition rather than a sine-only loop.
    unsigned rng=123;
    for(int i=0;i<2*rate;++i){
      rng=1664525u*rng+1013904223u;
      if(i%(rate/4)<rate/100){
        const float n=.08f*static_cast<float>((rng>>8)/16777216.0-.5);
        x.left[i]+=n;x.right[i]-=n;
        y.left[i]=x.left[i];y.right[i]=x.right[i];
      }
    }
    auto old=std::make_unique<Engine>(),now=std::make_unique<Engine>();
    Settings s;s.voices=5;s.wet=.75f;s.correction=true;
    need(old->prepare(rate)&&now->prepare(rate)&&
         old->setFastCorrelationForOfflineReference(false)&&
         old->setSettings(s)&&now->setSettings(s),"H10 prepare");
    std::vector<double> before,after;
    before.reserve(2*rate/block+1);after.reserve(2*rate/block+1);
    double maxDiff=0.;
    for(int at=0;at<2*rate;at+=block){
      const int n=std::min(block,2*rate-at);
      auto measure=[&](Engine& e,LeadFixture& z){
        const auto start=Clock::now();
        e.process(z.left.data()+at,z.right.data()+at,n);
        return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
      };
      double a,b;
      if((at/block)&1){b=measure(*now,y);a=measure(*old,x);}
      else{a=measure(*old,x);b=measure(*now,y);}
      if(at>=rate/3){before.push_back(a);after.push_back(b);}
      need(old->observedMidiNote()==now->observedMidiNote()&&
           old->voiced()==now->voiced(),"H10 pitch decision changed");
      for(int i=at;i<at+n;++i){
        need(std::isfinite(y.left[i])&&std::isfinite(y.right[i]),"H10 nonfinite");
        maxDiff=std::max({maxDiff,std::abs(double(x.left[i])-y.left[i]),
                                 std::abs(double(x.right[i])-y.right[i])});
      }
    }
    need(maxDiff<=1.e-6&&old->completedPitchAnalyses()==now->completedPitchAnalyses()&&
         old->completedPitchAnalyses()>100&&
         now->peakPitchLagWork()<=Engine::maxPitchLagsPerSlice&&
         old->abandonedPitchAnalyses()==now->abandonedPitchAnalyses(),
         "H10 reference PCM or bounded analysis changed");
    std::sort(before.begin(),before.end());std::sort(after.begin(),after.end());
    const auto percentile=[](const auto& v,int p){return v[(v.size()-1)*p/100];};
    const double deadline=1.e6*block/rate;
    const auto misses=[&](const auto& v){return std::count_if(v.begin(),v.end(),
       [&](double a){return a>deadline;});};
    std::cout<<std::fixed<<std::setprecision(3)
      <<"H10_CPU rate="<<rate<<" block="<<block<<" count="<<before.size()
      <<" old_p50_us="<<percentile(before,50)<<" new_p50_us="<<percentile(after,50)
      <<" old_p95_us="<<percentile(before,95)<<" new_p95_us="<<percentile(after,95)
      <<" old_p99_us="<<percentile(before,99)<<" new_p99_us="<<percentile(after,99)
      <<" old_max_us="<<before.back()<<" new_max_us="<<after.back()
      <<" old_misses="<<misses(before)<<" new_misses="<<misses(after)
      <<" deadline_us="<<deadline<<" max_pcm_diff="<<maxDiff
      <<" exact_pcm="<<(maxDiff==0.)<<" analyses="<<now->completedPitchAnalyses()
      <<std::defaultfloat<<std::setprecision(6)<<std::endl;
  }
}
static void testArticulation(){
  constexpr double tau=6.283185307179586;
  for(int rate:{32000,48000,96000,192000}){
    BackingArticulation guard;guard.prepare(rate);
    auto feed=[&](double hz,int ms,bool harmonics){
      float gain=1.f;
      for(int i=0;i<rate*ms/1000;++i){
        const double p=tau*hz*i/rate;
        const float x=static_cast<float>(.15*(std::sin(p)+
            (harmonics?.25*std::sin(2*p)+.17*std::sin(3*p):0.)));
        gain=guard.process(x);
        need(gain>=.5499f&&gain<=1.f,"H9 articulation bound");
      }
      return gain;
    };
    const float vowel=feed(880.,150,true),sibilant=feed(7000.,35,false);
    const float recovered=feed(440.,120,true);
    std::cout<<"H9_GUARD rate="<<rate<<" vowel="<<vowel
             <<" sibilant="<<sibilant<<" recovered="<<recovered<<std::endl;
    need(vowel>.995f&&sibilant<.68f&&recovered>.995f,"H9 articulation response");
  }
  for(int rate:{48000,96000}){
    // Voiced vowel -> high-frequency noisy consonant -> voiced vowel -> silence.
    const int n=rate*2;
    LeadFixture input{std::vector<float>(n),std::vector<float>(n)};
    unsigned rng=19;float previous=0.f;
    for(int i=0;i<n;++i){
      rng=1664525u*rng+1013904223u;
      const float noise=static_cast<float>((rng>>8)/16777216.0-.5);
      const double time=double(i)/rate,p=tau*220.*time;
      float x=0.f;
      if(time<.60|| (time>=.69&&time<1.4))
        x=static_cast<float>(.14*std::sin(p)+.04*std::sin(2*p));
      if(time>=.60&&time<.69)x=.23f*(noise-previous);
      previous=noise;input.left[i]=input.right[i]=x;
    }
    auto run=[&](bool enabled,bool wet,int voices,int block){
      auto e=std::make_unique<Engine>();Settings s;
      s.voices=voices;s.wet=wet?1.f:0.f;s.color=1.f;s.space=0.f;
      need(e->prepare(rate)&&e->setArticulationForOfflineReference(enabled)&&
           e->setSettings(s),"H9 engine setup");
      auto out=input;
      for(int i=0;i<n;i+=block)e->process(out.left.data()+i,out.right.data()+i,std::min(block,n-i));
      return out;
    };
    const auto old=run(false,true,5,127),now=run(true,true,5,127);
    const auto dry=run(true,false,5,127),split=run(true,true,5,64);
    need(now.left==split.left&&now.right==split.right,"H9 block parity");
    need(dry.left==run(false,false,5,64).left,"H9 dry changed");
    need(run(false,true,2,127).left==run(true,true,2,64).left,"H9 Duo changed");
    auto energy=[&](const LeadFixture& x,double begin,double end){
      double sum=0.;
      for(int i=static_cast<int>(rate*begin);i<static_cast<int>(rate*end);++i){
        const double l=x.left[i]-dry.left[i],r=x.right[i]-dry.right[i];
        need(std::isfinite(l)&&std::isfinite(r),"H9 nonfinite PCM");
        sum+=l*l+r*r;
      }
      return sum;
    };
    const double before=energy(old,.25,.55),after=energy(now,.25,.55);
    const double oldNoise=energy(old,.638,.69),newNoise=energy(now,.638,.69);
    const double recovery=10.*std::log10(energy(now,1.,1.3)/energy(old,1.,1.3));
    const double change=10.*std::log10(newNoise/oldNoise);
    std::cout<<"H9_PCM rate="<<rate<<" consonant_dB="<<change
             <<" recovered_dB="<<recovery<<std::endl;
    need(before>1.&&std::abs(10.*std::log10(after/before))<.05&&
         oldNoise>1.e-6&&change<-.8&&change>-8.&&std::abs(recovery)<.05,
         "H9 consonant suppression or vowel preservation");
  }
}
static void testSingerSpectralIdentity(){
  auto response=[](int rate,int role,double hz,float share){
    auto e=std::make_unique<EnsembleSpace>();
    need(e->prepare(rate),"H5 identity prepare");
    double in=0.,out=0.;
    for(int i=0;i<rate/5;++i){
      const float x=static_cast<float>(.2*std::sin(6.283185307179586*hz*i/rate));
      const float y=e->shapeSinger(role,x,1.f,1.7f,share);
      need(std::isfinite(y)&&std::abs(y)<.35f,"H5 identity unbounded");
      if(i>rate/10){in+=x*x;out+=y*y;}
    }
    return 10.*std::log10(out/in);
  };
  for(int rate:{32000,48000,96000,192000}){
    const double highBody=response(rate,2,440.,0.f);
    const double highPresence=response(rate,2,1800.,0.f);
    const double highAir=response(rate,2,6500.,0.f);
    const double fallbackAir=response(rate,2,6500.,1.f);
    const double lowAir=response(rate,0,6500.,0.f);
    const double midAir=response(rate,1,6500.,0.f);
    need(std::abs(highBody)<.7&&highPresence>highBody&&
         highPresence>highAir,"H5 HIGH lost body/presence contrast");
    need(fallbackAir<highAir-.45&&fallbackAir>-4.,
         "H5 fallback guard not bounded or ineffective");
    need(midAir>lowAir+.6,"H5 singers lack independent spectrum");
    std::cout<<"H5_IDENTITY rate="<<rate<<" body_dB="<<highBody
             <<" presence_dB="<<highPresence<<" air_dB="<<highAir
             <<" fallback_air_dB="<<fallbackAir<<std::endl;
  }
}
// Exercise the real native solo stem with a harmonic-rich vowel proxy.
// Compare H4/H5 at matched dry and prepared voice gain, same renderer.
static void testSingerIdentityPcm(){
  constexpr int rate=48000;
  LeadFixture input{std::vector<float>(2*rate),std::vector<float>(2*rate)};
  for(int i=0;i<2*rate;++i){
    const double p=6.283185307179586*220.*i/rate;
    double x=0.;
    for(int h=1;h<=18;++h){
      const double f=220.*h;
      const double vowel=.12+.8*std::exp(-std::pow((f-800.)/450.,2.))+
                         .55*std::exp(-std::pow((f-2300.)/700.,2.));
      x+=.17*vowel*std::sin(h*p+.13*h)/std::sqrt(static_cast<double>(h));
    }
    input.left[i]=input.right[i]=static_cast<float>(x);
  }
  auto render=[&](int role,int voices,bool identity,int block){
    auto e=std::make_unique<Engine>();Settings s;
    s.voices=voices;s.wet=.85f;s.space=0.f;s.color=1.f;
    Engine::PreparedSettings prep{};
    need(e->prepare(rate)&&e->setSingerIdentityForOfflineReference(identity)&&
         Engine::prepareSettings(s,prep),"H5 actual PCM setup");
    for(int v=0;v<5;++v)if(v!=role)prep.mixL[v]=prep.mixR[v]=0.f;
    need(e->applyPreparedSettings(prep),"H5 actual solo setup");
    auto out=input;
    for(int i=0;i<2*rate;i+=block)
      e->process(out.left.data()+i,out.right.data()+i,std::min(block,2*rate-i));
    return out;
  };
  const auto dry=render(-1,5,true,127),old=render(2,5,false,127);
  const auto now=render(2,5,true,127),split=render(2,5,true,64);
  need(now.left==split.left&&now.right==split.right,"H5 block parity");
  need(dry.left==render(-1,5,false,64).left,"H5 dry changed");
  need(render(2,2,false,127).left==render(2,2,true,64).left,"H5 Duo changed");
  double oldE=0.,newE=0.,difference=0.;
  for(int i=rate;i<2*rate;++i){
    const double a=old.left[i]-dry.left[i],b=now.left[i]-dry.left[i];
    oldE+=a*a;newE+=b*b;difference+=(a-b)*(a-b);
    need(std::isfinite(b),"H5 nonfinite vowel");
  }
  const double gain=10.*std::log10(newE/oldE);
  const double change=10.*std::log10(difference/oldE);
  std::cout<<"H5_PCM gain_dB="<<gain<<" residual_dB="<<change<<std::endl;
  need(std::abs(gain)<1.2&&change>-45.&&change<-12.,
       "H5 vowel change excessive or absent");
}
// H6 guide is an independent PCM source, not a guess from lead F0.
static void testChordAwareGuide(){
  for(int rate:{32000,48000,96000,192000}){
    MusicKeyDetector guide,split;
    need(guide.prepare(rate)&&split.prepare(rate),"H6 guide prepare");
    auto feed=[&](int root,int quality,int ms,bool single=false){
      const int count=rate*ms/1000;
      std::vector<float> x(count);
      for(int i=0;i<count;++i){
        double y=0.;
        if(root>=0){
          for(int interval:{0,quality?3:4,7}){
            if(single&&interval!=0)continue;
            const double hz=130.81278265*std::exp2((root+interval)/12.);
            y+=.12*std::sin(6.283185307179586*hz*i/rate);
          }
        }
        x[i]=static_cast<float>(y);
      }
      for(int i=0;i<count;i+=64)
        guide.process(x.data()+i,x.data()+i,std::min(64,count-i));
      for(int i=0;i<count;i+=257)
        split.process(x.data()+i,x.data()+i,std::min(257,count-i));
      const auto a=guide.chordEstimate(),b=split.chordEstimate();
      need(a.root==b.root&&a.quality==b.quality&&a.confidence==b.confidence,
           "H6 guide depends on callback partition");
      return a;
    };
    auto c=feed(0,0,600);
    need(c.valid()&&c.root==0&&c.quality==0,"H6 C major detection");
    c=feed(5,0,600);
    need(c.valid()&&c.root==5&&c.quality==0,"H6 F major transition");
    c=feed(9,1,600);
    need(c.valid()&&c.root==9&&c.quality==1,"H6 A minor transition");
    need(!feed(9,1,700,true).valid(),"H6 monophonic source guessed chord");
    need(!feed(-1,0,400).valid(),"H6 silent source retained chord");
    need(guide.peakChordScoresPerSample()<=2,"H6 scoring burst");
    guide.reset();need(!guide.chordEstimate().valid(),"H6 reset retained chord");
    std::cout<<"H6_GUIDE rate="<<rate<<" C/F/Am/mono/silence PASS"<<std::endl;
  }
  VoicePlanner planner;
  need(planner.planGuided(60,0,major,5,0,0),"H6 C chord arrangement");
  const int old=planner.target(2);
  need(planner.planGuided(60,0,major,5,5,0)&&planner.target(2)!=old,
       "H6 sustained melody did not follow F chord");
  for(int v=0;v<5;++v){
    const int pc=planner.target(v)%12;
    need(pc==0||pc==5||pc==9,"H6 backing not in actual F triad");
  }
  need(!planner.planGuided(62,0,major,5,5,0),"H6 incompatible melody forced chord");
  auto e=std::make_unique<Engine>();Settings s;s.guideMode=1;s.guideChannelId=99;
  s.voices=5;s.wet=1.f;
  need(e->prepare(48000)&&e->setSettings(s),"H6 vocal setup");
  auto input=leadFixture(48000,261.625565,0.f,0.f);input.right=input.left;
  e->setGuideChord(0,0);
  e->process(input.left.data(),input.right.data(),24000);
  need(e->voiced()&&e->guidedVoicingForOfflineTests(),"H6 native C voicing absent");
  const int high=e->singerTargetForOfflineTests(2);
  e->setGuideChord(5,0);
  e->process(input.left.data()+24000,input.right.data()+24000,24000);
  need(e->voiced()&&e->observedMidiNote()==60&&
       e->guidedVoicingForOfflineTests()&&e->singerTargetForOfflineTests(2)!=high,
       "H6 held lead failed chord-only replan");
  e->setGuideChord(-1,-1);
  e->process(input.left.data()+48000,input.right.data()+48000,24000);
  need(e->voiced()&&!e->guidedVoicingForOfflineTests(),"H6 chord loss failed fallback");
  s.guideMode=0;s.guideChannelId=0;
  need(e->setSettings(s),"H6 manual settings");
  e->setGuideChord(5,0);
  e->process(input.left.data()+72000,input.right.data()+72000,24000);
  need(!e->guidedVoicingForOfflineTests(),"H6 manual accepted chord");
}
// Audit chord membership across every key, both scale families and
// ascending/descending note history. Require actual register separation;
// a collection of independently diatonic pitches is insufficient.
static void testMusicalEnsemblePlanner(){
  int checked=0;
  for(const auto& scale:{major,minor})for(int key=0;key<12;++key){
    VoicePlanner planner;
    for(int direction:{1,-1}){
      planner.reset();
      for(int step=0;step<=48;++step){
        const int melody=direction>0?36+step:84-step;
        const int pc=((melody-key)%12+12)%12;
        if(std::find(scale.begin(),scale.end(),pc)==scale.end())continue;
        planner.plan(melody,key,scale,5);
        const std::array<int,6> notes{{planner.target(3),planner.target(0),
          melody,planner.target(1),planner.target(2),planner.target(4)}};
        for(std::size_t i=1;i<notes.size();++i)
          need(notes[i]-notes[i-1]>=3,"H3 singers packed closer than a third");
        bool triad=false;
        for(int root=0;root<7;++root){
          const std::array<int,3> chord{{scale[root],scale[(root+2)%7],scale[(root+4)%7]}};
          if((chord[2]-chord[0]+12)%12!=7)continue;
          bool all=true;
          for(int n:notes){
            const int c=((n-key)%12+12)%12;
            all=all&&std::find(chord.begin(),chord.end(),c)!=chord.end();
          }
          triad=triad||all;
        }
        need(triad,"H3 ensemble does not share a consonant diatonic triad");
        for(std::size_t i=0;i<singers.size();++i)
          need(planner.target(i)-melody>=singers[i].minInterval&&
               planner.target(i)-melody<=singers[i].maxInterval,
               "H3 singer escaped realtime pitch-ratio register");
        ++checked;
      }
    }
  }
  std::cout<<"H3_VOICING checked="<<checked<<std::endl;
  for(int n:{3,4,5}){
    Settings s;s.voices=n;s.space=.6f;
    Engine::PreparedSettings p{};
    need(Engine::prepareSettings(s,p),"H3 stage prepare");
    double prev=-2.;
    for(int i:{0,3,1,4,2}){
      if(!singerActive(singers[i].role,n))continue;
      const double pan=(p.mixR[i]-p.mixL[i])/(p.mixR[i]+p.mixL[i]);
      need(pan-prev>.12,"H3 active positions too close");
      prev=pan;
    }
  }
}
// Actual per-role native PCM, not just intended note metadata. Keep
// the same gain/pan/arrangement and solo one slot through prepared mixing.
static bool testMusicalHighStem(){
  bool pass=true;
  constexpr double tau=6.283185307179586;
  for(int rate:{48000,96000})for(double hz:{110.,220.,440.}){
    auto input=leadFixture(rate,hz,0.f,0.f);input.right=input.left;
    for(int count:{3,5}){
      Settings s;s.voices=count;s.wet=1.f;s.color=1.f;s.space=.6f;
      auto render=[&](int role,int block){
        auto e=std::make_unique<Engine>();Engine::PreparedSettings prep{};
        need(e->prepare(rate)&&Engine::prepareSettings(s,prep),"H3 PCM setup");
        for(int i=0;i<5;++i)if(i!=role)prep.mixL[i]=prep.mixR[i]=0.f;
        need(e->applyPreparedSettings(prep),"H3 PCM prepared solo");
        auto out=input;
        for(int i=0;i<2*rate;i+=block)
          e->process(out.left.data()+i,out.right.data()+i,std::min(block,2*rate-i));
        return out;
      };
      const auto dry=render(-1,127);
      auto energy=[&](const LeadFixture& o){
        double e=0.;
        for(int i=rate;i<2*rate;++i){
          const double l=o.left[i]-dry.left[i],r=o.right[i]-dry.right[i];
          e+=l*l+r*r;
        }
        return e;
      };
      const auto low=render(0,127),high=render(2,127);
      const auto high64=render(2,64);
      need(high.left==high64.left&&high.right==high64.right,"H3 high block parity");
      const double balance=10.*std::log10(energy(high)/energy(low));
      VoicePlanner planner;const int melody=69+static_cast<int>(std::lround(12.*std::log2(hz/440.)));
      planner.plan(melody,0,major,count);
      const double target=440.*std::exp2((planner.target(2)-69)/12.);
      // Search a local band of the isolated right-side HIGH fundamental.
      auto power=[&](double f){
        double a=0.,b=0.;
        for(int i=rate;i<rate+rate/2;i+=2){
          const double sample=high.right[i]-dry.right[i];
          a+=sample*std::cos(tau*f*i/rate);b+=sample*std::sin(tau*f*i/rate);
        }
        return a*a+b*b;
      };
      double best=0.,found=0.;
      for(int cents=-150;cents<=150;cents+=5){
        const double f=target*std::exp2(cents/1200.);
        const double powerAt=power(f);
        if(powerAt>best){best=powerAt;found=f;}
      }
      const double error=1200.*std::log2(found/target);
      std::cout<<"H3_HIGH rate="<<rate<<" hz="<<hz<<" voices="<<count
               <<" expected="<<target<<" found="<<found<<" cents="<<error
               <<" high_vs_low_dB="<<balance<<std::endl;
      pass=pass&&std::isfinite(balance)&&balance>=-3.&&balance<=8.&&
           std::abs(error)<=35.&&best>1.;
    }
  }
  return pass;
}
// Duo is two PITCHED wet voices; Doubler is a SINGLE synthetic unison take.
// This is a native PCM proof, not a certified real-singer naturalness test.
// Field report: long high notes and ambiguous/unvoiced intervals must
// never interrupt the new unison take. Check actual wet-only PCM, not
// just pitch detector state. 4s sustained vibrato at 220/880/1320 Hz.
static void testLowerPresenceAndPresets(){
  // Actual stem PCM, compare H11 normalized mix weights AND direct
  // field with H12, keeping all pitch/room synthesis otherwise equal.
  // Four-voice normalization alone can slightly reduce LOW amplitude;
  // the additional direct focus must deliver the net audible lift.
  constexpr std::array<float,5> prior{{1.20f,.88f,1.85f,.94f,1.30f}};
  for(int count:{4,5})for(double hz:{110.,220.,440.}){
    constexpr int rate=48000;
    auto input=leadFixture(rate,hz,0.f,0.f);input.right=input.left;
    Settings s;s.voices=count;s.wet=1.f;s.color=1.f;s.space=.75f;
    const auto render=[&](int role,bool previous){
      auto e=std::make_unique<Engine>();Engine::PreparedSettings p;
      need(e->prepare(rate)&&e->setLowerFocusForOfflineReference(!previous)&&
           Engine::prepareSettings(s,p),"H12 lower prep");
      if(previous){
        double oldPower=0.,newPower=0.;
        for(int v=0;v<5;++v)if(singerActive(singers[v].role,count)){
          oldPower+=prior[v]*prior[v];
          const float w=Engine::singerPresence(v,count);newPower+=w*w;
        }
        for(int v=0;v<5;++v){
          const float k=static_cast<float>(std::sqrt(newPower/oldPower))*
              prior[v]/Engine::singerPresence(v,count);
          p.mixL[v]*=k;p.mixR[v]*=k;
        }
      }
      for(int v=0;v<5;++v)if(v!=role)p.mixL[v]=p.mixR[v]=0.f;
      need(e->applyPreparedSettings(p),"H12 lower apply");
      auto out=input;
      for(int i=0;i<2*rate;i+=127)
        e->process(out.left.data()+i,out.right.data()+i,std::min(127,2*rate-i));
      return out;
    };
    const auto dry=render(-1,false);
    const auto energy=[&](const LeadFixture& x){
      double e=0.;
      for(int i=rate;i<2*rate;++i){
        const double l=x.left[i]-dry.left[i],r=x.right[i]-dry.right[i];e+=l*l+r*r;
      }
      return e;
    };
    const auto low=energy(render(0,false)),lower=energy(render(3,false));
    const auto oldLow=energy(render(0,true)),oldLower=energy(render(3,true));
    const double lowLift=10.*std::log10(low/oldLow);
    const double lowerLift=10.*std::log10(lower/oldLower);
    std::cout<<"H12_LOWER voices="<<count<<" hz="<<hz
             <<" low_lift_dB="<<lowLift<<" lower_lift_dB="<<lowerLift
             <<" lower_vs_low_dB="<<10.*std::log10(lower/low)<<std::endl;
    need(lowLift>0.&&lowerLift>1.2&&lower>1.,"H12 lower register presence");
  }
  // Exact factory preset pairs; the UI contract independently verifies
  // values. Remove output gain from stem comparison, then verify actual
  // nominal output headroom with the selected gain.
  struct Pair{const char* name;int mode,voices;float ow,os,oc,og,w,s,c,g;};
  constexpr Pair pairs[]{
    {"doubleLight",1,2,.38f,.35f,.57f,0.f,.68f,.58f,.78f,0.f},
    {"doubleWide",1,2,.52f,.82f,.48f,-1.f,.86f,.88f,.86f,0.f},
    {"duo",0,2,.31f,.35f,.45f,0.f,.76f,.72f,.78f,0.f},
    {"trio",0,3,.40f,.56f,.50f,0.f,.82f,.78f,.82f,0.f},
    {"quartet",0,4,.52f,.95f,.66f,-2.f,.88f,.94f,.88f,-1.f},
    {"quintet",0,5,.45f,.88f,.61f,-3.f,.90f,.94f,.90f,-1.f},
    {"choir",0,4,.58f,.30f,.24f,-3.f,.88f,.70f,.68f,-1.f}};
  constexpr int rate=48000;
  auto src=leadFixture(rate,220.,0.f,0.f);src.right=src.left;
  auto render=[&](Settings s){
    auto e=std::make_unique<Engine>();
    need(e->prepare(rate)&&e->setSettings(s),"H12 preset prep");
    auto out=src;
    for(int i=0;i<2*rate;i+=127)
      e->process(out.left.data()+i,out.right.data()+i,std::min(127,2*rate-i));
    return out;
  };
  Settings s;s.wet=0.f;const auto dry=render(s);
  for(const auto& p:pairs){
    s.voiceMode=p.mode;s.voices=p.voices;s.wet=p.ow;s.space=p.os;s.color=p.oc;s.gainDb=p.og;
    const auto old=render(s);
    s.wet=p.w;s.space=p.s;s.color=p.c;s.gainDb=p.g;
    const auto now=render(s);
    const auto level=[&](const LeadFixture& out,float gain){
      double wet=0.,lead=0.;const double g=std::pow(10.,gain/20.);
      for(int i=rate;i<2*rate;++i){
        need(std::abs(out.left[i])<1.f&&std::abs(out.right[i])<1.f,"H12 preset nominal clipping");
        const double l=out.left[i]/g-dry.left[i],r=out.right[i]/g-dry.right[i];
        wet+=l*l+r*r;lead+=dry.left[i]*dry.left[i]+dry.right[i]*dry.right[i];
      }
      return 10.*std::log10(wet/lead);
    };
    const double before=level(old,p.og),after=level(now,p.g);
    std::cout<<"H12_PRESET "<<p.name<<" prior_settings_dB="<<before
             <<" new_settings_dB="<<after<<" lift_dB="<<after-before<<std::endl;
    need(after>before+3.&&after>-12.&&after<2.,"H12 preset audibility");
  }
}
// Force loss at eight phase positions and measure the immediate slope
// discontinuity, not the ordinary derivative of a sung waveform.
// Exercise real renderer loss/recovery on a bass/high phrase. Do not
// infer that the losses are causal-boundary events without trace evidence.
// Non-gating callback timing: interleave exact H19 and optimized H20+
 // on a changing bass/high voice. Source generation and trace are untimed.
static void testEpochRecoveryPerformance(){
    using Clock=std::chrono::steady_clock;
    constexpr double tau=6.283185307179586;
    for(int rate:{48000,96000,192000})for(int block:{64,128}){
        LeadFixture src{std::vector<float>(2*rate),std::vector<float>(2*rate)};
        double phase=0.;
        for(int i=0;i<2*rate;++i){
            const double sec=double(i)/rate;
            const double hz=(i/(rate/3))%2?82.40689:330.;
            phase+=tau*hz*std::exp2(35.*std::sin(tau*5.*sec)/1200.)/rate;
            src.left[i]=src.right[i]=static_cast<float>(
                .17*std::sin(phase)+.055*std::sin(2*phase+.3)+.03*std::sin(3*phase+.6));
        }
        auto a=src,b=src;
        auto old=std::make_unique<Engine>(),now=std::make_unique<Engine>();
        Settings s;s.voices=5;s.wet=1.f;s.color=.78f;s.space=1.f;
        need(old->prepare(rate)&&now->prepare(rate)&&
             old->setEpochRecoveryForOfflineReference(false)&&old->setSettings(s)&&now->setSettings(s),
             "H21 CPU fixture setup");
        std::vector<double> before,after;
        before.reserve(2*rate/block+1);after.reserve(2*rate/block+1);
        for(int at=0;at<2*rate;at+=block){
            const int n=std::min(block,2*rate-at);
            const auto measure=[&](Engine& e,LeadFixture& z){
                const auto start=Clock::now();
                e.process(z.left.data()+at,z.right.data()+at,n);
                return std::chrono::duration<double,std::micro>(Clock::now()-start).count();
            };
            double x,y;
            if((at/block)&1){y=measure(*now,b);x=measure(*old,a);}
            else{x=measure(*old,a);y=measure(*now,b);}
            if(at>=rate/4&&n==block){before.push_back(x);after.push_back(y);}
        }
        double peak=0.;std::size_t overload=0;
        for(int i=0;i<2*rate;++i){
            need(std::isfinite(b.left[i])&&std::isfinite(b.right[i]),"H21 nonfinite PCM");
            peak=std::max({peak,double(std::abs(b.left[i])),double(std::abs(b.right[i]))});
            overload+=(std::abs(b.left[i])>1.f)+(std::abs(b.right[i])>1.f);
        }
        std::sort(before.begin(),before.end());std::sort(after.begin(),after.end());
        const double deadline=1.e6*block/rate;
        const auto p=[](const auto& v,int n){return v[(v.size()-1)*n/100];};
        const auto misses=[&](const auto& v){return std::count_if(v.begin(),v.end(),[&](double x){return x>deadline;});};
        std::cout<<"H21_CPU rate="<<rate<<" block="<<block<<" callbacks="<<before.size()
                 <<" old_p50_us="<<p(before,50)<<" new_p50_us="<<p(after,50)
                 <<" old_p99_us="<<p(before,99)<<" new_p99_us="<<p(after,99)
                 <<" old_max_us="<<before.back()<<" new_max_us="<<after.back()
                 <<" deadline_us="<<deadline<<" old_misses="<<misses(before)
                 <<" new_misses="<<misses(after)<<" peak="<<peak<<" overload="<<overload<<std::endl;
    }
}
static void testCausalAdmissionEngine(){
    constexpr double tau=6.283185307179586;
    for(int rate:{48000,96000}){
        const int n=rate*4;
        LeadFixture src{std::vector<float>(n),std::vector<float>(n)};
        double phase=0.;
        for(int i=0;i<n;++i){
            const double sec=double(i)/rate;
            const double hz=sec<1.?220.:sec<2.?82.40689:sec<3.?220.:110.;
            phase+=tau*hz*std::exp2(35.*std::sin(tau*5.*sec)/1200.)/rate;
            src.left[i]=src.right[i]=static_cast<float>(
                .17*std::sin(phase)+.055*std::sin(2*phase+.3)+.03*std::sin(3*phase+.6));
        }
        struct Result {LeadFixture pcm;std::uint64_t losses=0,acquisitions=0;Engine::RendererTrace trace{};};
        auto run=[&](bool drain,int block,bool wet,bool trace=true,bool recover=false,bool cubic=false){
            auto e=std::make_unique<Engine>();Settings s;s.voices=5;
            s.wet=wet?1.f:0.f;s.color=.78f;s.space=.6f;
            need(e->prepare(rate)&&e->setDrainForOfflineReference(drain)&&
                 e->setRendererTraceForOfflineTests(trace)&&e->setEpochRecoveryForOfflineReference(recover)&&
                 e->setCubicForOfflineReference(cubic)&&
                 e->setSettings(s),"H18 engine setup");
            Result out{src};
            for(int i=0;i<n;i+=block)e->process(out.pcm.left.data()+i,out.pcm.right.data()+i,std::min(block,n-i));
            for(int v=0;v<Engine::maxVoices;++v){
                const auto c=e->seamCountsForOfflineTests(v);
                out.losses+=c.losses;out.acquisitions+=c.acquisitions;
                const auto q=e->rendererTraceForOfflineTests(v);
                for(std::size_t k=0;k<q.values.size();++k)out.trace.values[k]+=q.values[k];
            }
            need(!e->setRendererTraceForOfflineTests(false),"H19 trace changed after PCM");
            return out;
        };
        const auto old=run(false,127,true),now=run(true,127,true),split=run(true,64,true);
        const auto dry=run(true,127,false),oldDry=run(false,64,false);
        const auto h20=run(true,127,true,true,true),h20Split=run(true,64,true,true,true);
        const auto h20Dry=run(true,64,false,true,true);
        const auto h22=run(true,127,true,true,true,true),h22Split=run(true,64,true,true,true,true);
        const auto h22Dry=run(true,64,false,true,true,true);
        need(h22.pcm.left==h22Split.pcm.left&&h22.pcm.right==h22Split.pcm.right&&
             h22.trace.values==h20.trace.values&&h22.losses==h20.losses&&
             h22Dry.pcm.left==h20Dry.pcm.left&&h22Dry.pcm.right==h20Dry.pcm.right,
             "H22 exact scheduler/block/dry parity");
        for(int ch:{0,1}){
            const auto& a=ch?h20.pcm.right:h20.pcm.left;const auto& b=ch?h22.pcm.right:h22.pcm.left;
            const auto& d=ch?dry.pcm.right:dry.pcm.left;
            double ea=0.,eb=0.,ma=0.,mb=0.;
            for(int i=rate/2;i<n;++i){
                const double u=a[i]-d[i],v=b[i]-d[i];
                need(std::isfinite(v),"H22 nonfinite backing");ea+=u*u;eb+=v*v;
                ma=std::max(ma,std::abs(u-(a[i-1]-d[i-1])));
                mb=std::max(mb,std::abs(v-(b[i-1]-d[i-1])));
            }
            const double gain=10.*std::log10(eb/ea),edge=mb/ma*std::sqrt(ea/eb);
            std::cout<<"H22_PCM rate="<<rate<<" ch="<<ch<<" wet_dB="<<gain
                     <<" normalized_max_ratio="<<edge<<std::endl;
            need(ea>1.&&eb>1.&&std::abs(gain)<.5&&edge<1.15,"H22 PCM regression");
        }
        need(h20.pcm.left==h20Split.pcm.left&&h20.pcm.right==h20Split.pcm.right&&
             h20.trace.values==h20Split.trace.values,"H20 block parity");
        need(h20Dry.pcm.left==dry.pcm.left&&h20Dry.pcm.right==dry.pcm.right,"H20 dry changed");
        using Trace=Engine::RendererTrace;
        const auto& x=h20.trace.values;
        std::cout<<"H20_ENGINE rate="<<rate<<" old_losses="<<now.losses<<" new_losses="<<h20.losses
                 <<" old_unready="<<now.trace.values[Trace::safeUnready]<<" new_unready="<<x[Trace::safeUnready]
                 <<" old_partial="<<now.trace.values[Trace::partial]<<" new_partial="<<x[Trace::partial]
                 <<" recovered="<<x[Trace::epochRecovered]<<" rescue_sites="<<x[Trace::recoverySites]<<std::endl;
        need(x[Trace::ready]>0&&h20.losses<=now.losses&&x[Trace::safeUnready]<=now.trace.values[Trace::safeUnready],
             "H20 engine readiness regression");
        for(int ch:{0,1}){
            const auto& a=ch?now.pcm.right:now.pcm.left;const auto& b=ch?h20.pcm.right:h20.pcm.left;
            const auto& d=ch?dry.pcm.right:dry.pcm.left;
            double ea=0.,eb=0.,ma=0.,mb=0.;
            for(int i=rate/2;i<n;++i){
                const double u=a[i]-d[i],v=b[i]-d[i];
                need(std::isfinite(v),"H20 nonfinite backing");ea+=u*u;eb+=v*v;
                ma=std::max(ma,std::abs(u-(a[i-1]-d[i-1])));
                mb=std::max(mb,std::abs(v-(b[i-1]-d[i-1])));
            }
            const double gain=10.*std::log10(eb/ea),edge=mb/ma*std::sqrt(ea/eb);
            std::cout<<"H20_PCM rate="<<rate<<" ch="<<ch<<" wet_dB="<<gain
                     <<" normalized_max_ratio="<<edge<<std::endl;
            need(ea>1.&&eb>1.&&std::abs(gain)<.5&&edge<1.15,"H20 PCM regression");
        }
        const auto untraced=run(true,127,true,false);
        need(now.pcm.left==untraced.pcm.left&&now.pcm.right==untraced.pcm.right,
             "H19 trace affected PCM");
        need(untraced.trace.values==Engine::RendererTrace{}.values&&
             now.trace.values==split.trace.values,"H19 trace lifecycle/block parity");
        using T=Engine::RendererTrace;
        const auto& q=now.trace.values;
        need(q[T::calls]>0&&q[T::ready]>0&&q[T::safeUnready]>0&&
             q[T::drained]<=q[T::causalBlocked]&&q[T::partial]<=q[T::ready]&&
             q[T::calls]==q[T::notCarrying]+q[T::causalBlocked]+q[T::safeUnready]+q[T::ready]-q[T::drained],
             "H19 trace accounting");
        std::cout<<"H19_TRACE rate="<<rate<<" calls="<<q[T::calls]
                 <<" no_clock="<<q[T::notCarrying]<<" causal_blocked="<<q[T::causalBlocked]
                 <<" drained="<<q[T::drained]<<" safe_unready="<<q[T::safeUnready]
                 <<" ready="<<q[T::ready]<<" partial="<<q[T::partial]
                 <<" weak_confidence="<<q[T::weakConfidence]<<std::endl;
        need(now.pcm.left==split.pcm.left&&now.pcm.right==split.pcm.right&&
             now.losses==split.losses&&now.acquisitions==split.acquisitions,"H18 block parity");
        need(dry.pcm.left==oldDry.pcm.left&&dry.pcm.right==oldDry.pcm.right,"H18 dry changed");
        std::cout<<"H18_ENGINE rate="<<rate<<" old_losses="<<old.losses
                 <<" new_losses="<<now.losses<<" old_acquisitions="<<old.acquisitions
                 <<" new_acquisitions="<<now.acquisitions<<std::endl;
        need(old.losses>0&&old.acquisitions>5&&now.losses>0&&now.acquisitions>5,
             "H18 engine fixture did not exercise renderer loss and recovery");
        for(int ch:{0,1}){
            const auto& a=ch?old.pcm.right:old.pcm.left;const auto& b=ch?now.pcm.right:now.pcm.left;
            const auto& d=ch?dry.pcm.right:dry.pcm.left;
            double ea=0.,eb=0.,ma=0.,mb=0.;
            for(int i=rate/2;i<n;++i){
                const double x=a[i]-d[i],y=b[i]-d[i];
                need(std::isfinite(y),"H18 nonfinite backing");ea+=x*x;eb+=y*y;
                ma=std::max(ma,std::abs(x-(a[i-1]-d[i-1])));
                mb=std::max(mb,std::abs(y-(b[i-1]-d[i-1])));
            }
            const double gain=10.*std::log10(eb/ea),edge=mb/ma*std::sqrt(ea/eb);
            std::cout<<"H18_PCM rate="<<rate<<" ch="<<ch<<" wet_dB="<<gain
                     <<" normalized_max_ratio="<<edge<<std::endl;
            need(ea>1.&&eb>1.&&std::abs(gain)<1.&&edge<1.25,"H18 backing regression");
        }
    }
}
static void testSlopeAwareSeam(){
    constexpr double tau=6.283185307179586;
    for(int rate:{32000,48000,96000,192000}){
      double oldError=0.,newError=0.,worstPeak=0.;int cases=0;
      for(int hz:{220,440,880})for(int phase=0;phase<8;++phase){
        BackingRenderSeam old{},now{};old.prepare(rate);now.prepare(rate);
        float prev=0.f,last=0.f;
        for(int i=0;i<rate/20;++i){
          const float x=.2f*static_cast<float>(std::sin(tau*hz*i/rate+tau*phase/8));
          prev=last;last=old.process(0.f,x,true,1.f,false);
          need(last==now.process(0.f,x,true,1.f,true)||i<rate/100,
               "H17 settled PCM changed");
        }
        const float a=old.process(0.f,0.f,false,1.f,false);
        const float b=now.process(0.f,0.f,false,1.f,true);
        oldError+=std::abs(a-2.f*last+prev);
        newError+=std::abs(b-2.f*last+prev);
        worstPeak=std::max(worstPeak,double(std::abs(b)));
        for(int i=0;i<rate/100;++i){
          const float y=now.process(0.f,0.f,false,1.f,true);
          need(std::isfinite(y)&&std::abs(y)<.41f,"H17 tangent overshoot");
          worstPeak=std::max(worstPeak,double(std::abs(y)));
        }
        need(now.process(.1f,0.f,false,1.f,true)==.1f,"H17 fallback endpoint");
        for(int i=0;i<rate/100;++i)now.process(.1f,-.2f,true,1.f,true);
        need(std::abs(now.process(.1f,-.2f,true,1.f,true)+.2f)<1.e-6f,
             "H17 reacquisition endpoint");
        const auto c=now.countsForOfflineTests();
        need(c.losses==1&&c.acquisitions==2,"H17 event count");
        now.reset();
        need(now.process(.1f,0.f,false,1.f,true)==.1f&&
             now.countsForOfflineTests().losses==1,"H17 phrase reset");
        ++cases;
      }
      BackingRenderSeam chatter;chatter.prepare(rate);
      for(int i=0;i<rate/4;++i){
        const float c=.25f*static_cast<float>(std::sin(tau*220*i/rate));
        const bool ready=i%(rate/200)<rate/300;
        const float y=chatter.process(c,-c,ready,1.f,true);
        need(std::isfinite(y)&&std::abs(y)<1.f,"H17 chatter bound");
      }
      std::cout<<"H17_SEAM rate="<<rate<<" cases="<<cases
               <<" old_slope_error="<<oldError<<" new_slope_error="<<newError
               <<" peak="<<worstPeak<<std::endl;
      need(oldError>.001&&newError<oldError*.10,"H17 boundary slope not improved");
    }
}
static void testBackingContinuity(){
  constexpr double tau=6.283185307179586;
  // Isolate readiness loss/re-entry without the detector obscuring it.
  for(int rate:{32000,48000,96000,192000}){
    BackingRenderSeam seam;seam.prepare(rate);
    for(int i=0;i<rate/50;++i)seam.process(.15f,.45f,true,1.f);
    const float last=seam.process(.15f,.45f,true,1.f);
    need(std::abs(seam.process(.16f,-.7f,false,1.f)-last)<.011f,"H13 loss step");
    float previous=seam.process(.15f,-.7f,false,1.f);
    for(int i=0;i<rate/100;++i){
      const float now=seam.process(.15f,-.7f,false,1.f);
      need(std::abs(now-previous)<.015f,"H13 loss bridge discontinuity");
      previous=now;
    }
    need(std::abs(previous-.15f)<1.e-6f,"H13 carrier did not recover");
    for(int i=0;i<rate/100;++i){
      const float now=seam.process(.15f,-.45f,true,1.f);
      need(std::abs(now-previous)<.016f,"H13 reacquire bridge discontinuity");
      previous=now;
    }
    need(std::abs(previous+.45f)<1.e-6f,"H13 candidate did not recover");
  }
  // Strong shifting harmonics, long high phrase, vibrato and brief
  // unvoiced segments, with actual Music-guide key/chord transitions.
  for(int rate:{48000,96000})for(int voices:{3,5}){
    const int n=rate*4;
    LeadFixture src{std::vector<float>(n),std::vector<float>(n)};
    double phase=0.;unsigned rng=17;
    for(int i=0;i<n;++i){
      const double time=double(i)/rate;
      const double hz=time<1.4?329.6276:time<2.7?659.2551:440.;
      phase+=tau*hz*std::exp2(65.*std::sin(tau*5.4*time)/1200.)/rate;
      const double vowel=.35+.25*std::sin(tau*1.7*time);
      rng=1664525u*rng+1013904223u;
      const double noise=double(rng>>8)/16777216.-.5;
      const bool gap=i%(rate/3)<rate/100;
      const double x=.12*std::sin(phase)+vowel*.16*std::sin(2*phase+.3)
         +.065*std::sin(3*phase+1.1)+.026*std::sin(5*phase+.6);
      src.left[i]=src.right[i]=static_cast<float>(gap?.08*noise:x);
    }
    auto run=[&](bool modern,bool wet,int block,bool support=true,bool natural=false,bool carrier=false,bool slope=false,bool drain=false){
      auto e=std::make_unique<Engine>();Settings s;
      s.voices=voices;s.wet=wet?1.f:0.f;s.color=.78f;s.space=1.f;
      s.guideMode=1;s.guideChannelId=2;
      need(e->prepare(rate)&&e->setContinuityForOfflineReference(modern)&&
           e->setSupportForOfflineReference(support)&&e->setNaturalForOfflineReference(natural)&&
           e->setCarrierForOfflineReference(carrier)&&e->setSlopeForOfflineReference(slope)&&
           e->setDrainForOfflineReference(drain)&&e->setEpochRecoveryForOfflineReference(false)&&e->setSettings(s),"H13 setup");
      auto out=src;
      for(int at=0;at<n;){
        // Split at control events; callback partition must not move them.
        if(at==rate*2)e->setGuideChord(5,0);
        if(at==rate*3)e->setGuideChord(0,0);
        if(at==0)e->setGuideChord(0,0);
        const int boundary=at<2*rate?2*rate:at<3*rate?3*rate:n;
        const int count=std::min(block,boundary-at);
        e->process(out.left.data()+at,out.right.data()+at,count);at+=count;
      }
      if(slope&&wet&&block==127){
        std::uint64_t losses=0,acquisitions=0;
        for(int v=0;v<Engine::maxVoices;++v){
          const auto c=e->seamCountsForOfflineTests(v);
          losses+=c.losses;acquisitions+=c.acquisitions;
        }
        std::cout<<"H17_EVENTS rate="<<rate<<" voices="<<voices
                 <<" losses="<<losses<<" acquisitions="<<acquisitions<<std::endl;
      }
      return out;
    };
    const auto old=run(false,true,127),now=run(true,true,127);
    const auto h13=run(true,true,127,false);
    const auto dry=run(true,false,127),split=run(true,true,64);
    need(now.left==split.left&&now.right==split.right,"H13 block parity");
    const auto h15=run(true,true,127,true,true),h15Split=run(true,true,64,true,true);
    need(h15.left==h15Split.left&&h15.right==h15Split.right,"H15 block parity");
    const auto dry15=run(true,false,127,true,true);
    need(dry.left==dry15.left&&dry.right==dry15.right,"H15 dry changed");
    const auto h16=run(true,true,127,true,true,true);
    const auto h16Split=run(true,true,64,true,true,true);
    need(h16.left==h16Split.left&&h16.right==h16Split.right,"H16 block parity");
    const auto dry16=run(true,false,127,true,true,true);
    need(dry16.left==dry.left&&dry16.right==dry.right,"H16 dry changed");
    const auto h17=run(true,true,127,true,true,true,true);
    const auto h17Split=run(true,true,64,true,true,true,true);
    need(h17.left==h17Split.left&&h17.right==h17Split.right,"H17 block parity");
    const auto dry17=run(true,false,127,true,true,true,true);
    need(dry17.left==dry.left&&dry17.right==dry.right,"H17 dry changed");
    for(int ch:{0,1}){
      const auto& a=ch?h16.right:h16.left;const auto& b=ch?h17.right:h17.left;
      const auto& d=ch?dry.right:dry.left;
      double ea=0.,eb=0.,ma=0.,mb=0.;
      std::vector<double> sa,sb;
      for(int i=rate/2;i<n;++i){
        const double x=a[i]-d[i],y=b[i]-d[i];
        need(std::isfinite(y),"H17 nonfinite backing");ea+=x*x;eb+=y*y;
        const double da=std::abs(x-(a[i-1]-d[i-1])),db=std::abs(y-(b[i-1]-d[i-1]));
        ma=std::max(ma,da);mb=std::max(mb,db);sa.push_back(da);sb.push_back(db);
      }
      const auto k=static_cast<std::size_t>(.999*(sa.size()-1));
      std::nth_element(sa.begin(),sa.begin()+k,sa.end());
      std::nth_element(sb.begin(),sb.begin()+k,sb.end());
      const double gain=10.*std::log10(eb/ea),norm=std::sqrt(ea/eb);
      const double maxRatio=mb/ma*norm,pRatio=sb[k]/sa[k]*norm;
      std::cout<<"H17_PCM rate="<<rate<<" voices="<<voices<<" ch="<<ch
               <<" wet_dB="<<gain<<" normalized_max_ratio="<<maxRatio
               <<" normalized_p999_ratio="<<pRatio<<std::endl;
      need(ea>1.&&eb>1.&&std::abs(gain)<.5&&maxRatio<1.15&&pRatio<1.10,
           "H17 backing continuity regression");
    }
    for(int ch:{0,1}){
      const auto& a=ch?h15.right:h15.left;const auto& b=ch?h16.right:h16.left;
      const auto& d=ch?dry.right:dry.left;
      double ea=0.,eb=0.,ma=0.,mb=0.;
      std::vector<double> sa,sb;
      for(int i=rate/2;i<n;++i){
        const double x=a[i]-d[i],y=b[i]-d[i];
        need(std::isfinite(y),"H16 nonfinite backing");ea+=x*x;eb+=y*y;
        const double da=std::abs(x-(a[i-1]-d[i-1])),db=std::abs(y-(b[i-1]-d[i-1]));
        ma=std::max(ma,da);mb=std::max(mb,db);sa.push_back(da);sb.push_back(db);
      }
      const auto k=static_cast<std::size_t>(.999*(sa.size()-1));
      std::nth_element(sa.begin(),sa.begin()+k,sa.end());
      std::nth_element(sb.begin(),sb.begin()+k,sb.end());
      const double gain=10.*std::log10(eb/ea),norm=std::sqrt(ea/eb);
      std::cout<<"H16_PCM rate="<<rate<<" voices="<<voices<<" ch="<<ch
               <<" wet_dB="<<gain<<" normalized_max_ratio="<<mb/ma*norm
               <<" normalized_p999_ratio="<<sb[k]/sa[k]*norm<<std::endl;
      need(ea>1.&&eb>1.&&std::abs(gain)<2.&&std::isfinite(mb/ma*norm),
           "H16 backing level regression");
    }
    for(int ch=0;ch<2;++ch){
      const auto& a=ch?now.right:now.left;const auto& b=ch?h15.right:h15.left;
      const auto& d=ch?dry.right:dry.left;
      double ea=0.,eb=0.,da=0.,db=0.;
      for(int i=rate/2;i<n;++i){
        const double x=a[i]-d[i],y=b[i]-d[i];
        need(std::isfinite(y),"H15 nonfinite backing");ea+=x*x;eb+=y*y;
        da=std::max(da,std::abs(x-(a[i-1]-d[i-1])));
        db=std::max(db,std::abs(y-(b[i-1]-d[i-1])));
      }
      const double gain=10.*std::log10(eb/ea),edge=db/da*std::sqrt(ea/eb);
      std::cout<<"H15_PCM rate="<<rate<<" voices="<<voices<<" ch="<<ch
               <<" wet_dB="<<gain<<" normalized_max_edge_ratio="<<edge<<std::endl;
      need(ea>1.&&eb>1.&&std::abs(gain)<2.&&std::isfinite(edge),"H15 backing regression");
    }
    need(dry.left==run(false,false,64).left,"H13 dry changed");
    for(int ch=0;ch<2;++ch){
      const auto& a=ch?old.right:old.left;const auto& b=ch?now.right:now.left;
      const auto& lead=ch?dry.right:dry.left;
      const auto& ref=ch?h13.right:h13.left;
      double ea=0.,eb=0.,ec=0.,maxA=0.,maxB=0.,maxC=0.;
      std::vector<double> da,db,dc;
      for(int i=rate/2;i<n;++i){
        const double x=a[i]-lead[i],y=b[i]-lead[i];
        const double u=std::abs(x-(a[i-1]-lead[i-1]));
        const double v=std::abs(y-(b[i-1]-lead[i-1]));
        const double z=ref[i]-lead[i];
        const double w=std::abs(z-(ref[i-1]-lead[i-1]));
        need(std::isfinite(y),"H13 nonfinite");
        ea+=x*x;eb+=y*y;ec+=z*z;
        maxA=std::max(maxA,u);maxB=std::max(maxB,v);maxC=std::max(maxC,w);
        da.push_back(u);db.push_back(v);dc.push_back(w);
      }
      std::sort(da.begin(),da.end());std::sort(db.begin(),db.end());std::sort(dc.begin(),dc.end());
      const double pa=da[da.size()*999/1000],pb=db[db.size()*999/1000];
      const double pc=dc[dc.size()*999/1000];
      const double relative=std::sqrt(ec/eb);
      const double normalizedP=pb*relative/pc,normalizedMax=maxB*relative/maxC;
      const double h14Gain=10.*std::log10(eb/ec);
      std::cout<<"H14_PCM rate="<<rate<<" voices="<<voices<<" ch="<<ch
               <<" h13_max="<<maxC<<" h14_max="<<maxB
               <<" h13_p999="<<pc<<" h14_p999="<<pb
               <<" level_matched_p999_ratio="<<normalizedP
               <<" level_matched_max_ratio="<<normalizedMax
               <<" wet_dB="<<h14Gain<<std::endl;
      need(ec>1.&&std::abs(h14Gain)<.5&&normalizedP<1.10&&normalizedMax<1.15,
           "H14 level-matched stereo continuity regression");
      const double gain=10.*std::log10(eb/ea);
      std::cout<<"H13_PCM rate="<<rate<<" voices="<<voices<<" ch="<<ch
               <<" old_max="<<maxA<<" new_max="<<maxB
               <<" old_p999="<<pa<<" new_p999="<<pb<<" wet_dB="<<gain<<std::endl;
      // Edge metrics are proxies, not a click classifier: retain energy
      // and prevent large regression while recording both stereo tails.
      need(ea>1.&&eb>1.&&std::abs(gain)<2.&&maxB<.30&&pb<pa*1.15,
           "H13 stereo continuity or level regression");
    }
  }
}
// H24A matched headroom audit: legacy reference and protected live path.
// A peak sweep at unity output
// gain, full wet, two Space settings and five modes. Float samples above
// unity are headroom violations at a fixed-point sink, not proof of clipping
// inside the float engine. Do not gate old behavior on the investigation.
static void auditFieldHeadroom(){
    constexpr int rate=48000,n=rate*2;
    constexpr double tau=6.283185307179586;
    LeadFixture src{std::vector<float>(n),std::vector<float>(n)};
    double phase=0.,sourcePeak=0.;
    for(int i=0;i<n;++i){
        const double t=double(i)/rate;
        const double hz=(t<.55?220.:t<1.05?440.:t<1.55?880.:220.)*
            std::exp2(45.*std::sin(tau*5.1*t)/1200.);
        phase+=tau*hz/rate;
        const double env=std::min({1.,t/.025,(2.-t)/.025})*
            (.78+.22*std::sin(tau*2.3*t));
        const float v=static_cast<float>(env*(.69*std::sin(phase)+
            .19*std::sin(2*phase+.3)+.08*std::sin(3*phase+.7)+.04*std::sin(5*phase+.2)));
        src.left[i]=src.right[i]=v;sourcePeak=std::max(sourcePeak,double(std::abs(v)));
    }
    for(bool protectedPath:{false,true})for(float inputPeak:{.25f,.5f,.8f})for(float space:{0.f,1.f})for(int mode=1;mode<=5;++mode){
        Settings s;s.voiceMode=mode==1?1:0;s.voices=std::max(2,mode);
        s.wet=1.f;s.space=space;s.color=.78f;s.gainDb=0.f;s.correction=false;
        auto x=src;
        for(int i=0;i<n;++i)x.left[i]=x.right[i]=static_cast<float>(src.left[i]*inputPeak/sourcePeak);
        auto e=std::make_unique<Engine>();
        need(e->prepare(rate)&&e->setDoublerMonoForOfflineReference(false)&&e->setHeadroomForOfflineReference(protectedPath)&&e->setSettings(s)&&e->setRendererTraceForOfflineTests(true),"field audit setup");
        for(int at=0;at<n;at+=64)e->process(x.left.data()+at,x.right.data()+at,std::min(64,n-at));
        double peakL=0.,peakR=0.;int aboveL=0,aboveR=0,internalClamp=0;std::uint64_t calls=0,losses=0;
        for(int i=0;i<n;++i){
            need(std::isfinite(x.left[i])&&std::isfinite(x.right[i]),"field audit nonfinite");
            peakL=std::max(peakL,double(std::abs(x.left[i])));
            peakR=std::max(peakR,double(std::abs(x.right[i])));
            aboveL+=std::abs(x.left[i])>1.f;aboveR+=std::abs(x.right[i])>1.f;
            internalClamp+=(std::abs(x.left[i])>=4.f)+(std::abs(x.right[i])>=4.f);
        }
        for(int v=0;v<Engine::maxVoices;++v){
            calls+=e->rendererTraceForOfflineTests(v).values[Engine::RendererTrace::calls];
            losses+=e->seamCountsForOfflineTests(v).losses;
        }
        if(mode==1)need(calls==0,"stable Doubler must not call pitch renderer");
        if(protectedPath){
            need(peakL<=.980001&&peakR<=.980001&&aboveL==0&&aboveR==0,"H24A sample ceiling");
            if(inputPeak==.25f)need(e->headroomForOfflineTests().limited()==0,"H24A low-level transparency");
        }
        std::cout<<"FIELD_HEADROOM protected="<<protectedPath<<" mode="<<mode<<" input_peak="<<inputPeak<<" space="<<space
                 <<" peakL="<<peakL<<" peakR="<<peakR<<" aboveL="<<aboveL<<" aboveR="<<aboveR
                 <<" internalClamp="<<internalClamp<<" shifted_calls="<<calls
                 <<" pitch_analyses="<<e->completedPitchAnalyses()<<" losses="<<losses
                 <<" limited="<<e->headroomForOfflineTests().limited()<<" emergency="<<e->headroomForOfflineTests().emergency()
                 <<" min_gain="<<e->headroomForOfflineTests().minimum()<<std::endl;
    }
}
static void testSourceAlignedCandidate(){
    SourcePitchTimeline timeline;
    need(SourcePitchTimeline::sourceCenter(1000,4,41.f)==600.,"H24C timestamp");
    need(timeline.nearest(100,20).period==0.f,"H24C empty");
    for(int i=0;i<20;++i)
        need(timeline.publish({double(i*100),float(100+i),.94f}),"H24C ring publish");
    need(timeline.nearest(450,100).period==104.f&&
         timeline.nearest(1890,100).period==119.f&&
         timeline.nearest(100,100).period==0.f&&
         timeline.nearest(2200,100).period==0.f,"H24C wrapped nearest/stale");
    need(!timeline.publish({1800.,100.f,.95f})&&!timeline.publish({2000.,100.f,.7f}),
         "H24C rejected nonmonotonic/weak evidence");
    timeline.reset();need(timeline.nearest(1900,100).period==0.f,"H24C reset");
    timeline.observe({100.,100.f,.95f},150.);
    timeline.observe({200.,100.f,.95f},150.);
    need(timeline.nearest(100,100).period==0.f,"H24C2 premature initial evidence");
    timeline.observe({300.,100.f,.95f},150.);
    need(timeline.nearest(100,10).period==100.f,"H24C2 backdated initial evidence");
    timeline.observe({400.,200.f,.95f},150.);
    timeline.observe({500.,200.f,.95f},150.);
    timeline.interrupt();
    timeline.observe({600.,100.f,.95f},150.);
    need(timeline.nearest(400,20).period==0.f,"H24C2 rejected isolated octave");
    timeline.observe({700.,200.f,.95f},150.);
    timeline.observe({800.,201.f,.95f},150.);
    timeline.observe({900.,199.f,.95f},150.);
    need(timeline.nearest(700,10).period==200.f&&timeline.nearest(800,10).period==201.f,
         "H24C2 confirmed source transition");
    constexpr double tau=6.283185307179586;
    for(int rate:{48000,96000})for(int voices:{3,5}){
        const int n=rate*3;LeadFixture src{std::vector<float>(n),std::vector<float>(n)};
        double phase=0.;
        for(int i=0;i<n;++i){
            const double sec=double(i)/rate;
            const double hz=(sec<.6?220.:sec<1.2?440.:sec<1.8?880.:sec<2.4?110.:220.)*
                std::exp2(45.*std::sin(tau*5.1*sec)/1200.);
            phase+=tau*hz/rate;
            src.left[i]=src.right[i]=static_cast<float>(.13*std::sin(phase)+
                .045*std::sin(2*phase+.3)+.02*std::sin(3*phase+.7));
        }
        struct Result{LeadFixture pcm;std::uint64_t losses=0,high=0,used=0;};
        auto render=[&](int aligned,float wet,int block){
            auto e=std::make_unique<Engine>();Settings s;s.voices=voices;s.wet=wet;s.space=1.f;s.color=.78f;
            need(e->prepare(rate)&&e->setSourceAlignedForOfflineReference(aligned!=0)&&
                e->setSourceConfirmationForOfflineReference(aligned==2)&&e->setSettings(s)&&e->setRendererTraceForOfflineTests(true),"H24C setup");
            Result x{src};
            for(int at=0;at<n;at+=block)e->process(x.pcm.left.data()+at,x.pcm.right.data()+at,std::min(block,n-at));
            for(int v=0;v<Engine::maxVoices;++v)x.losses+=e->seamCountsForOfflineTests(v).losses;
            x.high=e->seamCountsForOfflineTests(2).losses;x.used=e->alignedCallsForOfflineTests(2);
            return x;
        };
        const auto old=render(0,1.f,127),now=render(1,1.f,127),split=render(1,1.f,64);
        const auto confirmed=render(2,1.f,127),confirmedSplit=render(2,1.f,64);
        const auto dry=render(0,0.f,64),newDry=render(1,0.f,127),confirmedDry=render(2,0.f,127);
        need(confirmed.pcm.left==confirmedSplit.pcm.left&&confirmed.pcm.right==confirmedSplit.pcm.right&&
             confirmed.used==confirmedSplit.used&&confirmed.used>0&&confirmed.losses==confirmedSplit.losses&&
             dry.pcm.left==confirmedDry.pcm.left&&dry.pcm.right==confirmedDry.pcm.right,"H24C2 block/dry parity");
        need(now.pcm.left==split.pcm.left&&now.pcm.right==split.pcm.right&&
             now.losses==split.losses&&now.used==split.used&&now.used>0&&
             dry.pcm.left==newDry.pcm.left&&dry.pcm.right==newDry.pcm.right,"H24C block/dry/use contract");
        for(int variant:{1,2}){
        const auto& candidate=variant==1?now:confirmed;
        double ea=0.,eb=0.,ma=0.,mb=0.,prevA=0.,prevB=0.;
        for(int i=rate/2;i<n;++i){
            const double a=old.pcm.right[i]-dry.pcm.right[i],b=candidate.pcm.right[i]-dry.pcm.right[i];
            need(std::isfinite(b),"H24C finite");
            ea+=a*a;eb+=b*b;if(i>rate/2){ma=std::max(ma,std::abs(a-prevA));mb=std::max(mb,std::abs(b-prevB));}
            prevA=a;prevB=b;
        }
        need(ea>1.&&eb>1.,"H24C audible backing");
        std::cout<<"H24C_ALIGNED variant="<<variant<<" rate="<<rate<<" voices="<<voices<<" old_loss="<<old.losses
                 <<" new_loss="<<candidate.losses<<" old_high="<<old.high<<" new_high="<<candidate.high
                 <<" high_used="<<candidate.used<<" wet_dB="<<10.*std::log10(eb/ea)
                 <<" normalized_edge_ratio="<<mb/ma*std::sqrt(ea/eb)<<std::endl;
        }
    }
}
static void testHeadroomContract(){
    for(int rate:{32000,48000,96000,192000}){
        BackingHeadroom h;h.prepare(rate);
        for(int i=0;i<rate/10;++i){
            const float g=h.process(.2f,-.2f,.2f,-.2f,.3f,-.3f,1.f,1.f);
            need(g==1.f,"H24A nominal gain altered");
        }
        // Asymmetric polarity, signed constraint and stereo-linked gain.
        for(int i=0;i<rate/10;++i){
            const float g=h.process(.8f,.4f,.8f,-.4f,.9f,-1.2f,1.f,1.f);
            need(g>=0.f&&g<=1.f&&std::abs(.8f+.9f*g)<=.980001f&&
                std::abs(-.4f-1.2f*g)<=.980001f,"H24A linked safety");
        }
        const float g=h.process(1.2f,-1.1f,1.2f,-1.1f,.5f,-.6f,1.f,1.f);
        need(g==0.f,"H24A preserve over-unity dry, do not worsen");
        const float recovering=h.process(.1f,.1f,.1f,.1f,.1f,.1f,1.f,1.f);
        need(recovering>=0.f&&recovering<.01f,"H24A no sudden release");
    }
}
static void testDoublerMonoCompatibility(){
    constexpr double tau=6.283185307179586;
    for(int rate:{48000,96000})for(double hz:{100.,300.}){
        const int n=rate*3;LeadFixture src{std::vector<float>(n),std::vector<float>(n)};
        for(int i=0;i<n;++i)src.left[i]=src.right[i]=static_cast<float>(.12*std::sin(tau*hz*i/rate));
        auto render=[&](bool guard,float space,float wet,int block){
            auto e=std::make_unique<Engine>();Settings s;s.voiceMode=1;s.voices=2;
            s.wet=wet;s.space=space;s.color=.78f;
            need(e->prepare(rate)&&e->setSettings(s)&&e->setDoublerMonoForOfflineReference(guard),"H24B setup");
            auto out=src;
            for(int at=0;at<n;at+=block)e->process(out.left.data()+at,out.right.data()+at,std::min(block,n-at));
            return out;
        };
        const auto old=render(false,0.f,1.f,64),now=render(true,0.f,1.f,127),
                   split=render(true,0.f,1.f,64),dry=render(true,0.f,0.f,64);
        need(now.left==split.left&&now.right==split.right,"H24B block parity");
        const auto wideOld=render(false,1.f,1.f,64),wide=render(true,1.f,1.f,127);
        need(wideOld.left==wide.left&&wideOld.right==wide.right,"H24B wide exact parity");
        double minOld=1.e9,minNew=1.e9,wetOld=0.,wetNew=0.;
        for(int at=rate;at+rate/50<=n;at+=rate/50){
            double a=0.,b=0.,base=0.;
            for(int i=at;i<at+rate/50;++i){
                a+=old.left[i]*old.left[i];b+=now.left[i]*now.left[i];
                base+=dry.left[i]*dry.left[i];
                const double x=old.left[i]-dry.left[i],y=now.left[i]-dry.left[i];
                wetOld+=x*x;wetNew+=y*y;
            }
            minOld=std::min(minOld,a/base);minNew=std::min(minNew,b/base);
        }
        std::cout<<"H24B_MONO rate="<<rate<<" hz="<<hz<<" old_floor="<<10.*std::log10(minOld)
                 <<" new_floor="<<10.*std::log10(minNew)<<" wet_ratio="<<wetNew/wetOld<<std::endl;
        need(minNew>minOld&&wetNew>.10*wetOld,"H24B mono floor or audible backing");
    }
}
static void testContinuousDoubler(){
  constexpr double tau=6.283185307179586;
  for(int rate:{48000,96000})for(double hz:{220.,880.,1320.}){
    const int n=rate*4;
    LeadFixture src{std::vector<float>(n),std::vector<float>(n)};
    double phase=0.;
    for(int i=0;i<n;++i){
      phase+=tau*hz*std::exp2(65.*std::sin(tau*5.1*i/rate)/1200.)/rate;
      src.left[i]=src.right[i]=static_cast<float>(
          .12*std::sin(phase)+.035*std::sin(2*phase+.2));
    }
    const auto run=[&](float wet,int block){
      auto e=std::make_unique<Engine>();Settings s;
      s.voiceMode=1;s.voices=2;s.wet=wet;s.color=1.f;s.space=.62f;
      need(e->prepare(rate)&&e->setSettings(s),"H12 double prepare");
      auto out=src;
      for(int at=0;at<n;at+=block)e->process(out.left.data()+at,out.right.data()+at,std::min(block,n-at));
      return out;
    };
    const auto out=run(1.f,127),dry=run(0.f,127),split=run(1.f,64);
    need(out.left==split.left&&out.right==split.right,"H12 double block parity");
    double minEnergy=1.e9,maxEnergy=0.,sum=0.;
    int windows=0;
    for(int at=rate;at+rate/100<=n;at+=rate/100){
      double energy=0.;
      for(int i=at;i<at+rate/100;++i){
        const double l=out.left[i]-dry.left[i],r=out.right[i]-dry.right[i];
        need(std::isfinite(l)&&std::isfinite(r),"H12 double nonfinite");
        energy+=l*l+r*r;
      }
      minEnergy=std::min(minEnergy,energy);maxEnergy=std::max(maxEnergy,energy);
      sum+=energy;++windows;
    }
    const double floor=10.*std::log10(minEnergy/(sum/windows));
    std::cout<<"H12_DOUBLER rate="<<rate<<" hz="<<hz<<" min_window_dB="<<floor
             <<" span_dB="<<10.*std::log10(maxEnergy/minEnergy)<<std::endl;
    need(floor>-9.&&sum>1.,"H12 doubler sustained backing dropout");
  }
}
static void testUnisonDoubler(){
  for(int rate:{48000,96000}){
    Settings unison;unison.voiceMode=1;unison.voices=2;
    unison.wet=.75f;unison.space=.7f;unison.correction=false;
    Engine::PreparedSettings prep{};
    need(Engine::prepareSettings(unison,prep),"Doubler settings rejected");
    need(prep.mixL[0]>0.f&&prep.mixR[0]>0.f,
         "Doubler has no wet unison companion");
    for(int i=1;i<Engine::maxVoices;++i)
      need(prep.mixL[i]==0.f&&prep.mixR[i]==0.f,
           "Doubler unexpectedly generated pitched harmony singers");
    Settings duo=unison;duo.voiceMode=0;
    need(Engine::prepareSettings(duo,prep),"Duo settings rejected");
    need(prep.mixL[0]>0.f&&prep.mixL[2]>0.f,
         "Duo must retain two pitch-harmony voices");
    const auto render=[&](const Settings& set){
      auto dsp=std::make_unique<Engine>();
      need(dsp->prepare(rate)&&dsp->setSettings(set),
           "Doubler/duo DSP prepare failed");
      std::vector<float> l(rate/2),r(rate/2);
      for(int i=0;i<rate/2;++i){
        const double phase=6.283185307179586*220.*i/rate;
        l[i]=r[i]=static_cast<float>(
          .16*std::sin(phase)+.041*std::sin(2*phase+.2));
      }
      for(int i=0;i<rate/2;i+=64)
        dsp->process(l.data()+i,r.data()+i,std::min(64,rate/2-i));
      return l;
    };
    const auto a=render(unison);
    Settings alternate=unison;alternate.key=9;alternate.scale=1;
    need(a==render(alternate),
         "Unison Doubler pitch changed with Harmony key/scale");
    const auto pitched=render(duo);
    double wetEnergy=0.,modeDifference=0.;
    for(int i=rate/4;i<rate/2;++i){
      const float x=a[static_cast<std::size_t>(i)];
      const float y=pitched[static_cast<std::size_t>(i)];
      need(std::isfinite(x)&&std::isfinite(y),"Doubler non-finite PCM");
      const int t=i-static_cast<int>(std::lround(.024*rate));
      const double phase=6.283185307179586*220.*t/rate;
      const float delayedDry=static_cast<float>(
        .16*std::sin(phase)+.041*std::sin(2*phase+.2));
      wetEnergy+=std::abs(x-delayedDry);
      modeDifference+=std::abs(x-y);
    }
    need(wetEnergy>rate*.0004,"Unison Doubler wet path is silent");
    need(modeDifference>rate*.0004,
         "Doubler accidentally equals pitched Duo PCM");
  }
}
// Octave harmonics can become louder than the true F0 in a busy chorus.
// Two noisy analyses must not reset five active backing PSOLA windows;
// a genuine sustained octave jump must still converge in bounded time.
static void testPeriodContinuityOnLoudOctaveErrors(){
    PeriodContinuity tracker;
    need(tracker.current()==0.f,"new tracker not silent");
    need(tracker.observe(std::nanf(""))==0.f,
         "invalid octave period acquired");
    const float established=tracker.observe(218.f);
    need(established==218.f,"initial F0 period not acquired");
    for(int i=0;i<2;++i){
        need(tracker.observe(109.f)==established,
             "one/two spurious octave hops retuned singer");
    }
    need(!tracker.transitioning(),
         "short octave error entered period-transition state");
    need(std::abs(tracker.observe(218.5f)-established)<1.f,
         "return to original vowel failed");
    float previous=tracker.current();
    for(int hop=0;hop<18;++hop){
        const float next=tracker.observe(109.f);
        need(std::isfinite(next)&&next>8.f&&next<=previous*1.001f,
             "accepted octave caused nonmonotone/unbounded source period");
        need(previous-next<=previous*.111f,
             "source-period leap would abruptly retime PSOLA grains");
        previous=next;
    }
    need(std::abs(tracker.current()-109.f)<7.f,
         "real sustained octave did not converge");
    tracker.reset();
    need(tracker.current()==0.f&&!tracker.transitioning(),
         "new singer phrase inherited stale source period");
}

// A loss of F0 must NOT erase the still-audible PSOLA source period.
// That used to switch a decaying wet singer to a phase-unrelated granular
// renderer halfway through its envelope on loud consonants / long vibrato.
// Full silence must eventually retire grains; no endless pitched gate.
static void testUnvoicedGapPreservesWetTail(){
  // The exact same DSP must retain wet-tail epochs at both supported
  // singing rates. Callback sizes are deliberately mismatched (64/257).
  for(const int rate:{48000,96000}){
  const int first=rate*35/100,gap=rate*55/1000;
  const int reprise=rate*25/100,ending=rate*25/100;
  const int total=first+gap+reprise+ending;
  constexpr double tau=6.283185307179586;
  std::vector<float> inputL(total),inputR(total);
  double phase=0.;
  for(int i=0;i<total;++i){
    const double t=static_cast<double>(i)/rate;
    phase+=tau*880.*std::exp2(65.*std::sin(tau*5.15*t)/1200.)/rate;
    const bool active=i<first||(i>=first+gap&&i<first+gap+reprise);
    const int activePosition=i<first?i:i-(first+gap);
    const int duration=i<first?first:reprise;
    const double fade=std::clamp(activePosition/(rate*.004),0.,1.)*
                      std::clamp((duration-activePosition)/(rate*.004),0.,1.);
    const float amp=active?static_cast<float>(.16*fade):0.f;
    inputL[i]=amp*static_cast<float>(std::sin(phase)+
                                       .27*std::sin(2.*phase+.17));
    inputR[i]=amp*static_cast<float>(.82*std::sin(phase+.19)+
                                       .20*std::sin(3.*phase+.46));
  }
  struct Result{
    std::vector<float> l,r;
    float gapPeriod=0.f,gapGate=0.f,endPeriod=-1.f,endGate=-1.f;
    bool gapUnvoiced=false,revoice=false,finalSilent=false;
  };
  const auto render=[&](int block){
    auto e=std::make_unique<Engine>();
    need(e->prepare(rate)&&
         e->latencySamples()==std::lround(.024*rate),
         "wet-tail engine PDC changed");
    Settings wet;wet.voices=5;wet.wet=.74f;wet.space=.82f;
    need(e->setSettings(wet),"wet-tail test settings invalid");
    Result out;out.l=inputL;out.r=inputR;
    bool sampled=false;
    for(int pos=0;pos<total;pos+=block){
      const int count=std::min(block,total-pos);
      e->process(out.l.data()+pos,out.r.data()+pos,count);
      // Observe the gap after its source has become genuinely silent.
      if(!sampled&&pos+count>=first+gap-64){
        out.gapPeriod=e->sourcePeriodForOfflineTests();
        out.gapGate=e->wetGateForOfflineTests();
        out.gapUnvoiced=!e->voiced();sampled=true;
      }
      if(pos+count>=first+gap+reprise-64&&
         pos+count<first+gap+reprise+block){
        out.revoice=e->voiced()&&
            e->sourcePeriodForOfflineTests()>8.f;
      }
    }
    out.endPeriod=e->sourcePeriodForOfflineTests();
    out.endGate=e->wetGateForOfflineTests();
    out.finalSilent=!e->voiced();
    return out;
  };
  const auto fine=render(64),coarse=render(257);
  need(fine.l==coarse.l&&fine.r==coarse.r,
       "wet-tail recovery depends on ASIO callback partition");
  for(const auto* scene:{&fine,&coarse}){
    need(scene->gapUnvoiced&&scene->gapPeriod>30.f&&
         scene->gapGate>.002f,
         "pitch loss erased PSOLA period while wet tail still audible");
    need(scene->revoice,"returning soprano note failed to reacquire");
    need(scene->finalSilent&&scene->endPeriod==0.f&&
         scene->endGate<.001f,
         "long silence retained stale PSOLA source or voiced gate");
  }
  for(int i=1;i<total;++i){
    const auto a=static_cast<std::size_t>(i);
    need(std::isfinite(fine.l[a])&&std::isfinite(fine.r[a]),
         "wet-tail high-register output nonfinite");
    need(std::abs(fine.l[a]-fine.l[a-1])<.45f&&
         std::abs(fine.r[a]-fine.r[a-1])<.45f,
         "wet-tail gap caused an unbounded micro-sample discontinuity");
  }
  } // sample-rate qualification: 48k and 96k
}

// Stress the USER failure shape: a long 5Hz/70-cent vibrato under
// chorus-level gain with short noisy consonants. Dry/Wet are checked using
// the actual native Engine; fixture is NOT an ASIO or human-likeness claim.
static void testLoudVibratoContinuity(){
  constexpr int rate=48000,frames=rate*2;
  std::vector<float> inL(frames),inR(frames);
  constexpr double tau=6.283185307179586;
  double phase=0.;
  for(int i=0;i<frames;++i){
    const double t=static_cast<double>(i)/rate;
    const double vibrato=70.*std::sin(tau*5.1*t);
    phase+=tau*220.*std::exp2(vibrato/1200.)/rate;
    const double chorus=std::clamp((t-.4)*15.,0.,1.)*
                        (1.-std::clamp((t-1.25)*15.,0.,1.));
    const double level=.12+.16*chorus;
    // Busy phrase: a 14ms noisy consonant every 210ms after warm-up.
    const double noise=(i>rate/3 && i%(rate*21/100)<rate*14/1000)?
      .07*static_cast<double>((i*977+17)%137-68)/68.:0.;
    inL[i]=static_cast<float>(
      level*(std::sin(phase)+.26*std::sin(2.*phase+.2)+
             .16*std::sin(3.*phase))+noise);
    inR[i]=static_cast<float>(
      level*(.85*std::sin(phase+.12)+.31*std::sin(3.*phase+.4))+
      noise*.85);
  }
  auto process=[&](int block,int voices,bool wet,bool independent=true){
    auto dsp=std::make_unique<Engine>();
    need(dsp->prepare(rate)&&dsp->latencySamples()==1152,
         "vibrato engine PDC changed");
    Settings settings;settings.voices=voices;settings.wet=wet?.75f:0.f;
    settings.space=.82f;
    need(dsp->setIndependentExpressionForOfflineReference(independent)&&
         dsp->setSettings(settings),"vibrato DSP settings rejected");
    auto l=inL,r=inR;
    int audibleTicks=0,checkedTicks=0;
    for(int pos=0;pos<frames;pos+=block){
      const int count=std::min(block,frames-pos);
      dsp->process(l.data()+pos,r.data()+pos,count);
      if(pos>rate/3&&pos<rate*6/5){
        ++checkedTicks;
        if(dsp->voiced())++audibleTicks;
      }
    }
    need(audibleTicks>checkedTicks*.96,
         "loud-vibrato tracking repeatedly lost voiced continuity");
    return std::pair{std::move(l),std::move(r)};
  };
  const auto five64=process(64,5,true);
  const auto five257=process(257,5,true);
  need(five64==five257,
       "heavy five-voice vibrato output depends on ASIO callback partition");
  const auto dry=process(257,5,false);
  const auto h3=process(64,5,true,false);
  need(h3!=five64,"H4 expression did not affect actual ensemble PCM");
  need(process(64,2,true,false)==process(64,2,true,true),
       "H4 changed accepted Duo");
  need(process(64,5,false,false)==dry,"H4 changed dry lead");
  double harmonyE=0.,outE=0.,monoE=0.;
  float largestStep=0.f;
  for(int i=rate/2;i<frames;++i){
    const float l=five64.first[static_cast<std::size_t>(i)];
    const float r=five64.second[static_cast<std::size_t>(i)];
    const float dl=dry.first[static_cast<std::size_t>(i)];
    const float dr=dry.second[static_cast<std::size_t>(i)];
    need(std::isfinite(l)&&std::isfinite(r)&&
         std::abs(l)<3.9f&&std::abs(r)<3.9f,
         "loud vibrato harmonic mixed PCM nonfinite or clipped");
    harmonyE+=(l-dl)*(l-dl)+(r-dr)*(r-dr);
    outE+=l*l+r*r;
    monoE+=(l+r)*(l+r)*.25;
    largestStep=std::max(largestStep,std::abs(l-
      five64.first[static_cast<std::size_t>(i-1)]));
  }
  need(harmonyE>1.&&outE>4.&&monoE>1.,
       "high-demand ensemble lost harmony or mono energy");
  need(largestStep<.65f,
       "long-vibrato ensemble produced an unbounded sample discontinuity");
}

// Forced renderer readiness dropouts, independent of the F0 detector.
// Constant residual makes the exact edge continuity directly falsifiable.
static void testLeadRendererSeam(){
  for(int rate:{32000,48000,96000,192000}){
    LeadRenderSeam seam;seam.prepare(rate);
    auto tick=[&](bool ready,float dl=.2f,float dr=-.1f){
      seam.process(dl,dr,.6f,-.4f,ready,1.f);
      return std::pair{dl,dr};
    };
    for(int i=0;i<rate/50;++i)tick(true);
    const auto before=tick(true);
    const auto missing=tick(false);
    need(before==missing,"lead readiness loss hard-switched PCM");
    auto last=missing;
    for(int i=0;i<rate/100;++i){
      auto x=tick(false);
      need(std::abs(x.first-last.first)<.02f&&
           std::abs(x.second-last.second)<.02f,"lead residual release stepped");
      last=x;
    }
    need(last==std::pair{.2f,-.1f},"lead residual never returned to exact dry");
    for(int i=0;i<rate/100;++i)
      need(tick(i%2==0)==last,"flickering renderer entered without prewarm");
    for(int i=0;i<rate/50;++i)tick(true);
    need(tick(true)==before,"stable renderer failed to recover");
    seam.reset();
    need(tick(false)==last,"renderer reset leaked correction");
    for(int i=0;i<rate/20;++i){
      const float source=.15f*std::sin(.03f*i);
      float l=source,r=source;
      const float corrected=.14f*std::sin(.031f*i);
      seam.process(l,r,corrected,corrected,i%(rate/100)<rate/125,.7f);
      need(l==r,"mono seam introduced side energy");
    }
  }
}

// Matched dry subtraction, actual Engine PCM, not a prepared scalar proxy.
// Report old and new absolute stem dB for every case before enforcing the
// provisional gate. Synthetic voiced fixture is not a human loudness test.
static void testBackingUnity(){
  bool within=true;
  for(int rate:{44100,48000,96000}){
    auto input=leadFixture(rate,220.,0.f,0.f);
    input.right=input.left; // ordinary dual-mono mic
    auto render=[&](Settings s,bool unity,int block){
      auto e=std::make_unique<Engine>();
      need(e->prepare(rate)&&e->setDoublerMonoForOfflineReference(false)&&e->setBackingUnityForOfflineReference(unity)&&
           e->setSettings(s),"unity PCM fixture prepare");
      auto out=input;
      for(int i=0;i<2*rate;i+=block)
        e->process(out.left.data()+i,out.right.data()+i,std::min(block,2*rate-i));
      return out;
    };
    Settings s;s.color=1.f;s.correction=false;s.wet=0.f;
    const auto dry=render(s,true,127);
    auto level=[&](const LeadFixture& out){
      double wet=0.,lead=0.;
      for(int i=rate;i<2*rate;++i){
        need(std::abs(out.left[i])<1.f&&std::abs(out.right[i])<1.f,
             "unity fixture clipped nominal PCM");
        const double l=out.left[i]-dry.left[i],r=out.right[i]-dry.right[i];
        wet+=l*l+r*r;lead+=dry.left[i]*dry.left[i]+dry.right[i]*dry.right[i];
      }
      return 10.*std::log10(wet/lead);
    };
    for(int mode=0;mode<=1;++mode){
      s.voiceMode=mode;
      for(int voices=2;voices<=(mode==1?2:5);++voices){
        s.voices=voices;
        for(float space:{0.f,.6f,1.f}){
          s.space=space;s.wet=1.f;
          const auto prior=render(s,false,127),now=render(s,true,127);
          const double oldDb=level(prior),db=level(now);
          std::cout<<"H1_STEM rate="<<rate<<" mode="<<mode
                   <<" voices="<<voices<<" space="<<space
                   <<" old_dB="<<oldDb<<" new_dB="<<db<<std::endl;
          within=within&&std::isfinite(db)&&std::abs(db)<=2.;
          need(db>oldDb,"unity calibration failed to lift backing");
          s.wet=.45f;
          const auto factory=render(s,true,64),factoryOld=render(s,false,257);
          need(factory.left==factoryOld.left&&factory.right==factoryOld.right,
               "ordinary Wet preset or block invariance changed");
          need(level(factory)<db,"full Wet failed to exceed factory Wet");
        }
      }
    }
  }
  need(within,"H1 full-Wet aggregate stem outside +/-2dB target; inspect H1_STEM rows");
}
// Force the old two-head fallback into half-period cancellation, then
// exercise a varying pitch ratio and verify the new continuous read speed.
static void testPhaseCoherentCarrier(){
    constexpr double tau=6.283185307179586;
    for(int rate:{32000,48000,96000,192000}){
        const int grain=static_cast<int>(std::lround(rate*.016));
        const float period=rate/125.f,base=rate*.024f,ratio=1.5f;
        PhaseCoherentCarrier carrier;
        float p=0.f;double oldMin=1.,newMin=1.;
        // Analytic sine eliminates detector errors and isolates the carrier.
        // At 125Hz and ratio 1.5, the nominal head separation is a half period.
        double oldSum=0.,newSum=0.,oldTotal=0.,newTotal=0.;int count=0;
        const int window=std::max(1,rate/500);
        for(int i=0;i<rate/2;++i){
            const float q=p<.5f?p+.5f:p-.5f,w=smoothGrainBlend(p);
            const auto h=carrier.process(i,p,ratio,base,grain,period,true,16381.f);
            auto tap=[&](float delay){return std::sin(tau*(i-delay)/period);};
            const double a=w*tap(base+(1.f-ratio)*p*grain)+(1.f-w)*tap(base+(1.f-ratio)*q*grain);
            const double b=w*tap(h.a)+(1.f-w)*tap(h.b);
            need(h.a>=1.f&&h.b>=1.f&&h.a<=16381.f&&h.b<=16381.f,"H16 history bounds");
            if(i>rate/10){
                oldSum+=a*a;newSum+=b*b;++count;
                if(count==window){
                    oldMin=std::min(oldMin,oldSum/count);newMin=std::min(newMin,newSum/count);
                    oldTotal+=oldSum;newTotal+=newSum;oldSum=newSum=0.;count=0;
                }
            }
            p+=1.f/grain;if(p>=1.f)p-=1.f;
        }
        std::cout<<"H16_CARRIER rate="<<rate<<" old_min_power="<<oldMin
                 <<" new_min_power="<<newMin<<" energy_ratio="<<newTotal/oldTotal<<std::endl;
        need(newMin>oldMin*3.&&newMin>.08&&newTotal>oldTotal,
             "H16 phase cancellation not improved");
        carrier.reset();p=0.f;float prevP=0.f;
        PhaseCoherentCarrier::Delays prev{};
        for(int i=0;i<rate/4;++i){
            const float r=1.2f+.6f*static_cast<float>(std::sin(tau*8.*i/rate));
            const auto h=carrier.process(i,p,r,base,grain,period,true,16381.f);
            if(i>0&&p>=prevP)
                need(std::abs(h.a-prev.a-(1.f-r))<.002f,"H16 head A glide velocity");
            if(i>0&&!(prevP<.5f&&p>=.5f))
                need(std::abs(h.b-prev.b-(1.f-r))<.002f,"H16 head B glide velocity");
            need(h.a>=1.f&&h.b>=1.f&&h.a<=16381.f&&h.b<=16381.f,"H16 glide bounds");
            prev=h;prevP=p;p+=1.f/grain;if(p>=1.f)p-=1.f;
        }
        PhaseCoherentCarrier fresh;
        const auto resumed=carrier.process(rate,p,ratio,base,grain,period,true,16381.f);
        const auto initial=fresh.process(rate,p,ratio,base,grain,period,true,16381.f);
        need(resumed.a==initial.a&&resumed.b==initial.b,"H16 skipped-call state");
    }
}
static void testBackingPitchContour(){
    for(double hop:{1./150.,1./100.}){
        BackingPitchContour c;c.prepare(hop);
        double retained=0.,input=0.;int n=static_cast<int>(2./hop);
        for(int i=0;i<n;++i){
            const float midi=60.f+.50f*std::sin(6.283185307179586*5.*i*hop);
            const float correction=c.correctionMidi(60,midi);
            if(i>n/2){retained+=(midi-correction)*(midi-correction);input+=(midi-60.f)*(midi-60.f);}
        }
        const double fraction=std::sqrt(retained/input);
        std::cout<<"H15_CONTOUR hop="<<hop<<" retained="<<fraction<<std::endl;
        need(fraction>.45&&fraction<.75,"H15 vibrato contour retention");
        for(int i=0;i<n;++i)c.correctionMidi(60,60.3f);
        need(std::abs(c.correctionMidi(60,60.3f)-60.3f)<.001f,"H15 static tuning drift");
        need(c.correctionMidi(64,64.2f)==64.2f,"H15 note transition stale center");
        c.reset();need(c.correctionMidi(64,63.8f)==63.8f,"H15 phrase reset");
    }
}
int main(){
    testEpochRecoveryPerformance();
    testCausalAdmissionEngine();
    testSlopeAwareSeam();
    testPhaseCoherentCarrier();
    testBackingPitchContour();
  testCorrelationPerformance();
  testArticulation();
  testResonanceEngine();
  testReducedLatency();
  testChordAwareGuide();
  testSingerSpectralIdentity();
  testSingerIdentityPcm();
  testIndependentSingerExpression();
  testCommonToneEngineTransition();
  testMusicalEnsemblePlanner();
  const bool highPass=testMusicalHighStem();
  testLeadRendererSeam();
  testBackingUnity();
  need(highPass,"H3 HIGH presence or isolated fundamental failed; inspect all H3_HIGH rows");
  testUnvoicedGapPreservesWetTail();
  testBackingContinuity();
  testLowerPresenceAndPresets();
  testSourceAlignedCandidate();
  testHeadroomContract();
  auditFieldHeadroom();
  testDoublerMonoCompatibility();
  testContinuousDoubler();
  testUnisonDoubler();
  testPeriodContinuityOnLoudOctaveErrors();
  testLoudVibratoContinuity();
  testLeadTune();
  testVoicePlanner();
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
  for(int mode:{0,1})for(int voices=2;voices<=Engine::maxVoices;++voices){
    for(float space:{0.f,.6f,1.f}){
      float previous=1.f;
      for(float w:{0.f,.25f,.45f,.5f,.75f,1.f}){
        const float gain=Engine::backingUnityGain(w,space,mode,voices);
        need(gain>=previous&&gain<=2.5f&&(w>.45f||gain==1.f),
             "backing bus calibration unbounded or changed factory Wet");
        previous=gain;
      }
    }
  }
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
    float energy=0.f;
    int count=0;
    for(std::size_t voice=0;voice<singers.size();++voice){
      const float level=std::hypot(modern.mixL[voice],modern.mixR[voice]);
      const float prior=std::hypot(legacy.mixL[voice],legacy.mixR[voice]);
      energy+=level*level;
      if(singerActive(singers[voice].role,voices)){
        ++count;
        need(level>prior*1.73f&&level<prior*1.80f,"Wet gain changed per singer");
      }else need(level==0.f,"inactive singer contributes to Wet");
    }
    need(count==voices&&std::abs(energy-1.15f*1.15f)<1.e-5f,
         "ensemble gain changes with voice count");
    need(modern.mixL[0]>modern.mixR[0]&&modern.mixR[2]>modern.mixL[2],
         "LOW/HIGH changed sides when voice count changed");
    // 60% SPACE must now create an audible hard-sided LOW/HIGH pair
    // without making the center MID dominate the music mix.
    need(modern.mixL[0]>modern.mixR[0]*3.f&&
         modern.mixR[2]>modern.mixL[2]*3.f,
         "perceptual HARD stereo spread not applied to LOW/HIGH");
    const float low=std::hypot(modern.mixL[0],modern.mixR[0]);
    const float high=std::hypot(modern.mixL[2],modern.mixR[2]);
    need(high>low*1.11f,"HIGH voice still masked behind LOW");
    Settings centered=settings;centered.space=0.f;
    Engine::PreparedSettings mono{};
    need(Engine::prepareSettings(centered,mono),
         "Space0 layout invalid");
    need(std::abs(mono.mixL[0]-mono.mixR[0])<1.e-6f&&
         std::abs(mono.mixL[2]-mono.mixR[2])<1.e-6f,
         "hard pan leaks when Space=0");
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
  // H7 fixed declared 24ms delay; exact impulse and dry parity above.
  need(e->latencySamples()==1152,"P0 changed live session PDC without proof");
  std::cout<<"Native Harmony DSP: deterministic PCM, 2-5 voices, octave/scale, stereo, silence and 32-192k PASS\n";
}
