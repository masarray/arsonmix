// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PreparedMixerBlock.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

// P1H: One immutable prepared audio graph owns its OWN native processors.
// Neither the original CH1 graph nor another snapshot shares a processor
// instance; old/new graph processors cannot run concurrently on one object.
// Construction/preparation and destruction/releaseResources happen OFF RT.
// Only process() runs in the audio callback. No per-channel threads or heaps.
namespace armix::mixer {
class LiveChannelFxGraph final {
public:
    static constexpr int channels=stripCount;
    using Processor=juce::AudioProcessor;
    using FxBank=std::array<std::unique_ptr<Processor>,channels>;

    explicit LiveChannelFxGraph(const PreparedRenderGraph& plan,int frames,int pluginQuantum=512)
        : block_(std::make_unique<PreparedMixerBlock>(plan,frames)),
          pluginQuantum_(std::clamp(pluginQuantum,32,PreparedMixerBlock::hardFrameLimit)) {
        // P1H callers without live edits retain original compiled faders.
        for(int i=0;i<channels;++i)
            controls_.setPreparedCoefficients(i,plan.leftGain[i],plan.rightGain[i]);
    }

    LiveChannelFxGraph(const LiveChannelFxGraph&)=delete;
    LiveChannelFxGraph& operator=(const LiveChannelFxGraph&)=delete;
    ~LiveChannelFxGraph() noexcept {
        // Publisher retires graphs on the MESSAGE thread only, after every
        // reader lease has ended. Do not destruct/release third-party VST in RT.
        for(auto& processor:processors_)if(processor)processor->releaseResources();
    }

    int capacity() const noexcept{return block_?block_->capacity():0;}
    int maxFramesPerPlugin() const noexcept {return pluginQuantum_;}
    int activeInserts() const noexcept{return activeInserts_;}
    bool setStripControl(int channel,float gain,float pan,
                         bool mono,bool mute,bool solo) noexcept {
        return controls_.set(channel,gain,pan,mono,mute,solo);
    }
    const juce::String& insertName(int strip) const noexcept {
        static const juce::String empty;
        return strip>=0&&strip<channels?names_[strip]:empty;
    }

    // CONTROL only. Ownership of already-negotiated, prepared processor is
    // transferred exactly once. Slot may never be replaced after publication.
    bool install(int strip,std::unique_ptr<Processor> processor,
                 const juce::String& name) {
        if(strip<0 || strip>=channels || !processor || processors_[strip])
            return false;
        midi_[strip].ensureSize(1024); // prevent normal MIDI allocations
        names_[strip]=name;
        processors_[strip]=std::move(processor);
        ++activeInserts_;
        return true;
    }

    // The exact P1D callback: physical input -> independent per-channel VST3
    // -> PRE send -> fader/pan -> POST send -> AUX BUS -> MAIN. The native FX
    // sees real samples and never changes the original CH1 plugin objects.
    bool process(const float* const* physical,int inputs,
                 float* left,float* right,int frames,
                 RenderLevels* levels=nullptr) noexcept {
        auto channelFx=[this](int strip,float* l,float* r,int samples) noexcept {
            if(strip<0||strip>=channels || !processors_[strip])return;
            float* planar[2]={l,r};
            juce::AudioBuffer<float> buffer(planar,2,samples);
            auto& midi=midi_[strip];
            midi.clear();
            processors_[strip]->processBlock(buffer,midi);
        };
        return block_->process(physical,inputs,left,right,frames,channelFx,levels,&controls_);
    }

private:
    std::unique_ptr<PreparedMixerBlock> block_;
    RealtimeStripControls controls_ {};
    FxBank processors_ {};
    std::array<juce::MidiBuffer,channels> midi_ {};
    std::array<juce::String,channels> names_ {};
    int activeInserts_=0;
    int pluginQuantum_=512;
};
} // namespace armix::mixer
