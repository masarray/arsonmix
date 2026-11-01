// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/LiveChannelFxGraph.h"
#include "../engine/core/DynamicChannelFxLeaseBank.h"
#include "../engine/core/DynamicNativeSessionPublisher.h"
#include "../engine/core/UnifiedLegacyProductionAdmission.h"
#include "../engine/core/NativeOutputPairTap.h"
#include "../engine/core/RealtimeRoutePublisher.h"
#include "../src/ConsoleLiveBridgeV6.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <new>
using namespace armix::mixer;

static std::atomic<std::size_t> heapCalls{0};
void* operator new(std::size_t n) {
    heapCalls.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n==0?1:n))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}

class GainFx final : public juce::AudioProcessor {
public:
    static inline int living=0, prepared=0, released=0;
    explicit GainFx(float gain)
      : AudioProcessor(BusesProperties()
        .withInput("Main",juce::AudioChannelSet::stereo(),true)
        .withOutput("Main",juce::AudioChannelSet::stereo(),true)),gain_(gain){++living;}
    ~GainFx() override{--living;}
    const juce::String getName()const override{return "In-memory gain effect";}
    void prepareToPlay(double,int)override{++prepared;}
    void releaseResources()override{++released;}
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&)override{
        for(int c=0;c<buffer.getNumChannels();++c)
            for(int f=0;f<buffer.getNumSamples();++f)
                buffer.getWritePointer(c)[f]*=gain_;
    }
    double getTailLengthSeconds()const override{return 0;}
    bool acceptsMidi()const override{return false;}
    bool producesMidi()const override{return false;}
    juce::AudioProcessorEditor* createEditor()override{return nullptr;}
    bool hasEditor()const override{return false;}
    int getNumPrograms()override{return 1;}
    int getCurrentProgram()override{return 0;}
    void setCurrentProgram(int)override{}
    const juce::String getProgramName(int)override{return {};}
    void changeProgramName(int,const juce::String&)override{}
    void getStateInformation(juce::MemoryBlock& out)override{
        out.append(&gain_,sizeof(gain_));
    }
    void setStateInformation(const void* data,int size)override{
        if(data&&size==static_cast<int>(sizeof(gain_)))
            std::memcpy(&gain_,data,sizeof(gain_));
    }
private:
    float gain_;
};
static bool near(float x,float y){return std::abs(x-y)<0.0001f;}
static std::unique_ptr<LiveChannelFxGraph> makeGraph(float firstFx,float returnFx,bool withFx=true) {
    ChannelRoutingConfig config;
    config.stripsAndMaster.strips[0]={SourceMode::mono,2,-1,0.f,0.f,false,false};
    config.stripsAndMaster.strips[1]={SourceMode::disconnected,-1,-1,1.f,0.f,false,false};
    config.formats[1]=ChannelFormat::stereo;
    SendRequest sends;
    sends.toMaster[0]=false;
    sends.count=1;
    sends.sends[0]={0,1,SendTap::preFader,.5f,0.f,true};
    PreparedRenderGraph route;RenderError error;
    constexpr std::array ports{2,5,9};
    assert(PreparedRenderGraph::compile(config,ports,sends,route,error));
    auto graph=std::make_unique<LiveChannelFxGraph>(route,32,8);
    if(withFx) {
        auto a=std::make_unique<GainFx>(firstFx);
        auto b=std::make_unique<GainFx>(returnFx);
        a->prepareToPlay(48000,8);
        b->prepareToPlay(48000,8);
        assert(graph->install(0,std::move(a),"CH1 FX","vst3-ch1-id"));
        assert(graph->install(1,std::move(b),"BUS FX","vst3-bus-id"));
        assert(!graph->install(1,std::make_unique<GainFx>(2.f),"duplicate"));
        assert(graph->activeInserts()==2);
    }
    assert(graph->maxFramesPerPlugin()==32); // clamp minimum safe plugin quantum
    return graph;
}
// M5: real M4 JUCE plugin instances survive ChannelId graph migration.
// CH11 and FX Return are not borrowed legacy strips: they receive their own
// native processor leases and execute in the same prepared PCM schedule.
static void verifyUniversalChannelIdFxLeaseAndLivePcm() {
    using Bank=DynamicChannelFxLeaseBank;
    DynamicChannelRoster roster;
    std::array<ChannelEdit,4> adds{};
    for(int i=0;i<4;++i) {
        auto& op=adds[static_cast<std::size_t>(i)];
        op.kind=ChannelEdit::Kind::add;
        op.position=static_cast<std::size_t>(i);
        op.role=i==2?ChannelRole::fxReturn:i==3?ChannelRole::main:ChannelRole::input;
        op.format=i<2?ProcessingFormat::mono:ProcessingFormat::stereo;
        op.text=i==0?"CH1":i==1?"CH11":i==2?"FX RETURN":"MAIN";
    }
    const auto created=roster.edit(0,adds);
    assert(created.ok()&&created.createdIds.size()==4);
    const auto ch1=created.createdIds[0],ch11=created.createdIds[1];
    const auto ret=created.createdIds[2],main=created.createdIds[3];
    std::vector<ChannelEdit> wire;
    const auto link=[&](ChannelId src,ChannelId dst,bool primary,
                        float gain=1.f) {
        ChannelEdit op;op.kind=ChannelEdit::Kind::addSendReference;
        op.id=src;op.target=dst;op.value=gain;op.primaryOutput=primary;
        wire.push_back(std::move(op));
    };
    link(ch1,main,true);
    link(ch11,main,true);
    link(ret,main,true);
    link(ch11,ret,false,.5f);
    ChannelEdit marker;marker.kind=ChannelEdit::Kind::addInsertReference;
    marker.id=ret;marker.text="native-vst3-fx-return";
    wire.push_back(std::move(marker));
    assert(roster.edit(roster.revision(),wire).ok());
    const std::array<ChannelId,2> selected{ch1,ch11};
    const auto group=roster.createBusForSelected(
        roster.revision(),selected,"VOCAL GROUP",ChannelRole::group);
    assert(group.ok()&&group.createdIds.size()==1);
    const auto bus=group.createdIds[0];
    // The accepted routing view, not a UI draft, proves exclusive primary OUT.

    DynamicGraphRequest req;
    req.physicalInputs=2;req.maxFrames=32;
    req.sources={{ch1,0,-1,false},{ch11,1,-1,false}};
    DynamicNativeSessionPublisher native;
    constexpr std::uint64_t epoch=991;
    assert(native.commit(roster,req,0,epoch,epoch,true).ok());
    const auto* view=native.acceptedRouting(epoch,1);
    assert(view&&view->find(ch11)&&view->find(bus));
    assert(view->find(ch11)->primaryOutput==bus);
    assert(view->find(bus)->primaryOutput==main);

    auto original=makeGraph(2.f,3.f);
    Bank::Assignment first,groupOwner;
    assert(Bank::captureLegacy(0,ch1,ProcessingFormat::mono,*original,first));
    assert(Bank::captureLegacy(1,bus,ProcessingFormat::stereo,*original,groupOwner));
    // These are genuinely new CH11/FX Return native instances. The M4
    // vendor processor ownership class and binary state path are reused.
    const auto newFx=[&](float g,const char* catalog)->Bank::Lease {
        auto p=std::make_unique<GainFx>(g);
        p->prepareToPlay(48000.0,64);
        return std::make_shared<LiveChannelFxGraph::FxInstance>(
            std::move(p),juce::String(catalog),juce::String(catalog),64);
    };
    Bank::Assignment eleven;eleven.id=ch11;eleven.format=ProcessingFormat::mono;
    eleven.inserts.push_back(newFx(4.f,"ch11-gain"));
    Bank::Assignment returnFx;returnFx.id=ret;returnFx.format=ProcessingFormat::stereo;
    returnFx.inserts.push_back(newFx(2.f,"fx-return-gain"));
    std::array<Bank::Assignment,4> assignments{first,eleven,groupOwner,returnFx};
    Bank bank;
    // The accepted FX owner and native PCM must share the NEXT graph revision.
    assert(Bank::prepare(epoch,native.acceptedRevision()+1,assignments,bank)==
           Bank::Fault::none);
    assert(bank.lanes()==4&&bank.insertCount(ch11)==1&&
           bank.insertCount(bus)==1&&bank.insertCount(ret)==1);
    assert(bank.ownerForControl(ch1,0)&&
           (*bank.ownerForControl(ch1,0))->instanceId==
              original->insertInstanceId(0,0));
    Bank blocked;
    std::array<Bank::Assignment,2> alias{first,groupOwner};
    alias[1].id=ch11;
    alias[1].inserts=first.inserts; // same native processor twice
    assert(Bank::prepare(epoch,2,alias,blocked)==Bank::Fault::duplicateInstance);
    assert(blocked.lanes()==0);
    assert(Bank::prepare(epoch,2,assignments,blocked)==Bank::Fault::none);
    const auto token=original->insertInstanceId(0,0);
    auto migrated=*bank.ownerForControl(ch1,0);
    assert(migrated->instanceId==token);
    original.reset(); // vendor instances retained by bank, no new VST3 scan
    assert(GainFx::living>=4);
    auto acceptedOwner=std::make_shared<Bank>(std::move(bank));
    DynamicNativeSessionPublisher::FxPlan ownerPlan;
    ownerPlan.owner=std::static_pointer_cast<void>(acceptedOwner);
    ownerPlan.context=acceptedOwner.get();
    ownerPlan.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept {
        (*static_cast<Bank*>(ctx))(id,l,r,n);
    };
    ownerPlan.deviceEpoch=epoch;ownerPlan.graphRevision=2;
    assert(ownerPlan.valid());
    // A mismatched raw callback context cannot be published or overwrite a
    // working hardware graph. This is not a UI flag or an unchecked pointer.
    auto wrong=ownerPlan;wrong.context=nullptr;
    assert(native.commit(roster,req,1,epoch,epoch,true,std::move(wrong)).fault==
           DynamicNativeSessionPublisher::Fault::invalidCandidate);
    assert(native.acceptedRevision()==1);
    assert(native.commit(roster,req,1,epoch,epoch,true,ownerPlan).ok());
    assert(native.acceptedRevision()==2);
    // Only the accepted snapshot retains the vendor bank; local control
    // holders can retire without losing a live VST during callback.
    acceptedOwner.reset();
    std::array<float,32> mic{},music{},left{},right{};
    mic.fill(.2f);music.fill(.1f);
    const float* pins[]{mic.data(),music.data()};
    int legacyFallbackCalls=0;
    const auto legacyFx=[&](ChannelId,float*,float*,int) noexcept {
        ++legacyFallbackCalls;
    };
    const auto beforeHeap=heapCalls.load(std::memory_order_relaxed);
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,
                         legacyFx));
    assert(legacyFallbackCalls==0); // never dual-render M4 + M5
    assert(heapCalls.load(std::memory_order_relaxed)==beforeHeap);
    constexpr float center=.7071067811865475f;
    // Main: (CH1 .2*2 + CH11 .1*4) * mono pan * GROUP FX3
    //     + CH11 post-send .5 * FX Return VST2 (no extra dry source).
    assert(near(left[31],((.2f*2.f+.1f*4.f)*3.f+
                         (.1f*4.f)*.5f*2.f)*center));
    assert(near(left[31],right[31]));
    assert(native.setChannelMix(epoch,2,bus,.5f,0.f));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,
                         legacyFx));
    assert(legacyFallbackCalls==0);
    assert(near(left[31],((.2f*2.f+.1f*4.f)*3.f*.5f+
                         (.1f*4.f)*.5f*2.f)*center));
    // A structural recompile carries the same M4 VST3 lease and accepted
    // Group gain. In real production the control thread must serialize
    // the same revision/epoch and never recreate vendor state.
    assert(roster.rememberLiveMix(bus,.5f,0.f));
    // A structurally new graph must NOT silently accept an old-revision
    // FX binding, even when the raw processor pointers are still alive.
    assert(native.commit(roster,req,2,epoch,epoch,true,ownerPlan).fault==
           DynamicNativeSessionPublisher::Fault::invalidCandidate);
    assert(native.acceptedRevision()==2);
    Bank refreshed;
    assert(Bank::prepare(epoch,3,assignments,refreshed)==Bank::Fault::none);
    auto refreshedOwner=std::make_shared<Bank>(std::move(refreshed));
    auto refreshedPlan=ownerPlan;
    refreshedPlan.owner=std::static_pointer_cast<void>(refreshedOwner);
    refreshedPlan.context=refreshedOwner.get();
    refreshedPlan.graphRevision=3;
    const auto originalProcessors=GainFx::living;
    assert(native.commit(roster,req,2,epoch,epoch,true,refreshedPlan).ok());
    assert(GainFx::living==originalProcessors); // reuse, never rebuild VST
    assert(native.acceptedRevision()==3);
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,
                         legacyFx));
    assert(near(left[31],((.2f*2.f+.1f*4.f)*3.f*.5f+
                         (.1f*4.f)*.5f*2.f)*center));
    assert(legacyFallbackCalls==0);
    assert(!native.render(epoch+1,pins,2,left.data(),right.data(),32,false,
                          legacyFx));
    assert(left[31]==0.f&&right[31]==0.f);
}

// M-U1 full-fidelity preparation: 10 CH + 4 AUX + M4 VST owners,
// sparse verified input ports, independent physical MAIN and MONITOR,
// PRE/POST and native Show/epoch rejection. Compare actual old M4 processor
// output with the same owners in ONE new atomic ChannelId publication.
static void verifyMU1LegacyM4FxFullMigrationAndMonitorPcm() {
    constexpr std::array<int,4> ports{3,0,4,7};
    ChannelRoutingConfig channels;
    channels.stripsAndMaster.strips[0]={
        SourceMode::mono,3,-1,.5f,-.3f,false,false};
    channels.stripsAndMaster.strips[1]={
        SourceMode::stereo,4,7,.75f,.2f,false,false};
    channels.formats[2]=ChannelFormat::stereo;
    channels.stripsAndMaster.master={1.1f,.1f,false,true};
    SendRequest sends;
    sends.toMaster[0]=false;sends.count=1;
    sends.sends[0]={0,2,SendTap::postFader,.3f,-.3f,true};
    AuxBusRequest aux;
    aux.buses[0]={true,true,false,.8f};
    aux.count=1;
    aux.sends[0]={1,0,SendTap::postFader,.25f,.1f,true};
    std::array<float,stripCount+auxBusCount> monitorGains{};
    monitorGains[0]=.25f;monitorGains[stripCount]=.3f;
    PreparedRenderGraph legacyGraph;
    RenderError legacyError;
    assert(PreparedRenderGraph::compile(channels,ports,sends,
                                       legacyGraph,legacyError,aux));
    auto legacy=std::make_unique<LiveChannelFxGraph>(legacyGraph,32,32);
    assert(legacy->setMonitorOutputBus(2,3,4,monitorGains));
    auto ch1=std::make_unique<GainFx>(1.2f);
    auto returnFx=std::make_unique<GainFx>(.9f);
    ch1->prepareToPlay(48000,32);
    returnFx->prepareToPlay(48000,32);
    assert(legacy->install(0,std::move(ch1),"CH1 VST3","m4-ch1"));
    assert(legacy->install(stripCount,std::move(returnFx),"AUX VST3","m4-aux"));
    const auto oldCh1=legacy->insertInstanceId(0,0);
    const auto oldAux=legacy->insertInstanceId(stripCount,0);
    std::array<float,32> mono{},unrelated{},stL{},stR{};
    for(int i=0;i<32;++i){
        mono[i]=.2f+.001f*i;unrelated[i]=.8f;
        stL[i]=.15f+.002f*i;stR[i]=.35f-.001f*i;
    }
    const float* physical[]{mono.data(),unrelated.data(),stL.data(),stR.data()};
    std::array<float,32> legacyL{},legacyR{},legacyML{},legacyMR{};
    assert(legacy->process(physical,4,legacyL.data(),legacyR.data(),32,
                           nullptr,nullptr,nullptr,legacyML.data(),legacyMR.data()));
    DynamicNativeSessionPublisher publisher;
    UnifiedLegacyAccepted accepted;
    constexpr std::uint64_t epoch=565;
    VerifiedNativeOutputs outputs{4,0,1,2,3,true};
    const auto good=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitorGains,*legacy,false,outputs,
        32,2u*1024u*1024u,0,epoch,epoch,true,publisher,accepted);
    assert(good.ok());
    assert(publisher.acceptedRevision()==1);
    assert(accepted.deviceEpoch==epoch&&accepted.pcmRevision==1);
    assert(accepted.graph.roster.size()==stripCount+auxBusCount+2);
    assert(accepted.graph.request.sources.size()==2);
    assert(accepted.fxOwners->insertCount(accepted.graph.ids.inputs[0])==1);
    assert(accepted.fxOwners->insertCount(accepted.graph.ids.aux[0])==1);
    assert((*accepted.fxOwners->ownerForControl(
        accepted.graph.ids.inputs[0],0))->instanceId==oldCh1);
    assert((*accepted.fxOwners->ownerForControl(
        accepted.graph.ids.aux[0],0))->instanceId==oldAux);
    // Actual FX ownership survives the old M4 graph retirement.
    legacy.reset();
    std::array<float,32> dynamicL{},dynamicR{},dynamicML{},dynamicMR{};
    const auto mustNeverCall=[&](ChannelId,float*,float*,int) noexcept {
        assert(false && "M4 processor may not render twice");
    };
    const auto before=heapCalls.load(std::memory_order_relaxed);
    assert(publisher.render(epoch,physical,4,dynamicL.data(),dynamicR.data(),
                            32,false,mustNeverCall,
                            dynamicML.data(),dynamicMR.data()));
    assert(heapCalls.load(std::memory_order_relaxed)==before);
    for(int i=0;i<32;++i) {
        assert(near(legacyL[i],dynamicL[i]));
        assert(near(legacyR[i],dynamicR[i]));
        assert(near(legacyML[i],dynamicML[i]));
        assert(near(legacyMR[i],dynamicMR[i]));
    }
    // Faults are non-destructive: no external CH1 Orbit state dropped, no
    // wrong-speaker output, no stale hardware, no armed graph recreation.
    LiveChannelFxGraph blockedLegacy(legacyGraph,32,32);
    assert(blockedLegacy.setMonitorOutputBus(2,3,4,monitorGains));
    auto bad=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitorGains,blockedLegacy,
        true,outputs,32,2u*1024u*1024u,1,epoch,epoch,true,
        publisher,accepted);
    assert(bad.fault==UnifiedLegacyProductionAdmission::Fault::legacyCh1ExternalFx);
    auto wrongPins=outputs;
    wrongPins.monitorR=0; // same physical output as MAIN
    bad=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitorGains,blockedLegacy,
        false,wrongPins,32,2u*1024u*1024u,1,epoch,epoch,true,
        publisher,accepted);
    assert(bad.fault==UnifiedLegacyProductionAdmission::Fault::invalidOutputPins);
    bad=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitorGains,blockedLegacy,
        false,outputs,32,2u*1024u*1024u,1,epoch,epoch+1,true,
        publisher,accepted);
    assert(bad.fault==UnifiedLegacyProductionAdmission::Fault::staleHardware);
    bad=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitorGains,blockedLegacy,
        false,outputs,32,2u*1024u*1024u,1,epoch,epoch,false,
        publisher,accepted);
    assert(bad.fault==UnifiedLegacyProductionAdmission::Fault::outputArmed);
    assert(publisher.acceptedRevision()==1);
    assert(accepted.fxOwners->insertCount(accepted.graph.ids.inputs[0])==1);
    assert(publisher.render(epoch,physical,4,dynamicL.data(),dynamicR.data(),
                            32,false,mustNeverCall,
                            dynamicML.data(),dynamicMR.data()));
    for(int i=0;i<32;++i){
        assert(near(legacyL[i],dynamicL[i]));
        assert(near(legacyMR[i],dynamicMR[i]));
    }
    assert(!publisher.render(epoch+1,physical,4,dynamicL.data(),
                             dynamicR.data(),32,false,mustNeverCall,
                             dynamicML.data(),dynamicMR.data()));
    assert(dynamicL[31]==0.f&&dynamicR[31]==0.f);
}

// M-U1: occupied legacy CH1 with original JUCE FX owner; PCM and fail-closed.
static void verifyOccupiedExternalCh1Owner() {
    struct Owner {
        GainFx fx{1.7f}; int calls=0;
        Owner(){fx.prepareToPlay(48000,32);}
        void process(float* l,float* r,int n) noexcept {
            float* ptrs[]{l,r};juce::AudioBuffer<float> block(ptrs,2,n);
            juce::MidiBuffer midi;fx.processBlock(block,midi);++calls;
        }
    };
    ChannelRoutingConfig channels;
    channels.stripsAndMaster.strips[0]={
        SourceMode::mono,3,-1,.8f,-.2f,false,false};
    SendRequest sends;AuxBusRequest aux;
    constexpr std::array<int,2> ports{3,0};
    PreparedRenderGraph route;RenderError err;
    assert(PreparedRenderGraph::compile(channels,ports,sends,route,err,aux));
    auto old=std::make_unique<LiveChannelFxGraph>(route,32,32);
    auto vendor=std::make_shared<Owner>();auto* raw=vendor.get();
    std::array<float,32> src{},silent{},oldL{},oldR{},newL{},newR{};
    src.fill(.25f);
    const float* pins[]{src.data(),silent.data()};
    const auto legacyFx=+[](void* c,int strip,float* l,float* r,int n) noexcept {
        assert(strip==0);static_cast<Owner*>(c)->process(l,r,n);
    };
    assert(old->process(pins,2,oldL.data(),oldR.data(),32,
                        nullptr,raw,legacyFx));
    DynamicNativeSessionPublisher publisher;UnifiedLegacyAccepted accepted;
    std::array<float,stripCount+auxBusCount> monitor{};
    VerifiedNativeOutputs outputs{2,0,1,-1,-1,false};
    constexpr std::uint64_t epoch=701;
    auto status=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitor,*old,true,outputs,
        32,2u*1024u*1024u,0,epoch,epoch,true,publisher,accepted);
    assert(status.fault==UnifiedLegacyProductionAdmission::Fault::legacyCh1ExternalFx);
    assert(publisher.acceptedRevision()==0);
    LegacyMigratedGraph migrated;LegacyMigrationError migrationError;
    assert(LegacyGraphMigration::compile(channels,ports,sends,aux,false,
        monitor,32,2u*1024u*1024u,migrated,migrationError));
    DynamicChannelFxLeaseBank::ExternalLease lease;
    lease.id=migrated.ids.inputs[0];
    lease.owner=std::static_pointer_cast<void>(vendor);
    lease.context=raw;
    lease.render=+[](void* c,float* l,float* r,int n) noexcept {
        static_cast<Owner*>(c)->process(l,r,n);
    };
    auto invalid=lease;invalid.context=nullptr;
    status=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitor,*old,true,outputs,
        32,2u*1024u*1024u,0,epoch,epoch,true,publisher,accepted,invalid);
    assert(status.fault==UnifiedLegacyProductionAdmission::Fault::legacyCh1ExternalFx);
    status=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitor,*old,true,outputs,
        32,2u*1024u*1024u,0,epoch,epoch,true,publisher,accepted,lease);
    assert(status.ok()&&publisher.acceptedRevision()==1);
    assert(accepted.fxOwners->externalChannel()==lease.id);
    old.reset();vendor.reset();
    const auto fallback=+[](ChannelId,float*,float*,int) noexcept {
        assert(false && "occupied vendor FX must never disappear");
    };
    const auto before=heapCalls.load(std::memory_order_relaxed);
    assert(publisher.render(epoch,pins,2,newL.data(),newR.data(),
                            32,false,fallback));
    assert(heapCalls.load(std::memory_order_relaxed)==before);
    assert(raw->calls==2);
    for(int n=0;n<32;++n){
        assert(near(newL[n],oldL[n]));assert(near(newR[n],oldR[n]));
    }
    assert(!publisher.render(epoch+1,pins,2,newL.data(),newR.data(),
                             32,false,fallback));
    assert(newL[31]==0.f&&newR[31]==0.f);
    assert(publisher.acceptedRevision()==1);
}

// M-U1 source/processing format independence: a mono microphone through a
// stereo native rack must retain the same equal-power PAN as M4. Conversely
// physical stereo folded to mono processing must not become stereo-balance.
// Compare exact physical L/R PCM for all four source/format combinations.
static void verifySourceFormatIndependentPanPcmParity() {
    struct Case {bool sourceStereo,processStereo;float pan;};
    constexpr std::array<Case,6> cases{{
        {false,false,-.6f},{false,true,-.6f},
        {false,true,.6f},{true,false,.6f},
        {true,false,-.6f},{true,true,.6f}
    }};
    constexpr std::array<int,2> ports{3,7};
    std::array<float,32> mic{},micR{};
    for(int i=0;i<32;++i){
        mic[i]=.15f+.001f*i;
        micR[i]=.35f-.002f*i;
    }
    const float* physical[]{mic.data(),micR.data()};
    constexpr std::uint64_t epoch=800;
    const auto noFx=+[](ChannelId,float*,float*,int) noexcept {};
    for(const auto& choice:cases) {
        ChannelRoutingConfig cfg;
        cfg.stripsAndMaster.strips[0]={
            choice.sourceStereo?SourceMode::stereo:SourceMode::mono,
            3,choice.sourceStereo?7:-1,.7f,choice.pan,false,false
        };
        cfg.formats[0]=choice.processStereo?
            ChannelFormat::stereo:ChannelFormat::mono;
        SendRequest sends;AuxBusRequest aux;
        PreparedRenderGraph oldRoute;RenderError legacyError;
        assert(PreparedRenderGraph::compile(
            cfg,ports,sends,oldRoute,legacyError,aux));
        auto old=std::make_unique<LiveChannelFxGraph>(oldRoute,32,32);
        assert(old->setStripControl(0,.7f,choice.pan,
                                  !choice.sourceStereo||!choice.processStereo,
                                  false,false));
        std::array<float,32> oldL{},oldR{},nativeL{},nativeR{};
        assert(old->process(physical,2,oldL.data(),oldR.data(),32));
        DynamicNativeSessionPublisher native;
        UnifiedLegacyAccepted accepted;
        std::array<float,stripCount+auxBusCount> monitor{};
        VerifiedNativeOutputs outs{2,0,1,-1,-1,false};
        auto admission=UnifiedLegacyProductionAdmission::compileAndCommit(
            cfg,ports,sends,aux,monitor,*old,false,outs,
            32,2u*1024u*1024u,0,epoch,epoch,true,native,accepted);
        assert(admission.ok());
        const auto before=heapCalls.load(std::memory_order_relaxed);
        assert(native.render(epoch,physical,2,nativeL.data(),
                             nativeR.data(),32,false,noFx));
        assert(heapCalls.load(std::memory_order_relaxed)==before);
        for(int i=0;i<32;++i){
            assert(near(nativeL[i],oldL[i]));
            assert(near(nativeR[i],oldR[i]));
        }
        // A stale physical endpoint must fail silent and not change ACK.
        assert(!native.render(epoch+1,physical,2,nativeL.data(),
                              nativeR.data(),32,false,noFx));
        assert(nativeL[31]==0.f&&nativeR[31]==0.f);
        assert(native.acceptedRevision()==1);
    }
}

// G0: This is the real JUCE device/callback STATUS -> v6 Console wire,
// not a dry ChannelId graph or simulated universal engine. It must report
// measured M4 PCM while all unsupported ChannelId operations stay CLOSED.
static void verifyNativeConsoleWireRejectsUnsupportedM4AndPreservesEpoch() {
    using Bridge=armix::console::LiveConsoleBridgeV6;
    auto route=std::make_unique<juce::DynamicObject>();
    constexpr juce::int64 epoch=9007199254740993LL; // beyond JS Number safety
    route->setProperty("epoch",epoch);
    route->setProperty("revision",juce::int64(77));
    route->setProperty("active",true);
    route->setProperty("ready",true);
    route->setProperty("hasAcceptedRoute",true);
    juce::Array<juce::var> ports;
    auto port=std::make_unique<juce::DynamicObject>();
    port->setProperty("port",2);port->setProperty("callback",0);
    port->setProperty("label","ASIO Input 3");
    ports.add(juce::var(port.release()));
    route->setProperty("ports",juce::var(ports));
    juce::Array<juce::var> outputs;
    outputs.add(1);outputs.add(4);
    route->setProperty("outputPorts",juce::var(outputs));
    juce::Array<juce::var> levels;
    levels.add(.125);levels.add(0.0);
    route->setProperty("channelPeaks",juce::var(levels));
    auto status=std::make_unique<juce::DynamicObject>();
    status->setProperty("dryRoutingLab",juce::var(route.release()));
    status->setProperty("driver","ASIO TEST");
    status->setProperty("inputDevice","TEST CAPTURE");
    status->setProperty("outputDevice","TEST RENDER");
    status->setProperty("connected",true);
    status->setProperty("directRouteAuthorized",true);
    status->setProperty("monitor",true);
    status->setProperty("callbackCount",juce::int64(12));
    status->setProperty("callbackInputChannels",1);
    status->setProperty("inputPeak",.25);
    juce::Array<juce::var> perPort;
    perPort.add(.25);
    status->setProperty("captureCallbackPeaks",juce::var(perPort));
    status->setProperty("peak",.125);
    status->setProperty("rate",48000);
    status->setProperty("buffer",128);
    const auto live=juce::var(status.release());
    auto snap=Bridge::snapshot(live);
    auto device=snap.getProperty("device",{});
    auto graph=snap.getProperty("graph",{});
    auto measured=snap.getProperty("telemetry",{});
    assert(snap.getProperty("schemaVersion",0)==6);
    assert(!static_cast<bool>(snap.getProperty("productionCallbackLive",true)));
    assert(device.getProperty("epoch","").toString()=="9007199254740993");
    assert(static_cast<bool>(device.getProperty("outputArmed",false)));
    assert(static_cast<bool>(measured.getProperty("rawCapturePresent",false)));
    assert(static_cast<bool>(measured.getProperty("mainPcmPresent",false)));
    assert(graph.getProperty("revision","").toString()=="0");
    assert(graph.getProperty("rosterRevision","").toString()=="0");
    assert(graph.getProperty("channels",{}).getArray()->isEmpty());
    const auto caps=graph.getProperty("capabilities",{});
    assert(!static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",true)));
    assert(caps.getProperty("supportedOps",{}).getArray()->isEmpty());
    auto ins=device.getProperty("inputEndpoints",{});
    auto outs=device.getProperty("outputEndpoints",{});
    assert(ins.getArray()->size()==1 && outs.getArray()->size()==2);
    assert(ins.getArray()->getReference(0).getProperty("port",0)==2);
    assert(static_cast<double>(ins.getArray()->getReference(0)
        .getProperty("rawPeak",0.0))==.25);
    assert(static_cast<bool>(ins.getArray()->getReference(0)
        .getProperty("rawPresent",false)));
    assert(outs.getArray()->getReference(0).getProperty("port",0)==1);
    assert(outs.getArray()->getReference(1).getProperty("port",0)==4);
    const auto diagnostic=snap.getProperty("legacyM4",{});
    assert(diagnostic.getProperty("routingRevision","").toString()=="77");
    Bridge bridge;
    const auto intent=[&](const juce::String& req,const juce::String& ep,
                          const juce::String& rev="0") {
        auto o=std::make_unique<juce::DynamicObject>();
        o->setProperty("requestId",req);
        o->setProperty("deviceEpoch",ep);
        o->setProperty("baseGraphRevision",rev);
        o->setProperty("baseRosterRevision","0");
        o->setProperty("operation","SET_SOURCE");
        o->setProperty("targetChannelId","18446744073709551615");
        return juce::var(o.release());
    };
    const auto pending=intent("editor:nonce:1","9007199254740993");
    auto ack=bridge.rejectUnsupportedIntent(pending,snap);
    assert(ack.getProperty("requestId","").toString()=="editor:nonce:1");
    assert(ack.getProperty("status","").toString()=="REJECTED");
    assert(ack.getProperty("errorCode","").toString()=="UNIVERSAL_CALLBACK_NOT_READY");
    assert(ack.getProperty("graphRevision","").toString()=="0");
    assert(bridge.rejectUnsupportedIntent(pending,snap)
                .getProperty("errorCode","").toString()=="DUPLICATE_REQUEST_ID");
    assert(bridge.rejectUnsupportedIntent(intent("editor:nonce:2","9007199254740992"),snap)
                .getProperty("errorCode","").toString()=="STALE_DEVICE_EPOCH");
    assert(bridge.rejectUnsupportedIntent(intent("editor:nonce:3","9007199254740993","9"),snap)
                .getProperty("errorCode","").toString()=="STALE_GRAPH_REVISION");
    std::uint64_t decoded=0;
    assert(Bridge::decodeDecimal(juce::var("18446744073709551615"),decoded));
    assert(decoded==std::numeric_limits<std::uint64_t>::max());
    assert(!Bridge::decodeDecimal(juce::var("18446744073709551616"),decoded));
    assert(!Bridge::decodeDecimal(juce::var("01"),decoded));
    assert(!Bridge::decodeDecimal(juce::var(9007199254740993.0),decoded));
    // No request above mutates the genuinely accepted M4 hardware route.
    assert(live.getProperty("dryRoutingLab",{}).getProperty("revision",0)==77);
}

int main() {
    // R0: existing CH1 FX chain may be rendered inside the SAME prepared
    // ten-channel routing pass, without creating a second plugin or copying
    // a processor. Verify exactly one call and zero callback heap allocation.
    {
        auto fx=makeGraph(2.f,1.f,false);
        std::array<float,4> input{.4f,.4f,.4f,.4f};
        std::array<float,4> silent{};
        const float* pins[]{input.data(),silent.data(),silent.data()};
        std::array<float,4> l{},r{};
        int legacyCalls=0;
        const auto external=+[](void* context,int strip,float* left,float* right,int count) noexcept {
            assert(strip==0);
            ++*static_cast<int*>(context);
            for(int n=0;n<count;++n) {left[n]*=2.f;right[n]*=2.f;}
        };
        const auto before=heapCalls.load(std::memory_order_relaxed);
        assert(fx->process(pins,3,l.data(),r.data(),4,nullptr,&legacyCalls,external));
        assert(heapCalls.load(std::memory_order_relaxed)==before);
        assert(legacyCalls==1);
        // Mono CH1 PRE at center is attenuated sqrt(.5) per side.
        constexpr float monoCenter=0.7071067811865475f;
        assert(near(l[0],.4f*2.f*.5f*monoCenter)&&
               near(r[0],.4f*2.f*.5f*monoCenter));
        assert(fx->activeInserts()==0);
        // Without the external lease the unmodified route remains dry.
        assert(fx->process(pins,3,l.data(),r.data(),4));
        assert(near(l[0],.4f*.5f*monoCenter)&&near(r[0],.4f*.5f*monoCenter));
    }
    // This test executes actual JUCE AudioProcessor::processBlock for
    // DIFFERENT strip instances and a BUS return, no VST3 vendor binaries.
    constexpr float monoPreCenter=0.7071067811865475f;
    using Publication=armix::rt::RealtimeRoutePublisher<LiveChannelFxGraph>;
    Publication routes;
    auto first=std::make_unique<Publication::Snapshot>();
    first->deviceEpoch=11;first->graphRevision=1;
    first->prepared=makeGraph(3.f,2.f);
    assert(routes.publish(std::move(first),0,11)==Publication::Result::applied);
    const std::array<float,4> p2{.4f,.4f,.4f,.4f};
    const std::array<float,4> silent{};
    const float* phys[]{p2.data(),silent.data(),silent.data()};
    std::array<float,4> l{},r{};
    {
        auto lease=routes.acquire();
        assert(lease.get(11));
        auto* fx=lease.get(11)->prepared.get();
        assert(fx->activeInserts()==2);
        const auto before=heapCalls.load(std::memory_order_relaxed);
        RenderLevels meters;
        assert(fx->process(phys,3,l.data(),r.data(),4,&meters));
        assert(heapCalls.load(std::memory_order_relaxed)==before);
        // .4 * CH1 FX 3 * PRE .5 * mono center * BUS FX 2.
        assert(near(l[0],1.2f*monoPreCenter)&&near(r[0],1.2f*monoPreCenter));
        assert(near(meters.channelPeak[1],1.2f*monoPreCenter));
        assert(near(meters.channelPeak[0],0.f)); // fader zero, PRE audible
        auto newer=std::make_unique<Publication::Snapshot>();
        newer->deviceEpoch=11;newer->graphRevision=2;
        newer->prepared=makeGraph(2.f,4.f);
        assert(routes.publish(std::move(newer),1,11)==Publication::Result::applied);
        assert(!routes.pollRetirement()); // first graph still reader-leased
        assert(GainFx::living==4); // 2 FX per graph; no shared pointers
        assert(fx->process(phys,3,l.data(),r.data(),4));
        assert(near(l[0],1.2f*monoPreCenter));
    }
    assert(routes.pollRetirement());
    assert(GainFx::living==2); // old VST instances retired off audio thread
    {
        auto lease=routes.acquire();
        assert(lease.get(11));
        assert(lease.get(11)->prepared->process(phys,3,l.data(),r.data(),4));
        // .4 * 2 * .5 * mono center * BUS FX 4.
        assert(near(l[0],1.6f*monoPreCenter));
        assert(!lease.get(12)); // stale driver epoch is silent
    }
    routes.unpublish();
    assert(routes.pollRetirement());
    assert(GainFx::living==0);
    assert(GainFx::released==4);
    // P1J: route revision 2 reuses exact native VST3s from revision 1.
    // No duplicate creation, prepare/release, state loss, or RT heap use.
    // Retirement of old snapshot must NOT release shared FX still needed.
    {
        Publication stable;
        auto one=std::make_unique<Publication::Snapshot>();
        one->deviceEpoch=71;one->graphRevision=1;
        one->prepared=makeGraph(3.f,2.f);
        assert(stable.publish(std::move(one),0,71)==Publication::Result::applied);
        auto* previous=stable.controlPrepared(71,1);
        assert(previous && previous->activeInserts()==2);
        assert(!stable.controlPrepared(72,1));
        assert(!stable.controlPrepared(71,3));
        const auto* identity=previous->instanceProcessor(0);
        const auto* busIdentity=previous->instanceProcessor(1);
        const int livingBefore=GainFx::living;
        const int preparedBefore=GainFx::prepared;
        const int releasedBefore=GainFx::released;
        auto next=std::make_unique<Publication::Snapshot>();
        next->deviceEpoch=71;next->graphRevision=2;
        next->prepared=makeGraph(6.f,9.f,false);
        assert(!next->prepared->reuse(0,*previous,"wrong-vst3"));
        assert(next->prepared->reuse(0,*previous,"vst3-ch1-id"));
        assert(next->prepared->reuse(1,*previous,"vst3-bus-id"));
        assert(!next->prepared->reuse(0,*previous,"vst3-ch1-id"));
        assert(next->prepared->activeInserts()==2);
        assert(next->prepared->instanceProcessor(0)==identity);
        assert(next->prepared->instanceProcessor(1)==busIdentity);
        assert(GainFx::living==livingBefore&&GainFx::prepared==preparedBefore);
        {
            auto oldReader=stable.acquire();
            assert(oldReader.get(71));
            assert(stable.publish(std::move(next),1,71)==Publication::Result::applied);
            assert(!stable.pollRetirement());
        }
        assert(stable.pollRetirement());
        assert(GainFx::living==livingBefore);
        assert(GainFx::released==releasedBefore);
        {
            auto live=stable.acquire();
            assert(live.get(71));
            assert(live.get(71)->prepared->instanceProcessor(0)==identity);
            const auto allocated=heapCalls.load(std::memory_order_relaxed);
            assert(live.get(71)->prepared->process(phys,3,l.data(),r.data(),4));
            assert(heapCalls.load(std::memory_order_relaxed)==allocated);
            assert(near(l[0],1.2f*monoPreCenter)); // original FX gain preserved
        }
        stable.unpublish();
        assert(stable.pollRetirement());
        assert(GainFx::living==0);
        assert(GainFx::released==releasedBefore+2);
    }
    // P1K: real JUCE processors on both a physical INPUT and a dedicated
    // AUX. All ten input channels remain available while this bus is running.
    {
        constexpr std::array physical{2,5,9};
        ChannelRoutingConfig route;
        route.stripsAndMaster.strips[0]={SourceMode::mono,2,-1,0.f,0.f,false,false};
        SendRequest sends; sends.toMaster[0]=false;
        AuxBusRequest aux;
        aux.buses[0]={true,true,false,1.f};
        aux.count=1;
        aux.sends[0]={0,0,SendTap::preFader,.5f,0.f,true};
        PreparedRenderGraph plan; RenderError error;
        assert(PreparedRenderGraph::compile(route,physical,sends,plan,error,aux));
        auto fx=std::make_unique<LiveChannelFxGraph>(plan,32,32);
        auto dry=std::make_unique<GainFx>(2.f);
        auto wet=std::make_unique<GainFx>(3.f);
        dry->prepareToPlay(48000,32);
        wet->prepareToPlay(48000,32);
        assert(fx->install(0,std::move(dry),"CH1","source-vst3"));
        assert(fx->install(stripCount,std::move(wet),"AUX1","aux-vst3"));
        assert(fx->activeInserts()==2);
        const auto* originalBus=fx->instanceProcessor(stripCount);
        RenderLevels levels;
        const auto before=heapCalls.load(std::memory_order_relaxed);
        assert(fx->process(phys,3,l.data(),r.data(),4,&levels));
        assert(heapCalls.load(std::memory_order_relaxed)==before);
        assert(near(l[0],1.2f*monoPreCenter)&&near(r[0],1.2f*monoPreCenter));
        assert(near(levels.auxPeak[0],1.2f*monoPreCenter));
        auto second=std::make_unique<LiveChannelFxGraph>(plan,32,32);
        assert(second->reuse(0,*fx,"source-vst3"));
        assert(second->reuse(stripCount,*fx,"aux-vst3"));
        assert(second->instanceProcessor(stripCount)==originalBus);
        assert(!second->reuse(stripCount,*fx,"wrong-id"));
        const int oldPrepared=GainFx::prepared,oldReleased=GainFx::released;
        fx.reset(); // AUX processor must remain live in second graph
        assert(GainFx::released==oldReleased);
        assert(second->process(phys,3,l.data(),r.data(),4,&levels));
        assert(near(l[0],1.2f*monoPreCenter));
        assert(GainFx::prepared==oldPrepared);
        second.reset();
        assert(GainFx::released==oldReleased+2);
        assert(GainFx::living==0);
    }
    // G5A: actual sequential processors on CH2 with bounded 8-slot racks,
    // atomic-snapshot-compatible reuse from a different position, and NO
    // callback allocation. This is the DSP foundation, not the GUI protocol.
    {
        constexpr std::array ports{2,5,9};
        ChannelRoutingConfig config;
        config.stripsAndMaster.strips[1]={SourceMode::mono,5,-1,1.f,0.f,false,false};
        SendRequest sends;
        PreparedRenderGraph routing;RenderError error;
        assert(PreparedRenderGraph::compile(config,ports,sends,routing,error));
        auto baseline=std::make_unique<LiveChannelFxGraph>(routing,32,32);
        auto multi=std::make_unique<LiveChannelFxGraph>(routing,32,32);
        auto first=std::make_unique<GainFx>(2.f);
        auto second=std::make_unique<GainFx>(3.f);
        first->prepareToPlay(48000,32);
        second->prepareToPlay(48000,32);
        assert(!multi->installAt(1,1,std::make_unique<GainFx>(9.f),"gap"));
        assert(multi->installAt(1,0,std::move(first),"FIRST","first-id"));
        assert(multi->installAt(1,1,std::move(second),"SECOND","second-id"));
        assert(multi->activeInserts()==2&&multi->insertsOn(1)==2);
        assert(multi->insertId(1,1)=="second-id");
        assert(!multi->installAt(1,1,std::make_unique<GainFx>(9.f),"duplicate"));
        assert(!multi->installAt(1,8,std::make_unique<GainFx>(9.f),"overflow"));
        std::array<float,4> source{.2f,.2f,.2f,.2f},zero{};
        const float* pins[]{zero.data(),source.data(),zero.data()};
        std::array<float,4> cleanL{},cleanR{},wetL{},wetR{};
        const auto heapBefore=heapCalls.load(std::memory_order_relaxed);
        assert(baseline->process(pins,3,cleanL.data(),cleanR.data(),4));
        assert(multi->process(pins,3,wetL.data(),wetR.data(),4));
        assert(heapCalls.load(std::memory_order_relaxed)==heapBefore);
        assert(near(wetL[0],cleanL[0]*6.f)&&near(wetR[0],cleanR[0]*6.f));
        // G5C native PCM gate: bypass really changes signal while preserving
        // dry continuity, without callback allocations or plugin replacement.
        const auto stableToken=multi->insertInstanceId(1,0);
        // G5E: capturing an actual JUCE vendor state is a CONTROL operation,
        // not an RT callback allocation or a graph instance replacement.
        {
            auto owner=multi->leaseInsert(1,0,stableToken);
            assert(owner);
            juce::MemoryBlock bytes;
            assert(owner->tryCaptureState(bytes));
            assert(bytes.getSize()==sizeof(float));
            // Restore into an independent vendor instance, never mutate the
            // audible graph's reused processor before publication.
            GainFx candidate(9.f);
            candidate.setStateInformation(bytes.getData(),
                static_cast<int>(bytes.getSize()));
            std::array<float,4> left{.25f,.25f,.25f,.25f},right=left;
            float* output[]{left.data(),right.data()};
            juce::AudioBuffer<float> buffer(output,2,4);
            juce::MidiBuffer midi;
            candidate.processBlock(buffer,midi);
            assert(near(left[0],.5f));
            assert(owner->instanceId==stableToken);
        }
        assert(stableToken.isNotEmpty());
        assert(!multi->setInsertBypass(1,0,"stale-token",true));
        assert(multi->setInsertBypass(1,0,stableToken,true));
        const auto bypassHeap=heapCalls.load(std::memory_order_relaxed);
        for(int n=0;n<130;++n)
            assert(multi->process(pins,3,wetL.data(),wetR.data(),4));
        assert(heapCalls.load(std::memory_order_relaxed)==bypassHeap);
        assert(multi->insertBypassed(1,0));
        assert(near(wetL[0],cleanL[0]*3.f));
        assert(multi->setInsertBypass(1,0,stableToken,false));
        for(int n=0;n<130;++n)
            assert(multi->process(pins,3,wetL.data(),wetR.data(),4));
        assert(near(wetL[0],cleanL[0]*6.f));
        const auto* firstAddress=multi->instanceProcessor(1,0);
        const auto* secondAddress=multi->instanceProcessor(1,1);
        const int releaseBefore=GainFx::released;
        auto reordered=std::make_unique<LiveChannelFxGraph>(routing,32,32);
        assert(reordered->reuseAt(1,0,*multi,1,"second-id"));
        assert(!reordered->reuseAt(1,1,*multi,1,"second-id")); // same instance twice
        assert(reordered->reuseAt(1,1,*multi,0,"first-id"));
        assert(reordered->instanceProcessor(1,0)==secondAddress);
        assert(reordered->instanceProcessor(1,1)==firstAddress);
        assert(reordered->insertInstanceId(1,1)==stableToken);
        assert(!reordered->setInsertBypass(1,0,stableToken,true));
        assert(reordered->setInsertBypass(1,1,stableToken,true));
        assert(reordered->insertBypassed(1,1));
        assert(reordered->setInsertBypass(1,1,stableToken,false));
        assert(reordered->insertsOn(1)==2&&reordered->activeInserts()==2);
        multi.reset(); // old snapshot retired; new lease keeps both vendor instances
        assert(GainFx::released==releaseBefore);
        const auto beforeReplay=heapCalls.load(std::memory_order_relaxed);
        assert(reordered->process(pins,3,wetL.data(),wetR.data(),4));
        assert(heapCalls.load(std::memory_order_relaxed)==beforeReplay);
        assert(near(wetL[0],cleanL[0]*6.f));
        reordered.reset();
        assert(GainFx::released==releaseBefore+2);
        assert(GainFx::living==0);
    }
    // G5D: a native vendor editor keeps its processor alive across retirement
    // on CONTROL only, and stale positions cannot lease the same processor.
    {
        auto graph=makeGraph(2.f,1.f);
        const auto token=graph->insertInstanceId(0,0);
        const auto released=GainFx::released;
        auto editorLease=graph->leaseInsert(0,0,token);
        assert(editorLease && editorLease->instanceId==token);
        assert(!graph->leaseInsert(0,1,token));
        assert(!graph->leaseInsert(0,0,"stale-editor"));
        const auto before=heapCalls.load(std::memory_order_relaxed);
        std::array<float,4> source{.1f,.1f,.1f,.1f},silent{},l{},r{};
        const float* pins[]{source.data(),silent.data(),silent.data()};
        // Old graph's RT samples still require no smart-pointer bookkeeping.
        assert(graph->process(pins,3,l.data(),r.data(),4));
        assert(heapCalls.load(std::memory_order_relaxed)==before);
        graph.reset();
        assert(GainFx::living==1); // only leased instance remains
        assert(GainFx::released==released+1); // other graph FX retired
        editorLease.reset();
        assert(GainFx::living==0&&GainFx::released==released+2);
    }
    // MS3A2: selected sparse MAIN outputs 5/6, not legacy 1/2.
    // The same native PCM tap is shared by de-click and loudness.
    {
        std::array<std::array<float,8>,6> physical{};
        float* outputs[6]{};
        for(int i=0;i<6;++i)outputs[i]=physical[static_cast<size_t>(i)].data();
        const auto tapped=NativeOutputPairTap::select(outputs,6,{{4,5}});
        assert(tapped[0]==outputs[4]&&tapped[1]==outputs[5]);
        tapped[0][0]=0.7f;tapped[1][0]=-0.4f;
        assert(physical[0][0]==0.f&&physical[1][0]==0.f);
        assert(near(physical[4][0],0.7f)&&near(physical[5][0],-0.4f));
        for(const auto invalid:{std::array<int,2>{{-1,-1}},
                                std::array<int,2>{{4,4}},
                                std::array<int,2>{{4,6}}}) {
            const auto tap=NativeOutputPairTap::select(outputs,6,invalid);
            assert(tap[0]==nullptr&&tap[1]==nullptr);
        }
        outputs[5]=nullptr;
        const auto absent=NativeOutputPairTap::select(outputs,6,{{4,5}});
        assert(absent[0]==nullptr&&absent[1]==nullptr);
    }
    // MS3B: 2-output devices cannot host a second physical stereo bus.
    // An 8-output device admits independent 1/2 MAIN and 5/6 MONITOR
    // without duplicate ownership, fabricated pins, or invalid send gain.
    {
        auto graph=makeGraph(1.f,1.f);
        std::array<float,LiveChannelFxGraph::fxSlots> sends{};
        sends[1]=.5f;
        assert(!graph->setMonitorOutputBus(2,3,2,sends));
        assert(!graph->setMonitorOutputBus(0,1,8,sends));
        assert(!graph->setMonitorOutputBus(4,4,8,sends));
        assert(!graph->setMonitorOutputBus(4,8,8,sends));
        sends[1]=std::numeric_limits<float>::quiet_NaN();
        assert(!graph->setMonitorOutputBus(4,5,8,sends));
        sends[1]=.5f;
        assert(graph->setMonitorOutputBus(4,5,8,sends));
        assert(graph->monitorEnabled());
        assert(graph->monitorOutputLeft()==4&&graph->monitorOutputRight()==5);
        assert(near(graph->monitorGains()[1],.5f));
    }
    // FAT-X: an unpatched strip can own a real, reusable FX lease without
    // any fabricated input PCM, RT allocation or second render callback.
    {
        constexpr std::array active{2,5};
        ChannelRoutingConfig input;
        input.stripsAndMaster.strips[0]={
            SourceMode::mono,2,-1,1.f,0.f,false,false};
        SendRequest sends;
        PreparedRenderGraph original; RenderError error;
        assert(PreparedRenderGraph::compile(input,active,sends,original,error));
        auto dryGraph=std::make_unique<LiveChannelFxGraph>(original,32,32);
        auto fx=std::make_unique<GainFx>(2.f);fx->prepareToPlay(48000,32);
        assert(dryGraph->install(6,std::move(fx),"CH07 silent FX","fat-x-7"));
        const auto* identity=dryGraph->instanceProcessor(6);
        const std::array<float,4> samples{.25f,.25f,.25f,.25f};
        const float* pins[]{samples.data(),samples.data()};
        std::array<float,4> left{},right{};
        const auto before=heapCalls.load(std::memory_order_relaxed);
        assert(dryGraph->process(pins,2,left.data(),right.data(),4));
        assert(heapCalls.load(std::memory_order_relaxed)==before);
        assert(near(left[0],.25f*.7071067811865475f));
        input.stripsAndMaster.strips[6]={
            SourceMode::mono,5,-1,1.f,0.f,false,false};
        PreparedRenderGraph patched;
        assert(PreparedRenderGraph::compile(input,active,sends,patched,error));
        auto next=std::make_unique<LiveChannelFxGraph>(patched,32,32);
        assert(next->reuse(6,*dryGraph,"fat-x-7"));
        assert(next->instanceProcessor(6)==identity);
        // Graph construction and instance leasing are CONTROL-thread work
        // and legitimately allocate. Only the PROCESS callback must not.
        const auto beforeNextProcess=heapCalls.load(std::memory_order_relaxed);
        assert(next->process(pins,2,left.data(),right.data(),4));
        assert(heapCalls.load(std::memory_order_relaxed)==beforeNextProcess);
        assert(left[0]>.3f);
    }
    // Run M5 AFTER the legacy suite: older M4 tests use absolute global
    // GainFx::living/released counters as their own lifetime oracle. Running
    // M5 before them changes those historical baseline counts, not DSP.
    // M5 additionally asserts zero heap and valid vendor leases itself.
    verifyUniversalChannelIdFxLeaseAndLivePcm();
    verifyMU1LegacyM4FxFullMigrationAndMonitorPcm();
    verifyOccupiedExternalCh1Owner();
    verifySourceFormatIndependentPanPcmParity();
    verifyNativeConsoleWireRejectsUnsupportedM4AndPreservesEpoch();
    // Tests above use a real JUCE AudioProcessor processing seam. Vendor
    // VST3 formats and sound quality require separate Windows hardware FAT.
}
