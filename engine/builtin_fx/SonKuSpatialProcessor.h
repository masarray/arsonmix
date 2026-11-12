// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "sonku/SonKuSpatialCore.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>

// Built-in insert only; JUCE VST3 packaging is a separate adapter/quality gate.
// Existing FX instance owns bypass, MIDI scratch, ChannelId, PDC and retirement.
namespace armix::builtin_fx {
class SonKuSpatialProcessor final : public juce::AudioProcessor,
    public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.8d-sonku.v1";
    struct Completion {std::uint64_t requestId=0;armix::sonku::Settings settings{};bool applied=false;};
    SonKuSpatialProcessor()
        : juce::AudioProcessor(BusesProperties()
            .withInput("Input",juce::AudioChannelSet::stereo(),true)
            .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}
    const juce::String getName() const override {return "8D-SonKu";}
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
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double rate,int) override {
        ready_=core_.prepare(rate);
        if(ready_)ready_=core_.setSettings(accepted_);
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        core_.process(pcm.getWritePointer(0),pcm.getWritePointer(1),pcm.getNumSamples());
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {
        Edit next;
        if(!ready_||!complete_.canPush()||!queue_.pop(next))return;
        const bool ok=core_.setSettings(next.settings);
        complete_.push({next.requestId,next.settings,ok});
    }
    // Control thread: returns QUEUED, not accepted/applied. Host native ACK
    // is permitted only after corresponding completion/readback.
    bool queueSettings(const armix::sonku::Settings& next,std::uint64_t requestId) noexcept {
        return ready_&&requestId&&armix::sonku::valid(next)
            &&queue_.push(Edit{requestId,next});
    }
    bool pollCompletion(Completion& done) noexcept {
        if(!complete_.pop(done))return false;
        if(done.applied)accepted_=done.settings;
        return true;
    }
    armix::sonku::Settings acceptedSettings() const noexcept {return accepted_;}
    bool isReady() const noexcept {return ready_;}
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return queue_.empty()&&complete_.empty();
    }
    // CONTROL only; state is accepted, not speculative or PCM-owned.
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MemoryOutputStream out(dest,false);
        out.writeInt(0x534F4E4B); // SONK
        out.writeInt(1);
        out.writeFloat(accepted_.startAzimuthDegrees);
        out.writeFloat(accepted_.revolutionSeconds);
        out.writeFloat(accepted_.stereoSpreadDegrees);
        out.writeFloat(accepted_.spatialMix);
        out.writeFloat(accepted_.outputTrim);
        out.writeByte(accepted_.moving?1:0);
        out.writeByte(accepted_.clockwise?1:0);
    }
    bool restoreUnpublishedState(const void* data,int size) {
        if(published_||!data||size!=30||!queue_.empty()||!complete_.empty())return false;
        juce::MemoryInputStream in(data,static_cast<std::size_t>(size),false);
        if(in.readInt()!=0x534F4E4B||in.readInt()!=1)return false;
        armix::sonku::Settings next{};
        next.startAzimuthDegrees=in.readFloat();
        next.revolutionSeconds=in.readFloat();
        next.stereoSpreadDegrees=in.readFloat();
        next.spatialMix=in.readFloat();
        next.outputTrim=in.readFloat();
        const int moving=in.readByte(),clockwise=in.readByte();
        if(moving>1||clockwise>1||!in.isExhausted())return false;
        next.moving=moving!=0;
        next.clockwise=clockwise!=0;
        if(!armix::sonku::valid(next))return false;
        if(ready_&&!core_.setSettings(next))return false;
        accepted_=next;
        return true;
    }
    void setStateInformation(const void* data,int size) override {
        (void)restoreUnpublishedState(data,size);
    }
private:
    struct Edit {std::uint64_t requestId=0;armix::sonku::Settings settings{};};
    armix::sonku::SonKuSpatialCore core_{};
    FxSpsc<Edit,32> queue_{};
    FxSpsc<Completion,32> complete_{};
    armix::sonku::Settings accepted_{};
    bool ready_=false;
    bool published_=false;
};
} // namespace armix::builtin_fx
