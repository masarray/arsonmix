// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/PreparedMixerBlock.h"
#include "../engine/core/MasterOutputMonoPolicy.h"
#if defined(NDEBUG)
#undef NDEBUG // P1D must run assertions even under MSVC Release.
#endif
#include <cassert>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <limits>
#include <new>
using namespace armix::mixer;

static std::atomic<std::size_t> allocations {0};
// Count realtime allocations, including foreign DSP hooks if accidentally added.
void* operator new(std::size_t n) {
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n==0?1:n))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

static bool near(float a,float b,float d=0.00002f){return std::abs(a-b)<d;}
int main() {
    constexpr std::array physicalPorts {3,0,4,7};
    ChannelRoutingConfig channels;
    // Hardware mono CH1, hardware stereo CH2, BUS RETURN CH3/CH4.
    channels.stripsAndMaster.strips[0]={SourceMode::mono,3,-1,.5f,0.f,false,false};
    channels.stripsAndMaster.strips[1]={SourceMode::stereo,4,7,1.f,0.f,false,false};
    channels.stripsAndMaster.strips[2]={SourceMode::disconnected,-1,-1,1.f,0.f,false,false};
    channels.stripsAndMaster.strips[3]={SourceMode::disconnected,-1,-1,1.f,0.f,false,false};
    channels.formats[2]=ChannelFormat::stereo;
    channels.formats[3]=ChannelFormat::stereo;
    SendRequest sends;
    sends.toMaster[0]=false;
    sends.count=2;
    sends.sends[0]={0,3,SendTap::preFader,.5f,0.f,true};
    sends.sends[1]={0,2,SendTap::postFader,.25f,0.f,true};
    PreparedRenderGraph graph; RenderError error;
    assert(PreparedRenderGraph::compile(channels,physicalPorts,sends,graph,error));
    assert(graph.scheduled[0] && graph.scheduled[1]);
    assert(graph.scheduled[2] && graph.scheduled[3]); // real audio, no hardware
    assert(graph.sends.topologicalOrder[0]==0);
    assert(graph.channels.inputPorts.callbackFor(3)==0);
    assert(graph.channels.inputPorts.callbackFor(4)==2);
    PreparedMixerBlock mixer(graph,8);
    assert(mixer.capacity()==8);
    const std::array<float,4> mono {.4f,.4f,.4f,.4f};
    const std::array<float,4> unrelated {.7f,.7f,.7f,.7f};
    const std::array<float,4> musicL {.2f,.2f,.2f,.2f};
    const std::array<float,4> musicR {.6f,.6f,.6f,.6f};
    const float* audio[]={mono.data(),unrelated.data(),musicL.data(),musicR.data()};
    std::array<float,9> outputL{}, outputR{};
    RenderLevels levels;
    int chainCalls=0; std::array<int,11> visited{};
    auto fx = [&](int strip,float* l,float* r,int frames) noexcept {
        visited[chainCalls++]=strip;
        if(strip==0) {for(int i=0;i<frames;++i){l[i]*=2.f;r[i]*=2.f;}}
        if(strip==2) {for(int i=0;i<frames;++i){l[i]*=2.f;r[i]*=2.f;}}
        if(strip==3) {for(int i=0;i<frames;++i)r[i]*=.5f;}
    };
    const auto before=allocations.load(std::memory_order_relaxed);
    assert(mixer.process(audio,4,outputL.data(),outputR.data(),4,fx,&levels));
    assert(allocations.load(std::memory_order_relaxed)==before); // No RT heap
    constexpr float monoPost=.4f*2.f*.5f*.70710678118f;
    assert(near(outputL[0],.2f+monoPost*.25f*2.f+.4f));
    assert(near(outputR[0],.6f+monoPost*.25f*2.f+.2f));
    assert(near(levels.channelPeak[0],monoPost));
    assert(near(levels.channelPeak[2],monoPost*.25f*2.f));
    assert(near(levels.channelPeak[3],.4f));
    assert(near(levels.masterPeakL,outputL[0]));
    assert(near(levels.masterPeakR,outputR[0]));
    assert(chainCalls==5 && visited[0]==0 && visited[1]==1 &&
           visited[2]==2 && visited[3]==3 && visited[4]==-1);

    // SOURCE fader=0 is not an input-disconnect: pre-fader FX return STILL
    // receives signal; post-fader send stays silent; music remains audible.
    channels.stripsAndMaster.strips[0].faderLinear=0.f;
    assert(PreparedRenderGraph::compile(channels,physicalPorts,sends,graph,error));
    PreparedMixerBlock faderZero(graph,8);
    auto noFx = [](int,float*,float*,int) noexcept {};
    assert(faderZero.process(audio,4,outputL.data(),outputR.data(),4,noFx));
    // pre send .4*.5=.2; music .2/.6; CH3 and CH4 order-independent
    assert(near(outputL[0],.4f)&&near(outputR[0],.8f));

    // Duplicate L/R callback port, out-of-range physical input, feedback
    // self-loop, and send-cycle all reject atomically.
    const auto prior=graph;
    channels.stripsAndMaster.strips[1].inputB=8;
    assert(!PreparedRenderGraph::compile(channels,physicalPorts,sends,graph,error));
    assert(error.fault==RenderFault::invalidChannelRouting);
    assert(graph.channels.inputPorts.enabledCount==prior.channels.inputPorts.enabledCount);
    channels.stripsAndMaster.strips[1].inputB=7;
    sends.sends[2]={3,0,SendTap::postFader,1.f,0.f,true};
    sends.count=3;
    assert(!PreparedRenderGraph::compile(channels,physicalPorts,sends,graph,error));
    assert(error.fault==RenderFault::invalidSendRouting);
    assert(error.sends.fault==SendFault::cycleDetected);
    assert(graph.sends.edgeCount==prior.sends.edgeCount);
    sends.count=2;

    // SOLO source: keep its downstream returns, but suppress unrelated
    // physical music. A serial second solo test would additionally select
    // upstream ancestors of the returned bus.
    channels.stripsAndMaster.strips[0].faderLinear=.5f;
    channels.stripsAndMaster.strips[0].solo=true;
    assert(PreparedRenderGraph::compile(channels,physicalPorts,sends,graph,error));
    assert(graph.scheduled[0] && graph.scheduled[2] && graph.scheduled[3]);
    assert(!graph.scheduled[1]);
    PreparedMixerBlock soloMix(graph,8);
    assert(soloMix.process(audio,4,outputL.data(),outputR.data(),4,noFx));
    assert(near(outputL[0],.4f*.5f + .4f*.5f*.70710678f*.25f));
    assert(near(outputR[0],outputL[0]));
    // Muting solo source also removes the solo arbitration: music audible.
    channels.stripsAndMaster.strips[0].mute=true;
    assert(PreparedRenderGraph::compile(channels,physicalPorts,sends,graph,error));
    assert(graph.scheduled[1] && !graph.scheduled[0]);
    PreparedMixerBlock muted(graph,8);
    assert(muted.process(audio,4,outputL.data(),outputR.data(),4,noFx));
    assert(near(outputL[0],.2f)&&near(outputR[0],.6f));

    // A stereo hardware source on an explicitly MONO channel is folded
    // before plugin invocation. Mono panning remains balanced.
    channels={};
    sends={};
    channels.stripsAndMaster.strips[0]={SourceMode::stereo,4,7,1.f,0.f,false,false};
    channels.formats[0]=ChannelFormat::mono;
    assert(PreparedRenderGraph::compile(channels,physicalPorts,sends,graph,error));
    PreparedMixerBlock fold(graph,8);
    assert(fold.process(audio,4,outputL.data(),outputR.data(),4,noFx));
    assert(near(outputL[0],.4f*.70710678f));
    assert(near(outputR[0],.4f*.70710678f));

    // An invalid callback quantum fails closed and clears both master outputs.
    outputL.fill(999.f);outputR.fill(999.f);
    assert(!fold.process(audio,4,outputL.data(),outputR.data(),9,noFx));
    for(int i=0;i<9;++i)assert(outputL[i]==0.f&&outputR[i]==0.f);

    // Real master output policy: mute primes full mono; live stereo <-> mono
    // ramps without allocating or adding an output frame of latency.
    armix::dsp::MasterOutputMonoPolicy monoPolicy;
    float monoL=.3f,monoR=.7f;
    monoPolicy.primeWhileMuted(true);
    const auto monoAlloc=allocations.load(std::memory_order_relaxed);
    monoPolicy.process(true,monoL,monoR);
    assert(monoL==monoR && near(monoL,.5f));
    assert(allocations.load(std::memory_order_relaxed)==monoAlloc);
    for(int n=0;n<64;++n){monoL=.3f;monoR=.7f;monoPolicy.process(false,monoL,monoR);}
    assert(monoPolicy.blend()==0.f && near(monoL,.3f)&&near(monoR,.7f));
    for(int n=0;n<64;++n){monoL=.3f;monoR=.7f;monoPolicy.process(true,monoL,monoR);}
    assert(monoPolicy.blend()==1.f && monoL==monoR);
    assert(allocations.load(std::memory_order_relaxed)==monoAlloc);

    // Repeat block execution to verify no cumulative send residue / leakage.
    for(int i=0;i<1000;++i) {
        const auto pre=allocations.load(std::memory_order_relaxed);
        assert(fold.process(audio,4,outputL.data(),outputR.data(),4,noFx));
        assert(allocations.load(std::memory_order_relaxed)==pre);
        assert(near(outputL[0],.4f*.70710678f));
    }
}
