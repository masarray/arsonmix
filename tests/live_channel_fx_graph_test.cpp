// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/LiveChannelFxGraph.h"
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
static std::unique_ptr<LiveChannelFxGraph> makeGraph(float firstFx,float returnFx) {
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
    auto a=std::make_unique<GainFx>(firstFx);
    auto b=std::make_unique<GainFx>(returnFx);
    a->prepareToPlay(48000,8);
    b->prepareToPlay(48000,8);
    assert(graph->install(0,std::move(a),"CH1 FX"));
    assert(graph->install(1,std::move(b),"BUS FX"));
    assert(!graph->install(1,std::make_unique<GainFx>(2.f),"duplicate"));
    assert(graph->activeInserts()==2);
    assert(graph->maxFramesPerPlugin()==32); // clamp minimum safe plugin quantum
    return graph;
}
int main() {
    // This test executes actual JUCE AudioProcessor::processBlock for
    // DIFFERENT strip instances and a BUS return, no VST3 vendor binaries.
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
        // .4 input * CH1 FX 3 * PRE .5 * BUS FX 2 == 1.2
        assert(near(l[0],1.2f)&&near(r[0],1.2f));
        assert(near(meters.channelPeak[1],1.2f));
        assert(near(meters.channelPeak[0],0.f)); // fader zero, PRE audible
        auto newer=std::make_unique<Publication::Snapshot>();
        newer->deviceEpoch=11;newer->graphRevision=2;
        newer->prepared=makeGraph(2.f,4.f);
        assert(routes.publish(std::move(newer),1,11)==Publication::Result::applied);
        assert(!routes.pollRetirement()); // first graph still reader-leased
        assert(GainFx::living==4); // 2 FX per graph; no shared pointers
        assert(fx->process(phys,3,l.data(),r.data(),4));
        assert(near(l[0],1.2f));
    }
    assert(routes.pollRetirement());
    assert(GainFx::living==2); // old VST instances retired off audio thread
    {
        auto lease=routes.acquire();
        assert(lease.get(11));
        assert(lease.get(11)->prepared->process(phys,3,l.data(),r.data(),4));
        // .4 * 2 * .5 * 4 = 1.6
        assert(near(l[0],1.6f));
        assert(!lease.get(12)); // stale driver epoch is silent
    }
    routes.unpublish();
    assert(routes.pollRetirement());
    assert(GainFx::living==0);
    assert(GainFx::released==4);
    // Tests above use a real JUCE AudioProcessor processing seam. Vendor
    // VST3 formats and sound quality require separate Windows hardware FAT.
}
