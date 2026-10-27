// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/ChannelRoutingContract.h"
#include <array>
#if defined(NDEBUG)
#undef NDEBUG // Assertions are test execution, even in Windows Release.
#endif
#include <cassert>
#include <cmath>
#include <limits>
#include <span>
using namespace armix::mixer;
static bool approx(float a, float b) { return std::abs(a-b) <= 0.00001f; }
int main() {
    // Sparse physical ports ordered as the callback really delivers them.
    // callback[0] = physical 5, callback[1] = physical 2,
    // callback[2] = physical 7. Do not assume ordinal == hardware port.
    constexpr std::array enabled={5,2,7};
    ChannelRoutingConfig cfg;
    cfg.stripsAndMaster.strips[0]={SourceMode::mono,2,-1,1.f,0.f,false,false};
    cfg.stripsAndMaster.strips[1]={SourceMode::stereo,5,7,1.f,0.f,false,false};
    ChannelRoutingPlan plan; LayoutError error;
    assert(ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    assert(plan.inputPorts.callbackFor(2)==1);
    assert(plan.inputPorts.callbackFor(5)==0);
    assert(plan.inputPorts.callbackFor(7)==2);
    assert(plan.inputPorts.callbackFor(0)==-1);
    std::array<float,3> p5={0.8f,-0.8f,0.2f};
    std::array<float,3> p2={0.2f,0.4f,0.6f};
    std::array<float,3> p7={0.4f,-0.4f,0.6f};
    const float* callback[]={p5.data(),p2.data(),p7.data()};
    std::array<float,3> l{},r{},ml{},mr{};
    assert(plan.extract(0,callback,3,l.data(),r.data(),3));
    assert(approx(l[0],0.2f)&&approx(r[0],0.2f));
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),3);
    assert(approx(ml[0],.2f*.70710678f)&&approx(mr[0],.2f*.70710678f));
    assert(plan.extract(1,callback,3,l.data(),r.data(),3));
    assert(approx(l[0],.8f)&&approx(r[0],.4f));
    plan.accumulate(1,l.data(),r.data(),ml.data(),mr.data(),3);
    assert(approx(ml[0],.8f+.2f*.70710678f)&&approx(mr[0],.4f+.2f*.70710678f));

    // Source stereo -> channel MONO: pre-FX calibrated 0.5(L+R),
    // then equal-power center pan. Side cancellation is deterministic.
    cfg.formats[1]=ChannelFormat::mono;
    assert(ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    assert(plan.effectiveFormats[1]==ChannelFormat::mono);
    assert(plan.foldStereoToMono[1]);
    assert(plan.extract(1,callback,3,l.data(),r.data(),3));
    assert(approx(l[0],.6f)&&approx(r[0],.6f));
    assert(approx(l[1],-.6f)&&approx(r[1],-.6f));
    Plan::clearMaster(ml.data(),mr.data(),3);
    plan.accumulate(1,l.data(),r.data(),ml.data(),mr.data(),3);
    assert(approx(ml[0],.6f*.70710678f)&&approx(mr[0],ml[0]));

    // Source MONO -> channel STEREO: dual mono, no invented width and
    // no +3dB jump compared with original equal-power mono center.
    cfg.formats[0]=ChannelFormat::stereo;
    assert(ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    assert(plan.extract(0,callback,3,l.data(),r.data(),3));
    Plan::clearMaster(ml.data(),mr.data(),3);
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),3);
    assert(approx(ml[0],.2f*.70710678f)&&approx(mr[0],.2f*.70710678f));

    // Mono source keeps equal-power pan even with STEREO channel format.
    // At hard right, level is unity and never unintentionally -3dB.
    cfg.stripsAndMaster.strips[0].pan=1.f;
    assert(ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    Plan::clearMaster(ml.data(),mr.data(),3);
    assert(plan.extract(0,callback,3,l.data(),r.data(),3));
    plan.accumulate(0,l.data(),r.data(),ml.data(),mr.data(),3);
    assert(approx(ml[0],0.f)&&approx(mr[0],.2f));
    cfg.stripsAndMaster.strips[0].pan=0.f;
    assert(ChannelRoutingPlan::compile(cfg,enabled,plan,error));

    // Physical mapping rejected transactionally: plan stays bit-exact.
    const auto old=plan;
    cfg.stripsAndMaster.strips[0].inputA=6;
    assert(!ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    assert(error.fault==LayoutFault::unavailableSourcePort && error.strip==0 && error.physicalPort==6);
    assert(plan.base.strips[0].inputA==old.base.strips[0].inputA);
    cfg.stripsAndMaster.strips[0].inputA=2;
    cfg.formats[0]=static_cast<ChannelFormat>(99);
    assert(!ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    assert(error.fault==LayoutFault::invalidChannelFormat && error.strip==0);
    cfg.formats[0]=ChannelFormat::stereo;
    constexpr std::array duplicates={5,2,5};
    assert(!ChannelRoutingPlan::compile(cfg,duplicates,plan,error));
    assert(error.fault==LayoutFault::duplicateEnabledPort && error.physicalPort==5);
    constexpr std::array outside={5,256,7};
    assert(!ChannelRoutingPlan::compile(cfg,outside,plan,error));
    assert(error.fault==LayoutFault::invalidPhysicalPort && error.physicalPort==256);
    assert(plan.base.strips[0].inputA==old.base.strips[0].inputA);

    // Stereo L and R may be nonadjacent in *physical* space, but
    // L/R self alias is invalid (fail-closed, not dual-mono by accident).
    cfg.stripsAndMaster.strips[1].inputB=5;
    assert(!ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    assert(error.fault==LayoutFault::basePlanRejected);
    assert(error.baseError.code==Fault::duplicateStereoInput);
    cfg.stripsAndMaster.strips[1].inputB=7;

    // 10 channels share 1 input safely: fanout but no extra device pin.
    cfg={};
    for(auto& s:cfg.stripsAndMaster.strips) s={SourceMode::mono,2,-1,1.f,0.f,false,false};
    assert(ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    assert(plan.base.activeStrips==10);
    for(int i=0;i<stripCount;++i){
        assert(plan.extract(i,callback,3,l.data(),r.data(),3));
        assert(approx(l[0],.2f));
    }
    // Invalid NaN propagates through existing Plan validation and rolls back.
    cfg.stripsAndMaster.strips[9].faderLinear=std::numeric_limits<float>::quiet_NaN();
    assert(!ChannelRoutingPlan::compile(cfg,enabled,plan,error));
    assert(error.fault==LayoutFault::basePlanRejected && error.baseError.strip==9);
}
