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
    std::cout<<"Control ACK, rejection, queue backpressure and telemetry PASS\n";
}
