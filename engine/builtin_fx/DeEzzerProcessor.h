// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "de_ezzer/DeEzzerInsert.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
namespace armix::builtin_fx {
// Existing FxInstance owns this processor and its single stereo PCM callback.
class DeEzzerProcessor final : public juce::AudioProcessor,public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.de-ezzer.v1";
    DeEzzerProcessor():juce::AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
        instance_(nextInstance_.fetch_add(1,std::memory_order_relaxed)) {setLatencySamples(0);}
    const juce::String getName() const override {return "de-Ezzer";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 0.;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Current";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return false;} // one approved WebView editor
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&
               layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double rate,int frames) override {
        const auto epoch=insert_.identity().epoch;
        ready_=epoch!=std::numeric_limits<std::uint64_t>::max()&&
            insert_.prepare({instance_,epoch+1},rate,2,frames);
        if(ready_)setRateAndBufferSizeDetails(rate,frames);
        fullyBypassed_=false;
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        // FxInstance calls the edit hook itself only when fully bypassed.
        if(!fullyBypassed_)consumeRealtimeEdits();
        (void)insert_.processAudio(pcm.getWritePointer(0),pcm.getWritePointer(1),pcm.getNumSamples());
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {if(ready_)insert_.consumeRealtimeEdits();}
    bool requiresContinuousBypassProcessing() const noexcept override {return true;}
    void onRealtimeBypass(bool,bool fully) noexcept override {fullyBypassed_=fully;}
    void sealForRealtime() noexcept override {insert_.seal();}
    bool stateSnapshotQuiescent() const noexcept override {return insert_.stateSnapshotQuiescent();}
    bool isReady() const noexcept {return ready_;}
    bool queue(const de_ezzer::Command& cmd) noexcept {return ready_&&insert_.submit(cmd);}
    bool pollAck(de_ezzer::Ack& ack) noexcept {return insert_.pollAck(ack);}
    void requestAnalysis() noexcept {if(ready_)insert_.requestAnalysis();}
    de_ezzer::Snapshot readback() noexcept {return insert_.readback();}
    de_ezzer::Identity identity() const noexcept {return insert_.identity();}
    void getStateInformation(juce::MemoryBlock& out) override {
        out.setSize(0);if(!stateSnapshotQuiescent())return;
        const auto bytes=insert_.acceptedState();out.append(bytes.data(),bytes.size());
    }
    bool restoreUnpublishedState(const void* data,int size) {
        return size>=0&&insert_.restoreUnpublished(data,static_cast<std::size_t>(size));
    }
    void setStateInformation(const void* data,int size) override {(void)restoreUnpublishedState(data,size);}
private:
    inline static std::atomic<std::uint64_t> nextInstance_{1};
    de_ezzer::Insert insert_;
    const std::uint64_t instance_;
    bool ready_=false,fullyBypassed_=false;
};
} // namespace armix::builtin_fx
