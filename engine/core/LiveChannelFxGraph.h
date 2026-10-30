// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PreparedMixerBlock.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

// P1J: routing plans are replaceable; vendor FX instances are NOT routing
// plans. Identical catalog ID + same device epoch reuses the exact instance
// (parameters, state, reverb tail, and private MIDI scratch) across revisions.
// All shared_ptr bookkeeping occurs on CONTROL thread. Audio callback reads
// immutable raw pointers and does no refcounting/allocation/locking.
// An atomic try-enter prevents concurrent processing if a backend violates
// single-render-callback ordering. Contention FAILS CLOSED, no spinning.
namespace armix::mixer {
class LiveChannelFxGraph final {
public:
    static constexpr int channels=stripCount;
    static constexpr int fxSlots=stripCount+auxBusCount;
    static constexpr int maxInsertsPerStrip=8; // fixed capacity, no RT allocation
    using Processor=juce::AudioProcessor;

    struct FxInstance final {
        std::unique_ptr<Processor> processor;
        juce::MidiBuffer midi;
        const juce::String catalogId;
        const juce::String name;
        std::atomic_flag processing=ATOMIC_FLAG_INIT;
        std::atomic<unsigned> contention{0};
        std::atomic<unsigned> exceptions{0};

        FxInstance(std::unique_ptr<Processor> p,juce::String id,juce::String label)
            : processor(std::move(p)),catalogId(std::move(id)),name(std::move(label)) {
            midi.ensureSize(1024); // CONTROL, before publication
        }
        FxInstance(const FxInstance&)=delete;
        FxInstance& operator=(const FxInstance&)=delete;
        ~FxInstance() noexcept {
            // Shared by old/new immutable snapshots; last owner drops ONLY on
            // control-thread retirement after the final RT reader exits.
            if(processor) {
                try { processor->releaseResources(); }
                catch(...) { /* vendor cleanup cannot unwind graph retirement */ }
            }
        }
        void render(float* left,float* right,int samples) noexcept {
            if(processing.test_and_set(std::memory_order_acquire)) {
                contention.fetch_add(1,std::memory_order_relaxed);
                juce::FloatVectorOperations::clear(left,samples);
                juce::FloatVectorOperations::clear(right,samples);
                return;
            }
            try {
                float* planar[]{left,right};
                juce::AudioBuffer<float> buffer(planar,2,samples);
                midi.clear();
                processor->processBlock(buffer,midi);
            } catch(...) {
                // Vendor faults must never unwind through the JUCE callback.
                // A SEH crash still requires out-of-process isolation.
                exceptions.fetch_add(1,std::memory_order_relaxed);
                juce::FloatVectorOperations::clear(left,samples);
                juce::FloatVectorOperations::clear(right,samples);
            }
            processing.clear(std::memory_order_release);
        }
    };
    using FxRack=std::array<std::shared_ptr<FxInstance>,maxInsertsPerStrip>;
    using FxBank=std::array<FxRack,fxSlots>;

    explicit LiveChannelFxGraph(const PreparedRenderGraph& plan,int frames,int pluginQuantum=512)
        : block_(std::make_unique<PreparedMixerBlock>(plan,frames)),
          pluginQuantum_(std::clamp(pluginQuantum,32,PreparedMixerBlock::hardFrameLimit)) {
        for(int i=0;i<channels;++i)
            controls_.setPreparedCoefficients(i,plan.leftGain[i],plan.rightGain[i]);
    }

    LiveChannelFxGraph(const LiveChannelFxGraph&)=delete;
    LiveChannelFxGraph& operator=(const LiveChannelFxGraph&)=delete;
    ~LiveChannelFxGraph() noexcept=default;

    int capacity() const noexcept{return block_?block_->capacity():0;}
    int maxFramesPerPlugin() const noexcept {return pluginQuantum_;}
    int activeInserts() const noexcept{return activeInserts_;}
    bool setStripControl(int channel,float gain,float pan,
                         bool mono,bool mute,bool solo) noexcept {
        return controls_.set(channel,gain,pan,mono,mute,solo);
    }
    int insertsOn(int strip) const noexcept {
        return strip>=0&&strip<fxSlots?counts_[strip]:0;
    }
    const juce::String& insertName(int strip,int position=0) const noexcept {
        static const juce::String empty;
        return strip>=0&&strip<fxSlots&&position>=0&&position<counts_[strip]
            ?processors_[strip][position]->name:empty;
    }
    const juce::String& insertId(int strip,int position=0) const noexcept {
        static const juce::String empty;
        return strip>=0&&strip<fxSlots&&position>=0&&position<counts_[strip]
            ?processors_[strip][position]->catalogId:empty;
    }
    const Processor* instanceProcessor(int strip,int position=0) const noexcept {
        return strip>=0&&strip<fxSlots&&position>=0&&position<counts_[strip]
            ?processors_[strip][position]->processor.get():nullptr;
    }

    // Immutable in the RT callback. Caller uses an unpublished graph and
    // preserves epoch, sample-rate, quantum and plugin identity externally.
    // A move reuses the SAME FxInstance from another position of this lane.
    // Never reuse an instance twice in one new rack.
    bool reuseAt(int strip,int target,const LiveChannelFxGraph& previous,
                 int previousPosition,const juce::String& catalogId) {
        if(strip<0||strip>=fxSlots||target!=counts_[strip]||
           target<0||target>=maxInsertsPerStrip||
           previousPosition<0||previousPosition>=previous.counts_[strip]||
           catalogId.isEmpty()||previous.pluginQuantum_!=pluginQuantum_)
            return false;
        const auto& original=previous.processors_[strip][previousPosition];
        if(!original||original->catalogId!=catalogId)return false;
        for(int i=0;i<counts_[strip];++i)
            if(processors_[strip][i]==original)return false;
        processors_[strip][target]=original; // shared_ptr CONTROL only
        ++counts_[strip];++activeInserts_;
        return true;
    }
    bool reuse(int strip,const LiveChannelFxGraph& previous,
               const juce::String& catalogId) {
        return reuseAt(strip,0,previous,0,catalogId);
    }
    bool installAt(int strip,int position,std::unique_ptr<Processor> processor,
                   const juce::String& name,const juce::String& catalogId={}) {
        if(strip<0||strip>=fxSlots||position!=counts_[strip]||
           position<0||position>=maxInsertsPerStrip||!processor)
            return false;
        processors_[strip][position]=std::make_shared<FxInstance>(
            std::move(processor),catalogId,name);
        ++counts_[strip];++activeInserts_;
        return true;
    }
    bool install(int strip,std::unique_ptr<Processor> processor,
                 const juce::String& name,const juce::String& catalogId={}) {
        return installAt(strip,0,std::move(processor),name,catalogId);
    }

    // One actual physical -> FX -> PRE send -> fader -> POST send -> BUS ->
    // master path. FX lifetimes are external to the immutable routing plan.
    // R0: an existing multi-insert CH1 rack is invoked from the SAME native
    // routing render graph. Its callback-scoped lifetime is separately pinned
    // by the host's LiveGraphHandoff cycle. Never allocate/copy/retain it here.
    // No duplicate CH1 native insert may execute at the same time.
    using ExternalStripFx=void(*)(void*,int,float*,float*,int) noexcept;
    bool process(const float* const* physical,int inputs,
                 float* left,float* right,int frames,
                 RenderLevels* levels=nullptr,
                 void* externalContext=nullptr,
                 ExternalStripFx externalFx=nullptr) noexcept {
        auto channelFx=[this,externalContext,externalFx]
          (int strip,float* l,float* r,int samples) noexcept {
            if(strip<0||strip>=fxSlots)return;
            const int count=counts_[strip]; // immutable, prepared OFF RT
            if(count>0) {
                for(int slot=0;slot<count;++slot) {
                    auto* instance=processors_[strip][slot].get();
                    instance->render(l,r,samples); // no shared_ptr copies or RT heap use
                }
            } else if(strip==0 && externalFx)
                externalFx(externalContext,strip,l,r,samples);
        };
        return block_->process(physical,inputs,left,right,frames,channelFx,levels,&controls_);
    }

private:
    std::unique_ptr<PreparedMixerBlock> block_;
    RealtimeStripControls controls_ {};
    FxBank processors_ {};
    std::array<std::uint8_t,fxSlots> counts_ {};
    int activeInserts_=0;
    int pluginQuantum_=512;
};
} // namespace armix::mixer
