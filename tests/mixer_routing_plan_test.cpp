// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/MixerRoutingPlan.h"
#include "../engine/core/DynamicChannelRoster.h"
#include "../engine/core/DynamicPreparedMixerBlock.h"
#include <array>
#if defined(NDEBUG)
#undef NDEBUG // Never skip P1A assertions in Release native CTest.
#endif
#include <cassert>
#include <cmath>
#include <limits>

using namespace armix::mixer;
static bool close(float a,float b,float tol=1.e-5f){return std::abs(a-b)<tol;}


static void verifyDynamicChannelRoster() {
    DynamicChannelRoster bank;
    std::vector<ChannelEdit> addOps;
    for(std::size_t i=0;i<128;++i){
        ChannelEdit a;a.kind=ChannelEdit::Kind::add;a.position=i;addOps.push_back(a);
    }
    // Add must honor stereo format at creation; invalid formats roll back.
    addOps[3].format=ProcessingFormat::stereo;
    auto result=bank.edit(0,addOps);
    assert(result.ok()&&bank.size()==128&&result.createdIds.size()==128);
    const auto first=result.createdIds.front(),last=result.createdIds.back();
    assert(bank.find(result.createdIds[3])->format==ProcessingFormat::stereo);
    assert(bank.find(result.createdIds[2])->format==ProcessingFormat::mono);
    auto badAdd=ChannelEdit{};badAdd.kind=ChannelEdit::Kind::add;
    badAdd.position=bank.size();badAdd.format=static_cast<ProcessingFormat>(255);
    auto invalidFormat=bank.edit(1,std::span<const ChannelEdit>(&badAdd,1));
    assert(invalidFormat.fault==ChannelEditFault::invalidValue);
    assert(bank.revision()==1&&bank.size()==128);
    auto format=ChannelEdit{};format.kind=ChannelEdit::Kind::setFormat;
    format.id=last;format.format=ProcessingFormat::stereo;
    auto fx=ChannelEdit{};fx.kind=ChannelEdit::Kind::addInsertReference;
    fx.id=last;fx.text="vst3:example";
    const std::vector<ChannelEdit> cfg{format,fx};
    result=bank.edit(1,cfg);
    assert(result.ok()&&bank.find(last)->format==ProcessingFormat::stereo);
    assert(bank.find(last)->insertCatalogIds.size()==1);
    assert(bank.find(first)->insertCatalogIds.empty());
    auto move=ChannelEdit{};move.kind=ChannelEdit::Kind::move;
    move.id=last;move.position=0;
    result=bank.edit(2,std::span<const ChannelEdit>(&move,1));
    assert(result.ok()&&bank.channels().front().id==last);
    auto dup=ChannelEdit{};dup.kind=ChannelEdit::Kind::duplicate;
    dup.id=last;dup.position=1;
    result=bank.edit(3,std::span<const ChannelEdit>(&dup,1));
    assert(result.ok()&&result.createdIds.size()==1);
    const auto copyId=result.createdIds.front();
    assert(copyId!=last&&bank.find(copyId)->insertCatalogIds==bank.find(last)->insertCatalogIds);
    assert(bank.find(copyId)->sends.empty());
    auto send=ChannelEdit{};send.kind=ChannelEdit::Kind::addSendReference;
    send.id=last;send.target=copyId;send.value=.5f;
    result=bank.edit(4,std::span<const ChannelEdit>(&send,1));
    assert(result.ok()&&bank.find(last)->sends[0].destination==copyId);
    auto erase=ChannelEdit{};erase.kind=ChannelEdit::Kind::remove;erase.id=copyId;
    result=bank.edit(5,std::span<const ChannelEdit>(&erase,1));
    assert(result.fault==ChannelEditFault::linkedDestination&&bank.revision()==5);
    auto rename=ChannelEdit{};rename.kind=ChannelEdit::Kind::rename;
    rename.id=last;rename.text="Lead Vocal";
    auto invalid=ChannelEdit{};invalid.kind=ChannelEdit::Kind::setPan;
    invalid.id=first;invalid.value=std::numeric_limits<float>::quiet_NaN();
    const std::vector<ChannelEdit> reject{rename,invalid};
    result=bank.edit(5,reject);
    assert(result.fault==ChannelEditFault::invalidValue&&bank.revision()==5);
    assert(bank.find(last)->name!="Lead Vocal");
    result=bank.edit(4,std::span<const ChannelEdit>(&rename,1));
    assert(result.fault==ChannelEditFault::staleRevision);
    DynamicChannelRoster small({sizeof(ChannelStripDefinition)*2+100,8,32});
    const std::vector<ChannelEdit> over{addOps[0],addOps[1],addOps[2]};
    result=small.edit(0,over);
    assert(result.fault==ChannelEditFault::budgetExceeded&&small.size()==0);
    assert(small.revision()==0);
    result=small.edit(0,std::span<const ChannelEdit>(&addOps[0],1));
    assert(result.ok()&&small.channels().front().id.value==1);
    DynamicChannelRoster large;
    std::vector<ChannelEdit> bulk;bulk.reserve(1024);
    for(std::size_t i=0;i<1024;++i){
        ChannelEdit a;a.kind=ChannelEdit::Kind::add;a.position=i;bulk.push_back(a);
    }
    result=large.edit(0,bulk);
    assert(result.ok()&&large.size()==1024);
    assert(large.channels().back().id.value==1024);
    assert(large.channels().back().insertCatalogIds.empty());
}


static void verifyDynamicPreparedPcm() {
    using Kind=ChannelEdit::Kind;
    DynamicChannelRoster bank;
    std::vector<ChannelEdit> creates;
    for(int i=0;i<128;++i){
        ChannelEdit op;op.kind=Kind::add;op.position=creates.size();
        op.role=i==127?ChannelRole::main:ChannelRole::input;
        op.format=i==127?ProcessingFormat::stereo:ProcessingFormat::mono;
        creates.push_back(op);
    }
    auto result=bank.edit(0,creates);
    assert(result.ok()&&bank.size()==128);
    const auto ids=result.createdIds;
    std::vector<ChannelEdit> sends;
    for(int i=0;i<127;++i){
        ChannelEdit op;op.kind=Kind::addSendReference;op.id=ids[i];
        op.target=ids[127];op.value=1.f;
        if(i==0)op.preFader=true; // PRE source must ignore its own fader
        sends.push_back(op);
    }
    assert(bank.edit(bank.revision(),sends).ok());
    DynamicGraphRequest request;
    request.physicalInputs=2;
    request.maxFrames=32;
    request.sources={{ids[0],0,-1,false},{ids[10],1,-1,false},
                     {ids[31],0,-1,false},{ids[63],1,-1,false},
                     {ids[126],0,-1,false}};
    DynamicPreparedMixerBlock renderer;
    DynamicGraphError error;
    assert(DynamicPreparedMixerBlock::compile(bank,request,renderer,error));
    assert(renderer.channelCount()==128);
    assert(renderer.activeChannels()==6); // five patched lanes + MAIN, not 128 silent FX callbacks
    std::array<float,32> a{},b{},l{},r{};
    a.fill(.2f);b.fill(.4f);
    const float* physical[]{a.data(),b.data()};
    int fxCalls=0;
    const auto fx=[&](ChannelId id,float* left,float* right,int frames) noexcept {
        ++fxCalls;
        const float multiplier=id==ids[31]?2.f:1.f;
        for(int i=0;i<frames;++i){left[i]*=multiplier;right[i]*=multiplier;}
    };
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    assert(fxCalls==6); // identical FX seam on all audible CH1/11/32/64/127 + MAIN
    constexpr float center=.7071067811865475f;
    // Mono INPUT -> mono fader (-3dB); ST master -> all contributions sum.
    assert(close(l[0],(.2f+.4f+.2f*2.f+.4f+.2f)*center));
    assert(close(r[0],l[0]));
    // Duplicate IDs remain independent; moving a channel changes UI order
    // only, the send destination is the stable MAIN id.
    ChannelEdit move;move.kind=Kind::move;move.id=ids[63];move.position=0;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&move,1)).ok());
    assert(DynamicPreparedMixerBlock::compile(bank,request,renderer,error));
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    assert(close(l[0],(.2f+.4f+.2f*2.f+.4f+.2f)*center));
    // Muting CH32 does not affect CH1, CH11, CH64 or CH127.
    ChannelEdit muted;muted.kind=Kind::setFader;muted.id=ids[31];muted.value=0.f;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&muted,1)).ok());
    assert(DynamicPreparedMixerBlock::compile(bank,request,renderer,error));
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    assert(close(l[0],(.2f+.4f+.4f+.2f)*center));
    // CH1 uses PRE-send: its fader zero must NOT silence MAIN.
    ChannelEdit zeroFader;zeroFader.kind=Kind::setFader;
    zeroFader.id=ids[0];zeroFader.value=0.f;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&zeroFader,1)).ok());
    assert(DynamicPreparedMixerBlock::compile(bank,request,renderer,error));
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    assert(close(l[0],(.2f+.4f+.4f+.2f)*center));
    // MUTE does suppress PRE; unchanged independent send destinations.
    ChannelEdit mute;mute.kind=Kind::setMute;mute.id=ids[0];mute.enabled=true;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&mute,1)).ok());
    assert(DynamicPreparedMixerBlock::compile(bank,request,renderer,error));
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    assert(close(l[0],(.4f+.4f+.2f)*center));
    mute.enabled=false;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&mute,1)).ok());
    ChannelEdit solo;solo.kind=Kind::setSolo;solo.id=ids[10];solo.enabled=true;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&solo,1)).ok());
    assert(DynamicPreparedMixerBlock::compile(bank,request,renderer,error));
    assert(renderer.activeChannels()==2); // CH11 + MAIN only
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    assert(close(l[0],.4f*center));
    solo.enabled=false;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&solo,1)).ok());
    assert(DynamicPreparedMixerBlock::compile(bank,request,renderer,error));
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    assert(close(l[0],(.2f+.4f+.4f+.2f)*center));
    // All 128 have the exact same audio FX/route API, but nonpatched
    // descriptors must not allocate a plugin or provide fake input.
    auto wrong=request;
    wrong.sources.push_back({ids[10],0,-1,false});
    const auto previous=renderer.channelCount();
    assert(!DynamicPreparedMixerBlock::compile(bank,wrong,renderer,error));
    assert(error.fault==DynamicGraphFault::invalidSource);
    assert(renderer.channelCount()==previous);
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    assert(close(l[0],(.2f+.4f+.4f+.2f)*center));
    // Cycle must be caught by the compiler, previous audio remains valid.
    auto cycle=ChannelEdit{};cycle.kind=Kind::addSendReference;
    cycle.id=ids[127];cycle.target=ids[10];cycle.value=1.f;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&cycle,1)).ok());
    assert(!DynamicPreparedMixerBlock::compile(bank,request,renderer,error));
    assert(error.fault==DynamicGraphFault::mainFeedback);
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    // Duplicate (same source, destination, tap) must reject atomically:
    // previous active render block must remain safe and unchanged.
    DynamicChannelRoster two;
    ChannelEdit aIn;aIn.kind=Kind::add;aIn.position=0;
    ChannelEdit aMaster;aMaster.kind=Kind::add;aMaster.position=1;
    aMaster.role=ChannelRole::main;aMaster.format=ProcessingFormat::stereo;
    std::vector<ChannelEdit> twinCreates{aIn,aMaster};
    auto twinIds=two.edit(0,twinCreates);
    assert(twinIds.ok()&&twinIds.createdIds.size()==2);
    ChannelEdit link;link.kind=Kind::addSendReference;
    link.id=twinIds.createdIds[0];link.target=twinIds.createdIds[1];
    link.value=1.f;
    std::array<ChannelEdit,2> twinLinks{link,link};
    assert(two.edit(1,twinLinks).ok());
    DynamicGraphRequest twinReq;
    twinReq.maxFrames=32;twinReq.physicalInputs=1;
    twinReq.sources={{twinIds.createdIds[0],0,-1,false}};
    assert(!DynamicPreparedMixerBlock::compile(two,twinReq,renderer,error));
    assert(error.fault==DynamicGraphFault::duplicateSend);
    assert(renderer.channelCount()==128);
    assert(renderer.process(physical,2,l.data(),r.data(),32,fx));
    // 1024 disconnected channels cost only MAIN scratch, NOT 1024 FX
    // allocations. No fixed ten/twelve-channel implementation limit.
    DynamicChannelRoster big;
    std::vector<ChannelEdit> large;
    for(int i=0;i<1024;++i){
        ChannelEdit op;op.kind=Kind::add;op.position=large.size();
        op.role=i==1023?ChannelRole::main:ChannelRole::input;
        op.format=ProcessingFormat::stereo;
        large.push_back(op);
    }
    assert(big.edit(0,large).ok());
    DynamicGraphRequest zero;
    zero.maxFrames=256;zero.maxScratchBytes=4096;
    assert(DynamicPreparedMixerBlock::compile(big,zero,renderer,error));
    assert(renderer.channelCount()==1024 && renderer.activeChannels()==1);
    assert(renderer.scratchBytes()==2048);
    assert(renderer.process(nullptr,0,l.data(),r.data(),32,fx));
    assert(l[0]==0.f&&r[0]==0.f);
    // Full active graph cannot ignore operator memory admission limit.
    zero.maxScratchBytes=32;
    assert(!DynamicPreparedMixerBlock::compile(big,zero,renderer,error));
    assert(error.fault==DynamicGraphFault::scratchBudget);
    assert(renderer.channelCount()==1024&&renderer.scratchBytes()==2048);
}


static void verifyDynamicDualBusPcm() {
    using Kind=ChannelEdit::Kind;
    DynamicChannelRoster bank;
    // 64 input channels, one MAIN and one independent MONITOR sink.
    // Each ordinary channel uses the SAME stable ChannelId + FX seam.
    std::vector<ChannelEdit> creates;
    for(int i=0;i<66;++i) {
        ChannelEdit e;e.kind=Kind::add;e.position=creates.size();
        e.role=i==64?ChannelRole::main:i==65?ChannelRole::monitor:ChannelRole::input;
        e.format=i>=64?ProcessingFormat::stereo:ProcessingFormat::mono;
        creates.push_back(e);
    }
    const auto added=bank.edit(0,creates);
    assert(added.ok()&&bank.size()==66);
    const auto ids=added.createdIds;
    const auto link=[&](int from,int to,float gain,bool pre=false){
        ChannelEdit e;e.kind=Kind::addSendReference;e.id=ids[from];e.target=ids[to];
        e.value=gain;e.preFader=pre;return e;
    };
    const std::vector<ChannelEdit> connections {
        link(0,64,1.f),link(0,65,.5f),
        link(31,65,1.f), // CH32 is MONITOR-only; do not leak into MAIN.
        link(63,64,1.f)  // CH64 is MAIN-only.
    };
    assert(bank.edit(bank.revision(),connections).ok());
    DynamicGraphRequest req;
    req.physicalInputs=3;req.maxFrames=64;
    req.sources={{ids[0],0,-1,false},{ids[31],1,-1,false},{ids[63],2,-1,false}};
    DynamicPreparedMixerBlock live;
    DynamicGraphError error;
    assert(DynamicPreparedMixerBlock::compile(bank,req,live,error));
    assert(live.channelCount()==66 && live.activeChannels()==5);
    std::array<float,64> a{},b{},d{},mainL{},mainR{},monL{},monR{};
    a.fill(.2f);b.fill(.4f);d.fill(.6f);
    const float* inputs[]{a.data(),b.data(),d.data()};
    int fxCalls=0;
    const auto fx=[&](ChannelId,float*,float*,int) noexcept {++fxCalls;};
    assert(live.process(inputs,3,mainL.data(),mainR.data(),64,fx,
                        monL.data(),monR.data()));
    constexpr float center=.7071067811865475f;
    assert(fxCalls==5); // exactly three channels + two sinks, NO duplicate FX.
    assert(close(mainL[0],(.2f+.6f)*center));
    assert(close(mainR[0],mainL[0]));
    assert(close(monL[0],(.2f*.5f+.4f)*center));
    assert(close(monR[0],monL[0]));
    // Monitor-only session is independent of MAIN mute, including PRE send.
    ChannelEdit muteMain;muteMain.kind=Kind::setMute;muteMain.id=ids[64];
    muteMain.enabled=true;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&muteMain,1)).ok());
    assert(DynamicPreparedMixerBlock::compile(bank,req,live,error));
    assert(live.activeChannels()==3); // CH1, CH32 and MONITOR
    assert(live.process(inputs,3,mainL.data(),mainR.data(),64,fx,
                        monL.data(),monR.data()));
    assert(mainL[0]==0.f && mainR[0]==0.f);
    assert(close(monL[0],(.2f*.5f+.4f)*center));
    // Reordering CH32 must never change the output bus/channel references.
    ChannelEdit move;move.kind=Kind::move;move.id=ids[31];move.position=0;
    assert(bank.edit(bank.revision(),std::span<const ChannelEdit>(&move,1)).ok());
    assert(DynamicPreparedMixerBlock::compile(bank,req,live,error));
    assert(live.process(inputs,3,mainL.data(),mainR.data(),64,fx,
                        monL.data(),monR.data()));
    assert(close(monL[0],(.2f*.5f+.4f)*center));
    // Bounded stereo output aliases must fail CLOSED before writing PCM.
    assert(!live.process(inputs,3,mainL.data(),mainR.data(),64,fx,
                         monL.data(),monL.data()));
    assert(!live.process(inputs,3,mainL.data(),mainR.data(),64,fx));
    // A second MONITOR must reject transactionally: previous live block remains.
    ChannelEdit dupMonitor;dupMonitor.kind=Kind::add;
    dupMonitor.position=bank.size();dupMonitor.role=ChannelRole::monitor;
    const auto newMon=bank.edit(bank.revision(),std::span<const ChannelEdit>(&dupMonitor,1));
    assert(newMon.ok());
    assert(!DynamicPreparedMixerBlock::compile(bank,req,live,error));
    assert(error.fault==DynamicGraphFault::multipleMonitors);
    assert(live.channelCount()==66);
    assert(live.process(inputs,3,mainL.data(),mainR.data(),64,fx,
                        monL.data(),monR.data()));
    assert(close(monL[0],(.2f*.5f+.4f)*center));
    // The MONITOR sink cannot create a feedback path into any ordinary bus.
    DynamicChannelRoster feedback;
    std::vector<ChannelEdit> fCreate(3);
    for(int i=0;i<3;++i) {
        fCreate[i].kind=Kind::add;fCreate[i].position=i;
        fCreate[i].role=i==0?ChannelRole::input:i==1?ChannelRole::main:ChannelRole::monitor;
        fCreate[i].format=i==0?ProcessingFormat::mono:ProcessingFormat::stereo;
    }
    const auto fIds=feedback.edit(0,fCreate);
    assert(fIds.ok());
    ChannelEdit bad;bad.kind=Kind::addSendReference;bad.id=fIds.createdIds[2];
    bad.target=fIds.createdIds[0];bad.value=1.f;
    assert(feedback.edit(1,std::span<const ChannelEdit>(&bad,1)).ok());
    DynamicGraphRequest fReq;fReq.physicalInputs=1;fReq.maxFrames=64;
    fReq.sources={{fIds.createdIds[0],0,-1,false}};
    assert(!DynamicPreparedMixerBlock::compile(feedback,fReq,live,error));
    assert(error.fault==DynamicGraphFault::monitorFeedback);
    assert(live.channelCount()==66);
}

int main() {
    verifyDynamicDualBusPcm();
    verifyDynamicPreparedPcm();
    verifyDynamicChannelRoster();
    Configuration cfg;
    Plan plan;Error err;
    assert(Plan::compile(cfg,2,plan,err));
    assert(plan.activeStrips==0);
    const std::array<float,4> a={1.f,2.f,3.f,4.f};
    const std::array<float,4> b={.25f,.5f,.75f,1.f};
    const std::array<float,4> c={-1.f,-2.f,-3.f,-4.f};
    const float* input[]={a.data(),b.data(),c.data()};
    std::array<float,4> l{},r{},ml{},mr{};
    assert(!plan.extract(0,input,3,l.data(),r.data(),4));
    for(int i=0;i<4;++i)assert(l[i]==0.f&&r[i]==0.f);
    cfg.strips[0]={SourceMode::stereo,0,1,1.f,0.f,false,false};
    cfg.strips[1]={SourceMode::mono,2,-1,1.f,0.f,false,false};
    assert(Plan::compile(cfg,3,plan,err)&&plan.activeStrips==2);
    assert(plan.extract(0,input,3,l.data(),r.data(),4));
    Plan::clearMaster(ml.data(),mr.data(),4);
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),4);
    assert(close(ml[0],1.f)&&close(mr[0],.25f)); // stereo transparency
    assert(plan.extract(1,input,3,l.data(),r.data(),4));
    plan.accumulate(1,l.data(),r.data(),ml.data(),mr.data(),4);
    constexpr float center=.7071067811865475f;
    assert(close(ml[0],1.f-center)&&close(mr[0],.25f-center));
    plan.finishMaster(ml.data(),mr.data(),4);
    // Muting other bus must be independent, never change strip 0.
    cfg.strips[1].mute=true;
    assert(Plan::compile(cfg,3,plan,err)&&plan.activeStrips==1);
    Plan::clearMaster(ml.data(),mr.data(),4);
    assert(!plan.extract(1,input,3,l.data(),r.data(),4));
    assert(plan.extract(0,input,3,l.data(),r.data(),4));
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),4);
    assert(close(ml[3],4.f)&&close(mr[3],1.f));
    // Solo arbitration is determined off RT, and mute wins over solo.
    cfg.strips[1].mute=false;cfg.strips[1].solo=true;
    assert(Plan::compile(cfg,3,plan,err)&&plan.activeStrips==1);
    assert(!plan.extract(0,input,3,l.data(),r.data(),4));
    assert(plan.extract(1,input,3,l.data(),r.data(),4));
    cfg.strips[1].mute=true;
    assert(Plan::compile(cfg,3,plan,err)&&plan.activeStrips==1);
    assert(plan.extract(0,input,3,l.data(),r.data(),4));
    // Stereo balance and true mono master happen after summing.
    cfg.strips[1]={};
    cfg.master={2.f,1.f,false,true};
    assert(Plan::compile(cfg,3,plan,err));
    Plan::clearMaster(ml.data(),mr.data(),4);
    assert(plan.extract(0,input,3,l.data(),r.data(),4));
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),4);
    plan.finishMaster(ml.data(),mr.data(),4);
    assert(close(ml[0],.25f)&&close(mr[0],.25f)); // right-only, doubled, mono
    // Reject illegal hardware routes, NaNs and stereo self-alias; NO partial commit.
    const auto old=plan;
    cfg.strips[2]={SourceMode::stereo,1,9,1.f,0.f,false,false};
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::inputOutOfRange&&err.strip==2);
    assert(plan.activeStrips==old.activeStrips&&plan.masterMono==old.masterMono);
    cfg.strips[2].inputB=1;
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::duplicateStereoInput);
    cfg.strips[2]={};
    cfg.strips[0].pan=std::numeric_limits<float>::quiet_NaN();
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::invalidPan&&err.strip==0);
    cfg.strips[0].pan=0.f;
    cfg.strips[3].source=static_cast<SourceMode>(255);
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::invalidSourceMode&&err.strip==3);
    cfg.strips[3]={};
    cfg.master.gainLinear=10.f;
    assert(!Plan::compile(cfg,3,plan,err)&&err.code==Fault::invalidGain&&err.strip==-1);
    cfg.master.gainLinear=1.f;
    assert(!Plan::compile(cfg,-1,plan,err)&&err.code==Fault::invalidInputCount);
    // 10 independent mono strips, no fixed 2-channel or 1-channel shortcut.
    cfg=Configuration{};
    std::array<std::array<float,1>,stripCount> sources{};
    std::array<const float*,stripCount> pointers{};
    for(int i=0;i<stripCount;++i){
        sources[i][0]=static_cast<float>(i+1);
        pointers[i]=sources[i].data();
        cfg.strips[i]={SourceMode::mono,i,-1,1.f,-1.f,false,false};
    }
    assert(Plan::compile(cfg,stripCount,plan,err)&&plan.activeStrips==stripCount);
    float sumL=0.f,sumR=0.f,cl=0.f,cr=0.f;
    for(int i=0;i<stripCount;++i){
        assert(plan.extract(i,pointers.data(),stripCount,&cl,&cr,1));
        plan.accumulate(i,&cl,&cr,&sumL,&sumR,1);
    }
    assert(close(sumL,55.f)&&close(sumR,0.f));
    // Bad/null device input fails closed and does not leak another strip.
    assert(!plan.extract(9,pointers.data(),2,&cl,&cr,1));
    assert(cl==0.f&&cr==0.f);
}
