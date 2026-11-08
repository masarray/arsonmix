// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "VocalThrow.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>

// JUCE adapter for the existing ChannelId FX rack. All setting/throw requests
// are bound to an exact accepted graph instance by ConsoleWindow before they
// enter the control->audio SPSC. Audio never posts a WebView event.
namespace armix::builtin_fx {
class VocalThrowProcessor final : public juce::AudioProcessor,
                                  public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.vocal-throw.v1";
    enum class CommandKind : std::uint8_t { settings, trigger };
    struct Completion {
        std::uint64_t requestId=0;
        VocalThrowSettings settings{};
        CommandKind kind=CommandKind::settings;
        bool applied=false;
    };
    VocalThrowProcessor()
        : juce::AudioProcessor(BusesProperties()
            .withInput("Input",juce::AudioChannelSet::stereo(),true)
            .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {
        setLatencySamples(VocalThrow::addedLatencySamples);
    }
    const juce::String getName() const override {return "ArSon Vocal Throw";}
    bool acceptsMidi() const override{return false;}
    bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override{return 16.;}
    int getNumPrograms() override{return 1;}
    int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}
    const juce::String getProgramName(int) override{return "Current";}
    void changeProgramName(int,const juce::String&) override{}
    bool hasEditor() const override{return false;}
    juce::AudioProcessorEditor* createEditor() override{return nullptr;}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&
               layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double sampleRate,int) override {
        ready_=core_.prepare(sampleRate)&&core_.resetUnpublished(accepted_);
        if(ready_)rate_=sampleRate;
    }
    void releaseResources() override{
        ready_=false;musicInterval_.store(0,std::memory_order_release);
    }
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        // Transport is published by CONTROL as an already prepared sample
        // interval; AUDIO performs only one atomic read at the block edge.
        core_.setMusicIntervalSamples(
            musicInterval_.load(std::memory_order_acquire));
        core_.processStereo(pcm.getWritePointer(0),pcm.getWritePointer(1),
                            static_cast<std::size_t>(pcm.getNumSamples()));
        observedThrows_.store(core_.throwCount(),std::memory_order_relaxed);
        observedTail_.store(core_.echoActive(),std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override{}
    void consumeRealtimeEdits() noexcept override {
        Edit e{};
        if(!ready_||!done_.canPush()||!commands_.pop(e))return;
        const bool applied=e.kind==CommandKind::settings?
            core_.applyPrepared(e.prepared):core_.throwNow();
        (void)done_.push({e.id,e.prepared.settings,e.kind,applied});
    }
    // CONTROL-thread song/auto BPM, deliberately independent of accepted
    // serialized preset BPM. No queue ACK storm or GUI state overwrite.
    void publishMusicBpm(float bpm) noexcept {
        if(!ready_||!std::isfinite(bpm)||bpm<60.f||bpm>200.f) {
            musicInterval_.store(0,std::memory_order_release);return;
        }
        auto settings=accepted_;settings.bpm=bpm;
        VocalThrow::PreparedEdit packet{};
        const auto valid=VocalThrow::prepareEdit(settings,rate_,packet);
        musicInterval_.store(valid?packet.intervalSamples:0,
                             std::memory_order_release);
    }
    bool queueSettings(const VocalThrowSettings& settings,std::uint64_t id) noexcept {
        if(!ready_||id==0)return false;
        Edit e{};e.id=id;e.kind=CommandKind::settings;
        if(!VocalThrow::prepareEdit(settings,rate_,e.prepared))return false;
        return commands_.push(e);
    }
    bool queueTrigger(std::uint64_t id) noexcept {
        if(!ready_||id==0)return false;
        Edit e{};e.id=id;e.kind=CommandKind::trigger;
        return commands_.push(e);
    }
    bool pollCompletion(Completion& out) noexcept {
        if(!done_.pop(out))return false;
        if(out.applied&&out.kind==CommandKind::settings)accepted_=out.settings;
        return true;
    }
    VocalThrowSettings acceptedSettings() const noexcept{return accepted_;}
    double acceptedSampleRate() const noexcept{return rate_;}
    unsigned observedThrowCount() const noexcept {
        return observedThrows_.load(std::memory_order_relaxed);
    }
    bool observedTailActive() const noexcept {
        return observedTail_.load(std::memory_order_relaxed);
    }
    bool isReady() const noexcept{return ready_;}
    void sealForRealtime() noexcept override{published_=true;}
    bool stateSnapshotQuiescent() const noexcept override{
        return commands_.empty()&&done_.empty();
    }
    void getStateInformation(juce::MemoryBlock& out) override {
        juce::MemoryOutputStream writer(out,false);
        writer.writeInt(0x41565448); // AVTH
        writer.writeInt(1);
        writer.writeInt(accepted_.mode);
        writer.writeFloat(accepted_.bpm);
        writer.writeInt(accepted_.division);
        writer.writeInt(accepted_.repeats);
        writer.writeFloat(accepted_.captureMs);
        writer.writeFloat(accepted_.silenceMs);
        writer.writeFloat(accepted_.thresholdDb);
        writer.writeFloat(accepted_.wet);
        writer.writeFloat(accepted_.decay);
        writer.writeFloat(accepted_.ducking);
    }
    bool restoreUnpublishedState(const void* data,int bytes) {
        if(published_||!data||bytes!=48||!commands_.empty()||!done_.empty())
            return false;
        juce::MemoryInputStream reader(data,static_cast<std::size_t>(bytes),false);
        if(reader.readInt()!=0x41565448||reader.readInt()!=1)return false;
        VocalThrowSettings s{};
        s.mode=reader.readInt();
        s.bpm=reader.readFloat();
        s.division=reader.readInt();
        s.repeats=reader.readInt();
        s.captureMs=reader.readFloat();
        s.silenceMs=reader.readFloat();
        s.thresholdDb=reader.readFloat();
        s.wet=reader.readFloat();
        s.decay=reader.readFloat();
        s.ducking=reader.readFloat();
        if(!reader.isExhausted()||!VocalThrow::validSettings(s))return false;
        if(ready_&&!core_.resetUnpublished(s))return false;
        accepted_=s;return true;
    }
    void setStateInformation(const void* data,int bytes) override{
        (void)restoreUnpublishedState(data,bytes);
    }
private:
    struct Edit {
        std::uint64_t id=0;
        CommandKind kind=CommandKind::settings;
        VocalThrow::PreparedEdit prepared{};
    };
    VocalThrow core_{};
    FxSpsc<Edit,32> commands_{};
    FxSpsc<Completion,32> done_{};
    VocalThrowSettings accepted_{};
    std::atomic<unsigned> observedThrows_{0};
    std::atomic<std::uint32_t> musicInterval_{0};
    std::atomic<bool> observedTail_{false};
    double rate_=0.;
    bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
