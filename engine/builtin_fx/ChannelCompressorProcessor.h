// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelCompressor.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cmath>
#include <cstdint>

// One ordered ChannelId FX insert sharing VST3/EQ bypass, graph and Show
// lifecycle. No side thread, extra callback, FFT, lookahead or audio buffer.
namespace armix::builtin_fx {
class ChannelCompressorProcessor final : public juce::AudioProcessor,
    public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.channel-compressor.v1";
    struct Completion {
        std::uint64_t requestId=0;
        CompressorSettings settings{};
        bool applied=false;
    };
    ChannelCompressorProcessor()
      : juce::AudioProcessor(BusesProperties()
          .withInput("Input",juce::AudioChannelSet::stereo(),true)
          .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {
        setLatencySamples(ChannelCompressor::addedLatencySamples);
    }
    const juce::String getName() const override {return "Compressor";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 0.;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Current";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return false;}
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo() &&
               layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double sampleRate,int) override {
        ready_=core_.prepare(sampleRate)&&core_.resetUnpublished(accepted_);
        if(ready_)rate_=sampleRate;
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals denormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        core_.processStereo(pcm.getWritePointer(0),pcm.getWritePointer(1),
                            static_cast<std::size_t>(pcm.getNumSamples()));
        meter_.store(core_.gainReductionDb(),std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {
        Edit edit{};
        if(!ready_||!completion_.canPush()||!queued_.pop(edit))return;
        const bool applied=core_.applyPrepared(edit.packet);
        (void)completion_.push({edit.requestId,edit.packet.settings,applied});
    }
    // Single message-thread producer. The callback only copies prepared data.
    bool queueSettings(const CompressorSettings& s,std::uint64_t id) noexcept {
        if(!ready_||!id)return false;
        Edit edit{};edit.requestId=id;
        if(!ChannelCompressor::prepareEdit(s,rate_,edit.packet))return false;
        return queued_.push(edit);
    }
    bool pollCompletion(Completion& c) noexcept {
        if(!completion_.pop(c))return false;
        if(c.applied)accepted_=c.settings;
        return true;
    }
    [[nodiscard]] CompressorSettings acceptedSettings() const noexcept {return accepted_;}
    [[nodiscard]] double acceptedSampleRate() const noexcept {return rate_;}
    [[nodiscard]] float observedGainReductionDb() const noexcept {
        return meter_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool isReady() const noexcept {return ready_;}
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return queued_.empty()&&completion_.empty();
    }
    void getStateInformation(juce::MemoryBlock& out) override {
        juce::MemoryOutputStream writer(out,false);
        writer.writeInt(0x41434d50); // ACMP
        writer.writeInt(1);
        writer.writeFloat(accepted_.thresholdDb);
        writer.writeFloat(accepted_.ratio);
        writer.writeFloat(accepted_.attackMs);
        writer.writeFloat(accepted_.releaseMs);
        writer.writeFloat(accepted_.kneeDb);
        writer.writeFloat(accepted_.makeupDb);
        writer.writeFloat(accepted_.mix);
    }
    bool restoreUnpublishedState(const void* data,int bytes) {
        if(published_||!data||bytes!=36||!queued_.empty()||
           !completion_.empty())return false;
        juce::MemoryInputStream input(data,static_cast<std::size_t>(bytes),false);
        if(input.readInt()!=0x41434d50||input.readInt()!=1)return false;
        CompressorSettings s{};
        s.thresholdDb=input.readFloat();
        s.ratio=input.readFloat();
        s.attackMs=input.readFloat();
        s.releaseMs=input.readFloat();
        s.kneeDb=input.readFloat();
        s.makeupDb=input.readFloat();
        s.mix=input.readFloat();
        if(!input.isExhausted()||!ChannelCompressor::validSettings(s))return false;
        if(ready_&&!core_.resetUnpublished(s))return false;
        accepted_=s;
        return true;
    }
    void setStateInformation(const void* data,int size) override {
        (void)restoreUnpublishedState(data,size);
    }
private:
    struct Edit {std::uint64_t requestId=0;ChannelCompressor::PreparedEdit packet{};};
    ChannelCompressor core_{};
    FxSpsc<Edit,32> queued_{};
    FxSpsc<Completion,32> completion_{};
    CompressorSettings accepted_{};
    std::atomic<float> meter_{0.f};
    double rate_=0.;
    bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
