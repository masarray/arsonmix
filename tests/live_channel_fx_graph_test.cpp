// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/LiveChannelFxGraph.h"
#include "../engine/core/NativeOutputPairTap.h"
#include "../engine/core/RealtimeRoutePublisher.h"
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
    void getStateInformation(juce::MemoryBlock&)override{}
    void setStateInformation(const void*,int)override{}
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
    // Tests above use a real JUCE AudioProcessor processing seam. Vendor
    // VST3 formats and sound quality require separate Windows hardware FAT.
}
