// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AutoDuckerCore.h"
#include "FxSpsc.h"
#include "../core/AutoDuckerKeyContext.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace armix::builtin_fx {
// Immutable per-instance selection is borrowed from the SAME published bank
// as PCM. Empty scope is a real empty selection; absence is isolated fixture.
inline thread_local const std::span<const std::uint64_t>* currentDuckerSelection=nullptr;
class AutoDuckerSelectionScope final {
public:
    explicit AutoDuckerSelectionScope(std::span<const std::uint64_t> ids) noexcept
        : ids_(ids),previous_(currentDuckerSelection){currentDuckerSelection=&ids_;}
    ~AutoDuckerSelectionScope(){currentDuckerSelection=previous_;}
    AutoDuckerSelectionScope(const AutoDuckerSelectionScope&)=delete;
    AutoDuckerSelectionScope& operator=(const AutoDuckerSelectionScope&)=delete;
private:
    std::span<const std::uint64_t> ids_;
    const std::span<const std::uint64_t>* previous_;
};
// Native insert; immutable sidechain selection and acknowledged DSP edits.
class AutoDuckerProcessor final : public juce::AudioProcessor,
                                  public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.auto-ducker.v1";
    struct Settings {
        std::array<std::uint64_t,AutoDuckerCore::maxSources> sources{};
        std::uint32_t count=0;
        AutoDuckerSettings dynamics{};
        bool validSources() const noexcept {
            if(count>sources.size())return false;
            for(std::size_t i=0;i<sources.size();++i){
                if(i<count){if(!sources[i]||(i&&sources[i-1]>=sources[i]))return false;}
                else if(sources[i])return false;
            }
            return true;
        }
    };
    struct Meter final {
        std::uint64_t serial=0;
        float gain=1.f;
        int phase=0;
        std::array<std::uint64_t,AutoDuckerCore::maxSources> ids{};
        std::array<float,AutoDuckerCore::maxSources> power{};
        std::array<bool,AutoDuckerCore::maxSources> available{};
    };
    struct Completion {std::uint64_t requestId=0;Settings settings{};bool applied=false;};
    AutoDuckerProcessor():juce::AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)){
        setLatencySamples(AutoDuckerCore::addedLatencySamples);
    }
    const juce::String getName() const override {return "Auto Ducker";}
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
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo() &&
               layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double rate,int frames) override {
        ready_=frames>0&&core_.prepare(rate,applied_.dynamics);
        if(ready_)rate_=rate;
        gain_.store(1.f,std::memory_order_relaxed);
        keyValid_.store(false,std::memory_order_relaxed);
    }
    void releaseResources() override {ready_=false;}
    bool isReady() const noexcept {return ready_;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals guard;
        if(!ready_||pcm.getNumChannels()!=2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        const auto n=pcm.getNumSamples();
        std::array<AutoDuckerSource,AutoDuckerCore::maxSources> keys{};
        bool valid=false;
        const auto ids=currentDuckerSelection?*currentDuckerSelection:
            std::span<const std::uint64_t>(applied_.sources.data(),applied_.count);
        const auto count=std::min(ids.size(),keys.size());
        for(std::size_t i=0;i<count;++i){
            keys[i]={ids[i],armix::mixer::AutoDuckerKeyScope::findPostFader(ids[i],n)};
            valid|=keys[i].key.valid();
        }
        core_.processSources(pcm.getWritePointer(0),pcm.getWritePointer(1),n,
                            {keys.data(),count});
        gain_.store(core_.gain(),std::memory_order_relaxed);
        keyValid_.store(valid,std::memory_order_release);
        // Atomic block summary. No GUI allocation, locks, log/exp on RT.
        meterSerial_.fetch_add(1,std::memory_order_acq_rel); // odd while writing
        meterGain_.store(core_.gain(),std::memory_order_relaxed);
        meterPhase_.store(core_.phase(),std::memory_order_relaxed);
        for(std::size_t i=0;i<count;++i){
            meterIds_[i].store(keys[i].id,std::memory_order_relaxed);
            meterPower_[i].store(core_.sourcePower(i),std::memory_order_relaxed);
            meterAvailable_[i].store(keys[i].key.valid(),std::memory_order_relaxed);
        }
        // The seqlock-protected zero sentinel terminates the current frame.
        // Tail cells are deliberately not published or read; Meter value
        // initialization guarantees a zero/false tail in every readback.
        if(count<keys.size())meterIds_[count].store(0,std::memory_order_relaxed);
        meterSerial_.fetch_add(1,std::memory_order_release);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    bool requiresContinuousBypassProcessing() const noexcept override {return true;}
    void consumeRealtimeEdits() noexcept override {
        Edit e{};
        if(!ready_||!completion_.canPush()||!queue_.pop(e))return;
        // Recalculate on the CONTROL producer, never on audio. A pending
        // packet from an older device rate is rejected by a fresh request.
        if(e.rate!=rate_){(void)completion_.push({e.request,applied_,false});return;}
        core_.applyPrepared(e.prepared);applied_=e.settings;
        (void)completion_.push({e.request,applied_,true});
    }
    bool queueSettings(const Settings& s,std::uint64_t request) noexcept {
        Edit e{};e.settings=s;e.request=request;e.rate=rate_;
        // Structural source edits require a graph transaction, not a scalar mailbox.
        if(!ready_||!request||!s.validSources()||s.sources!=accepted_.sources||
           s.count!=accepted_.count||!AutoDuckerCore::prepareSettings(rate_,s.dynamics,e.prepared))return false;
        // Queue pop -> completion push is also an in-flight edit. Saving or
        // topology edits must never mistake that interval for quiescence.
        if(editInFlight_.exchange(true,std::memory_order_acq_rel))return false;
        if(queue_.push(e))return true;
        editInFlight_.store(false,std::memory_order_release);return false;
    }
    bool pollCompletion(Completion& out) noexcept {
        if(!completion_.pop(out))return false;
        if(out.applied)accepted_.dynamics=out.settings.dynamics;
        out.settings=accepted_;
        editInFlight_.store(false,std::memory_order_release);
        return true;
    }
    Settings acceptedSettings() const noexcept {return accepted_;}
    // CONTROL after successful publication of a staged immutable selection.
    // Audio reads that bank's scope, never these control-owned source IDs.
    void acknowledgePublishedSources(const Settings& s) noexcept {
        accepted_.sources=s.sources;accepted_.count=s.count;
    }
    float observedGain() const noexcept {return gain_.load(std::memory_order_relaxed);}
    bool observedKeyValid() const noexcept {return keyValid_.load(std::memory_order_acquire);}
    bool readMeter(Meter& out) const noexcept {
        for(int attempt=0;attempt<3;++attempt){
            const auto a=meterSerial_.load(std::memory_order_acquire);
            if(!a||(a&1))continue;
            Meter frame;frame.serial=a;frame.gain=meterGain_.load(std::memory_order_relaxed);
            frame.phase=meterPhase_.load(std::memory_order_relaxed);
            for(std::size_t i=0;i<frame.ids.size();++i){
                frame.ids[i]=meterIds_[i].load(std::memory_order_relaxed);
                if(!frame.ids[i])break;
                frame.power[i]=meterPower_[i].load(std::memory_order_relaxed);
                frame.available[i]=meterAvailable_[i].load(std::memory_order_relaxed);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            if(a==meterSerial_.load(std::memory_order_relaxed)){out=frame;return true;}
        }
        return false;
    }
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {return !editInFlight_.load(std::memory_order_acquire)&&queue_.empty()&&completion_.empty();}
    void getStateInformation(juce::MemoryBlock& out) override {
        juce::MemoryOutputStream w(out,false);
        w.writeInt(0x41445543);w.writeInt(2);w.writeInt(static_cast<int>(accepted_.count));
        for(std::size_t i=0;i<accepted_.count;++i)w.writeInt64(static_cast<juce::int64>(accepted_.sources[i]));
        const auto& s=accepted_.dynamics;
        for(float v:{s.thresholdDb,s.depthDb,s.attackMs,s.holdMs,s.releaseMs,s.hysteresisDb,s.highPassHz,s.lowPassHz})
            w.writeFloat(v);
    }
    bool setUnpublishedSettings(const Settings& s) noexcept {
        AutoDuckerCore::Prepared p{};
        if(published_||!stateSnapshotQuiescent()||!s.validSources()||
           !AutoDuckerCore::prepareSettings(ready_?rate_:48000.,s.dynamics,p))return false;
        if(ready_&&!core_.prepare(rate_,s.dynamics))return false;
        accepted_=applied_=s;return true;
    }
    bool restoreUnpublishedState(const void* data,int bytes) {
        if(published_||!data||bytes<12||!stateSnapshotQuiescent())return false;
        juce::MemoryInputStream in(data,static_cast<std::size_t>(bytes),false);
        if(in.readInt()!=0x41445543)return false;
        const auto version=in.readInt();
        Settings s{};
        if(version==1){
            if(bytes!=48)return false;
            const auto id=static_cast<std::uint64_t>(in.readInt64());
            if(id){s.sources[0]=id;s.count=1;}
        }else if(version==2){
            const auto count=in.readInt();
            if(count<0||count>static_cast<int>(s.sources.size())||bytes!=44+count*8)return false;
            s.count=static_cast<std::uint32_t>(count);
            for(std::size_t i=0;i<s.count;++i)s.sources[i]=static_cast<std::uint64_t>(in.readInt64());
        }else return false;
        if(!s.validSources())return false;
        auto& d=s.dynamics;
        d.thresholdDb=in.readFloat();d.depthDb=in.readFloat();d.attackMs=in.readFloat();
        d.holdMs=in.readFloat();d.releaseMs=in.readFloat();d.hysteresisDb=in.readFloat();
        d.highPassHz=in.readFloat();d.lowPassHz=in.readFloat();
        return in.isExhausted()&&setUnpublishedSettings(s);
    }
    void setStateInformation(const void* data,int bytes) override {(void)restoreUnpublishedState(data,bytes);}
private:
    struct Edit {Settings settings{};AutoDuckerCore::Prepared prepared{};std::uint64_t request=0;double rate=0.;};
    AutoDuckerCore core_{};
    FxSpsc<Edit,32> queue_{};
    FxSpsc<Completion,32> completion_{};
    Settings applied_{},accepted_{};
    std::atomic<float> gain_{1.f};
    std::atomic<bool> keyValid_{false};
    std::atomic<bool> editInFlight_{false};
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free&&
                  std::atomic<float>::is_always_lock_free&&std::atomic<int>::is_always_lock_free&&
                  std::atomic<bool>::is_always_lock_free);
    std::atomic<std::uint64_t> meterSerial_{0};
    std::atomic<float> meterGain_{1.f};
    std::atomic<int> meterPhase_{0};
    std::array<std::atomic<std::uint64_t>,AutoDuckerCore::maxSources> meterIds_{};
    std::array<std::atomic<float>,AutoDuckerCore::maxSources> meterPower_{};
    std::array<std::atomic<bool>,AutoDuckerCore::maxSources> meterAvailable_{};
    double rate_=0.;
    bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
