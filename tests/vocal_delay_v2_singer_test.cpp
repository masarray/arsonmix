// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/SingerWorkflow.h"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace armix::builtin_fx::vocal_delay;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
int main(){
    for(std::size_t i=0;i<singerPresets.size();++i){auto p=presetSettings(i,false);CHECK(valid(p)&&!p.enabled);}
    float time=0;CHECK(beatTime(120,3,time)&&time==500);
    CHECK(beatTime(120,2,time)&&time==375);CHECK(!beatTime(40,6,time));CHECK(!beatTime(0,3,time));
    TapTempo tap;double bpm=0;CHECK(!tap.tap(0,bpm));CHECK(!tap.tap(500,bpm));CHECK(tap.tap(1000,bpm)&&bpm==120);
    CHECK(!tap.tap(2000,bpm));CHECK(!tap.tap(4000,bpm));CHECK(!tap.tap(4500,bpm));CHECK(tap.tap(5000,bpm)&&bpm==120);
    NativeSession session;State s{};Lease l{1,2,3,4};CHECK(session.prepareStopped(48000,l,s));
    SingerControl c;CHECK(c.attach(session.acceptedSnapshot()));
    auto p=presetSettings(3);CHECK(c.offer(session.acceptedSnapshot(),p));c.service(session);
    p.timeMs=900;p.echo=.33f;CHECK(c.offer(session.acceptedSnapshot(),p));CHECK(c.display(session.acceptedSnapshot()).timeMs==900);
    session.consumeRealtimeEdits();c.service(session);CHECK(c.pending());
    session.consumeRealtimeEdits();c.service(session);CHECK(!c.pending()&&!c.fault());
    CHECK(session.acceptedSnapshot().accepted.timeMs==900);
    CHECK(session.restartStopped(96000,{1,2,4,5}));c.service(session);CHECK(!c.pending());
    std::puts("PASS singer presets, rhythmic range, robust TAP, delayed ACK and final close delivery");
}
