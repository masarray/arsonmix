// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/ChannelSendPlan.h"
#if defined(NDEBUG)
#undef NDEBUG // Windows Release must execute every acceptance assertion.
#endif
#include <cassert>
#include <array>
#include <cmath>
#include <limits>
using namespace armix::mixer;
static bool close(float x,float y) { return std::abs(x-y)<1.e-6f; }

int main() {
    ChannelSendPlan plan;
    SendRequest cfg;
    SendError err;
    assert(ChannelSendPlan::compile(cfg,plan,err));
    assert(plan.edgeCount==0);
    assert(!plan.receivesInternalAudio(3));
    for(int i=0;i<stripCount;++i) {
        assert(plan.topologicalOrder[i]==i);
        assert(plan.masterEnabled(i));
    }

    // Bus workflow: CH1 -> CH2 pre-fader, CH1 -> CH4 post-fader,
    // CH2 -> CH4 post-fader, CH3 -> CH4 pre-fader.
    cfg.toMaster[0]=false; // sends-only source avoids accidental dry duplication
    cfg.sends[0]={0,1,SendTap::preFader,.5f,0.f,true};
    cfg.sends[1]={0,3,SendTap::postFader,.4f,1.f,true};
    cfg.sends[2]={1,3,SendTap::postFader,1.f,0.f,true};
    cfg.sends[3]={2,3,SendTap::preFader,1.f,0.f,true};
    cfg.count=4;
    assert(ChannelSendPlan::compile(cfg,plan,err));
    assert(plan.edgeCount==4);
    // CH4 may be a pure FX Return with no hardware port, but it is active
    // as an internal-bus recipient. Physical input availability is separate.
    assert(plan.receivesInternalAudio(1));
    assert(plan.receivesInternalAudio(3));
    assert(!plan.receivesInternalAudio(0));
    assert(!plan.receivesInternalAudio(-1));
    assert(!plan.masterEnabled(0)&&plan.masterEnabled(1));
    assert(plan.upstreamOf(0,1));
    assert(plan.upstreamOf(1,3));
    assert(plan.upstreamOf(2,3));
    assert(plan.upstreamOf(0,3)); // transitive 0 -> 1 -> 3
    assert(!plan.upstreamOf(0,2)); // topological order != graph edge
    assert(!plan.upstreamOf(1,2));
    assert(!plan.upstreamOf(3,0));
    assert(!plan.upstreamOf(0,0));
    assert(!plan.upstreamOf(-1,3));
    const std::array<float,3> one {1.f,-.25f,.75f};
    const std::array<float,3> two {.5f,.25f,-.5f};
    std::array<std::array<float,3>,stripCount> l{},r{};
    std::array<float*,stripCount> lp{},rp{};
    for(int i=0;i<stripCount;++i){lp[i]=l[i].data();rp[i]=r[i].data();}
    plan.dispatch(0,SendTap::preFader,one.data(),two.data(),lp.data(),rp.data(),3);
    assert(close(l[1][0],.5f)&&close(r[1][0],.25f));
    assert(close(l[3][0],0.f)&&close(r[3][0],0.f)); // correct tap isolated
    plan.dispatch(0,SendTap::postFader,one.data(),two.data(),lp.data(),rp.data(),3);
    assert(close(l[3][0],0.f)&&close(r[3][0],.2f)); // hard-right pan
    plan.dispatch(1,SendTap::postFader,one.data(),two.data(),lp.data(),rp.data(),3);
    assert(close(l[3][0],1.f)&&close(r[3][0],.7f));
    plan.dispatch(2,SendTap::preFader,one.data(),two.data(),lp.data(),rp.data(),3);
    assert(close(l[3][0],2.f)&&close(r[3][0],1.2f));

    // Transactional validation: failures must NOT corrupt last compiled plan.
    const auto previous=plan;
    cfg.sends[4]={3,0,SendTap::postFader,.5f,0.f,true};
    cfg.count=5;
    assert(!ChannelSendPlan::compile(cfg,plan,err));
    assert(err.fault==SendFault::cycleDetected);
    assert(plan.edgeCount==previous.edgeCount && !plan.masterEnabled(0));
    cfg.sends[4]={0,0,SendTap::preFader,1.f,0.f,true};
    assert(!ChannelSendPlan::compile(cfg,plan,err));
    assert(err.fault==SendFault::selfFeedback && err.sendIndex==4);
    cfg.sends[4]={0,1,SendTap::preFader,.5f,0.f,true};
    assert(!ChannelSendPlan::compile(cfg,plan,err));
    assert(err.fault==SendFault::duplicateSend);
    // Distinct taps between the same channel pair are legal, but explicit.
    cfg.sends[4]={0,1,SendTap::postFader,.5f,0.f,true};
    assert(ChannelSendPlan::compile(cfg,plan,err));
    assert(plan.edgeCount==5);
    cfg.sends[4].tap=static_cast<SendTap>(77);
    assert(!ChannelSendPlan::compile(cfg,plan,err)&&err.fault==SendFault::invalidTap);
    cfg.sends[4].tap=SendTap::postFader;
    cfg.sends[4].gain=std::numeric_limits<float>::infinity();
    assert(!ChannelSendPlan::compile(cfg,plan,err)&&err.fault==SendFault::invalidGain);
    cfg.sends[4].gain=.5f;
    cfg.sends[4].pan=std::numeric_limits<float>::quiet_NaN();
    assert(!ChannelSendPlan::compile(cfg,plan,err)&&err.fault==SendFault::invalidPan);
    cfg.sends[4].pan=0.f;
    cfg.sends[4].destination=stripCount;
    assert(!ChannelSendPlan::compile(cfg,plan,err)&&err.fault==SendFault::invalidStrip);
    cfg.sends[4].destination=1;
    cfg.sends[4].enabled=false;
    assert(ChannelSendPlan::compile(cfg,plan,err));
    assert(plan.edgeCount==4); // disabled edge never processed
    assert(plan.receivesInternalAudio(3));
    cfg.count=maxChannelSends+1;
    assert(!ChannelSendPlan::compile(cfg,plan,err)&&err.fault==SendFault::tooManySends);
    cfg.count=-1;
    assert(!ChannelSendPlan::compile(cfg,plan,err)&&err.fault==SendFault::invalidCount);
    // Fails closed on missing input/output pointers and invalid channel id.
    std::array<float,3> sampleLeft {3.f,3.f,3.f},sampleRight {2.f,2.f,2.f};
    plan.dispatch(-1,SendTap::postFader,sampleLeft.data(),sampleRight.data(),lp.data(),rp.data(),3);
    plan.dispatch(99,SendTap::postFader,sampleLeft.data(),sampleRight.data(),lp.data(),rp.data(),3);
    plan.dispatch(0,SendTap::preFader,nullptr,sampleRight.data(),lp.data(),rp.data(),3);

    // Full 10-strip forward sends graph; source is processed before target.
    cfg=SendRequest{};
    cfg.count=stripCount-1;
    for(int i=0;i<stripCount-1;++i)
        cfg.sends[i]={i,i+1,SendTap::postFader,1.f,0.f,true};
    assert(ChannelSendPlan::compile(cfg,plan,err));
    for(int i=0;i<stripCount;++i)assert(plan.topologicalOrder[i]==i);
    assert(plan.upstreamOf(0,9) && !plan.upstreamOf(9,0));
    cfg.sends[cfg.count++]={9,0,SendTap::postFader,1.f,0.f,true};
    assert(!ChannelSendPlan::compile(cfg,plan,err)&&err.fault==SendFault::cycleDetected);

    // MS2A native PRE-send source format: mono uses an equal-power
    // projection, stereo remains independent BALANCE with no crossfeed.
    // POST remains pre-existing balance until mono post-pan is separated.
    cfg=SendRequest{};
    cfg.count=1;
    cfg.sends[0]={0,1,SendTap::preFader,1.f,0.f,true};
    std::array<ChannelFormat,stripCount> formats{};
    formats.fill(ChannelFormat::stereo);
    formats[0]=ChannelFormat::mono;
    assert(ChannelSendPlan::compile(cfg,plan,err,formats));
    assert(close(plan.orderedEdges[0].leftGain,std::sqrt(.5f)));
    assert(close(plan.orderedEdges[0].rightGain,std::sqrt(.5f)));
    cfg.sends[0].pan=-1.f;
    assert(ChannelSendPlan::compile(cfg,plan,err,formats));
    assert(close(plan.orderedEdges[0].leftGain,1.f));
    assert(close(plan.orderedEdges[0].rightGain,0.f));
    cfg.sends[0].pan=1.f;
    assert(ChannelSendPlan::compile(cfg,plan,err,formats));
    assert(close(plan.orderedEdges[0].leftGain,0.f));
    assert(close(plan.orderedEdges[0].rightGain,1.f));
    cfg.sends[0].pan=0.f;
    formats[0]=ChannelFormat::stereo;
    assert(ChannelSendPlan::compile(cfg,plan,err,formats));
    assert(close(plan.orderedEdges[0].leftGain,1.f));
    assert(close(plan.orderedEdges[0].rightGain,1.f));
    formats[0]=ChannelFormat::mono;
    cfg.sends[0].tap=SendTap::postFader;
    assert(ChannelSendPlan::compile(cfg,plan,err,formats));
    assert(close(plan.orderedEdges[0].leftGain,1.f));
    assert(close(plan.orderedEdges[0].rightGain,1.f));
    const auto acceptedFormatPlan=plan;
    formats[0]=ChannelFormat::followSource; // not a resolved graph format
    assert(!ChannelSendPlan::compile(cfg,plan,err,formats));
    assert(err.fault==SendFault::invalidSourceFormat);
    assert(plan.orderedEdges[0].leftGain==acceptedFormatPlan.orderedEdges[0].leftGain);
    formats[0]=ChannelFormat::mono;
    assert(!ChannelSendPlan::compile(cfg,plan,err,std::span<const ChannelFormat>(formats.data(),1)));
    assert(err.fault==SendFault::invalidSourceFormat);

    // Cross-module regression: fader=-infinity MUST NOT gate PRE-FADER sends.
    // Raw source access and post-fader/master activity are independent.
    ChannelRoutingConfig physical;
    physical.stripsAndMaster.strips[0]={SourceMode::mono,0,-1,0.f,0.f,false,false};
    ChannelRoutingPlan sourcePlan;
    LayoutError layoutError;
    constexpr std::array<int,1> enabledPort{0};
    assert(ChannelRoutingPlan::compile(physical,enabledPort,sourcePlan,layoutError));
    assert(!sourcePlan.base.strips[0].active);
    const std::array<float,2> mic {.8f,-.4f};
    const float* capture[]={mic.data()};
    std::array<float,2> rawL{},rawR{},postL{},postR{},mixL{},mixR{};
    assert(sourcePlan.extractSource(0,capture,1,rawL.data(),rawR.data(),2));
    assert(close(rawL[0],.8f)&&close(rawR[0],.8f));
    assert(!sourcePlan.extract(0,capture,1,postL.data(),postR.data(),2));
    assert(close(postL[0],0.f)&&close(postR[0],0.f));
    sourcePlan.accumulate(0,rawL.data(),rawR.data(),mixL.data(),mixR.data(),2);
    assert(close(mixL[0],0.f)&&close(mixR[0],0.f));

    SendRequest preOnly;
    preOnly.count=1;
    preOnly.toMaster[0]=false;
    preOnly.sends[0]={0,1,SendTap::preFader,1.f,0.f,true};
    ChannelSendPlan prePlan;
    assert(ChannelSendPlan::compile(preOnly,prePlan,err));
    std::array<std::array<float,2>,stripCount> recvL{},recvR{};
    std::array<float*,stripCount> recvLp{},recvRp{};
    for(int i=0;i<stripCount;++i) {
        recvLp[i]=recvL[i].data();recvRp[i]=recvR[i].data();
    }
    prePlan.dispatch(0,SendTap::preFader,rawL.data(),rawR.data(),
                     recvLp.data(),recvRp.data(),2);
    assert(close(recvL[1][0],.8f)&&close(recvR[1][0],.8f));
    prePlan.dispatch(0,SendTap::postFader,postL.data(),postR.data(),
                     recvLp.data(),recvRp.data(),2);
    assert(close(recvL[1][0],.8f)&&close(recvR[1][0],.8f));

    // Pre-send mute/solo policy must be deliberately applied by its caller.
    // Even a muted source retains raw physical input; MASTER stays quiet.
    physical.stripsAndMaster.strips[0].mute=true;
    assert(ChannelRoutingPlan::compile(physical,enabledPort,sourcePlan,layoutError));
    assert(sourcePlan.extractSource(0,capture,1,rawL.data(),rawR.data(),2));
    assert(!sourcePlan.extract(0,capture,1,postL.data(),postR.data(),2));

    // Source stereo folded into MONO even with a zero fader (before FX/send).
    constexpr std::array<int,2> stereoPorts{0,1};
    physical.stripsAndMaster.strips[0]={SourceMode::stereo,0,1,0.f,0.f,false,false};
    physical.formats[0]=ChannelFormat::mono;
    assert(ChannelRoutingPlan::compile(physical,stereoPorts,sourcePlan,layoutError));
    const std::array<float,2> side {-.2f,.4f};
    const float* stereoCapture[]={mic.data(),side.data()};
    assert(sourcePlan.extractSource(0,stereoCapture,2,rawL.data(),rawR.data(),2));
    assert(close(rawL[0],.3f)&&close(rawR[0],.3f));
    assert(!sourcePlan.extract(0,stereoCapture,2,postL.data(),postR.data(),2));
}
