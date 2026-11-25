// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerInsert.h"
#include <cstdlib>
#include <iostream>
using namespace armix::builtin_fx::de_ezzer;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
 for(double rate:{44100.,48000.,88200.,96000.}){
    Insert i;Settings s;s.mode=Mode::Voice;s.shape=Shape::Focused;s.amount=.77f;s.sensitivity=.39f;s.rangeDb=11.4f;
    const auto blob=StateCodec::encode(s);check(i.restoreUnpublished(blob.data(),blob.size()),"cold restore");
    check(i.prepare({9,1},rate,2,256),"prepare");check(i.acceptedState()==blob,"cold settings");
    auto bad=blob;bad[20]=8;check(!i.restoreUnpublished(bad.data(),bad.size()),"invalid flags");bad=blob;bad[4]=2;check(!i.restoreUnpublished(bad.data(),bad.size()),"invalid version");bad=blob;bad[8]=0;bad[9]=0;bad[10]=128;bad[11]=127;check(!i.restoreUnpublished(bad.data(),bad.size()),"nonfinite state");check(!i.restoreUnpublished(blob.data(),blob.size()-1),"truncated state");
    i.seal();check(!i.restoreUnpublished(blob.data(),blob.size()),"sealed restore refused");
    std::array<float,256> l{},r{},a{},b{};Core ref;check(ref.setSettings(s)&&ref.prepare(rate,2),"reference");
    for(int j=0;j<256;++j){l[j]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*j/rate)+.25*std::sin(2*std::numbers::pi*7200*j/rate));r[j]=-l[j];}a=l;b=r;
    i.consumeRealtimeEdits();check(i.processAudio(l.data(),r.data(),256),"PCM");ref.process(a.data(),b.data(),256);check(l==a&&r==b,"same core PCM");
    const auto id=i.identity();s.amount=.2f;check(!i.submit({id,1,s,Audition::Delta}),"audience delta rejected");check(!i.submit({{9,2},1,s}),"wrong epoch rejected");check(i.submit({id,1,s}),"submit");check(!i.stateSnapshotQuiescent(),"pending before consumption");
    i.consumeRealtimeEdits(); // bypass path, no PCM required to acknowledge.
    check(!i.stateSnapshotQuiescent(),"ACK outstanding blocks capture");check(i.acceptedState()==blob,"no optimistic state");Ack ack;
    check(i.pollAck(ack)&&ack.code==AckCode::Applied&&ack.actual.appliedSequence==1,"bypass ACK");check(i.stateSnapshotQuiescent()&&i.acceptedState()==StateCodec::encode(s),"accepted persistence");
    // A restart must retain the audio-owned edit even if CONTROL never polled ACK.
    s.rangeDb=5;check(i.submit({id,2,s}),"restart edit");i.consumeRealtimeEdits();
    check(!i.prepare({9,1},rate,2,256),"epoch reuse refused");check(!i.prepare({9,2},0,2,256),"bad restart preserves old state");
    check(i.prepare({9,2},rate,2,256),"restart");check(i.acceptedState()==StateCodec::encode(s)&&i.stateSnapshotQuiescent()&&!i.pollAck(ack),"restart latest audio state and flush");check(!i.submit({id,3,s}),"old epoch rejected");
    auto current=i.readback();check(current.appliedSequence==0&&current.frames==0&&current.id.epoch==2,"new readback epoch");
    check(!i.processAudio(l.data(),r.data(),257),"oversize buffer rejected");
    check(i.submit({i.identity(),1,s}),"fresh edit");i.consumeRealtimeEdits();check(i.processAudio(l.data(),r.data(),256),"new PCM");check(i.readback().appliedSequence==0,"telemetry cannot apply state");check(i.pollAck(ack)&&i.readback().appliedSequence==1,"new ACK");
 }
 std::cout<<"PASS: 4 rates, insert PCM parity, bypass ACK, sealed persistence, malformed state, restart/epoch, audience audition blocked\n";
}
