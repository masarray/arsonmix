// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/LiveChannelFxGraph.h"
#include "../engine/builtin_fx/ChannelEqProcessor.h"
#include "../engine/core/DynamicChannelFxLeaseBank.h"
#include "../engine/core/NativeFxRackTransaction.h"
#include "../engine/core/DynamicNativeSessionPublisher.h"
#include "../engine/core/UnifiedLegacyProductionAdmission.h"
#include "../engine/core/NativeOutputPairTap.h"
#include "../engine/core/RealtimeRoutePublisher.h"
#include "../src/ConsoleLiveBridgeV6.h"
#include "../src/NativeVerifiedSourcePatch.h"
#include "../engine/core/NativeDryRouteCommand.h"
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
#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    explicit GainFx(float gain,int delay=0,float offset=0)
      : AudioProcessor(BusesProperties()
        .withInput("Main",juce::AudioChannelSet::stereo(),true)
        .withOutput("Main",juce::AudioChannelSet::stereo(),true)),gain_(gain),offset_(offset),delaySamples_(delay){++living;setLatencySamples(delay);}
    ~GainFx() override{--living;}
    const juce::String getName()const override{return "In-memory gain effect";}
    void prepareToPlay(double,int)override{++prepared;assert(delay_.prepare(delaySamples_));}
    void releaseResources()override{++released;}
    void processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&)override{
        for(int c=0;c<buffer.getNumChannels();++c)
            for(int f=0;f<buffer.getNumSamples();++f)
                buffer.getWritePointer(c)[f]=buffer.getWritePointer(c)[f]*gain_+offset_;
        for(int f=0;f<buffer.getNumSamples();++f){
            float& l=buffer.getWritePointer(0)[f];float& r=buffer.getWritePointer(1)[f];
            delay_.process(l,r);
        }
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
    float gain_,offset_=0;
    int delaySamples_=0;
    armix::dsp::StereoLatencyDelay delay_;
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
    // Production control transaction appends a NEW real vendor instance
    // to CH11 without touching original CH1, Group or FX Return owners.
    Bank expanded;
    auto second=newFx(1.25f,"ch11-second-vst3");
    assert(Bank::appendOwned(epoch,3,bank,ch11,ProcessingFormat::mono,
                             second,expanded)==Bank::Fault::none);
    // Rack OFF applies to the new plugin too; old per-plugin OFF remains
    // untouched and native append carries the same authoritative override.
    assert(bank.setRackBypass(epoch,2,ch11,true));
    assert(bank.rackBypassed(ch11));
    assert(expanded.insertCount(ch11)==2);
    // expanded was already created before the OFF gesture. Its revision
    // holds the same shared rack gate, so it observes the control live.
    assert(expanded.rackBypassed(ch11));
    assert(!expanded.setRackBypass(epoch,2,ch11,false)); // stale version
    assert(expanded.setRackBypass(epoch,3,ch11,false));
    assert(!bank.rackBypassed(ch11));
    assert(!expanded.rackBypassed(ch11));
    assert(expanded.insertCount(ch1)==bank.insertCount(ch1));
    assert((*expanded.ownerForControl(ch11,0))->instanceId==
           (*bank.ownerForControl(ch11,0))->instanceId);
    assert((*expanded.ownerForControl(ch11,1))->instanceId==second->instanceId);
    assert(Bank::appendOwned(epoch,2,bank,ch11,ProcessingFormat::mono,
                             second,expanded)==Bank::Fault::invalidEpoch);
    assert(expanded.revision()==3);
    assert(Bank::appendOwned(epoch,4,expanded,ch11,ProcessingFormat::mono,
                             second,bank)==Bank::Fault::duplicateInstance);
    assert(bank.revision()==2); // rollback left original accepted rack intact

    assert(bank.ownerForControl(ch1,0)&&
           (*bank.ownerForControl(ch1,0))->instanceId==
              original->insertInstanceId(0,0));
    Bank blocked;
    std::array<Bank::Assignment,2> alias{first,groupOwner};
    alias[1].id=ch11;
    alias[1].inserts=first.inserts; // same native processor twice
    assert(Bank::prepare(epoch,2,alias,blocked)==Bank::Fault::duplicateInstance);
    assert(blocked.lanes()==0);
    // A fixed-latency VST lease is admitted; the PCM publisher consumes its
    // exact inventory to prepare fan-in compensation before publication.
    auto latentProcessor=std::make_unique<GainFx>(1.5f);
    latentProcessor->setLatencySamples(64);
    latentProcessor->prepareToPlay(48000.0,64);
    Bank::Assignment latent;
    latent.id=ch11;latent.format=ProcessingFormat::mono;
    latent.inserts.push_back(std::make_shared<LiveChannelFxGraph::FxInstance>(
        std::move(latentProcessor),"latent-vst","latency-64",64));
    const std::array<Bank::Assignment,1> latentScene{latent};
    assert(Bank::prepare(epoch,2,latentScene,blocked)==
           Bank::Fault::none);
    assert(blocked.latencySamples(ch11)==64);
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
    // E2: bypass a REAL hosted JUCE VST in the SAME native ChannelId graph.
    // Acknowledged plugin identity, epoch and revision must all agree. The
    // bypass uses FxInstance's existing 8ms audio-thread envelope, not a
    // second DSP path or a control-thread plugin restart.
    auto* acceptedBank=static_cast<Bank*>(ownerPlan.context);
    const auto ch11Id=(*acceptedBank->ownerForControl(ch11,0))->instanceId;
    const int livingAtBypass=GainFx::living;
    assert(!acceptedBank->setInsertBypass(epoch-1,2,ch11,0,ch11Id,true));
    assert(!acceptedBank->setInsertBypass(epoch,1,ch11,0,ch11Id,true));
    assert(!acceptedBank->setInsertBypass(epoch,2,ch11,0,"wrong-token",true));
    assert(!acceptedBank->setInsertBypass(epoch,2,ch11,1,ch11Id,true));
    {
        auto editorLease=acceptedBank->leaseEditor(epoch,2,ch11,0,ch11Id);
        assert(editorLease && editorLease->instanceId==ch11Id);
        assert(!acceptedBank->leaseEditor(epoch,1,ch11,0,ch11Id));
        assert(!acceptedBank->leaseEditor(epoch,2,ch11,0,"wrong-token"));
    }
    assert(acceptedBank->setInsertBypass(epoch,2,ch11,0,ch11Id,true));
    for(int i=0;i<16;++i)
        assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,
                             legacyFx));
    assert(near(left[31],((.2f*2.f+.1f)*3.f+.1f*.5f*2.f)*center));
    assert(GainFx::living==livingAtBypass && legacyFallbackCalls==0);
    assert(acceptedBank->setInsertBypass(epoch,2,ch11,0,ch11Id,false));
    for(int i=0;i<16;++i)
        assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,
                             legacyFx));
    assert(near(left[31],((.2f*2.f+.1f*4.f)*3.f+
                          (.1f*4.f)*.5f*2.f)*center));
    assert(GainFx::living==livingAtBypass && legacyFallbackCalls==0);
    // M5.3 physical SOURCE hot switch reuses the exact live JUCE VST
    // instances; no catalog lookup, plugin restart, heap or extra renderer.
    const auto heapAtSource=heapCalls.load(std::memory_order_relaxed);
    const auto liveAtSource=GainFx::living;
    assert(native.setPhysicalSource(epoch,2,ch11,0,-1,false));
    assert(!native.setPhysicalSource(epoch,2,ch11,2,-1,false));
    assert(!native.setPhysicalSource(epoch-1,2,ch11,0,-1,false));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,legacyFx));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,legacyFx));
    assert(heapCalls.load(std::memory_order_relaxed)==heapAtSource);
    assert(GainFx::living==liveAtSource&&legacyFallbackCalls==0);
    assert(near(left[31],((.2f*2.f+.2f*4.f)*3.f+
                         (.2f*4.f)*.5f*2.f)*center));
    assert(native.setPhysicalSource(epoch,2,ch11,1,-1,false));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,legacyFx));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,legacyFx));
    assert(heapCalls.load(std::memory_order_relaxed)==heapAtSource);
    assert(GainFx::living==liveAtSource&&legacyFallbackCalls==0);
    assert(near(left[31],((.2f*2.f+.1f*4.f)*3.f+
                         (.1f*4.f)*.5f*2.f)*center));
    // Real hosted JUCE VST instances remain leased exactly once while
    // a muted GROUP silences serial dry PCM, not the parallel FX Return.
    const auto livingBeforeMute=GainFx::living;
    const auto heapBeforeMute=heapCalls.load(std::memory_order_relaxed);
    assert(native.setChannelMuteSolo(epoch,2,bus,true,false));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,legacyFx));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,legacyFx));
    assert(heapCalls.load(std::memory_order_relaxed)==heapBeforeMute);
    assert(GainFx::living==livingBeforeMute && legacyFallbackCalls==0);
    assert(near(left[31],(.1f*4.f)*.5f*2.f*center));
    assert(native.setChannelMuteSolo(epoch,2,bus,false,false));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,legacyFx));
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,legacyFx));
    assert(heapCalls.load(std::memory_order_relaxed)==heapBeforeMute);
    assert(GainFx::living==livingBeforeMute && legacyFallbackCalls==0);
    assert(near(left[31],((.2f*2.f+.1f*4.f)*3.f+
                         (.1f*4.f)*.5f*2.f)*center));
    // M-U2: the canonical ChannelId fader is smoothed over 64 SAMPLES,
    // not 64 frames-per-callback. A 32-frame backend callback must reach
    // only the halfway coefficient; the next 32 reaches the exact goal.
    // Neither render may allocate, reinstantiate or call the old M4 FX.
    assert(native.setChannelMix(epoch,2,bus,.5f,0.f));
    const auto beforeHalfHeap=heapCalls.load(std::memory_order_relaxed);
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,
                         legacyFx));
    assert(heapCalls.load(std::memory_order_relaxed)==beforeHalfHeap);
    assert(legacyFallbackCalls==0);
    assert(near(left[31],((.2f*2.f+.1f*4.f)*3.f*.75f+
                         (.1f*4.f)*.5f*2.f)*center));
    const auto half=left[31];
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,
                         legacyFx));
    assert(heapCalls.load(std::memory_order_relaxed)==beforeHalfHeap);
    assert(legacyFallbackCalls==0);
    assert(left[0]<half); // continuous: no sudden first-sample jump
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
    const auto heapBeforeReload=heapCalls.load(std::memory_order_relaxed);
    assert(native.render(epoch,pins,2,left.data(),right.data(),32,false,
                         legacyFx));
    assert(heapCalls.load(std::memory_order_relaxed)==heapBeforeReload);
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
    // E2 native physical path reads true per-ChannelId POST-fader left
    // and right peaks from the SAME render (no stale M4 GUI meter).
    std::array<float,stripCount+auxBusCount+2> nativeMeterL{},nativeMeterR{};
    assert(publisher.render(epoch,physical,4,dynamicL.data(),dynamicR.data(),
                            32,false,mustNeverCall,
                            dynamicML.data(),dynamicMR.data(),
                            nativeMeterL,nativeMeterR));
    assert(heapCalls.load(std::memory_order_relaxed)==before);
    assert(nativeMeterL[0]>0.f&&nativeMeterR[0]>0.f);
    assert(!near(nativeMeterL[0],nativeMeterR[0]));
    assert(nativeMeterL[stripCount]>0.f&&nativeMeterR[stripCount]>0.f);
    assert(nativeMeterL[stripCount+auxBusCount]>0.f);
    assert(nativeMeterL[stripCount+auxBusCount+1]>0.f);

    for(int i=0;i<32;++i) {
        assert(near(legacyL[i],dynamicL[i]));
        assert(near(legacyR[i],dynamicR[i]));
        assert(near(legacyML[i],dynamicML[i]));
        assert(near(legacyMR[i],dynamicMR[i]));
    }
    // E2 native accepted-state FX seam: stale epoch/revision/token is
    // rejected; a genuine VST bypass changes PCM through the SAME accepted
    // ChannelId processor lease, not through old M4 or a mock plug-in.
    const auto ch1Id=accepted.graph.ids.inputs[0];
    assert(!accepted.setFxBypass(publisher,epoch-1,1,ch1Id,0,oldCh1,true));
    assert(!accepted.setFxBypass(publisher,epoch,0,ch1Id,0,oldCh1,true));
    assert(!accepted.setFxBypass(publisher,epoch,1,ch1Id,0,"unowned",true));
    {
        auto editor=accepted.leaseFxEditor(publisher,epoch,1,ch1Id,0,oldCh1);
        assert(editor && editor->instanceId==oldCh1);
        assert(!accepted.leaseFxEditor(publisher,epoch,1,ch1Id,0,"unowned"));
    }
    assert(accepted.setFxBypass(publisher,epoch,1,ch1Id,0,oldCh1,true));
    const auto heapAtFxControl=heapCalls.load(std::memory_order_relaxed);
    for(int i=0;i<16;++i)
        assert(publisher.render(epoch,physical,4,dynamicL.data(),dynamicR.data(),
                                32,false,mustNeverCall,
                                dynamicML.data(),dynamicMR.data()));
    assert(heapCalls.load(std::memory_order_relaxed)==heapAtFxControl);
    assert(!near(legacyL[31],dynamicL[31]));
    assert(accepted.setFxBypass(publisher,epoch,1,ch1Id,0,oldCh1,false));
    for(int i=0;i<16;++i)
        assert(publisher.render(epoch,physical,4,dynamicL.data(),dynamicR.data(),
                                32,false,mustNeverCall,
                                dynamicML.data(),dynamicMR.data()));
    assert(near(legacyL[31],dynamicL[31]));
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
    std::weak_ptr<Owner> retiringOwner=vendor;
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
    // The original JUCE Graph has no ChannelId. The native ADMISSION,
    // not GUI strip ordinals, must bind the verified imported CH1 identity.
    lease.id=ChannelId{};
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
    // A supplied but WRONG nonzero ChannelId cannot hijack another lane.
    invalid=lease;invalid.id=ChannelId{987654};
    status=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitor,*old,true,outputs,
        32,2u*1024u*1024u,0,epoch,epoch,true,publisher,accepted,invalid);
    assert(status.fault==UnifiedLegacyProductionAdmission::Fault::legacyCh1ExternalFx);
    assert(publisher.acceptedRevision()==0);
    status=UnifiedLegacyProductionAdmission::compileAndCommit(
        channels,ports,sends,aux,monitor,*old,true,outputs,
        32,2u*1024u*1024u,0,epoch,epoch,true,publisher,accepted,lease);
    assert(status.ok()&&publisher.acceptedRevision()==1);
    assert(accepted.fxOwners->externalChannel()==migrated.ids.inputs[0]);
    // The publisher owns the exact vendor processBlock and GUI state,
    // regardless of the former M4 route/control owner's retirement.
    old.reset();vendor.reset();
    lease={};invalid={};
    assert(!retiringOwner.expired());
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

    // E3: the same physical publisher and original external VST owner
    // survive a true serial GROUP then a parallel wet FX_RETURN. There is
    // NO additional JUCE callback, source alias or vendor re-instantiation.
    // A bounded prefix meter must not reject a graph > fixed 10CH+4AUX.
    const auto publishBus=[&](ChannelRole role,const char* name,
                              float gain) {
        auto candidate=accepted.graph.roster;
        const ChannelId selected[]{accepted.graph.ids.inputs[0]};
        auto edit=candidate.createBusForSelected(
            candidate.revision(),selected,name,role,gain);
        assert(edit.ok()&&edit.createdIds.size()==1);
        DynamicChannelFxLeaseBank nextBank;
        const auto nextRevision=accepted.pcmRevision+1;
        assert(DynamicChannelFxLeaseBank::reuseRevision(
            epoch,nextRevision,*accepted.fxOwners,nextBank)==
            DynamicChannelFxLeaseBank::Fault::none);
        auto owner=std::make_shared<DynamicChannelFxLeaseBank>(
            std::move(nextBank));
        DynamicNativeSessionPublisher::FxPlan plan;
        plan.owner=std::static_pointer_cast<void>(owner);
        plan.context=owner.get();
        plan.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept {
            (*static_cast<DynamicChannelFxLeaseBank*>(ctx))(id,l,r,n);
        };
        plan.deviceEpoch=epoch;plan.graphRevision=nextRevision;
        auto committed=publisher.commit(
            candidate,accepted.graph.request,accepted.pcmRevision,
            epoch,epoch,true,std::move(plan));
        assert(committed.ok());
        accepted.graph.roster=std::move(candidate);
        accepted.fxOwners=std::move(owner);
        accepted.pcmRevision=committed.revision;
        publisher.pollRetirement(); // old VST Graph reclaimed on CONTROL
        return edit.createdIds[0];
    };
    const auto group=publishBus(ChannelRole::group,"SERIAL GROUP",1.f);
    const auto* groupView=publisher.acceptedRouting(epoch,2);
    assert(groupView&&groupView->find(accepted.graph.ids.inputs[0])
                      ->primaryOutput==group);
    assert(groupView->find(group)->primaryOutput==accepted.graph.ids.main);
    std::array<float,14> meterL{},meterR{}; // fewer than total PCM lanes
    const auto beforeGroup=heapCalls.load(std::memory_order_relaxed);
    assert(publisher.render(epoch,pins,2,newL.data(),newR.data(),32,
        false,fallback,nullptr,nullptr,meterL,meterR));
    assert(heapCalls.load(std::memory_order_relaxed)==beforeGroup);
    assert(raw->calls==3);
    for(int i=0;i<32;++i){
        assert(near(newL[i],oldL[i]));assert(near(newR[i],oldR[i]));
    }
    // Operator GROUP fader changes the LIVE ChannelId bus. Preserve its
    // accepted target before the next graph publication (FX Return).
    assert(publisher.setChannelMix(epoch,2,group,2.f,0.f));
    assert(accepted.graph.roster.rememberLiveMix(group,2.f,0.f));
    assert(!publisher.setChannelMix(epoch,1,group,.5f,0.f));
    const auto wet=publishBus(ChannelRole::fxReturn,"PARALLEL WET",.25f);
    const auto* wetView=publisher.acceptedRouting(epoch,3);
    assert(wetView&&wetView->find(wet));
    assert(wetView->find(accepted.graph.ids.inputs[0])->primaryOutput==group);
    assert(wetView->find(accepted.graph.ids.inputs[0])->parallelSends.size()==1);
    assert(wetView->find(accepted.graph.ids.inputs[0])
           ->parallelSends[0].destination==wet);
    const auto beforeWet=heapCalls.load(std::memory_order_relaxed);
    assert(publisher.render(epoch,pins,2,newL.data(),newR.data(),32,
        false,fallback,nullptr,nullptr,meterL,meterR));
    assert(heapCalls.load(std::memory_order_relaxed)==beforeWet);
    assert(raw->calls==4); // ONE original vendor process per graph pass
    assert(std::abs(newL[31])>std::abs(oldL[31]));
    assert(std::abs(newR[31])>std::abs(oldR[31]));
    assert(publisher.acceptedRevision()==3);
    // Stale or incompatible native topology must not advance the accepted
    // graph; the existing audible GROUP + FX return remains intact.
    const auto stale=publisher.commit(accepted.graph.roster,
        accepted.graph.request,1,epoch,epoch,true);
    assert(!stale.ok()&&publisher.acceptedRevision()==3);
    // Once no callback may hold the accepted PCM snapshot, there is no
    // hidden second FX owner. All vendor/plugin cleanup is off realtime.
    publisher.unpublish();
    accepted=UnifiedLegacyAccepted{};
    publisher.pollRetirement();
    assert(retiringOwner.expired());
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
    // Exact native accepted publisher data, not an imagined v8 graph.
    auto nativeBus=std::make_unique<juce::DynamicObject>();
    nativeBus->setProperty("graphRevision","3");
    nativeBus->setProperty("rosterRevision","4");
    nativeBus->setProperty("structural",true);
    juce::Array<juce::var> realBuses;
    auto groupRow=std::make_unique<juce::DynamicObject>();
    groupRow->setProperty("id","18");
    groupRow->setProperty("role","GROUP");
    groupRow->setProperty("primaryOutput","15");
    realBuses.add(juce::var(groupRow.release()));
    nativeBus->setProperty("channels",juce::var(realBuses));
    route->setProperty("nativeBus",juce::var(nativeBus.release()));
    // These booleans are device-loop truth, NOT permission to promote
    // universal CH11, GROUP or WIRE operations in the GUI.
    route->setProperty("nativeChannelIdPcmActive",true);
    route->setProperty("originalCh1GraphLeased",true);
    // Genuine admitted M4 rows, not fabricated universal ChannelIds.
    juce::Array<juce::var> admittedRows;
    for(int i=0;i<armix::mixer::stripCount;++i) {
        auto row=std::make_unique<juce::DynamicObject>();
        row->setProperty("mode",i==0?"MONO":"NONE");
        row->setProperty("left",i==0?2:-1);
        row->setProperty("right",-1);
        admittedRows.add(juce::var(row.release()));
    }
    auto admittedRoute=std::make_unique<juce::DynamicObject>();
    admittedRoute->setProperty("channels",juce::var(admittedRows));
    route->setProperty("acceptedRoute",juce::var(admittedRoute.release()));
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
    assert(static_cast<int>(snap.getProperty("schemaVersion",0))==6);
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
    const auto acceptedInputs=device.getProperty("enabledInputs",{});
    const auto acceptedOutputs=device.getProperty("enabledOutputs",{});
    assert(acceptedInputs.getArray() && acceptedInputs.getArray()->size()==1);
    assert(acceptedOutputs.getArray() && acceptedOutputs.getArray()->size()==2);
    assert(static_cast<int>(acceptedInputs.getArray()->getReference(0))==2);
    assert(static_cast<int>(acceptedOutputs.getArray()->getReference(0))==1);
    assert(static_cast<int>(acceptedOutputs.getArray()->getReference(1))==4);
    assert(static_cast<int>(ins.getArray()->getReference(0).getProperty("port",0))==2);
    assert(static_cast<double>(ins.getArray()->getReference(0)
        .getProperty("rawPeak",0.0))==.25);
    assert(static_cast<bool>(ins.getArray()->getReference(0)
        .getProperty("rawPresent",false)));
    assert(static_cast<int>(outs.getArray()->getReference(0).getProperty("port",0))==1);
    assert(static_cast<int>(outs.getArray()->getReference(1).getProperty("port",0))==4);
    const auto diagnostic=snap.getProperty("legacyM4",{});
    assert(diagnostic.getProperty("routingRevision","").toString()=="77");
    assert(diagnostic.getProperty("controlPlane","").toString()=="LEGACY_M4_ORDINAL");
    assert(diagnostic.getProperty("pcmRenderer","").toString()=="CHANNEL_ID_10CH4AUX");
    assert(static_cast<bool>(diagnostic.getProperty("originalCh1GraphLeased",false)));
    const auto deliveredBus=diagnostic.getProperty("nativeBus",{});
    assert(deliveredBus.isObject());
    assert(deliveredBus.getProperty("graphRevision","").toString()=="3");
    assert(deliveredBus.getProperty("rosterRevision","").toString()=="4");
    const auto* deliveredChannels=deliveredBus.getProperty("channels",{}).getArray();
    assert(deliveredChannels&&deliveredChannels->size()==1);
    assert(deliveredChannels->getReference(0).getProperty("id","").toString()=="18");
    // A revoked native PCM mode must immediately report M4 fallback without
    // inventing any universal ChannelId capabilities or stale FX identity.
    auto pcmRouteReadback=live.getProperty("dryRoutingLab",{});
    pcmRouteReadback.getDynamicObject()->setProperty("nativeChannelIdPcmActive",false);
    auto fallbackSnap=Bridge::snapshot(live);
    const auto fallbackM4=fallbackSnap.getProperty("legacyM4",{});
    assert(fallbackM4.getProperty("pcmRenderer","").toString()=="M4_COMPAT");
    assert(!static_cast<bool>(fallbackM4.getProperty("originalCh1GraphLeased",true)));
    assert(!static_cast<bool>(fallbackSnap.getProperty("productionCallbackLive",true)));
    assert(fallbackM4.getProperty("nativeBus",{}).isVoid());
    pcmRouteReadback.getDynamicObject()->setProperty("nativeChannelIdPcmActive",true);
    const auto legacyRows=diagnostic.getProperty("acceptedChannels",{});
    assert(legacyRows.getArray()&&
           legacyRows.getArray()->size()==armix::mixer::stripCount);
    assert(legacyRows.getArray()->getReference(0).getProperty("mode","").toString()=="MONO");
    const auto nativeM4Ops=diagnostic.getProperty("supportedOps",{});
    assert(nativeM4Ops.getArray() &&
           nativeM4Ops.getArray()->contains(juce::var(juce::String("SET_FADER"))) &&
           nativeM4Ops.getArray()->contains(juce::var(juce::String("SET_MUTE"))) &&
           !nativeM4Ops.getArray()->contains(juce::var(juce::String("CREATE_CHANNEL"))));
    // No false universal promotion even when the installed M4 branch can
    // really mutate physical PCM through its own separately fenced transport.
    assert(!static_cast<bool>(snap.getProperty("productionCallbackLive",true)));
    assert(caps.getProperty("supportedOps",{}).getArray()->isEmpty());
    // Universal revisions cannot be borrowed from M4; this unready source
    // must not permit GUI ChannelId operations even while M4 PCM is active.
    assert(graph.getProperty("revision","").toString()=="0");
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
    // This adapter receives an actual operator WebView intent and returns
    // a post-publication stable ChannelId+revisions, never graph revision 0
    // masquerading as the native PCM version. No device is required here:
    // the transport test uses an injected native CONTROL writer; golden
    // vendor PCM and lifetime were verified separately above.
    struct BusControl {
        struct Report {
            bool accepted=false;
            ChannelId created{};
            std::uint64_t pcmRevision=0,rosterRevision=0;
            const char* error="NOT_ADMITTED";
        };
        int commits=0;
        int liveMixes=0;
        int nativeInputCreates=0,nativeInputPatches=0;
        int nativeInputMixes=0,nativeInputToggles=0;
        int nativeWireRoutes=0,nativeWireBatches=0;
        Report createNativeInputChannel(std::uint64_t ep,std::uint64_t pcm,
            std::uint64_t roster,const std::string& name,
            ProcessingFormat format){
            assert(ep==9007199254740993ULL&&pcm==3&&roster==4);
            assert(name=="CH11"&&format==ProcessingFormat::mono);
            ++nativeInputCreates;
            return {true,{20},4,6,""};
        }
        Report patchNativeInputSource(std::uint64_t ep,std::uint64_t pcm,
            std::uint64_t roster,ChannelId id,int left,int right,bool stereo){
            assert(ep==9007199254740993ULL&&pcm==3&&roster==4);
            assert(id.value==20&&left==2&&right==-1&&!stereo);
            ++nativeInputPatches;
            return {true,{},4,4,""};
        }
        bool setNativeInputMix(std::uint64_t ep,std::uint64_t rev,
            ChannelId id,float gain,float pan){
            assert(ep==9007199254740993ULL&&rev==3);
            assert(id.value==20&&near(gain,.5f)&&near(pan,0.f));
            ++nativeInputMixes;return true;
        }
        bool setNativeInputMuteSolo(std::uint64_t ep,std::uint64_t rev,
            ChannelId id,bool mute,bool solo){
            assert(ep==9007199254740993ULL&&rev==3);
            assert(id.value==20&&mute&&!solo);
            ++nativeInputToggles;return true;
        }
        bool setNativeBusMix(std::uint64_t ep,std::uint64_t rev,
                             ChannelId id,float gain,float pan) {
            assert(ep==9007199254740993ULL&&rev==3);
            assert(id.value==18&&near(gain,.5f)&&near(pan,0.f));
            ++liveMixes;return true;
        }
        Report setNativePrimaryOutput(std::uint64_t ep,std::uint64_t pcm,
            std::uint64_t roster,ChannelId src,ChannelId dst) {
            assert(ep==9007199254740993ULL&&pcm==3&&roster==4);
            assert(src.value==20&&dst.value==18);
            ++nativeWireRoutes;return {true,{},4,5,""};
        }
        Report applyNativeWireTransaction(std::uint64_t ep,
            std::uint64_t pcm,std::uint64_t roster,
            std::span<const WireChange> changes) {
            assert(ep==9007199254740993ULL&&pcm==3&&roster==4);
            assert(changes.size()==2);
            assert(changes[0].kind==WireChange::Kind::remove);
            assert(changes[0].from.value==20&&changes[0].edgeId==77);
            assert(changes[1].kind==WireChange::Kind::connect);
            assert(changes[1].from.value==20&&changes[1].to.value==18);
            assert(near(changes[1].gain,.5f));
            ++nativeWireBatches;
            return {true,{},4,5,""};
        }
        Report editNativeBus(std::uint64_t ep,std::uint64_t pcm,
                            std::uint64_t roster,ChannelRole role,
                            std::span<const ChannelId> selected,
                            const std::string& name,float gain,ChannelId remove) {
            assert(ep==9007199254740993ULL);
            assert(pcm==3 && roster==4);
            assert(role==ChannelRole::group && remove.value==0);
            assert(selected.size()==1 && selected[0].value==1);
            assert(name=="VOCALS"&&near(gain,.25f));
            ++commits;
            return {true,{19},4,5,""};
        }
    } busControl;

    // Control-thread test: no vendor VST3 effect required for routing.
    const auto routeBusIntent=[&](const juce::var& intent) {
        return bridge.processNativeBusIntent(intent,snap,busControl,false,
            [](const juce::String&,ChannelId,const juce::var&,
               std::uint64_t,std::uint64_t,std::uint64_t)
               ->BusControl::Report { return {}; });
    };
    const auto makeBusIntent=[&](const juce::String& req) {
        auto command=std::make_unique<juce::DynamicObject>();
        command->setProperty("requestId",req);
        command->setProperty("deviceEpoch","9007199254740993");
        command->setProperty("legacyRoutingRevision","77");
        command->setProperty("nativeGraphRevision","3");
        command->setProperty("nativeRosterRevision","4");
        command->setProperty("operation","NATIVE_CREATE_GROUP_SELECTED");
        auto payload=std::make_unique<juce::DynamicObject>();
        payload->setProperty("name","VOCALS");
        payload->setProperty("sendGain",.25);
        juce::Array<juce::var> selection;selection.add("1");
        payload->setProperty("selectedChannelIds",juce::var(selection));
        command->setProperty("payload",juce::var(payload.release()));
        return juce::var(command.release());
    };
    // Real installed bridge functions must compile against mock control writer:
    // native ChannelId input creation, physical source patch and bounded
    // scalar controls each preserve opaque request IDs and accepted rev.
    auto create=makeBusIntent("native:ch11:1");
    create.getDynamicObject()->setProperty("operation","NATIVE_CREATE_CHANNEL");
    auto inputPayload=std::make_unique<juce::DynamicObject>();
    inputPayload->setProperty("name","CH11");
    inputPayload->setProperty("format","MONO");
    create.getDynamicObject()->setProperty(
        "payload",juce::var(inputPayload.release()));
    auto inputAck=routeBusIntent(create);
    assert(inputAck.getProperty("status","").toString()=="ACCEPTED");
    assert(inputAck.getProperty("createdChannelId","").toString()=="20");
    assert(busControl.nativeInputCreates==1);
    auto patch=makeBusIntent("native:ch11:2");
    patch.getDynamicObject()->setProperty("operation","NATIVE_PATCH_CHANNEL");
    patch.getDynamicObject()->setProperty("targetChannelId","20");
    auto patchPayload=std::make_unique<juce::DynamicObject>();
    patchPayload->setProperty("physicalLeft",2);
    patchPayload->setProperty("physicalRight",-1);
    patchPayload->setProperty("sourceFormat","MONO");
    patch.getDynamicObject()->setProperty("payload",juce::var(patchPayload.release()));
    inputAck=routeBusIntent(patch);
    assert(inputAck.getProperty("status","").toString()=="ACCEPTED");
    assert(busControl.nativeInputPatches==1);
    auto inputMix=makeBusIntent("native:ch11:3");
    inputMix.getDynamicObject()->setProperty("operation","NATIVE_SET_CHANNEL_MIX");
    inputMix.getDynamicObject()->setProperty("targetChannelId","20");
    auto controlPayload=std::make_unique<juce::DynamicObject>();
    controlPayload->setProperty("gain",.5);
    controlPayload->setProperty("pan",0.0);
    inputMix.getDynamicObject()->setProperty(
        "payload",juce::var(controlPayload.release()));
    inputAck=routeBusIntent(inputMix);
    assert(inputAck.getProperty("status","").toString()=="ACCEPTED");
    assert(busControl.nativeInputMixes==1);
    auto inputToggle=makeBusIntent("native:ch11:4");
    inputToggle.getDynamicObject()->setProperty(
        "operation","NATIVE_SET_CHANNEL_MUTE_SOLO");
    inputToggle.getDynamicObject()->setProperty("targetChannelId","20");
    auto togglePayload=std::make_unique<juce::DynamicObject>();
    togglePayload->setProperty("mute",true);togglePayload->setProperty("solo",false);
    inputToggle.getDynamicObject()->setProperty(
        "payload",juce::var(togglePayload.release()));
    inputAck=routeBusIntent(inputToggle);
    assert(inputAck.getProperty("status","").toString()=="ACCEPTED");
    assert(busControl.nativeInputToggles==1);
    auto nativeAck=routeBusIntent(
        makeBusIntent("native:bus:1"));
    assert(nativeAck.getProperty("status","").toString()=="ACCEPTED");
    assert(nativeAck.getProperty("graphRevision","").toString()=="0");
    assert(nativeAck.getProperty("nativeGraphRevision","").toString()=="4");
    assert(nativeAck.getProperty("nativeRosterRevision","").toString()=="5");
    assert(nativeAck.getProperty("createdChannelId","").toString()=="19");
    assert(busControl.commits==1);
    nativeAck=routeBusIntent(
        makeBusIntent("native:bus:1"));
    assert(nativeAck.getProperty("errorCode","").toString()=="DUPLICATE_REQUEST_ID");
    assert(busControl.commits==1);
    auto staleBus=makeBusIntent("native:bus:2");
    staleBus.getDynamicObject()->setProperty("nativeGraphRevision","2");
    nativeAck=routeBusIntent(staleBus);
    assert(nativeAck.getProperty("status","").toString()=="REJECTED");
    assert(busControl.commits==1);

    auto wireIntent=makeBusIntent("native:wire:1");
    wireIntent.getDynamicObject()->setProperty("operation","NATIVE_SET_PRIMARY_OUT");
    wireIntent.getDynamicObject()->setProperty("targetChannelId","20");
    auto wirePayload=std::make_unique<juce::DynamicObject>();
    wirePayload->setProperty("destinationId","18");
    wireIntent.getDynamicObject()->setProperty(
        "payload",juce::var(wirePayload.release()));
    const auto wireAck=routeBusIntent(wireIntent);
    assert(wireAck.getProperty("status","").toString()=="ACCEPTED");
    assert(busControl.nativeWireRoutes==1);
    // Same existing native bridge -> exact batch -> post-publication ACK.
    // Legacy M4 revision remains separate; no false universal LIVE claim.
    auto batch=makeBusIntent("native:wire:batch:1");
    batch.getDynamicObject()->setProperty(
        "operation","NATIVE_APPLY_WIRE_TRANSACTION");
    juce::Array<juce::var> changes;
    auto unplug=std::make_unique<juce::DynamicObject>();
    unplug->setProperty("op","REMOVE");
    unplug->setProperty("from","20");
    unplug->setProperty("edgeId","77");
    changes.add(juce::var(unplug.release()));
    auto connect=std::make_unique<juce::DynamicObject>();
    connect->setProperty("op","CONNECT");
    connect->setProperty("from","20");
    connect->setProperty("to","18");
    connect->setProperty("tap","POST_FADER");
    connect->setProperty("gainLinear",.5);
    changes.add(juce::var(connect.release()));
    auto transaction=std::make_unique<juce::DynamicObject>();
    transaction->setProperty("changes",juce::var(changes));
    batch.getDynamicObject()->setProperty(
        "payload",juce::var(transaction.release()));
    auto batchAck=routeBusIntent(batch);
    assert(batchAck.getProperty("status","").toString()=="ACCEPTED");
    assert(batchAck.getProperty("nativeGraphRevision","").toString()=="4");
    assert(batchAck.getProperty("nativeRosterRevision","").toString()=="5");
    assert(busControl.nativeWireBatches==1);
    batchAck=routeBusIntent(batch);
    assert(batchAck.getProperty("errorCode","").toString()=="DUPLICATE_REQUEST_ID");
    assert(busControl.nativeWireBatches==1);
    auto invalidBatch=makeBusIntent("native:wire:bad:2");
    invalidBatch.getDynamicObject()->setProperty(
        "operation","NATIVE_APPLY_WIRE_TRANSACTION");
    auto malformed=std::make_unique<juce::DynamicObject>();
    juce::Array<juce::var> badChanges;
    auto bad=std::make_unique<juce::DynamicObject>();
    bad->setProperty("op","REMOVE");bad->setProperty("from","020");
    bad->setProperty("edgeId","77");
    badChanges.add(juce::var(bad.release()));
    malformed->setProperty("changes",juce::var(badChanges));
    invalidBatch.getDynamicObject()->setProperty(
        "payload",juce::var(malformed.release()));
    batchAck=routeBusIntent(invalidBatch);
    assert(batchAck.getProperty("status","").toString()=="REJECTED");
    assert(batchAck.getProperty("errorCode","").toString()=="INVALID_NATIVE_WIRE_SOURCE");
    assert(busControl.nativeWireBatches==1);
    auto liveMixIntent=makeBusIntent("native:mix:1");
    liveMixIntent.getDynamicObject()->setProperty(
        "operation","NATIVE_SET_BUS_MIX");
    liveMixIntent.getDynamicObject()->setProperty("targetChannelId","18");
    auto mixPayload=std::make_unique<juce::DynamicObject>();
    mixPayload->setProperty("gain",.5);
    mixPayload->setProperty("pan",0.0);
    liveMixIntent.getDynamicObject()->setProperty(
        "payload",juce::var(mixPayload.release()));
    const auto mixAck=routeBusIntent(
        liveMixIntent);
    assert(mixAck.getProperty("status","").toString()=="ACCEPTED");
    assert(busControl.liveMixes==1&&busControl.commits==1);
    // Canonical public v8 operation uses the SAME installed ChannelId
    // publisher and ACK revisions, never a WebView/legacy owned PCM graph.
    const auto v8Route=live.getProperty("dryRoutingLab",{});
    const auto v8Bus=v8Route.getProperty("nativeBus",{});
    auto* v8BusObject=v8Bus.getDynamicObject();
    assert(v8BusObject);
    const auto v8Rows=v8Bus.getProperty("channels",{});
    // Make the synthetic fixture structurally complete: partial native
    // rows must not advertise accepted ChannelId support.
    auto makeNativeRow=[](const char* id,const char* role) {
        auto view=std::make_unique<juce::DynamicObject>();
        view->setProperty("id",id);view->setProperty("role",role);
        view->setProperty("name",role);view->setProperty("format","STEREO");
        view->setProperty("primaryOutput","0");
        view->setProperty("fader",1.);view->setProperty("pan",0.);
        view->setProperty("mute",false);view->setProperty("solo",false);
        view->setProperty("inserts",juce::var(juce::Array<juce::var>{}));
        view->setProperty("peakL",.25);view->setProperty("peakR",.2);
        return juce::var(view.release());
    };
    v8Rows.getArray()->clear();
    auto nativeInput=makeNativeRow("20","INPUT");
    nativeInput.getDynamicObject()->setProperty("primaryOutput","18");
    v8Rows.getArray()->add(nativeInput);
    v8Rows.getArray()->add(makeNativeRow("18","MAIN"));
    juce::Array<juce::var> v8Edges;
    auto v8Edge=std::make_unique<juce::DynamicObject>();
    v8Edge->setProperty("edgeId","77");
    v8Edge->setProperty("from","20");
    v8Edge->setProperty("to","18");
    v8Edge->setProperty("tap","POST_FADER");
    v8Edge->setProperty("gainLinear",.5);
    v8Edge->setProperty("primaryOutput",true);
    v8Edges.add(juce::var(v8Edge.release()));
    v8BusObject->setProperty("edges",juce::var(v8Edges));
    juce::Array<juce::var> v8InstalledOps;
    v8InstalledOps.add(juce::var(juce::String("NATIVE_APPLY_WIRE_TRANSACTION")));
    v8BusObject->setProperty("supportedOps",juce::var(v8InstalledOps));
    const auto acceptedV8=Bridge::snapshot(live);
    const auto acceptedV8Graph=acceptedV8.getProperty("graph",{});
    const auto acceptedV8Caps=acceptedV8Graph.getProperty("capabilities",{});
    assert(!static_cast<bool>(acceptedV8.getProperty("productionCallbackLive",true)));
    assert(acceptedV8Graph.getProperty("revision","").toString()=="3");
    assert(acceptedV8Graph.getProperty("rosterRevision","").toString()=="4");
    assert(acceptedV8Graph.getProperty("channels",{}).getArray()->size()==2);
    assert(acceptedV8Graph.getProperty("mainChannelId","").toString()=="18");
    const auto publicChannel=acceptedV8Graph.getProperty("channels",{})
        .getArray()->getReference(0);
    assert(publicChannel.getProperty("processingFormat","").toString()=="STEREO");
    assert(publicChannel.getProperty("controls",{}).getProperty("faderDb",{}).isDouble());
    assert(acceptedV8Graph.getProperty("edges",{}).getArray()->size()==1);
    assert(acceptedV8Graph.getProperty("edges",{}).getArray()->getReference(0)
        .getProperty("signalKind","").toString()=="AUDIO");
    assert(acceptedV8Graph.getProperty("edges",{}).getArray()->getReference(0)
               .getProperty("edgeId","").toString()=="77");
    assert(static_cast<bool>(acceptedV8Caps.getProperty("callbackOwnsChannelIds",false)));
    assert(acceptedV8Caps.getProperty("supportedOps",{}).getArray()->contains(
        juce::var(juce::String("APPLY_WIRE_TRANSACTION"))));
    auto v8IntentObject=std::make_unique<juce::DynamicObject>();
    v8IntentObject->setProperty("requestId","v8:wire:1");
    v8IntentObject->setProperty("deviceEpoch","9007199254740993");
    v8IntentObject->setProperty("baseGraphRevision","3");
    v8IntentObject->setProperty("baseRosterRevision","4");
    v8IntentObject->setProperty("operation","APPLY_WIRE_TRANSACTION");
    v8IntentObject->setProperty("targetChannelId","20");
    v8IntentObject->setProperty("payload",batch.getProperty("payload",{}));
    const auto v8WireIntent=juce::var(v8IntentObject.release());
    const auto applyV8=[&](const juce::var& command,const juce::var& snapshot) {
        return bridge.processNativeBusIntent(command,snapshot,busControl,false,
            [](const juce::String&,ChannelId,const juce::var&,
               std::uint64_t,std::uint64_t,std::uint64_t)
               ->BusControl::Report {return {};});
    };
    auto v8Reply=applyV8(v8WireIntent,acceptedV8);
    assert(v8Reply.getProperty("status","").toString()=="ACCEPTED");
    assert(v8Reply.getProperty("graphRevision","").toString()=="4");
    assert(v8Reply.getProperty("rosterRevision","").toString()=="5");
    assert(busControl.nativeWireBatches==2);
    assert(applyV8(v8WireIntent,acceptedV8)
               .getProperty("errorCode","").toString()=="DUPLICATE_REQUEST_ID");
    auto staleV8=juce::JSON::parse(juce::JSON::toString(v8WireIntent,false));
    staleV8.getDynamicObject()->setProperty("requestId","v8:wire:stale");
    staleV8.getDynamicObject()->setProperty("baseGraphRevision","2");
    assert(applyV8(staleV8,acceptedV8).getProperty("status","").toString()=="REJECTED");
    assert(busControl.nativeWireBatches==2);
    // A focused MAIN plug cannot modify another admitted channel's wire
    // even if its edge ID is otherwise syntactically valid.
    auto wrongFocus=juce::JSON::parse(juce::JSON::toString(v8WireIntent,false));
    wrongFocus.getDynamicObject()->setProperty("requestId","v8:wire:wrong-focus");
    wrongFocus.getDynamicObject()->setProperty("targetChannelId","18");
    const auto rejectedFocus=applyV8(wrongFocus,acceptedV8);
    assert(rejectedFocus.getProperty("status","").toString()=="REJECTED");
    assert(rejectedFocus.getProperty("errorCode","").toString()==
           "UNIVERSAL_WIRE_UNFOCUSED_SOURCE");
    assert(busControl.nativeWireBatches==2);
    // Subsequent native producer snapshot must match the acknowledged graph
    // and roster revision before GUI may paint an accepted WIRE cable.
    v8BusObject->setProperty("graphRevision","4");
    v8BusObject->setProperty("rosterRevision","5");
    const auto afterV8=Bridge::snapshot(live);
    assert(afterV8.getProperty("graph",{}).getProperty("revision","")
               .toString()==v8Reply.getProperty("graphRevision","").toString());
    assert(afterV8.getProperty("graph",{}).getProperty("rosterRevision","")
               .toString()==v8Reply.getProperty("rosterRevision","").toString());
    // C2 native integration: public v8 ACK is exercised against the REAL
    // accepted PCM graph, not the mock authorizing the bridge above.
    // Direct + nested routes must change sample amplitudes on the SAME
    // sample index after the focused MAIN cable is actually unplugged.
    {
        constexpr std::uint64_t pcmEpoch=9007199254740993ULL;
        DynamicAcceptedSession pcm;
        DynamicGraphRequest patch;
        patch.physicalInputs=1;patch.maxFrames=64;
        const auto createRow=[](std::size_t position,ChannelRole role,
                                const char* name,ProcessingFormat width) {
            ChannelEdit e;e.kind=ChannelEdit::Kind::add;
            e.position=position;e.role=role;e.text=name;e.format=width;
            return e;
        };
        const std::array<ChannelEdit,3> create{
            createRow(0,ChannelRole::input,"PCM SOURCE",ProcessingFormat::mono),
            createRow(1,ChannelRole::input,"PCM VIA",ProcessingFormat::stereo),
            createRow(2,ChannelRole::main,"PCM MAIN",ProcessingFormat::stereo)
        };
        auto admission=pcm.edit(0,0,create,patch,pcmEpoch,pcmEpoch,true);
        assert(admission.ok()&&admission.edit.createdIds.size()==3);
        const auto src=admission.edit.createdIds[0];
        const auto via=admission.edit.createdIds[1];
        const auto sink=admission.edit.createdIds[2];
        patch.sources={{src,0,-1,false}};
        ChannelEdit direct;direct.kind=ChannelEdit::Kind::setPrimaryOutput;
        direct.id=src;direct.target=sink;
        ChannelEdit viaOut=direct;viaOut.id=via;
        const std::array<ChannelEdit,2> mains{direct,viaOut};
        admission=pcm.edit(1,1,mains,patch,pcmEpoch,pcmEpoch,true);
        assert(admission.ok()&&pcm.acceptedGraphRevision()==2);
        WireChange intermediate;intermediate.kind=WireChange::Kind::connect;
        intermediate.from=src;intermediate.to=via;
        const std::array<WireChange,1> toVia{intermediate};
        const auto linked=pcm.wireTransaction(
            2,2,toVia,patch,pcmEpoch,pcmEpoch,false);
        assert(linked.ok()&&pcm.acceptedGraphRevision()==3);
        const auto* acceptedPcm=pcm.acceptedRouting(pcmEpoch,3);
        assert(acceptedPcm&&acceptedPcm->edges.size()==3);
        std::uint64_t directId=0;
        for(const auto& edge:acceptedPcm->edges)
            if(edge.from==src&&edge.to==sink)directId=edge.edgeId;
        assert(directId!=0);
        std::array<float,64> impulse{},left{},right{};
        impulse[11]=.3f;
        const float* capture[]{impulse.data()};
        const auto noFx=+[](ChannelId,float*,float*,int) noexcept {};
        const auto renderPcm=[&] {
            return pcm.render(pcmEpoch,capture,1,left.data(),right.data(),
                              64,false,noFx);
        };
        assert(renderPcm());
        const float parallelValue=left[11];
        assert(parallelValue>.1f);
        assert(near(left[10],0.f)&&near(left[12],0.f));

        // The existing Console nativeBus is only a *view*; reproject it
        // from the actual tested accepted session and its stable IDs.
        auto admittedBus=live.getProperty("dryRoutingLab",{})
                             .getProperty("nativeBus",{});
        auto* busObject=admittedBus.getDynamicObject();
        assert(busObject);
        const auto projectNativePcm=[&](std::uint64_t rev) {
            const auto* accepted=pcm.acceptedRouting(pcmEpoch,rev);
            assert(accepted);
            juce::Array<juce::var> rows,edges;
            for(const auto& row:accepted->channels) {
                auto view=std::make_unique<juce::DynamicObject>();
                view->setProperty("id",juce::String(std::to_string(row.id.value)));
                view->setProperty("role",row.role==ChannelRole::main?"MAIN":"INPUT");
                view->setProperty("name",juce::String(row.name));
                view->setProperty("format",row.format==ProcessingFormat::mono?"MONO":"STEREO");
                view->setProperty("primaryOutput",
                    juce::String(std::to_string(row.primaryOutput.value)));
                const auto* admitted=pcm.acceptedRoster().find(row.id);
                assert(admitted);
                view->setProperty("fader",static_cast<double>(admitted->faderLinear));
                view->setProperty("pan",static_cast<double>(admitted->pan));
                view->setProperty("mute",admitted->mute);
                view->setProperty("solo",admitted->solo);
                view->setProperty("inserts",juce::var(juce::Array<juce::var>{}));
                view->setProperty("peakL",.3);view->setProperty("peakR",.3);
                rows.add(juce::var(view.release()));
            }
            for(const auto& edge:accepted->edges) {
                auto view=std::make_unique<juce::DynamicObject>();
                view->setProperty("edgeId",juce::String(std::to_string(edge.edgeId)));
                view->setProperty("from",juce::String(std::to_string(edge.from.value)));
                view->setProperty("to",juce::String(std::to_string(edge.to.value)));
                view->setProperty("tap",edge.preFader?"PRE_FADER":"POST_FADER");
                view->setProperty("gainLinear",static_cast<double>(edge.gainLinear));
                view->setProperty("primaryOutput",edge.primaryOutput);
                edges.add(juce::var(view.release()));
            }
            busObject->setProperty("graphRevision",
                                   juce::String(std::to_string(rev)));
            busObject->setProperty("rosterRevision",
                                   juce::String(std::to_string(
                                       pcm.acceptedRosterRevision())));
            busObject->setProperty("channels",juce::var(rows));
            busObject->setProperty("edges",juce::var(edges));
            return Bridge::snapshot(live);
        };
        const auto nativeBefore=projectNativePcm(3);
        assert(nativeBefore.getProperty("graph",{}).getProperty("edges",{})
                   .getArray()->size()==3);
        // Derive from the real session rather than mocking the APPLY result.
        struct ActualPcmControl final:BusControl {
            DynamicAcceptedSession& session;
            DynamicGraphRequest admitted;
            ActualPcmControl(DynamicAcceptedSession& s,
                             const DynamicGraphRequest& p)
                :session(s),admitted(p){}
            Report applyNativeWireTransaction(std::uint64_t ep,
                std::uint64_t graphRev,std::uint64_t rosterRev,
                std::span<const WireChange> edits) {
                const auto result=session.wireTransaction(
                    rosterRev,graphRev,edits,admitted,ep,ep,false);
                return {result.ok(),{},session.acceptedGraphRevision(),
                        session.acceptedRosterRevision(),
                        result.ok()?"":"NATIVE_PCM_REJECTED"};
            }
        } nativePcmControl(pcm,patch);
        auto userIntent=std::make_unique<juce::DynamicObject>();
        userIntent->setProperty("requestId","v8:pcm:unplug:1");
        userIntent->setProperty("deviceEpoch",juce::String(std::to_string(pcmEpoch)));
        userIntent->setProperty("baseGraphRevision","3");
        userIntent->setProperty("baseRosterRevision","3");
        userIntent->setProperty("operation","APPLY_WIRE_TRANSACTION");
        userIntent->setProperty("targetChannelId",
                                juce::String(std::to_string(src.value)));
        auto unplug=std::make_unique<juce::DynamicObject>();
        unplug->setProperty("op","REMOVE");
        unplug->setProperty("from",juce::String(std::to_string(src.value)));
        unplug->setProperty("edgeId",juce::String(std::to_string(directId)));
        juce::Array<juce::var> edits;edits.add(juce::var(unplug.release()));
        auto payload=std::make_unique<juce::DynamicObject>();
        payload->setProperty("changes",juce::var(edits));
        userIntent->setProperty("payload",juce::var(payload.release()));
        const auto change=juce::var(userIntent.release());
        const auto response=bridge.processNativeBusIntent(
            change,nativeBefore,nativePcmControl,false,
            [](const juce::String&,ChannelId,const juce::var&,
               std::uint64_t,std::uint64_t,std::uint64_t)
               ->ActualPcmControl::Report {return {};});
        assert(response.getProperty("status","").toString()=="ACCEPTED");
        assert(response.getProperty("graphRevision","").toString()=="4");
        assert(response.getProperty("rosterRevision","").toString()=="4");
        assert(pcm.acceptedGraphRevision()==4);
        assert(renderPcm());
        assert(left[11]>.05f&&left[11]<parallelValue);
        assert(near(left[10],0.f)&&near(left[12],0.f));
        const auto nativeAfter=projectNativePcm(4);
        const auto afterGraph=nativeAfter.getProperty("graph",{});
        assert(afterGraph.getProperty("revision","").toString()==
               response.getProperty("graphRevision","").toString());
        assert(afterGraph.getProperty("rosterRevision","").toString()==
               response.getProperty("rosterRevision","").toString());
        assert(afterGraph.getProperty("edges",{}).getArray()->size()==2);
        assert(nativeBefore.getProperty("graph",{})
                   .getProperty("revision","").toString()=="3");
        // Stale intent and exact edge replay must not remove any other MAIN.
        auto staleChange=juce::JSON::parse(juce::JSON::toString(change,false));
        staleChange.getDynamicObject()->setProperty("requestId","v8:pcm:stale");
        const auto staleResponse=bridge.processNativeBusIntent(
            staleChange,nativeAfter,nativePcmControl,false,
            [](const juce::String&,ChannelId,const juce::var&,
               std::uint64_t,std::uint64_t,std::uint64_t)
               ->ActualPcmControl::Report {return {};});
        assert(staleResponse.getProperty("status","").toString()=="REJECTED");
        assert(pcm.acceptedGraphRevision()==4);
        assert(renderPcm()&&left[11]<parallelValue);
        pcm.unpublish();pcm.pollRetirement();
    }
    // Fail-closed device fault: do not advertise a compatibility actuator
    // against a poisoned physical port topology or stale hardware session.
    auto liveRoute=live.getProperty("dryRoutingLab",{});
    liveRoute.getDynamicObject()->setProperty("topologyFault",true);
    const auto stale=Bridge::snapshot(live).getProperty("legacyM4",{});
    assert(stale.getProperty("supportedOps",{}).isVoid());
    assert(stale.getProperty("acceptedChannels",{}).isVoid());
    // No request above mutates the genuinely accepted M4 hardware route.
    assert(static_cast<int>(live.getProperty("dryRoutingLab",{}).getProperty("revision",0))==77);
}

// G1 physical First Sound: one enabled callback pin becomes a MONO
// production source in ONE native mutation. No fabricated stereo IN1/IN1,
// no accidental modification of the last accepted M4 request on failure.
static void verifyFirstSoundOnePinNativeSourceDraft() {
    using armix::mixer::NativeVerifiedSourcePatch;
    auto command=std::make_unique<juce::DynamicObject>();
    command->setProperty("deviceEpoch",juce::int64(17));
    command->setProperty("revision",juce::int64(8));
    juce::Array<juce::var> sources;
    for(int c=0;c<10;++c) {
        auto ch=std::make_unique<juce::DynamicObject>();
        ch->setProperty("mode","NONE");
        ch->setProperty("left",-1);
        ch->setProperty("right",-1);
        ch->setProperty("gain",.25);
        ch->setProperty("pan",0.0);
        sources.add(juce::var(ch.release()));
    }
    command->setProperty("channels",juce::var(sources));
    const auto accepted=juce::var(command.release());
    const std::array<int,1> physical{{5}};
    juce::var candidate;
    assert(NativeVerifiedSourcePatch::prepare(accepted,6,"MONO",5,-1,
        physical,17,9,candidate).isEmpty());
    const auto draftChannels=candidate.getProperty("channels",juce::var());
    const auto sourceBefore=accepted.getProperty("channels",juce::var());
    const auto* acceptedChannels=sourceBefore.getArray();
    const auto* changed=draftChannels.getArray();
    assert(changed && changed->size()==10 && acceptedChannels &&
           acceptedChannels->size()==10);
    assert(changed->getReference(6).getProperty("mode","").toString()=="MONO");
    assert(static_cast<int>(changed->getReference(6).getProperty("left",-1))==5);
    assert(static_cast<int>(changed->getReference(6).getProperty("right",0))==-1);
    assert(acceptedChannels->getReference(6).getProperty("mode","").toString()=="NONE");
    assert(candidate.getProperty("revision",{}).toString()=="9");
    candidate=juce::var();
    assert(NativeVerifiedSourcePatch::prepare(accepted,6,"STEREO",5,5,
        physical,17,9,candidate).isNotEmpty());
    assert(candidate.isVoid());
    assert(NativeVerifiedSourcePatch::prepare(accepted,6,"STEREO",5,7,
        physical,17,9,candidate).isNotEmpty());
    assert(NativeVerifiedSourcePatch::prepare(accepted,6,"MONO",4,-1,
        physical,17,9,candidate).isNotEmpty());
    assert(NativeVerifiedSourcePatch::prepare(accepted,6,"MONO",5,-1,
        physical,18,9,candidate).isNotEmpty());
    assert(acceptedChannels->getReference(6).getProperty("mode","").toString()=="NONE");
    const std::array<int,2> stereoPhysical{{5,7}};
    assert(NativeVerifiedSourcePatch::prepare(accepted,2,"STEREO",5,7,
        stereoPhysical,17,9,candidate).isEmpty());
    const auto stereoRows=candidate.getProperty("channels",juce::var());
    assert(stereoRows.getArray()->getReference(2).getProperty("mode","").toString()=="STEREO");

    // Real production route schema v2: distinguish physical SOURCE from
    // channel PROCESSING width, and compile the accepted plan on the same
    // NativeDryRouteCommand path used by LiveDryRoutingLab::apply().
    auto typed=juce::JSON::parse(juce::JSON::toString(accepted,false));
    typed.getDynamicObject()->setProperty("schemaVersion",2);
    const auto typedSource=typed.getProperty("channels",{});
    for(auto& value:*typedSource.getArray()) {
        auto* obj=value.getDynamicObject();
        assert(obj);
        obj->setProperty("sourceKind","NONE");
        obj->setProperty("sourceFormat","NONE");
        obj->setProperty("channelFormat","STEREO");
        obj->setProperty("main",true);
        obj->setProperty("mute",false);
        obj->setProperty("solo",false);
    }
    juce::var typedNew;
    assert(NativeVerifiedSourcePatch::prepare(typed,6,"MONO",5,-1,
        physical,17,9,typedNew).isEmpty());
    const auto newRows=typedNew.getProperty("channels",{});
    const auto newCh=newRows.getArray()->getReference(6);
    assert(newCh.getProperty("sourceKind","").toString()=="PHYSICAL");
    assert(newCh.getProperty("sourceFormat","").toString()=="MONO");
    assert(newCh.getProperty("channelFormat","").toString()=="STEREO");
    assert(static_cast<int>(newCh.getProperty("right",0))==-1);
    armix::mixer::NativeDryRouteCommand decoded;
    juce::String error;
    assert(armix::mixer::NativeDryRouteCommand::decode(typedNew,decoded,error));
    assert(decoded.channels.stripsAndMaster.strips[6].source==
           armix::mixer::SourceMode::mono);
    assert(decoded.channels.formats[6]==armix::mixer::ChannelFormat::stereo);
    armix::mixer::ChannelRoutingPlan routed;
    armix::mixer::LayoutError issue;
    assert(armix::mixer::ChannelRoutingPlan::compile(
        decoded.channels,physical,routed,issue));
    assert(routed.base.strips[6].inputA==0);
    assert(routed.effectiveFormats[6]==armix::mixer::ChannelFormat::stereo);
    const std::array<float,4> mic{{.25f,.5f,.75f,1.f}};
    const float* nativePins[]{mic.data()};
    std::array<float,4> routedL{},routedR{};
    assert(routed.base.extractSource(6,nativePins,1,
                                   routedL.data(),routedR.data(),4));
    assert(routedL==mic && routedR==mic);

    // The *installed M4 native PCM renderer*, not DynamicPreparedMixerBlock:
    // actual physical IN5 callback[0] -> independent CH07 -> stereo MAIN.
    // Exercise the same LiveChannelFxGraph::process invoked by
    // LiveDryRoutingLab::process from the sole JUCE hardware callback.
    armix::mixer::PreparedRenderGraph prepared;
    armix::mixer::RenderError renderError;
    assert(armix::mixer::PreparedRenderGraph::compile(
        decoded.channels,physical,decoded.sends,prepared,renderError,decoded.aux));
    armix::mixer::LiveChannelFxGraph production(prepared,64,64);
    assert(production.setMainOutputPair(0,1,2));
    std::array<float,4> mainL{},mainR{};
    armix::mixer::RenderLevels levels;
    const auto beforeCallback=heapCalls.load(std::memory_order_relaxed);
    assert(production.process(nativePins,1,mainL.data(),mainR.data(),4,&levels));
    assert(heapCalls.load(std::memory_order_relaxed)==beforeCallback);
    assert(mainL[0] > 0.f && mainR[0] > 0.f &&
           std::isfinite(mainL[0]) && std::isfinite(mainR[0]));
    assert(near(mainL[0],mainR[0]));
    assert(levels.channelPeak[6] > 0.f);
    assert(levels.channelPeak[1] == 0.f); // CH02 unpatched, no fake PCM.
    // The real strip deliberately de-clicks MUTE over up to 64 frames:
    // a transition is NOT an immediate memset. Verify sample-by-sample
    // monotonic fade and that steady muted blocks are truly silent.
    assert(production.setStripControl(6,.25f,0.f,true,true,false));
    mainL.fill(1.f);mainR.fill(1.f);
    const auto beforeMute=heapCalls.load(std::memory_order_relaxed);
    assert(production.process(nativePins,1,mainL.data(),mainR.data(),4,&levels));
    assert(heapCalls.load(std::memory_order_relaxed)==beforeMute);
    assert(mainL[0]>0.f && mainR[0]>0.f);
    // RAW physical microphone samples increase across the test block.
    // Compare actual per-frame GAIN envelope after removing input amplitude,
    // otherwise a correct fade is incorrectly rejected as nonmonotonic.
    assert(mainL[0]/mic[0]>mainL[1]/mic[1] &&
           mainL[1]/mic[1]>mainL[2]/mic[2]);
    assert(mainR[0]/mic[0]>mainR[1]/mic[1] &&
           mainR[1]/mic[1]>mainR[2]/mic[2]);
    assert(mainL[3]>0.f && mainR[3]>0.f); // 4/64 frames, not a hard mute
    // Sixteen small callback blocks complete EXACTLY the same 64-sample
    // ramp as one 64-frame callback, without RT heap allocations.
    float previousTail=mainL[3]/mic[3];
    for(int blockIndex=1;blockIndex<16;++blockIndex) {
        const auto heapBefore=heapCalls.load(std::memory_order_relaxed);
        assert(production.process(nativePins,1,mainL.data(),mainR.data(),4,&levels));
        assert(heapCalls.load(std::memory_order_relaxed)==heapBefore);
        const float normalizedTail=mainL[3]/mic[3];
        assert(normalizedTail<=previousTail+1.e-5f);
        previousTail=normalizedTail;
    }
    assert(near(mainL[3],0.f) && near(mainR[3],0.f));
    mainL.fill(1.f);mainR.fill(1.f);
    assert(production.process(nativePins,1,mainL.data(),mainR.data(),4,&levels));
    for(int n=0;n<4;++n)assert(mainL[n]==0.f && mainR[n]==0.f);
    assert(levels.channelPeak[6]==0.f);
    assert(production.setStripControl(6,.25f,0.f,true,false,false));
    assert(production.process(nativePins,1,mainL.data(),mainR.data(),4,&levels));
    assert(mainL[0]>0.f&&mainR[0]>0.f);
    assert(mainL[3]>mainL[0]); // unmute ramps, never spikes to unity
    for(int k=1;k<16;++k)
        assert(production.process(nativePins,1,mainL.data(),mainR.data(),4,&levels));
    assert(mainL[3]>0.f);
    // A rapid re-MUTE in the middle of an unmute envelope does not
    // interpolate from stale unity or introduce a discontinuity.
    assert(production.setStripControl(6,.25f,0.f,true,true,false));
    assert(production.process(nativePins,1,mainL.data(),mainR.data(),4,&levels));
    assert(mainL[0]>0.f);
    juce::var denied;
    assert(NativeVerifiedSourcePatch::prepare(typed,6,"STEREO",5,5,
        physical,17,9,denied).isNotEmpty());
    assert(denied.isVoid());
    assert(typedSource.getArray()->getReference(6)
        .getProperty("sourceKind","").toString()=="NONE");

    // SOURCE STEREO can be folded into MONO processing independently.
    auto typedFold=juce::JSON::parse(juce::JSON::toString(typed,false));
    auto foldedRows=typedFold.getProperty("channels",{});
    foldedRows.getArray()->getReference(2).getDynamicObject()
        ->setProperty("channelFormat","MONO");
    juce::var stereoFold;
    assert(NativeVerifiedSourcePatch::prepare(typedFold,2,"STEREO",5,7,
        stereoPhysical,17,9,stereoFold).isEmpty());
    assert(armix::mixer::NativeDryRouteCommand::decode(
        stereoFold,decoded,error));
    assert(armix::mixer::ChannelRoutingPlan::compile(
        decoded.channels,stereoPhysical,routed,issue));
    assert(routed.effectiveFormats[2]==armix::mixer::ChannelFormat::mono);
    assert(routed.foldStereoToMono[2]);

    // SOURCE/VST3 lifecycle migration at production M4 graph granularity:
    // both verified physical pins belong to one unchanged device epoch.
    // Selecting another MONO pin must preserve the very SAME prepared
    // JUCE processor (state/editor/tail identity), not silently rebuild it.
    juce::var beforeSwap;
    assert(NativeVerifiedSourcePatch::prepare(
        typed,6,"MONO",5,-1,stereoPhysical,17,9,beforeSwap).isEmpty());
    armix::mixer::NativeDryRouteCommand oldCommand;
    assert(armix::mixer::NativeDryRouteCommand::decode(
        beforeSwap,oldCommand,error));
    armix::mixer::PreparedRenderGraph oldPlan;
    assert(armix::mixer::PreparedRenderGraph::compile(
        oldCommand.channels,stereoPhysical,oldCommand.sends,oldPlan,
        renderError,oldCommand.aux));
    auto firstFxGraph=std::make_unique<armix::mixer::LiveChannelFxGraph>(
        oldPlan,64,64);
    auto nativeFx=std::make_unique<GainFx>(2.f);
    nativeFx->prepareToPlay(48000,64);
    assert(firstFxGraph->install(6,std::move(nativeFx),
           "CH07 native vendor FX","ch07-stable-vendor-id"));
    const auto* originalInstance=firstFxGraph->instanceProcessor(6);
    assert(originalInstance!=nullptr);
    const std::array<float,4> secondPin{{.1f,.2f,.3f,.4f}};
    const float* twoNativeInputs[]{mic.data(),secondPin.data()};
    std::array<float,4> oldMainL{},oldMainR{};
    const auto beforeOldRender=heapCalls.load(std::memory_order_relaxed);
    assert(firstFxGraph->process(twoNativeInputs,2,oldMainL.data(),
                                 oldMainR.data(),4));
    assert(heapCalls.load(std::memory_order_relaxed)==beforeOldRender);
    assert(oldMainL[0]>0.f && oldMainR[0]>0.f);
    juce::var afterSwap;
    assert(NativeVerifiedSourcePatch::prepare(
        beforeSwap,6,"MONO",7,-1,stereoPhysical,17,10,afterSwap).isEmpty());
    armix::mixer::NativeDryRouteCommand switchedCommand;
    assert(armix::mixer::NativeDryRouteCommand::decode(
        afterSwap,switchedCommand,error));
    armix::mixer::PreparedRenderGraph switchedPlan;
    assert(armix::mixer::PreparedRenderGraph::compile(
        switchedCommand.channels,stereoPhysical,switchedCommand.sends,
        switchedPlan,renderError,switchedCommand.aux));
    auto secondFxGraph=std::make_unique<armix::mixer::LiveChannelFxGraph>(
        switchedPlan,64,64);
    assert(secondFxGraph->reuseAt(6,0,*firstFxGraph,0,"ch07-stable-vendor-id"));
    assert(secondFxGraph->instanceProcessor(6)==originalInstance);
    firstFxGraph.reset(); // old control graph retired; vendor must stay alive
    assert(secondFxGraph->instanceProcessor(6)==originalInstance);
    std::array<float,4> newMainL{},newMainR{};
    const auto beforeNewRender=heapCalls.load(std::memory_order_relaxed);
    assert(secondFxGraph->process(twoNativeInputs,2,newMainL.data(),
                                  newMainR.data(),4));
    assert(heapCalls.load(std::memory_order_relaxed)==beforeNewRender);
    assert(newMainL[0]>0.f && newMainR[0]>0.f);
    // Exactly one FX call and one real new source: 0.1 / 0.25 amplitude.
    assert(near(newMainL[0]/oldMainL[0],.4f));
    assert(near(newMainR[0]/oldMainR[0],.4f));
    assert(typedSource.getArray()->getReference(6)
        .getProperty("mode","").toString()=="NONE");
}

// V8 S0: a C++ fixture, built with the SAME header serializer installed in
// JUCE WebView, never hand-maintained by GUI/FAT. No real audio device exists
// for this UNREADY fixture and it intentionally has no dynamic capabilities.
static void verifyNativePuzzleV8FixtureAndWire() {
    using namespace armix::console;
    PuzzleV8Snapshot fixtureSource;
    fixtureSource.epoch="1";
    PuzzleV8Ack rejected;
    rejected.requestId="native-fixture-session:1";
    rejected.deviceEpoch=fixtureSource.epoch;
    const auto expected=PuzzleV8Wire::fixture(fixtureSource,rejected);
    const auto fixtureFile=std::filesystem::path(__FILE__).parent_path()/
        "fixtures"/"console-v8-native-unready.json";
    std::ifstream source(fixtureFile,std::ios::binary);
    assert(source.is_open());
    const std::string checked((std::istreambuf_iterator<char>(source)),
                              std::istreambuf_iterator<char>());
    // Git on Windows can checkout a one-line JSON fixture with CRLF.
    // The native stdout generator deliberately emits canonical LF; accept
    // only the OS newline conversion, never changed JSON data.
    std::string canonical=checked;
    if(canonical.size()==expected.size()+1 &&
       canonical.size()>=2 && canonical.substr(canonical.size()-2)=="\r\n")
        canonical.erase(canonical.size()-2,1);
    assert(expected==canonical);
    const auto parsed=juce::JSON::parse(juce::String::fromUTF8(expected.c_str()));
    assert(parsed.isObject());
    const auto events=parsed.getProperty("events",{});
    assert(events.getArray()&&events.getArray()->size()==4);
    const auto snap=events.getArray()->getReference(0).getProperty("payload",{});
    const auto device=snap.getProperty("device",{});
    const auto graph=snap.getProperty("graph",{});
    const auto telem=snap.getProperty("telemetry",{});
    const auto cmd=events.getArray()->getReference(1).getProperty("payload",{});
    const auto ack=events.getArray()->getReference(2).getProperty("payload",{});
    const auto repeated=events.getArray()->getReference(3).getProperty("payload",{});
    assert(cmd.getProperty("requestId","").toString()==
           ack.getProperty("requestId","").toString());
    assert(cmd.getProperty("deviceEpoch","").toString()=="1");
    assert(juce::JSON::toString(snap)==juce::JSON::toString(repeated));
    // The same installed JUCE bridge must produce this UNREADY event state.
    const auto native=armix::console::LiveConsoleBridgeV6::snapshot(juce::var());
    assert(juce::JSON::toString(snap)==juce::JSON::toString(native));
    armix::console::LiveConsoleBridgeV6 runtime;
    const auto realAck=runtime.rejectUnsupportedIntent(cmd,native);
    assert(juce::JSON::toString(ack)==juce::JSON::toString(realAck));
    assert(static_cast<int>(snap.getProperty("schemaVersion",0))==6);
    assert(!static_cast<bool>(snap.getProperty("productionCallbackLive",true)));
    assert(device.getProperty("enabledInputs",{}).getArray()->isEmpty());
    assert(device.getProperty("epoch","").toString()=="1");
    assert(device.getProperty("enabledOutputs",{}).getArray()->isEmpty());
    assert(device.getProperty("inputEndpoints",{}).getArray()->isEmpty());
    assert(device.getProperty("outputEndpoints",{}).getArray()->isEmpty());
    assert(device.getProperty("sampleRate",{}).isVoid());
    assert(graph.getProperty("channels",{}).getArray()->isEmpty());
    assert(graph.getProperty("capabilities",{})
        .getProperty("supportedOps",{}).getArray()->isEmpty());
    assert(!static_cast<bool>(telem.getProperty("mainPcmPresent",true)));
    assert(ack.getProperty("requestId","").toString()=="native-fixture-session:1");
    assert(ack.getProperty("status","").toString()=="REJECTED");
    PuzzleV8Snapshot withPins;
    withPins.inputs.push_back({5,0,"Mic \"1\"\n",true,.25});
    withPins.outputs.push_back({7,0,"PHYS OUT8",false,0.});
    const auto physical=juce::JSON::parse(juce::String::fromUTF8(
        PuzzleV8Wire::snapshot(withPins).c_str()));
    const auto physicalDev=physical.getProperty("device",{});
    const auto inputMask=physicalDev.getProperty("enabledInputs",{});
    const auto outputMask=physicalDev.getProperty("enabledOutputs",{});
    assert(inputMask.getArray()->size()==1 &&
        static_cast<int>(inputMask.getArray()->getReference(0))==5);
    assert(outputMask.getArray()->size()==1 &&
        static_cast<int>(outputMask.getArray()->getReference(0))==7);
    const auto in=physicalDev.getProperty("inputEndpoints",{});
    assert(in.getArray()->getReference(0).getProperty("label","").toString()==
           "Mic \"1\"\n");
    assert(in.getArray()->getReference(0).getProperty("id","").toString()=="in:5");
    assert(static_cast<bool>(in.getArray()->getReference(0).getProperty("active",false)));
    assert(static_cast<double>(in.getArray()->getReference(0)
        .getProperty("rawPeak",0.))==.25);
}

// E2: native JUCE VST3 stays audible through a serial GROUP edit; a missing
// successor FX owner rejects atomically rather than deleting hosted processing.
static void verifyChannelIdGroupPreservesHostedFx() {
    using Bank=DynamicChannelFxLeaseBank;
    using Pub=DynamicNativeSessionPublisher;
    constexpr std::uint64_t epoch=1007;
    constexpr float center=.7071067811865475f;
    DynamicAcceptedSession live;
    DynamicGraphRequest route;route.physicalInputs=1;route.maxFrames=32;
    ChannelEdit micCreate;micCreate.kind=ChannelEdit::Kind::add;
    micCreate.position=0;micCreate.role=ChannelRole::input;
    micCreate.format=ProcessingFormat::mono;micCreate.text="VOICE";
    ChannelEdit mainCreate=micCreate;mainCreate.position=1;
    mainCreate.role=ChannelRole::main;
    mainCreate.format=ProcessingFormat::stereo;mainCreate.text="MAIN";
    const std::array<ChannelEdit,2> creates{micCreate,mainCreate};
    const auto created=live.edit(0,0,creates,route,epoch,epoch,true);
    assert(created.ok()&&created.edit.createdIds.size()==2);
    const auto voice=created.edit.createdIds[0],main=created.edit.createdIds[1];
    route.sources={{voice,0,-1,false}};
    ChannelEdit link;link.kind=ChannelEdit::Kind::setPrimaryOutput;
    link.id=voice;link.target=main;
    const std::array<ChannelEdit,1> routeEdit{link};
    auto gain=std::make_unique<GainFx>(2.f);
    gain->prepareToPlay(48000.,32);
    Bank::Assignment assignment;assignment.id=voice;
    assignment.format=ProcessingFormat::mono;
    assignment.inserts.push_back(std::make_shared<LiveChannelFxGraph::FxInstance>(
        std::move(gain),"voice-gain","voice-gain",32));
    const auto token=assignment.inserts[0]->instanceId;
    const std::array<Bank::Assignment,1> assignments{assignment};
    auto bank=std::make_shared<Bank>();
    assert(Bank::prepare(epoch,2,assignments,*bank)==Bank::Fault::none);
    const auto plan=[&](std::shared_ptr<Bank> owner,std::uint64_t rev) {
        Pub::FxPlan fx;fx.owner=std::static_pointer_cast<void>(owner);
        fx.context=owner.get();
        fx.render=+[](void* p,ChannelId id,float* l,float* r,int n) noexcept {
            (*static_cast<Bank*>(p))(id,l,r,n);
        };
        fx.deviceEpoch=epoch;fx.graphRevision=rev;return fx;
    };
    assert(live.edit(1,1,routeEdit,route,epoch,epoch,true,plan(bank,2)).ok());
    std::array<float,32> input{},l{},rr{};input.fill(.2f);
    const float* pins[]{input.data()};
    int fallbackCalls=0;
    const auto noFallback=[&](ChannelId,float*,float*,int) noexcept {
        ++fallbackCalls;
    };
    const auto render=[&]() {
        return live.render(epoch,pins,1,l.data(),rr.data(),32,false,noFallback);
    };
    auto heapBefore=heapCalls.load(std::memory_order_relaxed);
    assert(render()&&near(l[31],.2f*2.f*center));
    assert(heapCalls.load(std::memory_order_relaxed)==heapBefore);
    const std::array<ChannelId,1> selected{voice};
    const auto denied=live.createBusForSelected(2,2,selected,"VOCALS",
        ChannelRole::group,route,epoch,epoch,true);
    assert(!denied.ok()&&denied.edit.createdIds.empty());
    assert(denied.native.fault==Pub::Fault::invalidCandidate);
    assert(live.acceptedGraphRevision()==2);
    assert(render()&&near(l[31],.2f*2.f*center));
    auto nextBank=std::make_shared<Bank>();
    assert(Bank::reuseRevision(epoch,3,*bank,*nextBank)==Bank::Fault::none);
    assert(Bank::reuseRevision(epoch,2,*bank,*nextBank)==Bank::Fault::invalidEpoch);
    assert((*nextBank->ownerForControl(voice,0))->instanceId==token);
    const int pluginCount=GainFx::living;
    const auto group=live.createBusForSelected(2,2,selected,"VOCALS",
        ChannelRole::group,route,epoch,epoch,true,.25f,plan(nextBank,3));
    assert(group.ok()&&group.edit.createdIds.size()==1);
    assert(live.acceptedGraphRevision()==3);
    assert(live.acceptedRouting(epoch,3)->find(voice)->primaryOutput==
           group.edit.createdIds.front());
    heapBefore=heapCalls.load(std::memory_order_relaxed);
    assert(render()&&near(l[31],.2f*2.f*center));
    assert(heapCalls.load(std::memory_order_relaxed)==heapBefore);
    assert(GainFx::living==pluginCount&&fallbackCalls==0);
    assert(!live.setLiveMix(epoch,2,voice,0.f,0.f));
    live.unpublish();live.pollRetirement();
}

static void verifyLatencyAlignedBypass() {
    // Real JUCE processing (not merely reporting a latency value): an impulse
    // remains at sample 7 through wet, individual bypass and rack bypass.
    for(int mode=0;mode<3;++mode){
        auto p=std::make_unique<GainFx>(1.f,7);p->prepareToPlay(48000,64);
        LiveChannelFxGraph::FxInstance fx(std::move(p),"delayed","7 samples",64);
        fx.bypassRequested.store(mode==1);
        std::array<float,64> l{},r{};
        // Reach the bypass envelope's steady state first.
        for(int block=0;block<10;++block){l.fill(0);r.fill(0);fx.render(l.data(),r.data(),64,mode==2);}
        l.fill(0);r.fill(0);l[0]=r[0]=1;
        const auto before=heapCalls.load();
        fx.render(l.data(),r.data(),64,mode==2);
        assert(heapCalls.load()==before);
        for(int i=0;i<64;++i)assert(near(l[i],i==7?1.f:0.f)&&near(l[i],r[i]));
        fx.processor->setLatencySamples(8);
        assert(fx.latencyChanged.load()); // no unaligned wet enters accepted PCM
        l.fill(0);r.fill(0);l[0]=r[0]=1;
        fx.render(l.data(),r.data(),64,mode==2);
        for(int i=0;i<64;++i)assert(near(l[i],i==7?1.f:0.f));
    }
}
static void verifyNativeFxRackTransactions() {
    DynamicChannelRoster roster;
    std::vector<ChannelEdit> add;
    for(int i=0;i<3;++i){ChannelEdit e;e.kind=ChannelEdit::Kind::add;e.position=i;
        e.role=i==2?ChannelRole::main:ChannelRole::input;e.format=ProcessingFormat::stereo;e.text="channel";add.push_back(e);}
    const auto created=roster.edit(0,add);assert(created.ok());
    const auto source=created.createdIds[0],dest=created.createdIds[1],main=created.createdIds[2];
    std::vector<ChannelEdit> refs;
    for(auto label:{"gain","offset"}){ChannelEdit e;e.kind=ChannelEdit::Kind::addInsertReference;e.id=source;e.text=label;refs.push_back(e);}
    for(auto id:{source,dest}){ChannelEdit e;e.kind=ChannelEdit::Kind::setPrimaryOutput;e.id=id;e.target=main;refs.push_back(e);}
    assert(roster.edit(1,refs).ok());
    auto gain=std::make_unique<GainFx>(2.f);gain->prepareToPlay(48000,64);
    auto offset=std::make_unique<GainFx>(1.f,0,1.f);offset->prepareToPlay(48000,64);
    auto a=std::make_shared<LiveChannelFxGraph::FxInstance>(std::move(gain),"gain","gain",64);
    auto b=std::make_shared<LiveChannelFxGraph::FxInstance>(std::move(offset),"offset","offset",64);
    DynamicChannelFxLeaseBank::Assignment binding;binding.id=source;binding.inserts={a,b};
    DynamicChannelFxLeaseBank initial;
    assert(DynamicChannelFxLeaseBank::prepare(11,1,std::span(&binding,1),initial)==DynamicChannelFxLeaseBank::Fault::none);
    auto bank=std::make_shared<DynamicChannelFxLeaseBank>(std::move(initial));
    DynamicNativeSessionPublisher publisher;DynamicGraphRequest request;
    request.physicalInputs=4;request.maxFrames=64;request.sources={{source,0,1,true},{dest,2,3,true}};
    std::uint64_t revision=0;
    const auto publish=[&](){
        DynamicNativeSessionPublisher::FxPlan fx;fx.owner=bank;fx.context=bank.get();fx.deviceEpoch=11;fx.graphRevision=revision+1;
        fx.render=+[](void* p,ChannelId id,float* l,float* r,int n) noexcept{(*static_cast<DynamicChannelFxLeaseBank*>(p))(id,l,r,n);};
        fx.latency=+[](void* p,ChannelId id) noexcept{return static_cast<DynamicChannelFxLeaseBank*>(p)->latencySamples(id);};
        assert(publisher.commit(roster,request,revision,11,11,false,fx,false,true).ok());++revision;
    };
    const auto samples=[&](float expected){
        std::array<float,64> input{},other{},l{},r{};input.fill(.25f);other.fill(.1f);
        const float* pins[]{input.data(),input.data(),other.data(),other.data()};const auto count=heapCalls.load();
        assert(publisher.render(11,pins,4,l.data(),r.data(),64,false,[](ChannelId,float*,float*,int) noexcept{}));
        assert(heapCalls.load()==count);for(int i=0;i<64;++i)assert(near(l[i],expected)&&near(l[i],r[i]));
    };
    publish();samples(1.6f);const int prepares=GainFx::prepared;
    const auto edit=[&](ChannelId from,std::size_t slot,const juce::String& token,ChannelId to,std::size_t pos){
        DynamicChannelRoster next;DynamicChannelFxLeaseBank owners;
        assert(prepareNativeFxRackEdit(11,revision,roster.revision(),roster,*bank,from,slot,token,to,pos,next,owners));
        roster=std::move(next);bank=std::make_shared<DynamicChannelFxLeaseBank>(std::move(owners));publish();
        assert(GainFx::prepared==prepares); // no scan, instantiation or prepare/reset
    };
    DynamicChannelRoster rejected;DynamicChannelFxLeaseBank rejectedBank;
    assert(!prepareNativeFxRackEdit(11,revision,roster.revision(),roster,*bank,source,0,"wrong",dest,0,rejected,rejectedBank));
    assert(!prepareNativeFxRackEdit(12,revision,roster.revision(),roster,*bank,source,0,a->instanceId,dest,0,rejected,rejectedBank));
    assert(!prepareNativeFxRackEdit(11,revision,roster.revision()-1,roster,*bank,source,0,a->instanceId,dest,0,rejected,rejectedBank));
    assert(!prepareNativeFxRackEdit(11,revision,roster.revision(),roster,*bank,source,0,a->instanceId,dest,9,rejected,rejectedBank));
    samples(1.6f);
    edit(source,0,a->instanceId,source,1);samples(2.6f);assert(bank->retainsEditor(a));
    edit(source,1,a->instanceId,dest,0);samples(1.45f);
    assert(bank->retainsEditor(a)&&(*bank->ownerForControl(dest,0)).get()==a.get());
    assert(roster.find(source)->insertCatalogIds==std::vector<std::string>{"offset"});
    assert(roster.find(dest)->insertCatalogIds==std::vector<std::string>{"gain"});
    edit(source,0,b->instanceId,{},0);samples(.45f);assert(!bank->retainsEditor(b));
    edit(dest,0,a->instanceId,{},0);samples(.35f);assert(!bank->retainsEditor(a));
}

// Native eight-band Parametric EQ must be the exact ordered rack lease in PCM,
// not an independent "pre-VST" bank, a JS preview or a second audio callback.
// Verifies asynchronous audio-owned ACK while the same insert is bypassed,
// graph revision-safe reorder, and no allocations in the hot render call.
static void verifyNativeBuiltinEqOrderedPcmAndBypassAck() {
    using Bank=DynamicChannelFxLeaseBank;
    using Eq=armix::builtin_fx::ChannelEqProcessor;
    constexpr std::uint64_t ep=10997,revision=11;
    const ChannelId ch{12345678901234ULL};
    std::unique_ptr<Eq> eq=std::make_unique<Eq>();
    eq->prepareToPlay(48000.,64);
    assert(eq->isReady());
    Eq* live=eq.get();
    auto eqLease=std::make_shared<LiveChannelFxGraph::FxInstance>(
        std::move(eq),Eq::catalogIdentity,"Parametric EQ",64);
    auto other=std::make_unique<GainFx>(1.0f,0,.2f);
    other->prepareToPlay(48000.,64);
    auto offsetLease=std::make_shared<LiveChannelFxGraph::FxInstance>(
        std::move(other),"nonlinear-order-probe","Offset after EQ",64);
    Bank::Assignment a;a.id=ch;a.format=ProcessingFormat::stereo;
    a.inserts={eqLease,offsetLease};
    std::array<Bank::Assignment,1> assignment{a};
    Bank graph;
    assert(Bank::prepare(ep,revision,assignment,graph)==Bank::Fault::none);
    assert(graph.insertCount(ch)==2 && graph.latencySamples(ch)==0);
    assert((*graph.ownerForControl(ch,0))->instanceId==eqLease->instanceId);
    assert((*graph.ownerForControl(ch,1))->instanceId==offsetLease->instanceId);
    armix::builtin_fx::Band hp{};
    hp.enabled=true;hp.frequencyHz=1000.f;hp.gainDb=0.f;
    hp.q=.70710678f;hp.slope=armix::builtin_fx::Slope::db12;
    assert(live->queueBand(0,hp,1));
    std::array<float,64> l{},r{};
    const auto renderBlocks=[&](const Bank& bank,int blocks) {
        const auto allocations=heapCalls.load(std::memory_order_relaxed);
        for(int b=0;b<blocks;++b){
            l.fill(.5f);r.fill(.5f);
            bank(ch,l.data(),r.data(),64);
        }
        assert(heapCalls.load(std::memory_order_relaxed)==allocations);
        assert(std::isfinite(l.back()) && std::isfinite(r.back()));
    };
    renderBlocks(graph,64);
    Eq::Completion done;
    assert(live->pollCompletion(done)&&done.applied&&done.requestId==1);
    assert(done.bandIndex==0&&live->acceptedBands()[0].enabled);
    // A constant .5 enters HPF, then post-EQ vendor offset +.2 survives.
    assert(std::abs(l.back()-.2f)<.04f &&std::abs(r.back()-.2f)<.04f);
    const auto token=eqLease->instanceId;
    assert(!graph.setInsertBypass(ep,revision-1,ch,0,token,true));
    assert(!graph.setInsertBypass(ep,revision,ch,0,"wrong",true));
    assert(graph.setInsertBypass(ep,revision,ch,0,token,true));
    renderBlocks(graph,64);
    assert(std::abs(l.back()-.7f)<.04f);
    // Fully bypassed native EQ still consumes audio-synchronized commands;
    // no ACK may be fabricated from the control-thread enqueue itself.
    hp.frequencyHz=1600.f;
    assert(live->queueBand(0,hp,2));
    assert(!live->pollCompletion(done));
    renderBlocks(graph,2);
    assert(live->pollCompletion(done)&&done.applied&&done.requestId==2);
    assert(std::abs(live->acceptedBands()[0].frequencyHz-1600.f)<.01f);
    assert(graph.setInsertBypass(ep,revision,ch,0,token,false));
    renderBlocks(graph,64);
    assert(std::abs(l.back()-.2f)<.04f);
    Bank moved;
    assert(Bank::editOwned(ep,revision+1,graph,ch,0,token,
                            ch,1,ProcessingFormat::stereo,moved)==Bank::Fault::none);
    assert(moved.insertCount(ch)==2);
    assert((*moved.ownerForControl(ch,1)).get()==eqLease.get());
    assert((*moved.ownerForControl(ch,0)).get()==offsetLease.get());
    // Exact EQ now comes AFTER the offset-producing FX, so the HPF removes
    // DC from both input and offset. This distinguishes true PCM FX ordering.
    renderBlocks(moved,64);
    assert(std::abs(l.back())<.04f && std::abs(r.back())<.04f);
    assert(!moved.setInsertBypass(ep,revision,ch,1,token,true));
    assert(moved.leaseEditor(ep,revision+1,ch,1,token).get()==eqLease.get());
}

// Binary EQ state is captured only when the audio mailbox has settled.
// The verifier covers the pending/ACK window, state fidelity and the
// pre-publication restore rule without touching live device or worker threads.
static void verifyNativeEqCaptureRequiresAcceptedDspAndOfflineRestore() {
    using Eq=armix::builtin_fx::ChannelEqProcessor;
    auto processor=std::make_unique<Eq>();
    processor->prepareToPlay(48000.0,64);
    assert(processor->isReady());
    Eq* live=processor.get();
    auto owner=std::make_shared<LiveChannelFxGraph::FxInstance>(
        std::move(processor),Eq::catalogIdentity,"Parametric EQ",64);
    armix::builtin_fx::Band b{};
    b.enabled=true;b.frequencyHz=2500.f;b.gainDb=5.f;b.q=1.2f;
    b.slope=armix::builtin_fx::Slope::db12;
    assert(live->queueBand(2,b,501));
    juce::MemoryBlock saved;
    assert(!owner->tryCaptureState(saved)); // queued, not audio-owned
    std::array<float,64> left{},right{};
    left.fill(.1f);right.fill(.1f);
    const auto heapBefore=heapCalls.load(std::memory_order_relaxed);
    owner->render(left.data(),right.data(),64);
    assert(heapCalls.load(std::memory_order_relaxed)==heapBefore);
    assert(!owner->tryCaptureState(saved)); // applied but not ACK-consumed
    Eq::Completion ack{};
    assert(live->pollCompletion(ack)&&ack.applied&&ack.requestId==501);
    assert(ack.bandIndex==2&&ack.band.gainDb==5.f);
    assert(owner->tryCaptureState(saved)); // control state now stable
    assert(saved.getSize()==8+8*14);
    assert(!live->restoreUnpublishedState(saved.getData(),
        static_cast<int>(saved.getSize()))); // published LIVE = immutable

    // A newly built Show candidate may restore before preparation.
    Eq restored;
    assert(restored.restoreUnpublishedState(saved.getData(),
                                            static_cast<int>(saved.getSize())));
    assert(restored.acceptedBands()[2].enabled);
    assert(restored.acceptedBands()[2].frequencyHz==2500.f);
    assert(restored.acceptedBands()[2].gainDb==5.f);
    restored.prepareToPlay(44100.0,64);
    assert(restored.isReady());
    assert(restored.acceptedBands()[2].q==1.2f);

    // The existing Show pipeline restores vendor states after preparation.
    // That must work too, but only while the candidate is NOT published.
    auto prepared=std::make_unique<Eq>();
    prepared->prepareToPlay(96000.0,128);
    assert(prepared->restoreUnpublishedState(saved.getData(),
                              static_cast<int>(saved.getSize())));
    assert(prepared->acceptedBands()[2].gainDb==5.f);
    juce::MemoryBlock bad(saved);
    static_cast<unsigned char*>(bad.getData())[0]^=0x5f;
    assert(!prepared->restoreUnpublishedState(bad.getData(),
        static_cast<int>(bad.getSize())));
    assert(prepared->acceptedBands()[2].gainDb==5.f);
    auto sealed=std::make_shared<LiveChannelFxGraph::FxInstance>(
        std::move(prepared),Eq::catalogIdentity,"Restored EQ",128);
    auto* sealedEq=dynamic_cast<Eq*>(sealed->processor.get());
    assert(sealedEq&&!sealedEq->restoreUnpublishedState(saved.getData(),
        static_cast<int>(saved.getSize())));
    assert(sealed->tryCaptureState(bad)); // restored bank is quiescent
}

int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--emit-console-s0-json") {
        verifyNativePuzzleV8FixtureAndWire();
        armix::console::PuzzleV8Snapshot s;
        armix::console::PuzzleV8Ack ack;
        ack.requestId="native-fixture-session:1";
        ack.deviceEpoch=s.epoch;
        std::cout<<armix::console::PuzzleV8Wire::fixture(s,ack);
        return std::cout.good()?0:1;
    }
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
    // FX RACK POWER: one atomic master overrides effective processing,
    // without destroying the ON/OFF preferences of individual inserts.
    {
        auto graph=makeGraph(2.f,3.f);
        const auto token=graph->insertInstanceId(0);
        assert(!graph->rackBypassed(0));
        assert(graph->setInsertBypass(0,0,token,true)); // individual OFF
        assert(graph->insertBypassed(0,0));
        assert(graph->setRackBypass(0,true));           // master OFF
        assert(graph->rackBypassed(0)&&graph->insertBypassed(0,0));
        assert(graph->setRackBypass(0,false));          // master ON
        assert(!graph->rackBypassed(0));
        assert(graph->insertBypassed(0,0));             // stays OFF
        assert(graph->setInsertBypass(0,0,token,false));
        assert(graph->setRackBypass(0,true));
        assert(graph->setRackBypass(0,false));
        assert(!graph->insertBypassed(0,0));            // returns to ON
        assert(!graph->setRackBypass(-1,true));
        assert(!graph->setRackBypass(LiveChannelFxGraph::fxSlots,true));
        auto moved=makeGraph(1.f,1.f,false);
        assert(graph->setRackBypass(0,true));
        assert(moved->reuse(0,*graph,"vst3-ch1-id"));
        assert(moved->rackBypassed(0));                 // graph retirement retains gate
        assert(moved->insertBypassed(0,0)==graph->insertBypassed(0,0));
        assert(moved->setRackBypass(0,false));
        assert(!moved->insertBypassed(0,0));           // saved ON restored
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
    verifyFirstSoundOnePinNativeSourceDraft();
    verifyNativePuzzleV8FixtureAndWire();
    verifyChannelIdGroupPreservesHostedFx();
    verifyLatencyAlignedBypass();
    verifyNativeFxRackTransactions();
    verifyNativeBuiltinEqOrderedPcmAndBypassAck();
    verifyNativeEqCaptureRequiresAcceptedDspAndOfflineRestore();
    // Tests above use a real JUCE AudioProcessor processing seam. Vendor
    // VST3 formats and sound quality require separate Windows hardware FAT.
}
