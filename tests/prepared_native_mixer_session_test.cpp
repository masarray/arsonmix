// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/PreparedNativeMixerSession.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cmath>
using namespace armix::mixer;
static bool near(float a,float b){return std::abs(a-b)<1.e-5f;}
int main(){
    PreparedNativeMixerSession session;
    ChannelRoutingConfig cfg; SendRequest sends;
    juce::BigInteger in,out;
    // callback[0]=physical2, callback[1]=physical5, callback[2]=physical9
    in.setBit(2);in.setBit(5);in.setBit(9);
    out.setBit(0);out.setBit(1);
    cfg.stripsAndMaster.strips[0]={
      SourceMode::mono,9,-1,1.f,-1.f,false,false};
    cfg.stripsAndMaster.strips[1]={
      SourceMode::stereo,2,5,1.f,0.f,false,false};
    cfg.stripsAndMaster.strips[2]={
      SourceMode::disconnected,-1,-1,1.f,0.f,false,false};
    cfg.formats[2]=ChannelFormat::stereo;
    sends.count=1;
    sends.sends[0]={0,2,SendTap::postFader,0.5f,0.f,true};
    PreparedNativeMixerSession::PrepareResult result;
    auto candidate=PreparedNativeMixerSession::prepare(
        cfg,sends,in,out,41,1,64,result);
    assert(candidate && result.fault==decltype(result.fault)::none);
    assert(session.publish(std::move(candidate),0,41)==
        PreparedNativeMixerSession::PublishResult::applied);
    const std::array<float,3> a{.2f,.2f,.2f};
    const std::array<float,3> b{.4f,.4f,.4f};
    const std::array<float,3> c{.8f,.8f,.8f};
    const float* inputs[]{a.data(),b.data(),c.data()};
    std::array<float,3> l{},r{};
    auto noop=[](int,float*,float*,int) noexcept {};
    RenderLevels meters;
    assert(session.render(inputs,3,l.data(),r.data(),2,3,41,noop,&meters));
    // CH1 hard L .8; CH2 L .2, R .4; CH3 return L .4
    assert(near(l[0],1.4f) && near(r[0],.4f));
    assert(near(meters.channelPeak[0],.8f));
    // Device mismatches: silence, do not reuse stale hardware pointers.
    l.fill(8);r.fill(8);
    assert(!session.render(inputs,3,l.data(),r.data(),2,3,42,noop));
    assert(l[0]==0&&r[0]==0);
    l.fill(8);r.fill(8);
    assert(!session.render(inputs,2,l.data(),r.data(),2,3,41,noop));
    assert(l[0]==0&&r[0]==0);
    l.fill(8);r.fill(8);
    assert(!session.render(inputs,3,l.data(),r.data(),1,3,41,noop));
    assert(l[0]==0&&r[0]==0);

    // Invalid physical source and mutated device topology do not publish.
    cfg.stripsAndMaster.strips[0].inputA=8;
    auto bad=PreparedNativeMixerSession::prepare(
        cfg,sends,in,out,41,2,64,result);
    assert(!bad && result.fault==decltype(result.fault)::graphRejected);
    assert(session.lastRevision()==1);
    cfg.stripsAndMaster.strips[0].inputA=9;
    auto wrongEpoch=PreparedNativeMixerSession::prepare(
        cfg,sends,in,out,42,2,64,result);
    assert(wrongEpoch);
    assert(session.publish(std::move(wrongEpoch),1,41)==
        PreparedNativeMixerSession::PublishResult::staleDevice);
    assert(session.lastRevision()==1);
    auto second=PreparedNativeMixerSession::prepare(
        cfg,sends,in,out,41,2,64,result);
    assert(session.publish(std::move(second),1,41)==
        PreparedNativeMixerSession::PublishResult::applied);
    assert(session.lastRevision()==2);
    assert(session.render(inputs,3,l.data(),r.data(),2,3,41,noop));
    assert(near(l[0],1.4f));
    session.unpublish();
    l.fill(8);r.fill(8);
    assert(!session.render(inputs,3,l.data(),r.data(),2,3,41,noop));
    assert(l[0]==0&&r[0]==0);
    assert(session.pollRetirement());
}
