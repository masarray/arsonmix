// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelEq.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>

// One ordinary stereo FX processor in the EXISTING ordered ChannelId rack.
// The established FxInstance owns this processor, per-slot identity, bypass,
// graph latency and retirement. No second callback and no fixed pre-VST bank.
// Single message-thread producer; single audio callback consumer.
namespace armix::builtin_fx {
template<class T,std::size_t Capacity> using EqSpsc=FxSpsc<T,Capacity>;

class ChannelEqProcessor final : public juce::AudioProcessor,
    public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.channel-eq.v1";
    struct Completion {
        std::uint64_t requestId=0;
        std::uint8_t bandIndex=0;
        Band band{};
        bool applied=false;
    };

    ChannelEqProcessor()
      : juce::AudioProcessor(BusesProperties()
          .withInput("Input",juce::AudioChannelSet::stereo(),true)
          .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {
        // All 8 defaults are owned by the same DSP definition as the graph.
        for(std::size_t i=0;i<ChannelEq::bandCount;++i) {
            accepted_[i].frequencyHz=channelEqSlots[i].defaultHz;
            accepted_[i].q=channelEqSlots[i].kind==FilterKind::bell?1.f:0.70710678f;
        }
    }
    const juce::String getName() const override {return "Parametric EQ";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 0.;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Current";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return false;} // existing WebView2 modeless editor
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            &&layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double sampleRate,int) override {
        // Host prepares this before publishing the immutable native graph.
        if(!eq_.prepare(sampleRate)){ready_=false;return;}
        rate_=sampleRate;
        bool restored=true;
        for(std::size_t i=0;i<accepted_.size();++i)
            { auto setting=accepted_[i];
                if(!setting.enabled)setting.frequencyHz=std::min(setting.frequencyHz,eq_.maxFrequencyHz());
                restored=eq_.setBand(i,setting)&&restored;
                accepted_[i]=setting; }
        ready_=restored;
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        eq_.processStereo(pcm.getWritePointer(0),pcm.getWritePointer(1),
                          static_cast<std::size_t>(pcm.getNumSamples()));
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}

    // AUDIO thread: bounded one control packet per block. The host calls
    // this also when fully bypassed; there is no second callback or worker.
    void consumeRealtimeEdits() noexcept override {
        Edit next;
        if(!ready_||!completion_.canPush()||!queued_.pop(next))return;
        const bool ok=eq_.applyPreparedBand(next.prepared);
        completion_.push({next.requestId,
            static_cast<std::uint8_t>(next.prepared.index),next.prepared.band,ok});
    }

    // Control-thread only. Compiles coefficients off realtime, then publishes
    // one bounded block-boundary command. Queued is NOT DSP-applied.
    bool queueBand(std::size_t index,const Band& band,std::uint64_t id) noexcept {
        if(!ready_||!id||index>=ChannelEq::bandCount)return false;
        Edit e;e.requestId=id;
        if(!ChannelEq::prepareBandEdit(index,band,rate_,e.prepared))
            return false;
        return queued_.push(e);
    }
    // Control-thread only, after a callback completion: accepted_ is never
    // accessed by processBlock, so Show/GUI reads cannot race DSP histories.
    bool pollCompletion(Completion& result) noexcept {
        if(!completion_.pop(result))return false;
        if(result.applied&&result.bandIndex<accepted_.size())
            accepted_[result.bandIndex]=result.band;
        return true;
    }
    std::array<Band,ChannelEq::bandCount> acceptedBands() const noexcept {
        return accepted_; // CONTROL thread ONLY
    }
    double acceptedSampleRate() const noexcept {return rate_;}
    bool isReady() const noexcept {return ready_;}
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        // CONTROL only; the caller owns FxInstance's non-blocking capture
        // gate, so it cannot race the consumer inside processBlock.
        return queued_.empty()&&completion_.empty();
    }

    // The native Show's vendor-state lease already serializes state on the
    // control thread. Built-in DSP uses that same per-insert lease and schema,
    // not a second fixed channel EQ snapshot.
    void getStateInformation(juce::MemoryBlock& destination) override {
        juce::MemoryOutputStream output(destination,false);
        output.writeInt(0x41504551); // "APEQ"
        output.writeInt(1);
        for(const auto& b:accepted_) {
            output.writeByte(b.enabled?1:0);
            output.writeFloat(b.frequencyHz);
            output.writeFloat(b.gainDb);
            output.writeFloat(b.q);
            output.writeByte(static_cast<char>(b.slope));
        }
    }
    // Only a NEW, unpublished processor is permitted to receive a binary
    // Show restore. Mutating a live published DSP would race its callback.
    // The Show builder may call this before OR after prepareToPlay; both
    // restore the complete bank, and the next audio block starts settled.
    // For future ChannelId Show preflight, use the bool-returning method
    // rather than JUCE's legacy void setter so errors are not hidden.
    bool restoreUnpublishedState(const void* data,int size) {
        if(published_||!data||
           size!=8+static_cast<int>(ChannelEq::bandCount)*14 ||
           !queued_.empty()||!completion_.empty())return false;
        juce::MemoryInputStream input(data,static_cast<std::size_t>(size),false);
        if(input.readInt()!=0x41504551||input.readInt()!=1)return false;
        std::array<Band,ChannelEq::bandCount> next{};
        for(auto& b:next) {
            const int enabled=input.readByte();
            if(enabled!=0&&enabled!=1)return false;
            b.enabled=enabled!=0;
            b.frequencyHz=input.readFloat();
            b.gainDb=input.readFloat();
            b.q=input.readFloat();
            const int slope=input.readByte();
            if(slope!=12&&slope!=24)return false;
            b.slope=slope==24?Slope::db24:Slope::db12;
        }
        if(!input.isExhausted())return false;
        const double validationRate=ready_?rate_:48000.;
        const float maxHz=static_cast<float>(
            std::min(20000.0,validationRate*0.45));
        for(std::size_t i=0;i<next.size();++i) {
            // The DSP already clamps DISABLED bands on a device-rate switch.
            // Saved frequency must first be sane; then only an inactive band
            // may be clamped below the new Nyquist guard. Enabled invalid
            // filters are rejected, never silently retuned.
            if(!std::isfinite(next[i].frequencyHz)||
                next[i].frequencyHz<20.f||next[i].frequencyHz>20000.f)
                return false;
            if(!next[i].enabled)
                next[i].frequencyHz=std::min(next[i].frequencyHz,maxHz);
            ChannelEq::PreparedBandEdit prepared{};
            if(!ChannelEq::prepareBandEdit(i,next[i],validationRate,prepared))
                return false; // all-or-nothing preflight
        }
        if(ready_) {
            for(std::size_t i=0;i<next.size();++i)
                if(!eq_.setBand(i,next[i]))return false;
            if(!eq_.prepare(rate_))return false; // start without stale ramp
        }
        accepted_=next;
        return true;
    }
    void setStateInformation(const void* data,int size) override {
        (void)restoreUnpublishedState(data,size);
    }

private:
    struct Edit {std::uint64_t requestId=0;ChannelEq::PreparedBandEdit prepared{};};
    ChannelEq eq_{};
    EqSpsc<Edit,32> queued_{};
    EqSpsc<Completion,32> completion_{};
    std::array<Band,ChannelEq::bandCount> accepted_{};
    double rate_=0.; // control-owned immutable after prepare
    bool ready_=false; // prepared before graph publication
    bool published_=false; // CONTROL-only, sealed by FxInstance off callback
};
} // namespace armix::builtin_fx
