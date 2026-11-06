// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/DynamicNativeSessionPublisher.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstdlib>
#include <new>
static std::size_t allocations=0;
void* operator new(std::size_t n){++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
using namespace armix::mixer;
struct Effects {
    ChannelId a{},receiver{};
    armix::dsp::StereoLatencyDelay delayA,delayReceiver;
    int latency(ChannelId id) const noexcept{return id==a?7:id==receiver?3:0;}
    void render(ChannelId id,float* l,float* r,int n) noexcept {
        auto* delay=id==a?&delayA:id==receiver?&delayReceiver:nullptr;
        if(delay)for(int i=0;i<n;++i)delay->process(l[i],r[i]);
    }
};
int main(){
    for(bool pre:{false,true}) {
        DynamicChannelRoster roster;
        std::vector<ChannelEdit> edits;
        for(int i=0;i<5;++i){ChannelEdit e;e.kind=ChannelEdit::Kind::add;
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
        auto fx=std::make_shared<Effects>();fx->a=a;fx->receiver=r;
        assert(fx->delayA.prepare(7)&&fx->delayReceiver.prepare(3));
        DynamicNativeSessionPublisher publisher;
        DynamicNativeSessionPublisher::FxPlan plan;
        plan.owner=fx;plan.context=fx.get();plan.deviceEpoch=9;plan.graphRevision=1;
        plan.render=+[](void* p,ChannelId id,float* l,float* r,int n) noexcept {static_cast<Effects*>(p)->render(id,l,r,n);};
        plan.latency=+[](void* p,ChannelId id) noexcept{return static_cast<Effects*>(p)->latency(id);};
        assert(publisher.commit(roster,request,0,9,9,true,plan).ok());
        assert(publisher.latencySamples(9,1)==10);
        std::array<float,16> inA{},inB{},inR{},l{},right{},ml{},mr{};
        int time=0;
        for(int n:{3,1,7,5,16}){
            inA.fill(0);inB.fill(0);inR.fill(0);
            if(time==0){inA[0]=1;inB[0]=2;inR[0]=4;}
            const float* physical[]{inA.data(),inA.data(),inB.data(),inB.data(),inR.data(),inR.data()};
            const auto before=allocations;
            assert(publisher.render(9,physical,6,l.data(),right.data(),n,false,
                [](ChannelId,float*,float*,int) noexcept {},ml.data(),mr.data()));
            assert(allocations==before);
            for(int i=0;i<n;++i){
                assert(std::abs(l[i]-(time+i==10?8.f:0.f))<1.e-5f);
                assert(std::abs(right[i]-l[i])<1.e-5f);
                assert(std::abs(ml[i]-(time+i==10?2.f:0.f))<1.e-5f);
                assert(std::abs(mr[i]-ml[i])<1.e-5f);
            }
            time+=n;
        }
        // Failed candidate budget/admission never replaces the live graph.
        request.maxScratchBytes=16*2*sizeof(float)*5;
        plan.graphRevision=2;
        assert(!publisher.commit(roster,request,1,9,9,true,plan).ok());
        assert(publisher.acceptedRevision()==1);
    }
}
