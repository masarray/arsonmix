// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/PreparedMixerBlock.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
using namespace armix::mixer;

static std::atomic<unsigned> allocations{0};
void* operator new(std::size_t n){
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
static bool near(float a,float b){return std::abs(a-b)<.0003f;}
int main(){
    static_assert(stripCount==10 && auxBusCount==4);
    constexpr int frames=8;
    ChannelRoutingConfig cfg;
    std::array<int,stripCount> activePorts{};
    for(int ch=0;ch<stripCount;++ch) {
        activePorts[ch]=ch;
        cfg.stripsAndMaster.strips[ch]=
            {SourceMode::mono,ch,-1,1.f,0.f,false,false};
    }
    SendRequest legacy;
    AuxBusRequest aux;
    aux.buses[0]={true,true,false,.5f};
    aux.buses[1]={true,true,false,1.f};
    aux.count=2;
    aux.sends[0]={0,0,SendTap::preFader,.5f,0.f,true};
    aux.sends[1]={9,1,SendTap::postFader,.25f,0.f,true};

    PreparedRenderGraph plan;
    RenderError error;
    assert(PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    assert(plan.aux.active&&plan.aux.edgeCount==2);
    for(int ch=0;ch<stripCount;++ch)assert(plan.scheduled[ch]);

    std::array<std::array<float,frames>,stripCount> signal{};
    std::array<const float*,stripCount> input{};
    for(int ch=0;ch<stripCount;++ch){
        signal[ch].fill(.1f);
        input[ch]=signal[ch].data();
    }
    PreparedMixerBlock block(plan,frames);
    std::array<float,frames> outL{},outR{};
    RenderLevels meters;
    std::array<int,auxBusCount> fxInvocations{};
    auto effects=[&](int strip,float* l,float* r,int n) noexcept {
        if(strip>=stripCount&&strip<stripCount+auxBusCount) {
            const int bus=strip-stripCount;
            ++fxInvocations[bus];
            const float gain=bus==0?2.f:3.f;
            for(int i=0;i<n;++i){l[i]*=gain;r[i]*=gain;}
        }
    };
    const auto before=allocations.load(std::memory_order_relaxed);
    assert(block.process(input.data(),stripCount,outL.data(),outR.data(),frames,effects,&meters));
    assert(allocations.load(std::memory_order_relaxed)==before);
    // All 10 physical sources MIX into MAIN independently of AUX slots.
    const float direct=10.f*.1f*std::sqrt(.5f);
    const float auxA=.1f*.5f*std::sqrt(.5f)*2.f*.5f; // mono PRE pan center
    const float auxB=.1f*std::sqrt(.5f)*.25f*3.f;
    assert(near(outL[0],direct+auxA+auxB));
    assert(near(outR[0],direct+auxA+auxB));
    assert(fxInvocations[0]==1 && fxInvocations[1]==1);
    assert(near(meters.auxPeak[0],auxA)); // after bus gain 0.5
    assert(near(meters.auxPeak[1],auxB));
    assert(near(meters.channelPeak[9],.1f*std::sqrt(.5f)));
    // This is real PRE/POST native PCM, never a JS send simulation.
    // A 64-frame gain ramp must not collapse to an 8-frame hard step.
    assert(block.setAuxSendGain(0,0,SendTap::preFader,0.f));
    assert(block.setAuxSendGain(9,1,SendTap::postFader,0.f));
    assert(!block.setAuxSendGain(9,0,SendTap::postFader,0.f)); // edge absent
    const auto beforeSendAutomation=allocations.load(std::memory_order_relaxed);
    for(int cycle=0;cycle<8;++cycle)
        assert(block.process(input.data(),stripCount,outL.data(),outR.data(),
                             frames,effects,&meters));
    assert(allocations.load(std::memory_order_relaxed)==beforeSendAutomation);
    assert(near(outL[7],direct)&&near(outR[7],direct));
    assert(near(block.auxSendGain(0,0,SendTap::preFader),0.f));
    assert(near(block.auxSendGain(9,1,SendTap::postFader),0.f));
    assert(block.setAuxSendGain(0,0,SendTap::preFader,.5f));
    assert(block.setAuxSendGain(9,1,SendTap::postFader,.25f));
    for(int cycle=0;cycle<8;++cycle)
        assert(block.process(input.data(),stripCount,outL.data(),outR.data(),
                             frames,effects,&meters));
    assert(near(outL[7],direct+auxA+auxB));

    // Shared stereo AUX receives an explicitly left-panned mono PRE send.
    // The right output must not inherit phantom mono power.
    aux.sends[0].pan=-1.f;
    assert(PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    assert(near(plan.aux.edges[0].leftGain,.5f));
    assert(near(plan.aux.edges[0].rightGain,0.f));
    aux.sends[0].pan=0.f;

    // Zero source fader: PRE-FADER send stays audible, no source direct.
    cfg.stripsAndMaster.strips[0].faderLinear=0.f;
    legacy.toMaster[0]=false;
    assert(PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    PreparedMixerBlock zeroFader(plan,frames);
    assert(zeroFader.process(input.data(),stripCount,outL.data(),outR.data(),frames,effects,&meters));
    assert(near(outL[0],9.f*.1f*std::sqrt(.5f)+auxA+auxB));

    // AUX1 routing MUTE silences only its return, NOT any input source.
    aux.buses[0].mute=true;
    assert(PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    PreparedMixerBlock muteAux(plan,frames);
    assert(muteAux.process(input.data(),stripCount,outL.data(),outR.data(),frames,effects,&meters));
    assert(near(outL[0],9.f*.1f*std::sqrt(.5f)+auxB));

    // Exact rollback of compiler destination when a route is invalid.
    const auto approved=plan;
    aux.sends[1].destination=4;
    assert(!PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    assert(error.fault==RenderFault::invalidAuxRouting&&error.aux.fault==AuxFault::invalidBus);
    assert(plan.aux.edgeCount==approved.aux.edgeCount);
    aux.sends[1].destination=1;
    aux.sends[1].source=10; // cannot route AUX back into INPUT (no feedback)
    assert(!PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    assert(error.aux.fault==AuxFault::invalidStrip);
    aux.sends[1].source=9;
    aux.buses[1].enabled=false;
    assert(!PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    assert(error.aux.fault==AuxFault::disabledTarget);
    aux.buses[1].enabled=true;
    aux.sends[1]=aux.sends[0];
    assert(!PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    assert(error.aux.fault==AuxFault::duplicateSend);
    aux.sends[1].enabled=false;
    assert(PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    assert(plan.aux.edgeCount==1);
    aux.count=33;
    assert(!PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error,aux));
    assert(error.aux.fault==AuxFault::invalidCount);

    // Old P1G/P1H callers without AUX fields remain valid (no extra FX).
    aux=AuxBusRequest{};
    assert(PreparedRenderGraph::compile(cfg,activePorts,legacy,plan,error));
    assert(!plan.aux.active&&plan.aux.edgeCount==0);
    PreparedMixerBlock legacyBlock(plan,frames);
    fxInvocations.fill(0);
    assert(legacyBlock.process(input.data(),stripCount,outL.data(),outR.data(),frames,effects));
    assert(fxInvocations[0]==0 && fxInvocations[1]==0);
    // R0B B3: a verified device may expose ten physical capture ports while
    // a newly prepared production session has ZERO input assignments.
    // This is a valid native 10CH+4AUX graph, not a CH1 fallback or an
    // automatically guessed microphone-to-speaker route.
    ChannelRoutingConfig unpatched;
    SendRequest noSends;
    AuxBusRequest noAux;
    PreparedRenderGraph silentPlan;
    assert(PreparedRenderGraph::compile(unpatched,activePorts,noSends,
                                        silentPlan,error,noAux));
    for(int ch=0;ch<stripCount;++ch)assert(!silentPlan.scheduled[ch]);
    assert(!silentPlan.aux.active);
    PreparedMixerBlock silent(silentPlan,frames);
    outL.fill(123.f);outR.fill(456.f);
    // The DSP core invokes the MASTER seam once even if all channels
    // and AUX returns are disconnected. The native R0B B3 authorization
    // gate skips this entire core until a physical INPUT PATCH is ACKed.
    int silentStripFxCalls=0,silentMasterFxCalls=0;
    auto countFx=[&](int strip,float*,float*,int) noexcept {
        if(strip==-1)++silentMasterFxCalls;
        else ++silentStripFxCalls;
    };
    const auto beforeSilence=allocations.load(std::memory_order_relaxed);
    assert(silent.process(input.data(),stripCount,outL.data(),outR.data(),
                          frames,countFx,&meters));
    assert(allocations.load(std::memory_order_relaxed)==beforeSilence);
    assert(silentStripFxCalls==0 && silentMasterFxCalls==1);
    for(int i=0;i<frames;++i)assert(outL[i]==0.f&&outR[i]==0.f);
    for(int ch=0;ch<stripCount;++ch)assert(meters.channelPeak[ch]==0.f);
    for(int b=0;b<auxBusCount;++b)assert(meters.auxPeak[b]==0.f);

    // MS3B independent MAIN/MONITOR PCM. Two physical channels are routed
    // to different buses in ONE pass; monitor does not rerun any VST.
    {
        ChannelRoutingConfig cfg2;
        cfg2.stripsAndMaster.strips[0]=
            {SourceMode::mono,0,-1,1.f,0.f,false,false};
        cfg2.stripsAndMaster.strips[1]=
            {SourceMode::mono,1,-1,1.f,0.f,false,false};
        std::array<int,2> pins{0,1};
        SendRequest sends2;
        sends2.toMaster[0]=true;
        // SendRequest is legacy MAIN-ON by default. Explicitly disable
        // CH2→MAIN; otherwise both signals mix into MAIN and this is not
        // a genuine independent output bus regression.
        sends2.toMaster[1]=false;
        // CH2 is MONITOR-only even when it does NOT feed MAIN.
        PreparedRenderGraph routed;
        RenderError failure;
        assert(PreparedRenderGraph::compile(cfg2,pins,sends2,routed,failure));
        PreparedMixerBlock single(routed,frames);
        std::array<float,frames> mainL{},mainR{},monL{},monR{};
        std::array<float,frames> mic1{},mic2{};
        mic1.fill(.2f);mic2.fill(.4f);
        const float* physical[2]{mic1.data(),mic2.data()};
        std::array<float,stripCount+auxBusCount> monGains{};
        monGains[1]=.75f;
        int firstFx=0,secondFx=0,masterFx=0;
        auto fxOnce=[&](int lane,float*,float*,int) noexcept {
            if(lane==0)++firstFx;
            if(lane==1)++secondFx;
            if(lane==-1)++masterFx;
        };
        const auto beforeMonitor=allocations.load(std::memory_order_relaxed);
        assert(single.process(physical,2,mainL.data(),mainR.data(),frames,
                              fxOnce,nullptr,nullptr,&monGains,
                              monL.data(),monR.data()));
        assert(allocations.load(std::memory_order_relaxed)==beforeMonitor);
        assert(firstFx==1&&secondFx==1&&masterFx==1);
        const float monoCenter=std::sqrt(.5f);
        for(int f=0;f<frames;++f) {
            assert(near(mainL[f],.2f*monoCenter));
            assert(near(mainR[f],.2f*monoCenter));
            assert(near(monL[f],.4f*monoCenter*.75f));
            assert(near(monR[f],.4f*monoCenter*.75f));
        }
        // Native MONITOR targets move independent of MAIN, and their ramp
        // follows AUDIO samples across eight 8-frame callbacks.
        std::array<std::atomic<float>,stripCount+auxBusCount> liveMonitor{};
        for(int i=0;i<stripCount+auxBusCount;++i)
            liveMonitor[i].store(monGains[i],std::memory_order_relaxed);
        liveMonitor[1].store(0.f,std::memory_order_release);
        const auto beforeLiveMonitor=allocations.load(std::memory_order_relaxed);
        for(int cycle=0;cycle<8;++cycle)
            assert(single.process(physical,2,mainL.data(),mainR.data(),frames,
                                  fxOnce,nullptr,nullptr,&monGains,
                                  monL.data(),monR.data(),&liveMonitor));
        assert(allocations.load(std::memory_order_relaxed)==beforeLiveMonitor);
        assert(near(mainL[7],.2f*monoCenter)); // MAIN unaffected
        assert(near(monL[7],0.f)&&near(monR[7],0.f));
        liveMonitor[1].store(.75f,std::memory_order_release);
        for(int cycle=0;cycle<8;++cycle)
            assert(single.process(physical,2,mainL.data(),mainR.data(),frames,
                                  fxOnce,nullptr,nullptr,&monGains,
                                  monL.data(),monR.data(),&liveMonitor));
        assert(near(monL[7],.4f*monoCenter*.75f));
        assert(near(mainL[7],.2f*monoCenter));
        // Output buffers overlap: no implicit mixing or dual-ownership.
        assert(!single.process(physical,2,mainL.data(),mainR.data(),frames,
                               fxOnce,nullptr,nullptr,&monGains,
                               mainL.data(),mainR.data()));
    }

}
