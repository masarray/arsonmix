// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PreparedMixerBlock.h"
#include "FxBypassEnvelope.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

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
        const juce::String instanceId; // stable through MOVE; never a slot index
        std::atomic<bool> bypassRequested{false};
        armix::dsp::FxBypassEnvelope bypassFade; // RT exclusive state
        std::vector<float> dryLeft,dryRight; // CONTROL allocation
        inline static std::atomic<std::uint64_t> nextId{1};
        std::atomic_flag processing=ATOMIC_FLAG_INIT;
        std::atomic<unsigned> contention{0};
        std::atomic<unsigned> exceptions{0};

        FxInstance(std::unique_ptr<Processor> p,juce::String id,juce::String label,
                   int maxSamples)
            : processor(std::move(p)),catalogId(std::move(id)),name(std::move(label)),
              instanceId("g5c-"+juce::String(static_cast<juce::int64>(
                  nextId.fetch_add(1,std::memory_order_relaxed)))),
              dryLeft(static_cast<std::size_t>(maxSamples)),
              dryRight(static_cast<std::size_t>(maxSamples)) {
            midi.ensureSize(1024);
            bypassFade.prepare(false,processor?processor->getSampleRate():48000.0,0);
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
        // G5E: vendor binary state capture is CONTROL only. This is the same
        // fail-closed gate used by realtime processBlock. While control owns
        // the gate, a racing muted callback returns silence without waiting.
        bool tryCaptureState(juce::MemoryBlock& out) noexcept {
            if(!processor || processing.test_and_set(std::memory_order_acquire))
                return false;
            bool ok=false;
            try {
                juce::MemoryBlock candidate;
                processor->getStateInformation(candidate);
                if(candidate.getSize()<=8U*1024U*1024U) {
                    out=std::move(candidate);
                    ok=true;
                }
            } catch(...) { ok=false; }
            processing.clear(std::memory_order_release);
            return ok;
        }
        void render(float* left,float* right,int samples) noexcept {
            if(processing.test_and_set(std::memory_order_acquire)) {
                contention.fetch_add(1,std::memory_order_relaxed);
                juce::FloatVectorOperations::clear(left,samples);
                juce::FloatVectorOperations::clear(right,samples);
                return;
            }
            // G5C: atomic request from control; fade and scratch RT-owned.
            bypassFade.request(bypassRequested.load(std::memory_order_relaxed));
            if(bypassFade.fullyBypassed()) {
                processing.clear(std::memory_order_release);
                return;
            }
            if(samples<=0 || static_cast<std::size_t>(samples)>dryLeft.size()) {
                juce::FloatVectorOperations::clear(left,std::max(0,samples));
                juce::FloatVectorOperations::clear(right,std::max(0,samples));
                processing.clear(std::memory_order_release);
                return;
            }
            const bool fading=bypassFade.transitioning();
            if(fading) {
                juce::FloatVectorOperations::copy(dryLeft.data(),left,samples);
                juce::FloatVectorOperations::copy(dryRight.data(),right,samples);
            }
            try {
                float* planar[]{left,right};
                juce::AudioBuffer<float> buffer(planar,2,samples);
                midi.clear();
                processor->processBlock(buffer,midi);
            } catch(...) {
                // Vendor exceptions must never unwind through the callback.
                exceptions.fetch_add(1,std::memory_order_relaxed);
                juce::FloatVectorOperations::clear(left,samples);
                juce::FloatVectorOperations::clear(right,samples);
            }
            if(fading)for(int i=0;i<samples;++i) {
                const auto mix=bypassFade.next();
                left[i]=left[i]*mix.wet+dryLeft[static_cast<std::size_t>(i)]*mix.dry;
                right[i]=right[i]*mix.wet+dryRight[static_cast<std::size_t>(i)]*mix.dry;
            }
            processing.clear(std::memory_order_release);
        }
    };
    using InstanceLease=std::shared_ptr<FxInstance>; // CONTROL only; never copy on callback
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
    // MS3A: one immutable output destination pair per published topology.
    // These are callback indices within the verified active output mask,
    // never invented adjacent physical pin numbers.
    bool setMainOutputPair(int left,int right,int count) noexcept {
        if(left<0||right<0||left>=count||right>=count||left==right)return false;
        mainOutputLeft_=left;mainOutputRight_=right;
        return true;
    }
    int mainOutputLeft() const noexcept{return mainOutputLeft_;}
    int mainOutputRight() const noexcept{return mainOutputRight_;}
    // MS3B: independent MONITOR stereo bus with 10 CH + 4 AUX POST sends.
    // One prepared FX render produces both hardware destinations.
    bool setMonitorOutputBus(int left,int right,int outputCount,
                             const std::array<float,fxSlots>& gains) noexcept {
        if(left<0||right<0||left>=outputCount||right>=outputCount||
           left==right||left==mainOutputLeft_||left==mainOutputRight_||
           right==mainOutputLeft_||right==mainOutputRight_)return false;
        for(float v:gains)if(!std::isfinite(v)||v<0.f||v>1.f)return false;
        monitorLeft_=left;monitorRight_=right;
        monitorGains_=gains;monitorEnabled_=true;
        return true;
    }
    bool monitorEnabled() const noexcept{return monitorEnabled_;}
    int monitorOutputLeft() const noexcept{return monitorLeft_;}
    int monitorOutputRight() const noexcept{return monitorRight_;}
    const std::array<float,fxSlots>& monitorGains() const noexcept{return monitorGains_;}
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
    const juce::String& insertInstanceId(int strip,int position=0) const noexcept {
        static const juce::String empty;
        return strip>=0&&strip<fxSlots&&position>=0&&position<counts_[strip]
            ?processors_[strip][position]->instanceId:empty;
    }
    bool insertBypassed(int strip,int position) const noexcept {
        return strip>=0&&strip<fxSlots&&position>=0&&position<counts_[strip]
            ?processors_[strip][position]->bypassRequested.load(std::memory_order_relaxed):false;
    }
    bool setInsertBypass(int strip,int position,const juce::String& token,
                         bool bypass) noexcept {
        if(strip<0||strip>=fxSlots||position<0||position>=counts_[strip]||
           token.isEmpty())return false;
        auto* fx=processors_[strip][position].get();
        if(!fx||fx->instanceId!=token)return false;
        fx->bypassRequested.store(bypass,std::memory_order_release);
        return true;
    }
    // Same opaque token used by the native BYPASS ACK. A vendor editor takes
    // an owner lease ONLY on the message thread, never in process().
    InstanceLease leaseInsert(int strip,int position,const juce::String& token) const noexcept {
        if(strip<0||strip>=fxSlots||position<0||position>=counts_[strip]||
           token.isEmpty())return {};
        const auto& fx=processors_[strip][position];
        return fx&&fx->instanceId==token?fx:InstanceLease{};
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
            std::move(processor),catalogId,name,pluginQuantum_);
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
                 ExternalStripFx externalFx=nullptr,
                 float* monitorL=nullptr,float* monitorR=nullptr) noexcept {
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
        if(monitorEnabled_ && (!monitorL||!monitorR))return false;
        return block_->process(physical,inputs,left,right,frames,channelFx,levels,
                               &controls_,monitorEnabled_?&monitorGains_:nullptr,
                               monitorL,monitorR);
    }

private:
    std::unique_ptr<PreparedMixerBlock> block_;
    RealtimeStripControls controls_ {};
    FxBank processors_ {};
    std::array<std::uint8_t,fxSlots> counts_ {};
    int activeInserts_=0;
    int pluginQuantum_=512;
    int mainOutputLeft_=0,mainOutputRight_=1;
    int monitorLeft_=-1,monitorRight_=-1;
    bool monitorEnabled_=false;
    std::array<float,fxSlots> monitorGains_{};
};
} // namespace armix::mixer
