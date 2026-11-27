// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerControl.h"
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
using namespace armix::builtin_fx::de_ezzer;
void check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
using Signal=std::vector<float>;
void fill(Signal& l,Signal& r,double rate,int start=0){for(int i=0;i<static_cast<int>(l.size());++i){double t=(i+start)/rate;l[i]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+.25*std::sin(2*std::numbers::pi*7200*t));r[i]=-l[i];}}
void fence(){
    Control c;Identity id{11,1};check(c.prepare(id,48000,1),"prepare");Signal x(128,0);Ack ack;Snapshot snap;
    Command cmd{id,1,{}};
    check(c.submit(cmd),"enqueue");check(!c.process(x.data(),nullptr,0),"invalid block");check(!c.pollAck(ack),"invalid block must not consume");
    check(c.process(x.data(),nullptr,128)&&c.pollAck(ack)&&ack.code==AckCode::Applied&&ack.actual.appliedSequence==1,"apply ACK");
    cmd.sequence=2;cmd.id.epoch=9;check(c.submit(cmd),"wrong epoch enqueue");cmd.id=id;cmd.sequence=1;check(c.submit(cmd),"stale enqueue");cmd.sequence=2;cmd.settings.amount=2;check(c.submit(cmd),"invalid enqueue");cmd.settings.amount=.5f;check(c.submit(cmd),"invalid retry enqueue");cmd.sequence=3;cmd.settings.enabled=false;cmd.audition=Audition::Delta;check(c.submit(cmd),"invalid audition enqueue");
    check(c.process(x.data(),nullptr,128),"process rejects");for(auto code:{AckCode::WrongIdentity,AckCode::StaleSequence,AckCode::Invalid,AckCode::StaleSequence,AckCode::Invalid})check(c.pollAck(ack)&&ack.code==code&&ack.actual.appliedSequence==1,"reject preserves readback");
    cmd.settings={};cmd.audition=Audition::Normal;
    for(int i=0;i<32;++i){cmd.sequence=4+i;check(c.submit(cmd),"fill command");}cmd.sequence=36;check(!c.submit(cmd),"bounded command full");
    for(int i=0;i<4;++i)check(c.process(x.data(),nullptr,128),"fill ACK");
    for(int i=0;i<32;++i){cmd.sequence=36+i;check(c.submit(cmd),"refill command");}
    check(c.process(x.data(),nullptr,128),"audio survives full ACK queue");cmd.sequence=68;check(!c.submit(cmd),"ACK backpressure holds commands");
    for(int i=0;i<32;++i)check(c.pollAck(ack)&&ack.sequence==static_cast<unsigned>(4+i),"ACK FIFO no loss");
    for(int j=0;j<4;++j){check(c.process(x.data(),nullptr,128),"resume");for(int i=0;i<8;++i)check(c.pollAck(ack)&&ack.sequence==static_cast<unsigned>(36+j*8+i),"resume ACK FIFO");}
    for(int i=0;i<200;++i)check(c.process(x.data(),nullptr,128),"telemetry pressure");
    while(c.pollTelemetry(snap)){}
    for(int i=0;i<20;++i)check(c.process(x.data(),nullptr,128),"telemetry resume");
    check(c.pollTelemetry(snap)&&snap.droppedTelemetry>0&&snap.appliedSequence==67,"droppable telemetry readback");
    check(!c.prepare(id,48000,1)&&!c.prepare({11,2},0,1),"invalid lifecycle");check(c.submit(cmd),"pending before reset");check(c.prepare({11,2},48000,1),"new epoch");check(!c.pollAck(ack)&&!c.pollTelemetry(snap),"quiescent flush");check(c.submit(cmd),"late stale command");check(c.process(x.data(),nullptr,128)&&c.pollAck(ack)&&ack.code==AckCode::WrongIdentity&&ack.actual.id.epoch==2&&ack.actual.appliedSequence==0,"epoch fence");
}
void audio(){
 for(double rate:{44100.,48000.,88200.,96000.}){
    Control c;Core ref;Settings s;s.mode=Mode::Voice;s.shape=Shape::Focused;Identity id{7,1};check(c.prepare(id,rate,2,s)&&ref.setSettings(s)&&ref.prepare(rate,2),"audio prepare");
    Signal l(4096),r(4096),wetL(4096),wetR(4096),dL(4096),dR(4096);Ack ack;Snapshot snap;
    fill(l,r,rate);wetL=l;wetR=r;ref.process(wetL.data(),wetR.data(),4096);check(c.process(l.data(),r.data(),4096)&&l==wetL&&r==wetR,"normal bit parity");
    for(auto mode:{Audition::Detect,Audition::Delta,Audition::Normal}){
        const auto seq=static_cast<unsigned>(mode==Audition::Detect?1:mode==Audition::Delta?2:3);check(c.submit({id,seq,s,mode}),"audition submit");fill(l,r,rate,static_cast<int>(seq)*4096);auto dryL=l,dryR=r;wetL=l;wetR=r;ref.processWithDetect(wetL.data(),wetR.data(),4096,dL.data(),dR.data());check(c.process(l.data(),r.data(),4096)&&c.pollAck(ack)&&ack.code==AckCode::Applied&&ack.actual.audition==mode,"audition ACK");
        double energy=0;for(int i=1024;i<4096;++i){float expected=mode==Audition::Detect?dL[i]:mode==Audition::Delta?dryL[i]-wetL[i]:wetL[i];check(std::abs(l[i]-expected)<1e-7&&l[i]==-r[i],"audition actual signal");energy+=static_cast<double>(l[i])*l[i];}check(energy>.01,"nonzero audition");
    }
    check(c.submit({id,4,s,Audition::Detect}),"lease submit");int frames=0;while(frames<static_cast<int>(rate*3.1)){fill(l,r,rate,frames);wetL=l;wetR=r;ref.process(wetL.data(),wetR.data(),4096);check(c.process(l.data(),r.data(),4096),"lease process");while(c.pollTelemetry(snap)){}frames+=4096;}
    check(snap.audition==Audition::Normal&&snap.appliedSequence==4,"lease expires without new command");check(l==wetL&&r==wetR,"expired audition exact normal");
 }
}
void concurrent(){
    Control c;Identity id{19,4};check(c.prepare(id,48000,2),"thread prepare");constexpr unsigned count=20000;
    std::atomic<bool> done=false;std::thread producer([&]{unsigned sent=0,received=0;Ack a;Snapshot s;while(received<count){if(sent<count){Command cmd{id,sent+1,{}};cmd.settings.amount=static_cast<float>(sent%11)/10;if(c.submit(cmd))++sent;}while(c.pollAck(a)){check(a.sequence==received+1&&a.code==AckCode::Applied&&a.actual.appliedSequence==a.sequence,"thread ACK order");++received;}while(c.pollTelemetry(s))check(s.id==id&&s.appliedSequence<=sent,"thread snapshot");std::this_thread::yield();}done.store(true,std::memory_order_release);});
    std::array<float,64> l{},r{};while(!done.load(std::memory_order_acquire)){check(c.process(l.data(),r.data(),64),"concurrent process");std::this_thread::yield();}producer.join();
}
int main(){fence();audio();concurrent();std::cout<<"PASS: bounded queues, ACK/epoch/sequence, telemetry drops, exact wet/Detect/Delta, lease, 20000 threaded commands\n";}
