// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GrandStageDsp.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>

// Native mixer INSERT, not an independent player or ASIO callback.
// Control -> audio via SPSC, applied ACK only after callback completion;
// published .smix snapshots use the last accepted control-side state.
namespace armix::builtin_fx {
class GrandStageProcessor final : public juce::AudioProcessor,
    public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity=
        "armix.builtin.grandstage.v1";
    struct Completion {
        std::uint64_t requestId=0;
        armix::grandstage::Settings settings{};
        bool applied=false;
    };
    GrandStageProcessor():juce::AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}

    const juce::String getName() const override {return "Harmonice GrandStage";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 2.6;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Concert Hall";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return false;}
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }

    void prepareToPlay(double rate,int) override {
        ready_=dsp_.prepare(rate);
    }
    void releaseResources() override {ready_=false;}
    bool isReady() const noexcept {return ready_;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals guard;
        if(!ready_ || pcm.getNumChannels()<2 || pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        dsp_.process(pcm.getWritePointer(0),pcm.getWritePointer(1),
                     pcm.getNumSamples());
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}

    void consumeRealtimeEdits() noexcept override {
        Edit next;
        if(!ready_ || !complete_.canPush() || !queue_.pop(next))return;
        const bool ok=dsp_.setSettings(next.settings);
        complete_.push({next.requestId,next.settings,ok});
    }
    bool queueSettings(const armix::grandstage::Settings& s,
                       std::uint64_t requestId) noexcept {
        return ready_ && requestId && armix::grandstage::valid(s)
            && queue_.push(Edit{requestId,s});
    }
    bool pollCompletion(Completion& result) noexcept {
        if(!complete_.pop(result))return false;
        if(result.applied)accepted_=result.settings;
        return true;
    }
    armix::grandstage::Settings acceptedSettings() const noexcept {
        return accepted_;
    }
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return queue_.empty() && complete_.empty();
    }

    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MemoryOutputStream out(dest,false);
        out.writeInt(0x47525354); // GRST; never mistaken for SONK
        out.writeInt(1);
        out.writeInt(accepted_.venue);
        out.writeFloat(accepted_.immersion);
        out.writeFloat(accepted_.space);
        out.writeFloat(accepted_.width);
        out.writeFloat(accepted_.outputTrim);
    }
    bool restoreUnpublishedState(const void* data,int size) {
        if(published_ || !data || size!=28 ||
           !queue_.empty() || !complete_.empty())return false;
        juce::MemoryInputStream in(data,static_cast<std::size_t>(size),false);
        if(in.readInt()!=0x47525354 || in.readInt()!=1)return false;
        armix::grandstage::Settings next{};
        next.venue=in.readInt();
        next.immersion=in.readFloat();
        next.space=in.readFloat();
        next.width=in.readFloat();
        next.outputTrim=in.readFloat();
        if(!in.isExhausted() || !armix::grandstage::valid(next))return false;
        if(ready_ && !dsp_.setSettings(next))return false;
        accepted_=next;
        return true;
    }
    void setStateInformation(const void* data,int size) override {
        (void)restoreUnpublishedState(data,size);
    }

private:
    struct Edit {
        std::uint64_t requestId=0;
        armix::grandstage::Settings settings{};
    };
    armix::grandstage::GrandStageDsp dsp_{};
    FxSpsc<Edit,32> queue_{};
    FxSpsc<Completion,32> complete_{};
    armix::grandstage::Settings accepted_{};
    bool ready_=false;
    bool published_=false;
};
} // namespace armix::builtin_fx
