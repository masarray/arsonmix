// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelEq.h"
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
template<class T,std::size_t Capacity> class EqSpsc final {
public:
    bool push(const T& value) noexcept {
        const auto w=write_.load(std::memory_order_relaxed);
        if(w-read_.load(std::memory_order_acquire)>=Capacity)return false;
        data_[w%Capacity]=value;
        write_.store(w+1,std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto r=read_.load(std::memory_order_relaxed);
        if(r==write_.load(std::memory_order_acquire))return false;
        value=data_[r%Capacity];
        read_.store(r+1,std::memory_order_release);
        return true;
    }
    bool canPush() const noexcept {
        return write_.load(std::memory_order_relaxed)-
            read_.load(std::memory_order_acquire)<Capacity;
    }
private:
    alignas(64) std::atomic<std::uint32_t> write_{0};
    alignas(64) std::atomic<std::uint32_t> read_{0};
    std::array<T,Capacity> data_{};
};

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
    void setStateInformation(const void* data,int size) override {
        if(!data||size!=8+static_cast<int>(ChannelEq::bandCount)*14)return;
        juce::MemoryInputStream input(data,static_cast<std::size_t>(size),false);
        if(input.readInt()!=0x41504551||input.readInt()!=1)return;
        std::array<Band,ChannelEq::bandCount> next{};
        for(auto& b:next){
            b.enabled=input.readByte()!=0;
            b.frequencyHz=input.readFloat();b.gainDb=input.readFloat();
            b.q=input.readFloat();
            const auto slope=input.readByte();
            if(slope!=12&&slope!=24)return;
            b.slope=slope==24?Slope::db24:Slope::db12;
        }
        const double rate=rate_>0.?rate_:48000.;
        for(std::size_t i=0;i<next.size();++i){
            ChannelEq::PreparedBandEdit validated;
            if(!ChannelEq::prepareBandEdit(i,next[i],rate,validated))return;
        }
        if(!ready_){accepted_=next;return;}
        // A live Show recall must use the same bounded audio-owned handoff.
        // Reject when it cannot be fully queued instead of half-applying.
        // Current Show recall creates/replaces an unpublished processor.
        // Never mutate the published EQ DSP from the GUI/control thread.
    }

private:
    struct Edit {std::uint64_t requestId=0;ChannelEq::PreparedBandEdit prepared{};};
    ChannelEq eq_{};
    EqSpsc<Edit,32> queued_{};
    EqSpsc<Completion,32> completion_{};
    std::array<Band,ChannelEq::bandCount> accepted_{};
    double rate_=0.; // control-owned immutable after prepare
    bool ready_=false; // prepared before graph publication
};
} // namespace armix::builtin_fx
