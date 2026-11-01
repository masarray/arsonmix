// SPDX-License-Identifier: AGPL-3.0-only
#include "../integration/ConsoleNativeContractV6.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace armix::mixer;
using namespace armix::integration;
static bool approx(float a,float b,float eps=4.e-5f) {
    return std::abs(a-b)<eps;
}
static ChannelEdit linkOut(ChannelId a,ChannelId b) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::setPrimaryOutput;
    e.id=a;e.target=b;return e;
}
static ChannelEdit parallel(ChannelId a,ChannelId b,float g,bool pre=false) {
    ChannelEdit e;e.kind=ChannelEdit::Kind::addSendReference;
    e.id=a;e.target=b;e.value=g;e.preFader=pre;return e;
}
static const ConsoleChannelV6* find(
    const ConsoleAcceptedReadbackV6& snap,ChannelId id) {
    for(const auto& ch:snap.channels)
        if(ch.id==std::to_string(id.value))return &ch;
    return nullptr;
}
int main() {
    constexpr std::uint64_t epoch=69;
    constexpr float center=.7071067811865475f;
    DynamicAcceptedSession native;
    DynamicGraphRequest route;
    route.physicalInputs=2;route.maxFrames=32;
    std::vector<ChannelEdit> add;
    for(int i=0;i<10;++i) {
        ChannelEdit e;e.kind=ChannelEdit::Kind::add;
        e.position=add.size();e.role=ChannelRole::input;
        e.format=ProcessingFormat::mono;
        e.text="CH "+std::to_string(i+1);add.push_back(e);
    }
    ChannelEdit m;m.kind=ChannelEdit::Kind::add;
    m.position=add.size();m.role=ChannelRole::main;
    m.format=ProcessingFormat::stereo;m.text="MAIN";add.push_back(m);
    ChannelEdit mon=m;mon.position=add.size();
    mon.role=ChannelRole::monitor;mon.text="MONITOR";add.push_back(mon);

    auto first=native.edit(0,0,add,route,epoch,epoch,true);
    assert(first.ok() && first.edit.createdIds.size()==12);
    const auto ch7=first.edit.createdIds[6],ch2=first.edit.createdIds[1];
    const auto main=first.edit.createdIds[10],monitor=first.edit.createdIds[11];
    // Two IN pins are callback ordinals, not guessed adjacent physical DB labels.
    route.sources={{ch7,0,-1,false},{ch2,1,-1,false}};
    std::array<ChannelEdit,4> edges{linkOut(ch7,main),linkOut(ch2,main),
                                   parallel(ch7,monitor,.2f),
                                   parallel(ch2,monitor,.3f)};
    auto patched=native.edit(1,1,edges,route,epoch,epoch,true);
    assert(patched.ok() && native.acceptedGraphRevision()==2);

    ConsoleAcceptedReadbackV6 snap;
    assert(projectAcceptedConsoleV6(native,epoch,2,snap));
    assert(snap.channels.size()==12);
    assert(snap.mainId==std::to_string(main.value));
    assert(snap.monitorId==std::to_string(monitor.value));
    assert(find(snap,ch7)->primaryOutId==snap.mainId);
    assert(find(snap,ch2)->primaryOutId==snap.mainId);
    assert(!find(snap,ch7)->sourcePinVerified);
    assert(!snap.productionCallbackLive);
    assert(!snap.capabilities.universalLive());
    assert(!snap.capabilities.group&&!snap.capabilities.fxReturn);

    ConsoleIntentV6 intent;
    intent.requestId=41;intent.deviceEpoch=epoch;
    intent.baseGraphRevision=2;intent.baseRosterRevision=2;
    intent.operation=ConsoleIntentV6::Operation::groupSelected;
    intent.target=ch7;
    assert(validateConsoleIntent(intent,epoch,40,native)==IntentFaultV6::none);
    assert(validateConsoleIntent(intent,epoch,41,native)==IntentFaultV6::duplicateOrOldRequest);
    assert(validateConsoleIntent(intent,epoch+1,40,native)==IntentFaultV6::staleDevice);
    intent.baseGraphRevision=1;
    assert(validateConsoleIntent(intent,epoch,40,native)==IntentFaultV6::staleGraph);
    intent.baseGraphRevision=2;intent.baseRosterRevision=1;
    assert(validateConsoleIntent(intent,epoch,40,native)==IntentFaultV6::staleRoster);
    intent.baseRosterRevision=2;

    std::array<float,32> a{},b{},left{},right{},ml{},mr{};
    a.fill(.2f);b.fill(.4f);
    const float* pins[]{a.data(),b.data()};
    ChannelId group{},wet{};
    const auto fx=[&](ChannelId id,float* l,float* r,int frames) noexcept {
        const float gain=id==group?2.f:id==wet?.5f:1.f;
        for(int i=0;i<frames;++i){l[i]*=gain;r[i]*=gain;}
    };
    const auto render=[&]() {
        return native.render(epoch,pins,2,left.data(),right.data(),
                             32,false,fx,ml.data(),mr.data());
    };
    assert(render());
    assert(approx(left[31],(.2f+.4f)*center));
    assert(approx(ml[31],(.2f*.2f+.4f*.3f)*center));

    std::array<ChannelId,2> selected{ch7,ch2};
    // Operator gestures DO NOT advance accepted readback until native ACK.
    const auto stale=native.createBusForSelected(
        2,2,selected,"BAD",ChannelRole::group,route,epoch+1,epoch,true);
    assert(!stale.ok());
    auto nack=consoleAckFromNative(intent,IntentFaultV6::none,native,&stale);
    assert(nack.status==ConsoleAckV6::Status::rejected);
    assert(nack.graphRevision==2 && !nack.productionCallbackLive);
    const auto prev=snap;
    assert(!projectAcceptedConsoleV6(native,epoch+1,2,snap));
    assert(snap.graphRevision==prev.graphRevision);
    assert(snap.channels.size()==12);
    assert(render() && approx(left[31],(.2f+.4f)*center));

    auto added=native.createBusForSelected(2,2,selected,"VOCAL GROUP",
                 ChannelRole::group,route,epoch,epoch,true);
    assert(added.ok() && added.edit.createdIds.size()==1);
    group=added.edit.createdIds[0];
    auto ack=consoleAckFromNative(intent,IntentFaultV6::none,native,&added);
    assert(ack.status==ConsoleAckV6::Status::acceptedGraph);
    assert(ack.graphRevision==3 && !ack.productionCallbackLive);
    assert(projectAcceptedConsoleV6(native,epoch,3,snap));
    assert(find(snap,ch7)->primaryOutId==std::to_string(group.value));
    assert(find(snap,ch2)->primaryOutId==std::to_string(group.value));
    assert(find(snap,group)->primaryInputCount==2);
    assert(render()&&approx(left[31],(.2f+.4f)*center*2.f));
    assert(approx(ml[31],(.2f*.2f+.4f*.3f)*center));

    const std::array<ChannelId,1> onlyCh7{ch7};
    auto addedReturn=native.createBusForSelected(3,3,onlyCh7,"REVERB",
             ChannelRole::fxReturn,route,epoch,epoch,true,.25f);
    assert(addedReturn.ok());
    wet=addedReturn.edit.createdIds[0];
    assert(projectAcceptedConsoleV6(native,epoch,4,snap));
    const auto* liveCh7=find(snap,ch7);
    assert(liveCh7 && liveCh7->primaryOutId==std::to_string(group.value));
    assert(liveCh7->sends.size()==2); // MONITOR remains, wet SEND added
    assert(find(snap,wet)->parallelSendInputCount==1);
    assert(render());
    assert(approx(left[31],(.2f+.4f)*center*2.f+.2f*center*.25f*.5f));
    assert(approx(ml[31],(.2f*.2f+.4f*.3f)*center));

    // Missing hardware pin or stale graph NEVER gets advertised as LIVE.
    route.sources[1].inputL=2; // physicalInputs=2 -> out of range
    const std::array<ChannelEdit,1> editOne{linkOut(ch2,main)};
    auto rejected=native.edit(4,4,editOne,route,epoch,epoch,true);
    assert(!rejected.ok());
    assert(native.acceptedGraphRevision()==4);
    assert(projectAcceptedConsoleV6(native,epoch,4,snap));
    assert(snap.channels.size()==14 && !snap.productionCallbackLive);
    route.sources[1].inputL=1;
    assert(render()&&left[31]>0.f);
    // OUTPUT MUTE must silence BOTH main and monitor and never invoke FX.
    assert(!native.render(epoch,pins,2,left.data(),right.data(),
                          32,true,fx,ml.data(),mr.data()));
    assert(left[31]==0.f&&ml[31]==0.f);
}
