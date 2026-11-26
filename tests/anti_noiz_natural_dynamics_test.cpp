// SPDX-License-Identifier: AGPL-3.0-only
// Deterministic envelope and latency-aligned PCM qualification; not a vocal listening test.
#include "../engine/builtin_fx/anti_noiz/AntiNoizCore.h"
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
namespace {
void require(bool x,const char* why){if(!x){std::cerr<<why<<'\n';std::abort();}}
float sine(int i,int rate,float hz){return std::sin(2.f*std::numbers::pi_v<float>*hz*float(i)/float(rate));}
void run(int rate,int ch,bool enforce){
    const int delay=rate>48000?512:256;
    ManualExpander::Settings s;s.enabled=true;s.gate=true;s.thresholdDb=-45;s.rangeDb=96;s.attackMs=2;s.holdMs=80;s.releaseMs=150;
    ManualExpander e;e.prepare(rate,ch,delay);e.apply(ManualExpander::prepareSettings(s));
    for(int i=0;i<rate/2;++i)e.step(0,0);
    require(e.gain()==0,"settled digital mute");
    float first=0,peak=0,last=0,previous=0;int openSamples=0;
    for(int i=0;i<rate/10;++i){
        const float x=.1f*sine(i,rate,600),g=e.step(ch==1?x:0,ch==2?x:0),delta=g-previous;
        require(g>=previous&&g>=0&&g<=1,"monotonic opening");
        if(delta>0){if(openSamples==0)first=delta;last=delta;peak=std::max(peak,delta);++openSamples;}
        previous=g;
    }
    require(e.gain()==1,"open endpoint");
    float closeFirst=0,closeLast=0,closePeak=0;int closeSamples=0;
    for(int i=0;i<rate/2;++i){const float g=e.step(0,0),delta=previous-g;
        require(g<=previous&&g>=0,"monotonic closure");
        if(delta>0){if(closeSamples==0)closeFirst=delta;closeLast=delta;closePeak=std::max(closePeak,delta);++closeSamples;}previous=g;}
    require(e.gain()==0,"finite exact gate zero");
    if(enforce){
        require(first<.1f*peak&&last<.1f*peak,"soft opening endpoints");
        require(closeFirst<.1f*closePeak&&closeLast<.1f*closePeak,"soft closing endpoints");
        require(openSamples<=int(rate*.002f)+2&&closeSamples<=int(rate*.15f)+2,"bounded configured time");
    }
    // Synthetic phrase: low-level onset, strong vowel, brief interruption,
    // weak tail. The strong trigger occurs within the existing WOLA delay.
    Core::Settings cfg;cfg.mode=Core::Mode::Voice;cfg.spectralEnabled=false;cfg.cleanDb=0;cfg.silenceDb=0;cfg.manual=s;
    Core core,partition;require(core.prepare(rate,ch)&&partition.prepare(rate,ch)&&core.setSettings(cfg)&&partition.setSettings(cfg),"prepare");
    require(core.latencySamples()==delay,"unchanged PDC");
    const int start=rate/2,weak=int(rate*.002f),end=start+rate/4;
    std::vector<float> input(rate*2),right(input.size());
    for(int i=0;i<int(input.size());++i){
        const int j=i-start;float x=.00025f*sine(i,rate,1700);
        if(j>=0&&j<weak)x+=.003f*sine(j,rate,950);
        else if(j>=weak&&i<end)x+=.08f*sine(j,rate,340);
        if(i>=end&&i<end+rate/100)x+=.004f*sine(i-end,rate,450);
        (ch==1?input:right)[i]=x;
    }
    auto l=input,r=right,a=l,b=r;core.process(l.data(),ch==2?r.data():nullptr,int(l.size()));
    for(int pos=0;pos<int(a.size());pos+=127)partition.process(a.data()+pos,ch==2?b.data()+pos:nullptr,std::min(127,int(a.size())-pos));
    require(l==a&&(ch==1||r==b),"arbitrary partition exact");
    const auto& dry=ch==1?input:right;const auto& wet=ch==1?l:r;
    auto projection=[&](int lo,int hi){double p=0,d=0;for(int i=lo;i<hi;++i){p+=double(wet[i+delay])*dry[i];d+=double(dry[i])*dry[i];}return p/d;};
    const double onset=projection(start,start+weak),body=projection(start+weak,start+weak+rate/100),tail=projection(end,end+rate/100);
    require(body>.99&&body<1.01&&tail>.99&&tail<1.01,"strong onset and weak tail preserved");
    require(std::all_of(wet.begin()+rate*3/2,wet.end(),[](float v){return v==0.f;}),"idle exact zero");
    if(enforce){
        require(onset>.70,"existing lookahead preserves weak precursor");
        // A closed gate must not jump to unity when the user selects a
        // different mode, changes a filter, or disables manual dynamics.
        for(int edit=0;edit<3;++edit){Core copy=core;auto q=cfg;
            if(edit==0)q.manual.enabled=false;else if(edit==1)q.mode=Core::Mode::Music;else q.enabled=false;
            require(copy.setSettings(q),"live handoff");
            float x=.00025f,y=.00025f;copy.process(&x,ch==2?&y:nullptr,1);
            require(copy.residualGain()<.02f,"no reset-to-unity gain jump");
            for(int i=0;i<rate/10;++i){x=.00025f;y=x;copy.process(&x,ch==2?&y:nullptr,1);}
            require(copy.residualGain()==1.f&&std::abs(x-.00025f)<1e-7f,"settled unity handoff");
        }
        Core copy=core;auto q=cfg;q.manual.hpHz=300;require(copy.setSettings(q),"filter edit");
        float x=0,y=0;copy.process(&x,ch==2?&y:nullptr,1);require(copy.residualGain()==0,"filter edit preserves gain state");
    }
    std::cout<<"NATURAL,"<<rate<<','<<ch<<','<<first<<','<<last<<','<<peak<<','<<closeFirst<<','<<closeLast<<','<<closePeak<<','<<openSamples<<','<<closeSamples<<','<<onset<<','<<body<<','<<tail<<','<<delay<<'\n';
}
}
int main(int argc,char** argv){const bool enforce=!(argc==2&&std::string_view(argv[1])=="--report-only");for(int rate:{44100,48000,88200,96000})for(int ch:{1,2})run(rate,ch,enforce);}
