// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/PreparedMixerFlow.h"
#ifdef NDEBUG
#undef NDEBUG // Release CTest MUST run assertions.
#endif
#include <cassert>
#include <array>
#include <cmath>
#include <limits>

using namespace armix::mixer;
static bool eq(float a,float b,float eps=1.e-5f){return std::abs(a-b)<eps;}
static constexpr int frames=4;
static constexpr std::array<int,1> port0={0};
using Flow=PreparedMixerFlow;
using Work=Flow::Workspace<frames>;
static constexpr auto dry=[](int,ChannelFormat,float*,float*,int) noexcept {};

int main() {
    Flow flow; FlowError error;Work work;
    ChannelRoutingConfig cfg; SendRequest request;
    std::array<float,frames> in0={1.f,-.5f,.25f,.75f};
    const float* capture[]={in0.data()};
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(flow.render(capture,1,frames,work,dry));
    for(int i=0;i<frames;++i)assert(eq(work.masterL[i],0)&&eq(work.masterR[i],0));

    // PRE-FADER source at -infinity can drive a physical-input-free FX RETURN.
    cfg.stripsAndMaster.strips[0]={SourceMode::mono,0,-1,0.f,0.f,false,false};
    cfg.formats[2]=ChannelFormat::stereo; // empty physical bus has explicit width
    request.toMaster[0]=false;
    request.count=1;
    request.sends[0]={0,2,SendTap::preFader,1.f,0.f,true};
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(!flow.mix[0].audible&&flow.mix[0].canSendPre);
    assert(flow.mix[2].audible); // internal bus return even without physical input
    assert(flow.render(capture,1,frames,work,dry));
    for(int i=0;i<frames;++i) {
        assert(eq(work.masterL[i],in0[i]));
        assert(eq(work.masterR[i],in0[i]));
    }

    // PRE is after FX but before FADER: mocked plugin multiplies pre-tap by 2.
    const auto doubleFirst=[](int strip,ChannelFormat,float* l,float* r,int n) noexcept {
        if(strip!=0)return;
        for(int i=0;i<n;++i){l[i]*=2.f;r[i]*=2.f;}
    };
    assert(flow.render(capture,1,frames,work,doubleFirst));
    assert(eq(work.masterL[0],2.f)&&eq(work.masterR[0],2.f));

    // POST-FADER sends cannot carry a channel with a zero fader.
    request.sends[0].tap=SendTap::postFader;
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],0.f)&&eq(work.masterR[0],0.f));

    // PRE respects mute/solo policy by explicit host rule, not by accident.
    request.sends[0].tap=SendTap::preFader;
    cfg.stripsAndMaster.strips[0].mute=true;
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(!flow.mix[0].canSendPre);
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],0.f)&&eq(work.masterR[0],0.f));

    // Two-hop topological bus: CH1 -> CH2 -> CH3 -> MASTER (no duplicate dry).
    cfg.stripsAndMaster.strips[0]={SourceMode::mono,0,-1,1.f,-1.f,false,false};
    cfg.formats[0]=ChannelFormat::mono;
    cfg.formats[1]=ChannelFormat::stereo;
    request.count=2;request.toMaster[1]=false;
    request.sends[0]={0,1,SendTap::postFader,1.f,0.f,true};
    request.sends[1]={1,2,SendTap::postFader,1.f,0.f,true};
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],1.f)&&eq(work.masterR[0],0.f));

    // Solo BUS RETURN: non-solo physical CH is silent, but raw input can be
    // captured for analysis/FX while post-fader and pre-fader sends are gated.
    cfg.stripsAndMaster.strips[2].solo=true;
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(!flow.mix[0].canSendPre && !flow.mix[0].audible);
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],0.f));

    // Sparse physical input identity: hardware ports 3,9 appear in callback
    // order [3,9], while stereo mapping requests [9,3].
    cfg=ChannelRoutingConfig{};request=SendRequest{};
    cfg.stripsAndMaster.strips[0]={SourceMode::stereo,9,3,1.f,0.f,false,false};
    cfg.formats[0]=ChannelFormat::stereo;
    constexpr std::array<int,2> sparsePorts={3,9};
    std::array<float,frames> left={.2f,.4f,.6f,.8f};
    std::array<float,frames> right={.9f,.7f,.5f,.3f};
    const float* sparseCapture[]={left.data(),right.data()};
    assert(Flow::compile(cfg,sparsePorts,request,flow,error));
    assert(flow.render(sparseCapture,2,frames,work,dry));
    assert(eq(work.masterL[0],right[0]) && eq(work.masterR[0],left[0]));

    // Mono physical source explicitly promoted to stereo MUST have unity
    // center balance, not mono -3dB applied to both replicated channels.
    cfg=ChannelRoutingConfig{};cfg.stripsAndMaster.strips[0]=
       {SourceMode::mono,0,-1,1.f,0.f,false,false};
    cfg.formats[0]=ChannelFormat::stereo;
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],1.f) && eq(work.masterR[0],1.f));
    cfg.formats[0]=ChannelFormat::mono;
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(flow.render(capture,1,frames,work,dry));
    constexpr float center=.7071067811865475f;
    assert(eq(work.masterL[0],center)&&eq(work.masterR[0],center));

    // Upstream fanout, then fan-in into empty return bus, all destinations
    // initialized exactly once per block (no leaked previous audio block).
    request=SendRequest{};
    cfg.formats[1]=ChannelFormat::stereo;
    cfg.formats[2]=ChannelFormat::stereo;
    request.count=2;request.toMaster[0]=false;
    request.sends[0]={0,1,SendTap::preFader,.25f,0.f,true};
    request.sends[1]={0,2,SendTap::preFader,.5f,0.f,true};
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],.75f)&&eq(work.masterR[0],.75f));
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],.75f)&&eq(work.masterR[0],.75f));

    // Invalid DAG/source compile leaves last good entire P1D plan intact.
    const Flow previous=flow;
    request.sends[1].destination=0;
    assert(!Flow::compile(cfg,port0,request,flow,error));
    assert(error.fault==FlowFault::sendTopology);
    assert(flow.sends.edgeCount==previous.sends.edgeCount);
    request.sends[1].destination=2;
    cfg.stripsAndMaster.strips[0].inputA=77;
    assert(!Flow::compile(cfg,port0,request,flow,error));
    assert(error.fault==FlowFault::inputLayout);
    assert(flow.sends.edgeCount==previous.sends.edgeCount);
    cfg.stripsAndMaster.strips[0].inputA=0;

    // Fail closed when requested block exceeds preallocated memory.
    work.masterL[0]=123.f;
    assert(!flow.render(capture,1,frames+1,work,dry));
    assert(work.masterL[0]==123.f);
    assert(!flow.render(capture,257,frames,work,dry));

    // MASTER mono fold, gain and mute must work with internal routing too.
    request=SendRequest{};
    cfg.stripsAndMaster.master={2.f,1.f,false,true};
    cfg.formats[0]=ChannelFormat::stereo;
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],1.f)&&eq(work.masterR[0],1.f));
    cfg.stripsAndMaster.master.mute=true;
    assert(Flow::compile(cfg,port0,request,flow,error));
    assert(flow.render(capture,1,frames,work,dry));
    assert(eq(work.masterL[0],0.f)&&eq(work.masterR[0],0.f));
}
