// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/HarmonyMusicKey.h"
#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
using namespace armix::harmony;
static void need(bool test,const char* reason){
    if(!test){std::cerr<<reason<<"\n";std::exit(2);}
}
static void sendChord(MusicKeyDetector& detector,double sampleRate,
                      const std::vector<double>& pitches,double seconds){
    const int size=static_cast<int>(seconds*sampleRate);
    std::vector<float> left(static_cast<std::size_t>(size));
    std::vector<float> right(static_cast<std::size_t>(size));
    for(int i=0;i<size;++i){
        double sample=0.;
        for(const auto pitch:pitches)
            sample+=.28*std::sin(6.283185307179586*pitch*i/sampleRate)/
                   static_cast<double>(pitches.size());
        left[static_cast<std::size_t>(i)]=static_cast<float>(sample);
        right[static_cast<std::size_t>(i)]=static_cast<float>(sample);
    }
    for(int pos=0;pos<size;pos+=127)
        detector.process(left.data()+pos,right.data()+pos,std::min(127,size-pos));
}
int main(){
    MusicKeyDetector bad;
    need(!bad.prepare(8000.)&&!bad.prepare(384000.),"unsafe sample rate accepted");
    for(const double rate:{48000.,96000.}){
        MusicKeyDetector c;need(c.prepare(rate),"C-major detector preparation failed");
        sendChord(c,rate,{261.625565,329.627557,391.995436},3.5);
        auto estimate=c.estimate();
        need(estimate.valid()&&estimate.key==0&&estimate.scale==0,
             "C-major music track did not produce stable C major");
        need(estimate.confidence>=.045f,"C-major confidence lower than acceptance");
        // Source-channel silence invalidates old guidance; Manual is safe.
        sendChord(c,rate,{0.},1.4);
        need(!c.estimate().valid(),"silent music source kept stale key");
        MusicKeyDetector am;need(am.prepare(rate),"A-minor detector preparation failed");
        sendChord(am,rate,{220.,261.625565,329.627557},3.5);
        estimate=am.estimate();
        need(estimate.valid()&&estimate.key==9&&estimate.scale==1,
             "A-minor music track did not identify natural minor");
        am.reset();need(!am.estimate().valid(),"channel retarget retained stale key");
    }
    // Manual Key stays unchanged when the music detector has no evidence;
    // selection is an audio-thread-only, valid other-ChannelId control packet.
    auto engine=std::make_unique<Engine>();
    need(engine->prepare(48000.),"vocal DSP prepare failed");
    Settings s;s.key=2;s.scale=1;s.guideMode=1;s.guideChannelId=777;
    need(engine->setSettings(s),"music guide settings rejected");
    need(engine->effectiveKey()==2&&engine->effectiveScale()==1,
         "ambiguous guide did not retain manual fallback");
    engine->setGuideKey(0,0);
    need(engine->effectiveKey()==0&&engine->effectiveScale()==0,
         "valid music channel guide not applied");
    engine->setGuideKey(-1,-1);
    need(engine->effectiveKey()==2&&engine->effectiveScale()==1,
         "lost music reverted to incorrect manual key");
    Settings invalid=s;invalid.guideMode=0;
    need(!valid(invalid),"manual mode accepted nonzero music channel");
    Settings manual=s;manual.guideMode=0;manual.guideChannelId=0;
    need(engine->setSettings(manual),"manual source switch failed");
    engine->setGuideKey(0,0);
    need(engine->effectiveKey()==2&&engine->effectiveScale()==1,
         "manual key was overridden by unsolicited music");
    std::cout<<"Harmony manual/music guide: real PCM chroma C/A minor, silence, "
                "stable fallback and source settings PASS\n";
}
