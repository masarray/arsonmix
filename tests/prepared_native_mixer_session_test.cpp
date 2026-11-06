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
// Real prepared native DSP kernel: deterministic multichannel PCM through
// the existing production mixer/publisher, NOT browser-mock or hardware FAT.
static void multiChannelGoldenPcm() {
    PreparedNativeMixerSession native;
    ChannelRoutingConfig cfg;SendRequest sends;
    juce::BigInteger inputsMask,outputsMask;
    for(int pin=0;pin<3;++pin)inputsMask.setBit(pin);
    outputsMask.setBit(0);outputsMask.setBit(1);
    cfg.stripsAndMaster.strips[0]={SourceMode::mono,0,-1,.8f,-1.f,false,false};
    cfg.stripsAndMaster.strips[4]={SourceMode::stereo,1,2,.5f,0.f,false,false};
    cfg.stripsAndMaster.strips[9]={SourceMode::mono,2,-1,.25f,1.f,false,false};
    cfg.stripsAndMaster.master.gainLinear=.5f;
    std::uint64_t revision=0;
    auto publish=[&](){
        PreparedNativeMixerSession::PrepareResult result;
        auto candidate=PreparedNativeMixerSession::prepare(
            cfg,sends,inputsMask,outputsMask,77,revision+1,64,result);
        assert(candidate);
        assert(native.publish(std::move(candidate),revision,77)==
               PreparedNativeMixerSession::PublishResult::applied);
        ++revision;
        (void)native.pollRetirement();
    };
    const std::array<float,8> a{.1f,.1f,.1f,.1f,.1f,.1f,.1f,.1f};
    const std::array<float,8> b{.2f,.2f,.2f,.2f,.2f,.2f,.2f,.2f};
    const std::array<float,8> c{.4f,.4f,.4f,.4f,.4f,.4f,.4f,.4f};
    const float* inputs[]{a.data(),b.data(),c.data()};
    std::array<float,8> l{},r{};
    auto noop=[](int,float*,float*,int) noexcept {};
    auto check=[&](float left,float right){
        assert(native.render(inputs,3,l.data(),r.data(),2,8,77,noop));
        for(int i=0;i<8;++i){assert(near(l[i],left));assert(near(r[i],right));}
    };
    publish();check(.09f,.15f); // CH01+CH05+CH10, true mono/stereo PCM
    cfg.stripsAndMaster.strips[4].solo=true;
    publish();check(.05f,.10f); // native SOLO excludes both other inputs
    cfg.stripsAndMaster.strips[4].solo=false;
    cfg.stripsAndMaster.strips[9].mute=true;
    publish();check(.09f,.10f); // native per-lane MUTE
    cfg.stripsAndMaster.master.mute=true;
    publish();check(0.f,0.f); // real PCM post-mixer MAIN mute
    auto invalid=cfg;
    invalid.stripsAndMaster.strips[0].inputA=7;
    PreparedNativeMixerSession::PrepareResult result;
    auto rejected=PreparedNativeMixerSession::prepare(
        invalid,sends,inputsMask,outputsMask,77,revision+1,64,result);
    assert(!rejected);check(0.f,0.f); // invalid pin never replaces graph
    cfg.stripsAndMaster.master.mute=false;
    cfg.stripsAndMaster.strips[9].mute=false;
    cfg.stripsAndMaster.strips[0].inputA=2;
    publish();check(.21f,.15f); // source switch, retained lane controls
    // Production PCM regression: connected input -> NONE -> same input.
    // Every control/other channel stays attached; silence never changes
    // physical MAIN authorization, which belongs to LiveAudioEngine.
    const auto ch1=cfg.stripsAndMaster.strips[0];
    const auto ch5=cfg.stripsAndMaster.strips[4];
    const auto ch10=cfg.stripsAndMaster.strips[9];
    cfg.stripsAndMaster.strips[0].source=SourceMode::disconnected;
    cfg.stripsAndMaster.strips[0].inputA=-1;
    cfg.stripsAndMaster.strips[0].inputB=-1;
    publish();check(.05f,.15f); // CH5+CH10 still sound
    cfg.stripsAndMaster.strips[0]=ch1;
    publish();check(.21f,.15f); // CH1 resumes without resetting gain
    cfg.stripsAndMaster.strips[4].source=SourceMode::disconnected;
    cfg.stripsAndMaster.strips[4].inputA=-1;
    cfg.stripsAndMaster.strips[4].inputB=-1;
    publish();check(.16f,.05f); // CH5 stereo->NONE, others still sound
    cfg.stripsAndMaster.strips[4]=ch5;
    publish();check(.21f,.15f); // CH5 stereo resumes
    cfg.stripsAndMaster.strips[9].source=SourceMode::disconnected;
    cfg.stripsAndMaster.strips[9].inputA=-1;
    cfg.stripsAndMaster.strips[9].inputB=-1;
    publish();check(.21f,.10f); // CH10 mono->NONE
    cfg.stripsAndMaster.strips[9]=ch10;
    publish();check(.21f,.15f); // CH10 mono resumes
    // An explicitly muted MAIN remains silent through both graph edits.
    cfg.stripsAndMaster.master.mute=true;
    cfg.stripsAndMaster.strips[4].source=SourceMode::disconnected;
    cfg.stripsAndMaster.strips[4].inputA=-1;
    cfg.stripsAndMaster.strips[4].inputB=-1;
    publish();check(0.f,0.f);
    cfg.stripsAndMaster.strips[4]=ch5;
    publish();check(0.f,0.f);
    cfg.stripsAndMaster.master.mute=false;
    publish();check(.21f,.15f);
    auto stale=PreparedNativeMixerSession::prepare(
        cfg,sends,inputsMask,outputsMask,77,revision+1,64,result);
    assert(stale);
    assert(native.publish(std::move(stale),revision-1,77)!=
           PreparedNativeMixerSession::PublishResult::applied);
    check(.21f,.15f); // rejected stale revision cannot change PCM
    cfg.stripsAndMaster.strips[3]=cfg.stripsAndMaster.strips[0];
    cfg.stripsAndMaster.strips[0]=StripConfig{};
    publish();check(.21f,.15f); // lane reorder preserves actual routing PCM
    l.fill(8.f);r.fill(8.f);
    assert(!native.render(inputs,3,l.data(),r.data(),2,8,78,noop));
    assert(l[0]==0.f&&r[0]==0.f); // device hotplug epoch fails silent
    native.unpublish();
}
int main(){
    multiChannelGoldenPcm();
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
