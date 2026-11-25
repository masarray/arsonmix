// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/vocal_delay/SingerWorkflow.h"
#include "../engine/builtin_fx/vocal_delay/MusicFollow.h"
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace armix::builtin_fx::vocal_delay;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL:%d %s\n",__LINE__,#x);std::abort();}}while(false)
int main(){
    for(std::size_t i=0;i<singerPresets.size();++i){
        auto p=presetSettings(i,false);CHECK(valid(p)&&!p.enabled&&styleIndex(p)==int(i));
        p.timeMs=875;p.echo=.42f;p.feedback=.45f;CHECK(styleIndex(p)==int(i));
        auto q=smartVoicing(p);CHECK(valid(q)&&!q.enabled&&q.timeMs==p.timeMs&&q.echo==p.echo&&q.feedback==p.feedback&&q.mode==p.mode);
        CHECK(sameParameters(q,smartVoicing(q)));
        p.duck=.99f;CHECK(styleIndex(p)==-1);
    }
    auto quiet=presetSettings(1),heavy=quiet;heavy.echo=.9f;heavy.feedback=.9f;heavy.timeMs=4000;
    auto soft=smartVoicing(quiet),strong=smartVoicing(heavy);
    CHECK(strong.duck>soft.duck&&strong.highPassHz>soft.highPassHz&&strong.lowPassHz<soft.lowPassHz);
    CHECK(strong.duckReleaseMs>soft.duckReleaseMs);
    State voiced{};voiced.continuous=strong;StateCodec::Bytes saved{};State round{};
    CHECK(StateCodec::encode(voiced,saved)&&StateCodec::decode(saved.data(),saved.size(),round)&&sameParameters(strong,round.continuous));
    float time=0;CHECK(beatTime(120,3,time)&&time==500);
    CHECK(beatTime(120,2,time)&&time==375);CHECK(!beatTime(40,6,time));CHECK(!beatTime(0,3,time));
    TapTempo tap;double bpm=0;CHECK(!tap.tap(0,bpm));CHECK(!tap.tap(500,bpm));CHECK(tap.tap(1000,bpm)&&bpm==120);
    CHECK(!tap.tap(2000,bpm));CHECK(!tap.tap(4000,bpm));CHECK(!tap.tap(4500,bpm));CHECK(tap.tap(5000,bpm)&&bpm==120);
    {
        MusicFollow follow;Parameters base{},next{};MusicObservation o{7,2,3,100,120,.8f,true};
        CHECK(!follow.update(o,9,0,base,next));
        follow.enable(true);
        CHECK(!follow.update(o,9,0,base,next));o.frame=200;
        CHECK(!follow.update(o,9,250,base,next));o.frame=300;
        CHECK(follow.update(o,9,500,base,next)&&next.timeMs==500&&next.echo==base.echo);
        base=next;o.frame=400;CHECK(!follow.update(o,9,750,base,next));
        o.confidence=.1f;CHECK(!follow.update(o,9,1000,base,next)&&follow.status()==MusicFollow::Status::holding);
        o.confidence=.9f;o.source=9;CHECK(!follow.update(o,9,1250,base,next));
        o.source=7;o.epoch=4;o.bpm=100;o.frame=500;
        CHECK(!follow.update(o,9,1500,base,next));o.frame=600;
        CHECK(follow.update(o,9,2000,base,next)&&next.timeMs==600);
        base=next;o.frame=700;o.bpm=60;base.division=6;
        CHECK(!follow.update(o,9,2250,base,next));o.frame=800;
        CHECK(follow.update(o,9,2750,base,next)&&next.timeMs==4000);
        o.bpm=40;o.frame=900;CHECK(!follow.update(o,9,3000,base,next));o.frame=1000;
        CHECK(!follow.update(o,9,3500,base,next)&&follow.status()==MusicFollow::Status::range);
        follow.enable(false);CHECK(follow.status()==MusicFollow::Status::manual);
    }
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
