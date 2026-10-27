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
    cfg.sends[cfg.count++]={9,0,SendTap::postFader,1.f,0.f,true};
    assert(!ChannelSendPlan::compile(cfg,plan,err)&&err.fault==SendFault::cycleDetected);
}
