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
    CHECK(stagePresetCount==12&&singerPresets.size()==33);
    for(std::size_t i=0;i<concertPresetCount;++i){
        const auto p=presetSettings(concertPresetStart+i);CHECK(valid(p)&&p.smartStage&&p.throwMode!=ThrowMode::off);
        State s{};s.continuous=p;StateCodec::Bytes b{};State out{};
        CHECK(StateCodec::encode(s,b)&&b[4]==7&&StateCodec::decode(b.data(),b.size(),out)&&sameParameters(p,out.continuous));
        auto bad=b;bad[44]=0;CHECK(!StateCodec::decode(bad.data(),bad.size(),out));
        bad=b;bad[44]=19;CHECK(!StateCodec::decode(bad.data(),bad.size(),out));
        CHECK(styleIndex(p)==int(concertPresetStart+i));
    }
    for(std::size_t i=0;i<stagePresetCount;++i){
        const auto p=presetSettings(i);
        CHECK(p.enabled&&p.adaptiveFocus&&p.spillover&&p.smartStage&&p.style!=EchoStyle::classic);
        CHECK(p.feedback<=.42f&&p.echo<=.28f&&p.duckAttackMs<=8.f);
        if(i!=4&&i!=10)CHECK(p.duck>=.83f);
        State s{};s.continuous=p;StateCodec::Bytes bytes{};State out{};
        CHECK(StateCodec::encode(s,bytes)&&bytes[4]==6&&StateCodec::decode(bytes.data(),bytes.size(),out)&&sameParameters(p,out.continuous));
        auto invalid=bytes;invalid[4]=5;CHECK(!StateCodec::decode(invalid.data(),invalid.size(),out));
        invalid=bytes;invalid[43]&=0x7f;CHECK(!StateCodec::decode(invalid.data(),invalid.size(),out));
    }
    CHECK(presetSettings(0).style==EchoStyle::dual&&presetSettings(0).duck>=.9f);
    auto changed=presetSettings(0);changed.adaptiveFocus=false;CHECK(styleIndex(changed)==-1);
    changed=presetSettings(0);changed.spillover=false;CHECK(styleIndex(changed)==-1);
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
    {
        MusicFollow autoTempo;autoTempo.enable(true,300.f);auto base=presetSettings(0);Parameters next{};
        MusicObservation o{7,2,3,100,120,.5f,true};
        CHECK(!autoTempo.update(o,9,0,base,next));o.confidence=.9f;o.frame=200;
        CHECK(!autoTempo.update(o,9,100,base,next));o.frame=300;
        CHECK(autoTempo.update(o,9,600,base,next)&&next.timeMs==250&&next.division==1&&next.smartStage);
        o.locked=false;CHECK(!autoTempo.update(o,9,700,next,base)&&autoTempo.status()==MusicFollow::Status::holding);
    }
    {
        MusicFollow f;f.enable(true,300.f);auto base=presetSettings(0);Parameters next{};
        MusicObservation o{7,2,3,100,120,.9f,true};
        CHECK(!f.update(o,9,0,base,next));o.frame=200;o.bpm=120.5f;
        CHECK(!f.update(o,9,250,base,next));o.frame=300;o.bpm=119.5f;
        CHECK(f.update(o,9,500,base,next)&&next.timeMs==250);
        base=next;
        for(int i=1;i<=8;++i){o.frame+=100;o.bpm=i%2?120.75f:119.25f;
            CHECK(!f.update(o,9,500.+i*250.,base,next));}
        CHECK(f.status()==MusicFollow::Status::locked);
        CHECK(!f.update(o,9,4500,base,next)&&f.status()==MusicFollow::Status::holding);
        o.frame+=100;o.bpm=90;CHECK(!f.update(o,9,4600,base,next));
        o.frame+=100;CHECK(f.update(o,9,5100,base,next)&&next.bpmQuarter==360);
        CHECK(next.timeMs>80&&next.timeMs<=1000);
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
