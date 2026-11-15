// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ImerSipDsp.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>
#include <atomic>

// Native mixer INSERT, not an independent player or ASIO callback.
// Control -> audio via SPSC, applied ACK only after callback completion;
// published .smix snapshots use the last accepted control-side state.
namespace armix::builtin_fx {
class ImerSipProcessor final : public juce::AudioProcessor,
    public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity=
        "armix.builtin.imersip.v1";
    struct Completion {
        std::uint64_t requestId=0;
        armix::imersip::Settings settings{};
        bool applied=false;
    };
    ImerSipProcessor():juce::AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {}

    const juce::String getName() const override {return "ImerSip";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 2.6;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Concert Hall";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }

    void prepareToPlay(double rate,int) override {
        ready_=dsp_.prepare(rate);
        // A/B compare is a transient monitor action, not session state.
        compareDry_.store(false,std::memory_order_relaxed);
    }
    void releaseResources() override {ready_=false;}
    bool isReady() const noexcept {return ready_;}
    // Authoritative native DSP capability, never inferred from a browser dial.
    bool measuredStereoStageEnabled() const noexcept {
        return ready_ && dsp_.measuredStereoStageEnabled();
    }
    // Measured LIVE PCM, not just parameter ACK. Unchanged counts imply
    // the INSERT is bypassed/not being called by the actual audio graph.
    std::uint64_t processedFrames() const noexcept {
        return processedFrames_.load(std::memory_order_relaxed);
    }
    float liveInputEnergy() const noexcept {
        return liveInputEnergy_.load(std::memory_order_relaxed);
    }
    float liveDifferenceEnergy() const noexcept {
        return liveDifferenceEnergy_.load(std::memory_order_relaxed);
    }
    float liveStereoRetention() const noexcept {
        return liveStereoRetention_.load(std::memory_order_relaxed);
    }
    // UI thread only writes the atomic; PCM callback owns its sample ramp.
    // Save/restore NEVER serializes this transient monitor comparison.
    void setCompareDry(bool compare) noexcept {
        compareDry_.store(compare,std::memory_order_release);
    }
    bool comparingOriginal() const noexcept {
        return compareDry_.load(std::memory_order_acquire);
    }
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals guard;
        if(!ready_ || pcm.getNumChannels()<2 || pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        dsp_.setCompareDry(compareDry_.load(std::memory_order_acquire));
        dsp_.process(pcm.getWritePointer(0),pcm.getWritePointer(1),
                     pcm.getNumSamples());
        liveInputEnergy_.store(dsp_.lastInputEnergy(),std::memory_order_relaxed);
        liveDifferenceEnergy_.store(dsp_.lastDifferenceEnergy(),
                                    std::memory_order_relaxed);
        liveStereoRetention_.store(dsp_.lastStereoRetention(),
                                   std::memory_order_relaxed);
        processedFrames_.fetch_add(static_cast<std::uint64_t>(
            pcm.getNumSamples()),std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}

    void consumeRealtimeEdits() noexcept override {
        Edit next;
        if(!ready_ || !complete_.canPush() || !queue_.pop(next))return;
        const bool ok=dsp_.setSettings(next.settings);
        complete_.push({next.requestId,next.settings,ok});
    }
    // Unique across closing/reopening a JUCE editor on this FX instance.
    // CONTROL-only; never called by the realtime PCM callback.
    std::uint64_t nextEditorRequestId() noexcept {
        if(++lastIssuedRequest_==0)++lastIssuedRequest_;
        return lastIssuedRequest_;
    }
    bool queueSettings(const armix::imersip::Settings& s,
                       std::uint64_t requestId) noexcept {
        if(!ready_ || !requestId || !armix::imersip::valid(s) ||
           !queue_.push(Edit{requestId,s}))return false;
        if(requestId>lastIssuedRequest_)lastIssuedRequest_=requestId;
        return true;
    }
    bool pollCompletion(Completion& result) noexcept {
        if(!complete_.pop(result))return false;
        lastCompletedRequest_=result.requestId;
        lastCompletedApplied_=result.applied;
        if(result.applied)accepted_=result.settings;
        return true;
    }
    // Both session Save and the floating editor run on CONTROL. Save may
    // consume the very ACK that the editor is awaiting; expose a latched
    // completion record so it never gets stuck on "APPLYING" after Save.
    std::uint64_t lastCompletedRequestId() const noexcept {
        return lastCompletedRequest_;
    }
    bool lastCompletionApplied() const noexcept {return lastCompletedApplied_;}
    armix::imersip::Settings acceptedSettings() const noexcept {
        return accepted_;
    }
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return queue_.empty() && complete_.empty();
    }

    void getStateInformation(juce::MemoryBlock& dest) override {
        // Native mixer save runs on CONTROL. A popup closed immediately
        // after audio ACK must not serialize the previous settings simply
        // because its editor timer no longer polls the completion FIFO.
        Completion drained{};
        while(pollCompletion(drained)){}
        juce::MemoryOutputStream out(dest,false);
        out.writeInt(0x494D5253); // IMRS; isolated from SonKu/old research state
        out.writeInt(3); // IMRS v3: separately persisted stereo and late tail
        out.writeInt(accepted_.venue);
        out.writeFloat(accepted_.immersion);
        out.writeFloat(accepted_.space);
        out.writeFloat(accepted_.width);
        out.writeFloat(accepted_.bassImpact);
        out.writeFloat(accepted_.outputTrim);
        out.writeFloat(accepted_.detail);
        out.writeFloat(accepted_.stereoLock);
        out.writeFloat(accepted_.lateRoom);
    }
    bool restoreUnpublishedState(const void* data,int size) {
        if(published_ || !data || (size!=32 && size!=36 && size!=44) ||
           !queue_.empty() || !complete_.empty())return false;
        juce::MemoryInputStream in(data,static_cast<std::size_t>(size),false);
        if(in.readInt()!=0x494D5253)return false;
        const int version=in.readInt();
        if(version!=(size==32?1:size==36?2:3))return false;
        armix::imersip::Settings next{};
        next.venue=in.readInt();
        next.immersion=in.readFloat();
        next.space=in.readFloat();
        next.width=in.readFloat();
        next.bassImpact=in.readFloat();
        next.outputTrim=in.readFloat();
        // Exactly preserved v1 record; Detail defaults OFF on migration
        // rather than silently altering old tonal preferences.
        next.detail=version>=2?in.readFloat():0.f;
        next.stereoLock=version>=3?in.readFloat():1.f;
        next.lateRoom=version>=3?in.readFloat():.45f;
        if(!in.isExhausted() || !armix::imersip::valid(next))return false;
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
        armix::imersip::Settings settings{};
    };
    armix::imersip::ImerSipDsp dsp_{};
    FxSpsc<Edit,32> queue_{};
    FxSpsc<Completion,32> complete_{};
    armix::imersip::Settings accepted_{};
    bool ready_=false;
    bool published_=false;
    std::uint64_t lastCompletedRequest_=0,lastIssuedRequest_=0;
    bool lastCompletedApplied_=false;
    std::atomic<std::uint64_t> processedFrames_{0};
    std::atomic<float> liveInputEnergy_{0.f},liveDifferenceEnergy_{0.f};
    std::atomic<float> liveStereoRetention_{-1.f};
    std::atomic<bool> compareDry_{false};
};
} // namespace armix::builtin_fx

// JUCE editor implementation shares this exact processor, never a second DSP.
#include "ImerSipEditor.h"
