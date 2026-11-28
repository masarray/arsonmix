// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizControl.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace armix::builtin_fx::anti_noiz;
void check(bool ok){if(!ok)std::abort();}
int main(){
    Control c;check(c.prepare(48000,1));
    Control::Completion ack;Control::Snapshot meter;
    Core::Settings s; s.cleanDb=9;s.musicProtect=.8f;
    check(c.submit({1,Control::Kind::settings,s}));check(!c.poll(ack));
    float pcm[64]{};c.process(pcm,nullptr,64);
    check(c.poll(ack)&&ack.id==1&&ack.applied&&ack.state.settings.cleanDb==9);
    s.musicProtect=std::numeric_limits<float>::quiet_NaN();
    check(c.submit({2,Control::Kind::settings,s}));c.consume();
    check(c.poll(ack)&&!ack.applied&&ack.state.settings.musicProtect==.8f);
    s.musicProtect=1;s.enabled=false;
    check(c.submit({3,Control::Kind::settings,s}));c.consume();check(c.poll(ack)&&ack.applied);
    check(c.submit({4,Control::Kind::learn,{}}));c.consume();check(c.poll(ack)&&!ack.applied);
    for(unsigned i=0;i<16;++i)check(c.submit({10+i,Control::Kind::settings,s}));
    check(!c.submit({99,Control::Kind::settings,s}));
    for(unsigned i=0;i<16;++i)c.consume();
    check(c.submit({100,Control::Kind::settings,s}));c.consume();
    for(unsigned i=0;i<16;++i)check(c.poll(ack)&&ack.id==10+i);
    c.consume();check(c.poll(ack)&&ack.id==100);
    for(int i=0;i<100;++i)c.process(pcm,nullptr,64);
    check(c.pollTelemetry(meter));check(c.pollTelemetry(meter));check(!c.pollTelemetry(meter));
    check(c.prepare(96000,2));check(c.snapshot().latency==512);
    // Sample-clock telemetry: 20 Hz regardless of irregular partitions,
    // with immediate first snapshot and no catch-up burst.
    Control cadence;check(cadence.prepare(48000,1));float block[1024]{};
    int snapshots=0;
    for(int pos=0;pos<48000;){
        const int n=std::min(127,48000-pos);
        cadence.process(block,nullptr,n);pos+=n;
        while(cadence.pollTelemetry(meter))++snapshots;
    }
    check(snapshots==20);
    // A pending Learn is cancelled before its 2s qualification completes.
    Control life;check(life.prepare(48000,1));
    s={};s.mode=Core::Mode::Voice;
    check(life.submit({1,Control::Kind::settings,s}));life.consume();check(life.poll(ack)&&ack.applied);
    check(life.submit({2,Control::Kind::learn,{}}));life.process(block,nullptr,64);
    check(life.poll(ack)&&ack.applied&&ack.state.learning);
    check(life.submit({3,Control::Kind::learn,{}}));life.consume();
    check(life.poll(ack)&&!ack.applied&&ack.state.learning);
    s.enabled=false;check(life.submit({4,Control::Kind::settings,s}));life.consume();
    check(life.poll(ack)&&ack.applied&&!ack.state.learning&&!ack.state.profile);
    check(life.submit({5,Control::Kind::learn,{}}));life.consume();check(life.poll(ack)&&!ack.applied);
    s.enabled=true;s.mode=Core::Mode::Music;
    check(life.submit({6,Control::Kind::settings,s}));life.consume();check(life.poll(ack)&&ack.applied);
    check(life.submit({7,Control::Kind::learn,{}}));life.consume();check(life.poll(ack)&&!ack.applied);
    std::cout<<"Control ACK, rejection, queue backpressure, 20Hz sample cadence and Learn cancellation PASS\n";
}
