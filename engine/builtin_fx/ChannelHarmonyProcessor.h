// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "HarmonyDsp.h"
#include "HarmonyMusicKey.h"
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
    void prepareToPlay(double rate,int maximumExpectedSamplesPerBlock) override {
        ready_=dsp_.prepare(rate)&&guide_.prepare(rate)&&dsp_.setSettings(accepted_);
        guideSource_.store(accepted_.guideMode==1?accepted_.guideChannelId:0,
                           std::memory_order_release);
        guideAge_=0;guideObservedKey_.store(-1,std::memory_order_relaxed);
        guideObservedScale_.store(-1,std::memory_order_relaxed);
        guideConfidence_.store(0.f,std::memory_order_relaxed);
        publishEffectiveKey();
        if(ready_) {
            // Native built-ins are prepared directly (not by a JUCE plugin
            // wrapper). Without this CONTROL-side metadata, AudioProcessor's
            // getSampleRate() may remain zero. FxInstance captures that rate
            // for its fixed-duration bypass envelope when the lease is built.
            // A zero rate previously clamped to 8 kHz: an intended 8ms fade
            // became only 64 frames, i.e. 1.33ms at the real 48 kHz device.
            // No realtime mutation, graph rebuild, latency change or UI step.
            setRateAndBufferSizeDetails(rate,
                std::max(1,maximumExpectedSamplesPerBlock));
            sampleRate_=rate;
            setLatencySamples(dsp_.latencySamples());
        }
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& samples,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||samples.getNumChannels()<2||samples.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        // Missing/deleted/disconnected source must not leave a stale accepted
        // tonal estimate running indefinitely. Manual Key is the fallback.
        const int guideTimeout=static_cast<int>(sampleRate_*2.);
        const bool guideWasFresh=guideAge_<=guideTimeout;
        guideAge_=std::min(guideAge_+samples.getNumSamples(),
                            static_cast<int>(sampleRate_*3.));
        if(guideWasFresh && guideAge_>guideTimeout){
            // Disconnected ChannelId must reacquire from fresh music, rather
            // than instantly reinstating the pre-dropout detector estimate
            // when source PCM resumes. Reset exactly once at the timeout;
            // repeated missing-source callbacks do no detector housekeeping.
            guide_.reset();
            dsp_.setGuideKey(-1,-1);
            guideObservedKey_.store(-1,std::memory_order_relaxed);
            guideObservedScale_.store(-1,std::memory_order_relaxed);
            guideConfidence_.store(0.f,std::memory_order_relaxed);
        }
        dsp_.process(samples.getWritePointer(0),samples.getWritePointer(1),
                     samples.getNumSamples());
        observedNote_.store(dsp_.observedMidiNote(),std::memory_order_relaxed);
        publishEffectiveKey();
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {
        Edit next;
        if(!ready_||!completion_.canPush()||!queued_.pop(next))return;
        const auto previous=dsp_.settings();
        const auto& nextSettings=next.prepared.settings;
        const bool sourceChanged=previous.guideMode!=nextSettings.guideMode||
            previous.guideChannelId!=nextSettings.guideChannelId;
        const bool applied=dsp_.applyPreparedSettings(next.prepared);
        if(applied){
            if(sourceChanged){
                guide_.reset();guideAge_=0;
                guideObservedKey_.store(-1,std::memory_order_relaxed);
                guideObservedScale_.store(-1,std::memory_order_relaxed);
                guideConfidence_.store(0.f,std::memory_order_relaxed);
            }
            guideSource_.store(nextSettings.guideMode==1?
                               nextSettings.guideChannelId:0,
                               std::memory_order_release);
            publishEffectiveKey();
        }
        completion_.push({next.requestId,applied});
    }
    bool queueSettings(const armix::harmony::Settings& settings,
                       std::uint64_t requestId) noexcept {
        if(!ready_||!requestId)return false;
        armix::harmony::Engine::PreparedSettings prepared;
        if(!armix::harmony::Engine::prepareSettings(settings,prepared))return false;
        return queued_.push({requestId,prepared});
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
    // One atomic pair (scale*12+key) published by the single audio owner.
    // Main/JS must not read non-atomic DSP state or independently guess the
    // effective manual/music fallback. Values are always 0..23 when ready.
    int effectiveKeyCode() const noexcept {
        return effectiveKeyCode_.load(std::memory_order_relaxed);
    }
    std::uint64_t musicSourceChannel() const noexcept {
        return guideSource_.load(std::memory_order_acquire);
    }
    // Native ChannelId audio callback ONLY, called at another channel's FX
    // seam. Never copies PCM, mixes audio into vocal, locks or starts a worker.
    void observeMusicTrack(const float* left,const float* right,int frames) noexcept {
        if(!ready_||musicSourceChannel()==0||frames<=0)return;
        guideAge_=0;
        guide_.process(left,right,frames);
        const auto estimate=guide_.estimate();
        dsp_.setGuideKey(estimate.valid()?estimate.key:-1,
                         estimate.valid()?estimate.scale:-1);
        guideObservedKey_.store(estimate.valid()?estimate.key:-1,std::memory_order_relaxed);
        guideObservedScale_.store(estimate.valid()?estimate.scale:-1,std::memory_order_relaxed);
        guideConfidence_.store(estimate.confidence,std::memory_order_relaxed);
        publishEffectiveKey();
    }
    int observedMusicKey() const noexcept {
        return guideObservedKey_.load(std::memory_order_relaxed);
    }
    int observedMusicScale() const noexcept {
        return guideObservedScale_.load(std::memory_order_relaxed);
    }
    float observedMusicConfidence() const noexcept {
        return guideConfidence_.load(std::memory_order_relaxed);
    }
    double acceptedSampleRate() const noexcept {return sampleRate_;}
    bool isReady() const noexcept{return ready_;}
    bool requiresContinuousBypassProcessing() const noexcept override {return true;}
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return queued_.empty()&&completion_.empty()&&!pendingId_;
    }
    void getStateInformation(juce::MemoryBlock& out) override {
        juce::MemoryOutputStream stream(out,false);
        stream.writeInt(0x41484d31); // AHM1
        stream.writeInt(2);
        stream.writeInt(accepted_.key);
        stream.writeInt(accepted_.scale);
        stream.writeInt(accepted_.voices);
        stream.writeBool(accepted_.correction);
        stream.writeFloat(accepted_.wet);
        stream.writeFloat(accepted_.space);
        stream.writeFloat(accepted_.color);
        stream.writeFloat(accepted_.gainDb);
        stream.writeInt(accepted_.guideMode);
        stream.writeInt64(static_cast<juce::int64>(accepted_.guideChannelId));
    }
    bool restoreUnpublishedState(const void* bytes,int len) {
        if(published_||!bytes||len<=0||len>128)return false;
        juce::MemoryInputStream stream(bytes,static_cast<std::size_t>(len),false);
        if(stream.readInt()!=0x41484d31)return false;
        const int version=stream.readInt();
        if(version!=1&&version!=2)return false;
        armix::harmony::Settings incoming{};
        incoming.key=stream.readInt();incoming.scale=stream.readInt();
        incoming.voices=stream.readInt();incoming.correction=stream.readBool();
        incoming.wet=stream.readFloat();incoming.space=stream.readFloat();
        incoming.color=stream.readFloat();incoming.gainDb=stream.readFloat();
        if(version==2){
            incoming.guideMode=stream.readInt();
            const auto guide=stream.readInt64();
            if(guide<0)return false;
            incoming.guideChannelId=static_cast<std::uint64_t>(guide);
        }
        if(stream.getPosition()!=stream.getTotalLength()||
           !armix::harmony::valid(incoming))return false;
        if(ready_&&!dsp_.setSettings(incoming))return false;
        // NativeMixerSessionFxRestore restores AFTER prepareToPlay. An accepted
        // v2 ChannelId must become the actual AUDIO guide source, not merely a
        // saved GUI value. This is unpublished/control-thread only.
        if(ready_) {
            guide_.reset();
            guideAge_=0;
            guideSource_.store(incoming.guideMode==1?incoming.guideChannelId:0,
                               std::memory_order_release);
            guideObservedKey_.store(-1,std::memory_order_relaxed);
            guideObservedScale_.store(-1,std::memory_order_relaxed);
            guideConfidence_.store(0.f,std::memory_order_relaxed);
            publishEffectiveKey();
        }
        accepted_=incoming;return true;
    }
    void setStateInformation(const void* data,int size) override {
        (void)restoreUnpublishedState(data,size);
    }
private:
    void publishEffectiveKey() noexcept {
        effectiveKeyCode_.store(dsp_.effectiveScale()*12+dsp_.effectiveKey(),
                                std::memory_order_relaxed);
    }
    struct Edit{std::uint64_t requestId=0;armix::harmony::Engine::PreparedSettings prepared;};
    armix::harmony::Engine dsp_{};
    armix::harmony::MusicKeyDetector guide_{};
    // Requested source is published only by AUDIO after SPSC ACK. Snapshot
    // revisions retain this exact processor, never a second routing owner.
    std::atomic<std::uint64_t> guideSource_{0};
    std::atomic<int> guideObservedKey_{-1},guideObservedScale_{-1};
    std::atomic<float> guideConfidence_{0.f};
    int guideAge_=0; // AUDIO-thread exclusive
    EqSpsc<Edit,32> queued_{};
    EqSpsc<Completion,32> completion_{};
    armix::harmony::Settings accepted_{},pendingSettings_{};
    std::uint64_t pendingId_=0; // control-only, no callback reads
    std::atomic<int> observedNote_{-1};
    std::atomic<int> effectiveKeyCode_{0};
    double sampleRate_=0.;bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
