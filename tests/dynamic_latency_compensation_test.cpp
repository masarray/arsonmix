// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/DynamicNativeSessionPublisher.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdlib>
#include <new>
#include <bit>
#include <cstdint>
#include <cstdio>
// Keep the intentional test replacement malloc/free pair opaque to GCC's
// inlined mismatched-new-delete diagnostic. No warning flags are disabled.
#if defined(__GNUC__)
#define ARMIX_TEST_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define ARMIX_TEST_NOINLINE __declspec(noinline)
#else
#define ARMIX_TEST_NOINLINE
#endif
static std::size_t allocations=0;
ARMIX_TEST_NOINLINE void* operator new(std::size_t n){++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
ARMIX_TEST_NOINLINE void* operator new[](std::size_t n){return ::operator new(n);}
ARMIX_TEST_NOINLINE void operator delete(void* p) noexcept {std::free(p);}
ARMIX_TEST_NOINLINE void operator delete[](void* p) noexcept {std::free(p);}
ARMIX_TEST_NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}
ARMIX_TEST_NOINLINE void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
#undef ARMIX_TEST_NOINLINE
using namespace armix::mixer;
struct Effects {
    ChannelId a{},receiver{},isolated{},monitor{};
    int monitorLatency=0;
    armix::dsp::StereoLatencyDelay delayA,delayReceiver,delayIsolated,delayMonitor;
    int latency(ChannelId id) const noexcept{return id==a?7:id==receiver?3:id==isolated?19:id==monitor?monitorLatency:0;}
    void render(ChannelId id,float* l,float* r,int n) noexcept {
        auto* delay=id==a?&delayA:id==receiver?&delayReceiver:id==isolated?&delayIsolated:id==monitor?&delayMonitor:nullptr;
        if(delay)for(int i=0;i<n;++i)delay->process(l[i],r[i]);
    }
};
static void verifyExactHistoryAndSend() {
    auto bits=[](float x){return std::bit_cast<std::uint32_t>(x);};
    std::uint32_t rng=0x9e3779b9u;
    auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    auto pcm=[&](){return static_cast<float>(static_cast<int>(next()%2049)-1024)/1024.f;};
    std::array<float,257> l{},r{},dl{},dr{},refL{},refR{};
    for(int length:{0,1,7,16,19,64,127,256,1024}){
        armix::dsp::StereoLatencyDelay fast,ref;
        assert(fast.prepare(length)&&ref.prepare(length));
        for(int block=0;block<1000;++block){
            const int n=1+static_cast<int>(next()%257);
            for(int i=0;i<n;++i){l[i]=pcm();r[i]=pcm();}
            const auto before=allocations;
            if(block%5!=0){
                fast.advanceHistory(l.data(),r.data(),n);
                for(int i=0;i<n;++i){float x=l[i],y=r[i];ref.process(x,y);}
            }else for(int i=0;i<n;++i){
                float x=l[i],y=r[i],a=x,b=y;
                fast.process(x,y);ref.process(a,b);
                assert(bits(x)==bits(a)&&bits(y)==bits(b));
            }
            assert(allocations==before);
        }
    }
    armix::dsp::RealtimeSlew64 fast,ref;
    for(int block=0;block<4000;++block){
        const int n=1+static_cast<int>(next()%257);
        if(block%3==0){const float v=static_cast<float>(next()%17)/8.f;
            fast.retarget(v);ref.retarget(v);}
        const float panL=static_cast<float>(next()%9)/8.f;
        const float panR=static_cast<float>(next()%9)/8.f;
        for(int i=0;i<n;++i){
            l[i]=i%13==0?-0.f:pcm();r[i]=i%17==0?1.e-20f:pcm();
            dl[i]=refL[i]=pcm();dr[i]=refR[i]=pcm();
        }
        const auto before=allocations;
        fast.accumulateStereo(l.data(),r.data(),dl.data(),dr.data(),n,panL,panR);
        for(int i=0;i<n;++i){
            const float gain=ref.next();
            const float x=l[i]*gain*panL,y=r[i]*gain*panR;
            refL[i]+=x;refR[i]+=y;
            assert(bits(dl[i])==bits(refL[i])&&bits(dr[i])==bits(refR[i]));
        }
        assert(allocations==before&&fast.remaining==ref.remaining&&fast.current==ref.current);
    }
    std::puts("PASS: 9000 variable history blocks and 4000 exact stereo SEND blocks");
}
int main(){
    verifyExactHistoryAndSend();
    armix::dsp::StereoLatencyDelay probe;
    assert(probe.prepare(0)&&!probe.active());
    assert(probe.prepare(7)&&probe.active());
    assert(probe.prepare(0)&&!probe.active());
    for(bool pre:{false,true}) for(bool potentialOnly:{false,true})
    for(bool independent:{false,true}) for(int ownMonitorLatency:{0,5}) {
        DynamicChannelRoster roster;
        std::vector<ChannelEdit> edits;
        for(int i=0;i<(potentialOnly?6:5);++i){ChannelEdit e;e.kind=ChannelEdit::Kind::add;
            e.position=i;e.role=i==3?ChannelRole::main:i==4?ChannelRole::monitor:ChannelRole::input;
            e.format=ProcessingFormat::stereo;e.text="node";edits.push_back(e);}
        const auto added=roster.edit(0,edits);assert(added.ok());
        const auto ids=added.createdIds;const auto a=ids[0],b=ids[1],r=ids[2],main=ids[3],monitor=ids[4];
        std::vector<ChannelEdit> sends;
        const auto send=[&](ChannelId from,ChannelId to,bool before=false){ChannelEdit e;
            e.kind=ChannelEdit::Kind::addSendReference;e.id=from;e.target=to;e.value=1;e.preFader=before;sends.push_back(e);};
        send(a,main);send(a,r,pre);send(b,r);send(r,main);send(b,monitor);
        assert(roster.edit(1,sends).ok());
        // Physical receiver input is mixed alongside internally delayed PCM.
        DynamicGraphRequest request;request.physicalInputs=6;request.maxFrames=16;
        request.sources={{a,0,1,true},{b,2,3,true},{r,4,5,true}};
        request.monitorLatencyPolicy=independent?MonitorLatencyPolicy::independent:MonitorLatencyPolicy::aligned;
        auto fx=std::make_shared<Effects>();fx->a=a;fx->receiver=r;
        fx->monitor=monitor;fx->monitorLatency=ownMonitorLatency;
        assert(fx->delayMonitor.prepare(ownMonitorLatency));
        if(potentialOnly) {
            // No WIRE to MAIN/MONITOR and no armed direct output. A source
            // only lane nevertheless participates in today's global PDC.
            fx->isolated=ids[5];
            request.physicalInputs=8;
            request.sources.push_back({ids[5],6,7,true});
            assert(fx->delayIsolated.prepare(19));
        }
        assert(fx->delayA.prepare(7)&&fx->delayReceiver.prepare(3));
        DynamicNativeSessionPublisher publisher;
        DynamicNativeSessionPublisher::FxPlan plan;
        plan.owner=fx;plan.context=fx.get();plan.deviceEpoch=9;plan.graphRevision=1;
        plan.render=+[](void* p,ChannelId id,float* l,float* r,int n) noexcept {static_cast<Effects*>(p)->render(id,l,r,n);};
        plan.latency=+[](void* p,ChannelId id) noexcept{return static_cast<Effects*>(p)->latency(id);};
        assert(publisher.commit(roster,request,0,9,9,true,plan).ok());
        const int expected=potentialOnly?19:10;
        const int expectedMonitor=independent?ownMonitorLatency:expected;
        assert(publisher.latencySamples(9,1)==expected);
        const auto summary=publisher.latencyPlanSummary(9,1);
        assert(summary.valid&&summary.hasMonitor&&summary.alignedSamples==expected);
        assert(summary.mainBeforeAlignment==10&&summary.monitorBeforeAlignment==ownMonitorLatency);
        assert(summary.monitorPolicy==request.monitorLatencyPolicy);
        assert(summary.monitorOutputSamples==expectedMonitor);
        assert(summary.monitorAlignmentPad()==expectedMonitor-ownMonitorLatency);
        assert(summary.monitorIndependentSaving()==expected-ownMonitorLatency);
        assert(summary.potentialDirectMaxSamples==expected);
        assert(summary.potentialDirectMaxChannel==(potentialOnly?ids[5]:r));
        assert(summary.potentialDirectMaxTies==1);
        assert(summary.directTapRaisesTarget()==potentialOnly);
        assert(!publisher.latencyPlanSummary(8,1).valid);
        assert(!publisher.latencyPlanSummary(9,2).valid);
        std::array<float,16> inA{},inB{},inR{},silent{},l{},right{},ml{},mr{};
        int time=0;
        for(int n:{3,1,7,5,16}){
            inA.fill(0);inB.fill(0);inR.fill(0);
            if(time==0){inA[0]=1;inB[0]=2;inR[0]=4;}
            const float* physical[]{inA.data(),inA.data(),inB.data(),inB.data(),inR.data(),inR.data(),silent.data(),silent.data()};
            const auto before=allocations;
            assert(publisher.render(9,physical,request.physicalInputs,l.data(),right.data(),n,false,
                [](ChannelId,float*,float*,int) noexcept {},ml.data(),mr.data()));
            assert(allocations==before);
            for(int i=0;i<n;++i){
                assert(std::abs(l[i]-(time+i==expected?8.f:0.f))<1.e-5f);
                assert(std::abs(right[i]-l[i])<1.e-5f);
                assert(std::abs(ml[i]-(time+i==expectedMonitor?2.f:0.f))<1.e-5f);
                assert(std::abs(mr[i]-ml[i])<1.e-5f);
            }
            time+=n;
        }
        // Exercise the actual disconnected PDC tap, not just the primitive.
        // Feed an identifiable timeline while disconnected, then reconnect
        // with the existing 64-sample fade. No stale tail or missing frame.
        std::array<float,16> extL{},extR{};
        float* hardware[]{l.data(),right.data(),ml.data(),mr.data(),extL.data(),extR.data()};
        const auto signal=[](int frame){return static_cast<float>(frame+1)/512.f;};
        for(int block=0;block<12;++block){
            if(block==6)assert(publisher.setExternalOutput(9,1,b,4,5,6));
            inA.fill(0);inR.fill(0);
            for(int i=0;i<16;++i)inB[i]=signal(block*16+i);
            const float* physical[]{inA.data(),inA.data(),inB.data(),inB.data(),inR.data(),inR.data(),silent.data(),silent.data()};
            const auto before=allocations;
            assert(publisher.render(9,physical,request.physicalInputs,l.data(),right.data(),16,false,
                [](ChannelId,float*,float*,int) noexcept {},ml.data(),mr.data(),{},{},{},hardware,6));
            assert(allocations==before);
            for(int i=0;i<16;++i){
                const int elapsed=(block-6)*16+i;
                const float expectedTap=elapsed<0?0.f:signal(block*16+i-expected)*
                    std::min(1.f,static_cast<float>(elapsed+1)/64.f);
                assert(std::abs(extL[i]-expectedTap)<1.e-6f&&std::abs(extR[i]-expectedTap)<1.e-6f);
            }
        }
        assert(publisher.appliedExternalOutput(9,1,b)==std::make_pair(4,5));
        // Failed candidate budget/admission never replaces the live graph.
        request.maxScratchBytes=16*2*sizeof(float)*5;
        plan.graphRevision=2;
        assert(!publisher.commit(roster,request,1,9,9,true,plan).ok());
        assert(publisher.acceptedRevision()==1);
        assert(publisher.latencyPlanSummary(9,1).alignedSamples==expected);
        // Policy switching cannot use armed WIRE or FX-only commit
        // exceptions, even if the caller attests unchanged physical pins.
        request.maxScratchBytes=64U*1024U*1024U;
        request.monitorLatencyPolicy=independent?MonitorLatencyPolicy::aligned:MonitorLatencyPolicy::independent;
        for(bool fxOnly:{false,true}){
            const auto refused=publisher.commit(roster,request,1,9,9,false,plan,!fxOnly,fxOnly);
            assert(refused.fault==DynamicNativeSessionPublisher::Fault::outputArmed);
            assert(publisher.acceptedRevision()==1);
            assert(publisher.latencyPlanSummary(9,1).monitorOutputSamples==expectedMonitor);
        }
        // Invalid enum is rejected and leaves the accepted graph untouched.
        auto invalid=request;invalid.monitorLatencyPolicy=static_cast<MonitorLatencyPolicy>(255);
        assert(!publisher.commit(roster,invalid,1,9,9,true,plan).ok());
        assert(publisher.acceptedRevision()==1);
        assert(publisher.commit(roster,request,1,9,9,true,plan).ok());
        const auto switched=publisher.latencyPlanSummary(9,2);
        assert(switched.valid&&switched.alignedSamples==expected);
        assert(switched.monitorOutputSamples==(independent?expected:ownMonitorLatency));
        assert(!publisher.latencyPlanSummary(9,1).valid);
        // Same-policy independent rebuilds also cannot silently retime.
        if(!independent){
            plan.graphRevision=3;
            const auto refused=publisher.commit(roster,request,2,9,9,false,plan,true);
            assert(refused.fault==DynamicNativeSessionPublisher::Fault::outputArmed);
        }
    }
    // Without an actual independent MONITOR sink the policy is invalid.
    DynamicChannelRoster mainOnly;
    ChannelEdit add;add.kind=ChannelEdit::Kind::add;add.role=ChannelRole::main;
    add.text="MAIN";add.position=0;add.format=ProcessingFormat::stereo;
    assert(mainOnly.edit(0,std::span<const ChannelEdit>(&add,1)).ok());
    DynamicGraphRequest request;request.monitorLatencyPolicy=MonitorLatencyPolicy::independent;
    DynamicNativeSessionPublisher publisher;
    const auto refused=publisher.commit(mainOnly,request,0,9,9,true);
    assert(refused.fault==DynamicNativeSessionPublisher::Fault::invalidGraph);
    assert(refused.graphError.fault==DynamicGraphFault::invalidMonitorLatencyPolicy);
    assert(publisher.acceptedRevision()==0);
    std::puts("PASS: 16 monitor policy/latency/topology fixtures and disarmed transition fences");
}
