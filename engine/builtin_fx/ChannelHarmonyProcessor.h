// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "HarmonyDsp.h"
#include "ChannelEqProcessor.h"  // Existing bounded SPSC mailbox.
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>

namespace armix::builtin_fx {
class ChannelHarmonyProcessor final : public juce::AudioProcessor,
                                      public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.arson-harmony.v1";
    struct Completion {std::uint64_t requestId=0;bool applied=false;};
    ChannelHarmonyProcessor() : juce::AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}
    const juce::String getName() const override {return "ArSon Harmony";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 0.12;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Current";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return false;} // approved WebView2 editor
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo() &&
               b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double rate,int) override {
        ready_=dsp_.prepare(rate)&&dsp_.setSettings(accepted_);
        if(ready_) {sampleRate_=rate;setLatencySamples(dsp_.latencySamples());}
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& samples,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||samples.getNumChannels()<2||samples.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        dsp_.process(samples.getWritePointer(0),samples.getWritePointer(1),
                     samples.getNumSamples());
        observedNote_.store(dsp_.observedMidiNote(),std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {
        Edit next;
        if(!ready_||!completion_.canPush()||!queued_.pop(next))return;
        const bool applied=dsp_.setSettings(next.settings);
        completion_.push({next.requestId,applied});
    }
    bool queueSettings(const armix::harmony::Settings& settings,
                       std::uint64_t requestId) noexcept {
        return ready_&&requestId&&armix::harmony::valid(settings)&&
               queued_.push({requestId,settings});
    }
    bool pollCompletion(Completion& completion) noexcept {
        if(!completion_.pop(completion))return false;
        if(completion.applied && completion.requestId==pendingId_) {
            accepted_=pendingSettings_;pendingId_=0;
        }
        return true;
    }
    bool queueAccepted(const armix::harmony::Settings& settings,
                       std::uint64_t requestId) noexcept {
        if(pendingId_||!queueSettings(settings,requestId))return false;
        pendingId_=requestId;pendingSettings_=settings;return true;
    }
    armix::harmony::Settings acceptedSettings() const noexcept {return accepted_;}
    int observedNote() const noexcept {return observedNote_.load(std::memory_order_relaxed);}
    double acceptedSampleRate() const noexcept {return sampleRate_;}
    bool isReady() const noexcept{return ready_;}
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return queued_.empty()&&completion_.empty()&&!pendingId_;
    }
    void getStateInformation(juce::MemoryBlock& out) override {
        juce::MemoryOutputStream stream(out,false);
        stream.writeInt(0x41484d31); // AHM1
        stream.writeInt(1);
        stream.writeInt(accepted_.key);
        stream.writeInt(accepted_.scale);
        stream.writeInt(accepted_.voices);
        stream.writeBool(accepted_.correction);
        stream.writeFloat(accepted_.wet);
        stream.writeFloat(accepted_.space);
        stream.writeFloat(accepted_.color);
        stream.writeFloat(accepted_.gainDb);
    }
    bool restoreUnpublishedState(const void* bytes,int len) {
        if(published_||!bytes||len<=0||len>128)return false;
        juce::MemoryInputStream stream(bytes,static_cast<std::size_t>(len),false);
        if(stream.readInt()!=0x41484d31||stream.readInt()!=1)return false;
        armix::harmony::Settings incoming{};
        incoming.key=stream.readInt();incoming.scale=stream.readInt();
        incoming.voices=stream.readInt();incoming.correction=stream.readBool();
        incoming.wet=stream.readFloat();incoming.space=stream.readFloat();
        incoming.color=stream.readFloat();incoming.gainDb=stream.readFloat();
        if(stream.getPosition()!=stream.getTotalLength()||
           !armix::harmony::valid(incoming))return false;
        if(ready_&&!dsp_.setSettings(incoming))return false;
        accepted_=incoming;return true;
    }
    void setStateInformation(const void* data,int size) override {
        (void)restoreUnpublishedState(data,size);
    }
private:
    struct Edit{std::uint64_t requestId=0;armix::harmony::Settings settings;};
    armix::harmony::Engine dsp_{};
    EqSpsc<Edit,32> queued_{};
    EqSpsc<Completion,32> completion_{};
    armix::harmony::Settings accepted_{},pendingSettings_{};
    std::uint64_t pendingId_=0; // control-only, no callback reads
    std::atomic<int> observedNote_{-1};
    double sampleRate_=0.;bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
