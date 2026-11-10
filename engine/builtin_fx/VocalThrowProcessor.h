// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "VocalThrow.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include "../core/NativeBeatFrameContext.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
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
        bool bypassed=false;
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
        // CONTROL/device thread, only after the prior audio callback stops.
        // The DSP may already have APPLIED an edit whose completion is still
        // waiting in the AUDIO->CONTROL SPSC. Rebuilding from accepted_
        // would silently undo that change on a buffer/device/rate restart.
        // Preserve the actual last DSP target, just like Compressor does.
        // Cold Show restore (not yet prepared) takes accepted_ instead.
        const auto latest=core_.ready()?core_.settingsForTests():accepted_;
        // Each device epoch gets a new frame clock. A previously accepted
        // auto BPM interval is invalid if the hardware sample rate changed.
        lastTempoBpm_=0.f;lastTempoDivision_=-1;
        bypassed_=false;captureInvalidated_=false;
        musicInterval_.store(0,std::memory_order_release);
        ready_=core_.prepare(sampleRate)&&core_.resetUnpublished(latest);
        if(ready_)rate_=sampleRate;
    }
    void releaseResources() override{
        ready_=false;musicInterval_.store(0,std::memory_order_release);
    }
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        // consumeRealtimeEdits() snapshots the accepted tempo BEFORE any
        // manual cue in the same block; bypassed inserts use the same path.
        consumeRealtimeEdits();
        core_.processStereo(pcm.getWritePointer(0),pcm.getWritePointer(1),
                            static_cast<std::size_t>(pcm.getNumSamples()));
        observedThrows_.store(core_.throwCount(),std::memory_order_relaxed);
        observedTail_.store(core_.echoActive(),std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override{}
    void onRealtimeBypass(bool requested,bool fully) noexcept override {
        // Called by the single native render owner before any FX edits or
        // PCM in this block. Preserve a playing tail through fade-out; clear
        // only after full bypass, or on any re-enable (including quick flips).
        const bool clear=(requested&&fully&&!captureInvalidated_) ||
                         (!requested&&bypassed_);
        if(clear&&ready_) {
            core_.invalidateCaptureHistory();
            observedTail_.store(false,std::memory_order_relaxed);
        }
        if(requested&&fully)captureInvalidated_=true;
        if(!requested)captureInvalidated_=false;
        bypassed_=requested;
    }
    void consumeRealtimeEdits() noexcept override {
        if(!ready_)return;
        // One bounded attempt, never spin in ASIO. The native PCM callback
        // is the ONLY owner of a valid block sample clock. A stale epoch,
        // graph revision, or torn control publication falls back to TAP.
        const auto block=armix::mixer::NativeBeatFrameScope::current();
        const auto v0=gridVersion_.load(std::memory_order_acquire);
        bool valid=block.valid && (v0&1U)==0U;
        std::uint64_t anchor=0;
        std::uint32_t interval=0;
        if(valid) {
            const auto epoch=gridEpoch_.load(std::memory_order_relaxed);
            const auto revision=gridRevision_.load(std::memory_order_relaxed);
            anchor=gridAnchor_.load(std::memory_order_relaxed);
            interval=musicInterval_.load(std::memory_order_relaxed);
            const auto v1=gridVersion_.load(std::memory_order_acquire);
            valid=v0==v1 && block.epoch==epoch &&
                  block.revision==revision && interval>0;
        }
        // Low-confidence music can provide a real BPM while its beat
        // position is ambiguous. Keep that useful interval, but do NOT
        // claim grid quantization. If the whole transport is stale/torn,
        // fall all the way back to accepted Manual/TAP.
        if(valid&&anchor>0&&anchor<=block.startFrame)
            core_.setMusicBeatGrid(interval,anchor,block.startFrame,true);
        else
            core_.setMusicIntervalSamples(valid?interval:0);
        Edit e{};
        if(!done_.canPush()||!commands_.pop(e))return;
        // CONTROL can reject an expired UI request while ASIO is stopped.
        // A delayed callback must NEVER play its old vocal cue on resume.
        // One acquire load and one fixed-size completion; no RT clock/heap.
        // Multiple UI requests may time out while ASIO is suspended.
        // A single last-cancelled ID is NOT sufficient: later timeouts
        // would reactivate earlier queued cues. 32 cancellation slots
        // cover the 32-entry command FIFO with fixed bounded RT reads.
        bool expired=false;
        for(const auto& cancelled:cancelledIds_)
            expired=expired ||
                cancelled.load(std::memory_order_acquire)==e.id;
        const bool blockedTrigger=e.kind==CommandKind::trigger && bypassed_;
        // Settings are remembered while OFF; performance cues are not
        // scheduled into a stopped capture clock or revived on rack ON.
        const bool applied=!expired&&!blockedTrigger &&
            (e.kind==CommandKind::settings?
                core_.applyPrepared(e.prepared):core_.throwNow());
        (void)done_.push({e.id,e.prepared.settings,e.kind,applied,
                         blockedTrigger});
    }
    // CONTROL-thread song/auto BPM, deliberately independent of accepted
    // serialized preset BPM. No queue ACK storm or GUI state overwrite.
    void publishMusicBpm(float bpm) noexcept {
        if(!ready_||!std::isfinite(bpm)||bpm<60.f||bpm>200.f) {
            if(lastTempoBpm_!=0.f) {
                lastTempoBpm_=0.f;lastTempoDivision_=-1;
                musicInterval_.store(0,std::memory_order_release);
            }
            return;
        }
        // The 12Hz message timer may deliver the same accepted tempo many
        // times. Never recompute power/division or publish redundant atomics.
        if(lastTempoBpm_==bpm && lastTempoDivision_==accepted_.division)
            return;
        auto settings=accepted_;settings.bpm=bpm;
        VocalThrow::PreparedEdit packet{};
        const auto valid=VocalThrow::prepareEdit(settings,rate_,packet);
        lastTempoBpm_=valid?bpm:0.f;
        lastTempoDivision_=valid?accepted_.division:-1;
        musicInterval_.store(valid?packet.intervalSamples:0,
                             std::memory_order_release);
    }
    // CONTROL ONLY: one source-derived BPM+phase packet is fenced by the
    // accepted native PCM epoch/revision. The sample-grid anchor comes from
    // onset timestamped INSIDE the same audio callback (10ms granularity).
    // Writer is the existing serialized JUCE timer, not a second thread.
    void publishMusicGrid(float bpm,std::uint64_t anchorFrame,
                          std::uint64_t epoch,
                          std::uint64_t revision) noexcept {
        gridVersion_.fetch_add(1,std::memory_order_acq_rel); // odd, unstable
        publishMusicBpm(bpm);
        gridAnchor_.store(bpm>=60.f&&bpm<=200.f?anchorFrame:0,
                          std::memory_order_relaxed);
        gridEpoch_.store(epoch,std::memory_order_relaxed);
        gridRevision_.store(revision,std::memory_order_relaxed);
        gridVersion_.fetch_add(1,std::memory_order_release); // even, ready
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
    void cancelQueuedRequest(std::uint64_t id) noexcept {
        if(id)cancelledIds_[cancelWrite_++ % cancelledIds_.size()].store(
            id,std::memory_order_release); // CONTROL single writer
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
    std::atomic<std::uint64_t> gridVersion_{0},gridAnchor_{0};
    std::atomic<std::uint64_t> gridEpoch_{0},gridRevision_{0};
    std::array<std::atomic<std::uint64_t>,32> cancelledIds_{};
    std::uint32_t cancelWrite_=0; // CONTROL only
    bool bypassed_=false,captureInvalidated_=false; // AUDIO only
    std::atomic<bool> observedTail_{false};
    double rate_=0.;
    float lastTempoBpm_=0.f; // CONTROL only
    int lastTempoDivision_=-1; // CONTROL only
    bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
