// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../third_party/arsonverb/include/arverb/Reverb.hpp"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace armix::builtin_fx {
inline constexpr int reverbSisPresetCount=13;
inline constexpr int reverbSisCustomItem=reverbSisPresetCount+1;
struct VocalReverbSettings {
    std::array<float, arverb::parameterCount> values{};
    int algorithm=1; // Plate
    int preset=0;    // -1 means edited
};
inline bool validVocalReverbSettings(const VocalReverbSettings& v) noexcept {
    if(v.algorithm<0||v.algorithm>=static_cast<int>(arverb::algorithmCount)||
       v.preset < -1 || v.preset >= reverbSisPresetCount) return false;
    for(std::size_t i=0;i<v.values.size();++i) {
        const auto& d=arverb::parameterInfo(static_cast<arverb::Parameter>(i));
        if(!std::isfinite(v.values[i]) || v.values[i]<d.minimum ||
           v.values[i]>d.maximum) return false;
    }
    return true;
}
inline constexpr std::array<const char*,reverbSisPresetCount> vocalReverbPresetNames{{
    "Krenyess Vocal", "Sweet Karaoke", "Crisp Pop Plate", "Silky Ballad",
    "Tight Live Chamber", "Air Vocal Hall", "Warm Vintage Plate", "Large Stage Hall",
    "Acoustic Guitar Room", "Piano Concert Hall", "Drum Studio Room", "Keys Dream Hall",
    "Master Ceremony"
}};
inline VocalReverbSettings vocalReverbFactoryPreset(int index) {
    using P=arverb::Preset;
    constexpr std::array<P,reverbSisPresetCount> origins{{
        P::VocalPlateClean,P::VocalPlate,P::VocalPlateClean,P::AiryBallad,
        P::VocalChamberTight,P::VocalWideAir,P::VocalPlateWarm,P::VocalHallBallad,
        P::GuitarShortSpace,P::PianoHallWarm,P::DrumRoomTight,P::KeysStereoWash,
        P::VocalSpaceShort
    }};
    if(index<0||index>=static_cast<int>(origins.size()))index=0;
    VocalReverbSettings result;
    result.values=arverb::presetValues(origins[static_cast<std::size_t>(index)]);
    result.algorithm=static_cast<int>(arverb::presetAlgorithm(origins[static_cast<std::size_t>(index)]));
    result.preset=index;
    using Param=arverb::Parameter;
    auto put=[&](Param key,float value) {result.values[static_cast<std::size_t>(key)]=value;};
    // Keep stable factory indices and serialized parameter IDs. Retune only
    // factory values; custom/previously saved numeric settings remain exact.
    // Wet return is separately calibrated in Engine::prepare(rate,true).
    switch(index) {
        case 0: // bright, present singing plate
            put(Param::Mix,.34f);put(Param::PreDelayMs,32.f);
            put(Param::DecaySeconds,2.45f);put(Param::Diffusion,.94f);
            put(Param::DampingHz,10400.f);put(Param::LowCutHz,150.f);
            put(Param::HighCutHz,15500.f);put(Param::Width,1.15f);
            put(Param::EarlyLevel,.36f);put(Param::ModDepthMs,.20f);
            put(Param::DuckAmount,.18f);break;
        case 1:
            put(Param::Mix,.31f);put(Param::PreDelayMs,29.f);
            put(Param::DecaySeconds,2.05f);put(Param::Diffusion,.88f);
            put(Param::LowCutHz,155.f);
            put(Param::HighCutHz,12400.f);put(Param::EarlyLevel,.38f);
            put(Param::DuckAmount,.18f);break;
        case 2:
            put(Param::Mix,.25f);put(Param::PreDelayMs,28.f);
            put(Param::DecaySeconds,1.65f);put(Param::Diffusion,.9f);
            put(Param::LowCutHz,170.f);put(Param::HighCutHz,14500.f);
            put(Param::EarlyLevel,.30f);put(Param::DuckAmount,.24f);break;
        case 3:
            put(Param::Mix,.38f);put(Param::PreDelayMs,48.f);
            put(Param::DecaySeconds,3.5f);put(Param::Diffusion,.91f);
            put(Param::LowCutHz,160.f);
            put(Param::HighCutHz,12000.f);put(Param::EarlyLevel,.25f);
            put(Param::DuckAmount,.22f);break;
        case 4:
            put(Param::Mix,.22f);put(Param::PreDelayMs,14.f);
            put(Param::DecaySeconds,1.05f);put(Param::LowCutHz,135.f);
            put(Param::EarlyLevel,.48f);put(Param::DuckAmount,.20f);break;
        case 5:
            put(Param::Mix,.36f);put(Param::PreDelayMs,43.f);
            put(Param::DecaySeconds,3.05f);put(Param::Diffusion,.92f);
            put(Param::LowCutHz,145.f);
            put(Param::HighCutHz,15200.f);put(Param::Width,1.25f);
            put(Param::EarlyLevel,.24f);put(Param::DuckAmount,.22f);break;
        case 6:
            put(Param::Mix,.29f);put(Param::LowCutHz,140.f);
            put(Param::HighCutHz,8500.f);put(Param::DampingHz,5300.f);
            put(Param::EarlyLevel,.30f);put(Param::DuckAmount,.18f);break;
        case 7:
            put(Param::Mix,.32f);put(Param::PreDelayMs,55.f);
            put(Param::DecaySeconds,4.1f);put(Param::LowCutHz,165.f);
            put(Param::EarlyLevel,.24f);put(Param::DuckAmount,.24f);break;
        case 8:
            put(Param::Mix,.24f);put(Param::PreDelayMs,16.f);
            put(Param::LowCutHz,110.f);put(Param::EarlyLevel,.42f);
            put(Param::DuckAmount,.08f);break;
        case 9:
            put(Param::Mix,.28f);put(Param::LowCutHz,95.f);
            put(Param::EarlyLevel,.32f);put(Param::DuckAmount,.08f);break;
        case 10:
            put(Param::Mix,.22f);put(Param::LowCutHz,100.f);
            put(Param::EarlyLevel,.62f);put(Param::DuckAmount,.06f);break;
        case 11:
            put(Param::Mix,.32f);put(Param::LowCutHz,140.f);
            put(Param::Width,1.25f);put(Param::DuckAmount,.08f);break;
        case 12: // Master Ceremony: intimate, clear speech with tasteful space.
            // Append only: old saved preset indices and all numeric IDs stay stable.
            result.algorithm=static_cast<int>(arverb::Algorithm::Chamber);
            put(Param::Mix,.14f);put(Param::PreDelayMs,27.f);
            put(Param::DecaySeconds,.78f);put(Param::Size,.80f);
            put(Param::Diffusion,.78f);put(Param::DampingHz,8300.f);
            put(Param::LowCutHz,215.f);put(Param::HighCutHz,12800.f);
            put(Param::ModDepthMs,.045f);put(Param::ModRateHz,.22f);
            put(Param::Width,1.02f);put(Param::EarlyLevel,.33f);
            put(Param::DuckAmount,.42f);break;
    }
    return result;
}

class VocalReverbProcessor final : public juce::AudioProcessor,
    public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.vocal-reverb.v1";
    static constexpr int stateBytes=16+static_cast<int>(arverb::parameterCount)*4;
    VocalReverbProcessor():juce::AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {
        desired_=vocalReverbFactoryPreset(0);
        storeAccepted(desired_,0);
    }
    const juce::String getName() const override {return "Reverb-Sis";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 12.3;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Custom / saved";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo() &&
               layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double rate,int maxBlock) override {
        ready_=false;
        if(!std::isfinite(rate)||rate<8000.||rate>192000.||maxBlock<16||maxBlock>8192)
            return;
        VocalReverbSettings s;
        if(!readAccepted(s)||!validVocalReverbSettings(s))return;
        // P8 upstream primes coefficient targets in prepare(). Stage the
        // RESTORED audio settings BEFORE prepare, never in the first callback.
        applyToEngine(s);
        engine_.prepare(rate,true); // CONTROL/host suspended; only place for allocation
        rate_=rate;
        // FxInstance uses AudioProcessor rate metadata for its 8ms bypass
        // transition. Native direct prepare must publish the real rate too.
        setRateAndBufferSizeDetails(rate,maxBlock);
        renderedOnAir_=false;
        fullyBypassed_=false;
        draining_=false;
        drainFrames_=0;
        drainLimitFrames_=0;
        clearMeters();
        ready_.store(true,std::memory_order_release);
    }
    void releaseResources() override {
        ready_.store(false,std::memory_order_release);
        clearMeters();
    }
    bool isReady() const noexcept{return ready_.load(std::memory_order_acquire);}
    // A time-dependent FX must never freeze an audible tail inside the
    // native rack's fully-bypassed fast path. The already existing host
    // callback remains the only DSP owner; no additional latency or worker.
    bool requiresContinuousBypassProcessing() const noexcept override {return true;}
    void onRealtimeBypass(bool, bool fully) noexcept override {
        if(fully && !fullyBypassed_) {
            clearMeters();
            fullyBypassed_=true;
            draining_=renderedOnAir_;
            drainFrames_=0;
            drainLimitFrames_=drainWindowFrames_=tailDrainLimit();
        } else if(!fully && fullyBypassed_) {
            fullyBypassed_=false;
            draining_=false;
            drainFrames_=0;
            drainLimitFrames_=0;
        }
    }
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!isReady()||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        // The existing FxInstance calls consumeRealtimeEdits() ONCE before
        // invoking a fully-bypassed continuous processor. Do not pop another
        // packet here: that would double RT automation work per callback.
        if(!fullyBypassed_)consumeRealtimeEdits();
        if(fullyBypassed_ && !draining_)return; // parked tank, minimal CPU
        const auto frames=static_cast<std::size_t>(pcm.getNumSamples());
        auto* left=pcm.getWritePointer(0);
        auto* right=pcm.getWritePointer(1);
        // A closed editor must cost ZERO extra PCM peak scans. Subscribers
        // are independent of processing/state and use one relaxed load.
        const bool measure = meterSubscribers_.load(std::memory_order_relaxed)>0 &&
                             !fullyBypassed_;
        if(measure) {
            meterInputL_.store(meterPeak(left,frames),std::memory_order_relaxed);
            meterInputR_.store(meterPeak(right,frames),std::memory_order_relaxed);
        }
        if(fullyBypassed_) {
            // FxInstance already preserved the exact dry PCM in its RT
            // scratch and will overwrite our output after this call.
            // Feed silence to dissipate, not new speech into a muted tail.
            juce::FloatVectorOperations::clear(left,static_cast<int>(frames));
            juce::FloatVectorOperations::clear(right,static_cast<int>(frames));
        }
        engine_.process(left,right,left,right,frames);
        if(measure) {
            meterOutputL_.store(meterPeak(left,frames),std::memory_order_relaxed);
            meterOutputR_.store(meterPeak(right,frames),std::memory_order_relaxed);
            meterGeneration_.fetch_add(1,std::memory_order_release);
        }
        processedBlocks_.fetch_add(1,std::memory_order_release);
        if(!fullyBypassed_) {renderedOnAir_=true;return;}
        // Engine::reset() clears all delay rings and is forbidden here:
        // one-shot memory clearing in ASIO would create a deadline spike.
        // Post-mix silence does NOT prove an empty wet tank (e.g. Mix=0).
        // Drain the bounded decay window even if the audible output is zero.
        // If decay is extended during bypass, extend the remaining window.
        drainFrames_+=frames;
        if(drainFrames_>=drainLimitFrames_) {
            draining_=false;
            renderedOnAir_=false;
        }
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {
        if(!isReady())return;
        // Single latest-intent mailbox: a complete settings packet supersedes
        // older unrendered packets. No FIFO backlog during fast knob drags.
        const auto id=requestedId_.load(std::memory_order_seq_cst);
        if(id<=appliedRequestId())return;
        VocalReverbSettings edit;
        const auto before=requestedSequence_.load(std::memory_order_seq_cst);
        if(before&1u)return;
        for(std::size_t i=0;i<edit.values.size();++i)
            edit.values[i]=requestedValues_[i].load(std::memory_order_seq_cst);
        edit.algorithm=requestedAlgorithm_.load(std::memory_order_seq_cst);
        edit.preset=requestedPreset_.load(std::memory_order_seq_cst);
        const auto capturedId=requestedId_.load(std::memory_order_seq_cst);
        if(before!=requestedSequence_.load(std::memory_order_seq_cst)||
           capturedId<=appliedRequestId())return; // bounded retry next block
        applyToEngine(edit);
        storeAccepted(edit,capturedId); // PCM callback is the sole ACK authority
        if(fullyBypassed_&&draining_) {
            const auto limit=tailDrainLimit();
            if(limit>drainWindowFrames_) {
                drainWindowFrames_=limit;
                drainLimitFrames_=std::max(drainLimitFrames_,drainFrames_+limit);
            }
        }
    }
    // Message-thread single producer. Valid intent survives editor close and
    // audio stop; it is not an ACK until the next actual PCM callback.
    std::uint64_t queueSettings(const VocalReverbSettings& v) noexcept {
        if(!validVocalReverbSettings(v))return 0;
        const auto id=++nextId_;
        desired_=v;
        requestedSequence_.fetch_add(1,std::memory_order_seq_cst);
        for(std::size_t i=0;i<v.values.size();++i)
            requestedValues_[i].store(v.values[i],std::memory_order_seq_cst);
        requestedAlgorithm_.store(v.algorithm,std::memory_order_seq_cst);
        requestedPreset_.store(v.preset,std::memory_order_seq_cst);
        requestedId_.store(id,std::memory_order_seq_cst);
        requestedSequence_.fetch_add(1,std::memory_order_seq_cst);
        return id;
    }
    // Message-thread only: opening a new editor must show the last intent,
    // not roll back a value while waiting for the device/callback.
    VocalReverbSettings desiredSettings() const noexcept {return desired_;}
    bool settingsPending() const noexcept {
        return (requestedSequence_.load(std::memory_order_seq_cst)&1u)||
               requestedId_.load(std::memory_order_seq_cst)!=appliedRequestId();
    }
    std::uint64_t appliedRequestId() const noexcept {
        return appliedId_.load(std::memory_order_acquire);
    }
    std::uint64_t processedBlocks() const noexcept {
        return processedBlocks_.load(std::memory_order_acquire);
    }
    // Lock-free visual telemetry only. No UI control or processing decision
    // reads these; sampling from the GUI timer never blocks ASIO.
    void attachMeterView() noexcept {
        meterSubscribers_.fetch_add(1,std::memory_order_relaxed);
    }
    void detachMeterView() noexcept {
        // Balanced by editor ownership; multiple simultaneously open editors
        // cannot prematurely suppress the remaining observer's meters.
        meterSubscribers_.fetch_sub(1,std::memory_order_relaxed);
    }
    std::uint64_t meterGeneration() const noexcept {
        return meterGeneration_.load(std::memory_order_acquire);
    }
    std::array<float,4> meterPeaks() const noexcept {
        return {meterInputL_.load(std::memory_order_relaxed),
                meterInputR_.load(std::memory_order_relaxed),
                meterOutputL_.load(std::memory_order_relaxed),
                meterOutputR_.load(std::memory_order_relaxed)};
    }
    bool acceptedSettings(VocalReverbSettings& v) const noexcept {
        return readAccepted(v);
    }
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return !settingsPending() &&
               !(acceptedSequence_.load(std::memory_order_seq_cst)&1u);
    }
    void getStateInformation(juce::MemoryBlock& out) override {
        VocalReverbSettings accepted;
        const auto snapshotId=appliedRequestId();
        if(!stateSnapshotQuiescent()||!readAccepted(accepted)||
           !validVocalReverbSettings(accepted)||!stateSnapshotQuiescent()||
           snapshotId!=appliedRequestId()){
            out.reset();return;
        }
        juce::MemoryOutputStream writer(out,false);
        writer.writeInt(0x41565242); // AVRB, distinct from factory VST3
        writer.writeInt(1);
        writer.writeInt(accepted.preset);
        writer.writeInt(accepted.algorithm);
        for(const auto v:accepted.values)writer.writeFloat(v);
        writer.flush();
    }
    // Detached logical editor is CONTROL-owned, never installed in PCM.
    // Consume its latest packet explicitly before durable state capture.
    // A published owner must only be acknowledged by its audio callback.
    bool captureDetachedState(juce::MemoryBlock& out) {
        out.reset();
        if(published_||!isReady())return false;
        consumeRealtimeEdits();
        if(!stateSnapshotQuiescent())return false;
        getStateInformation(out);
        if(out.getSize()!=static_cast<std::size_t>(stateBytes)){
            out.reset();return false;
        }
        return true;
    }
    bool restoreUnpublishedState(const void* data,int bytes) {
        if(published_||!data||bytes!=stateBytes||settingsPending())return false;
        juce::MemoryInputStream reader(data,static_cast<std::size_t>(bytes),false);
        if(reader.readInt()!=0x41565242||reader.readInt()!=1)return false;
        VocalReverbSettings s;
        s.preset=reader.readInt();s.algorithm=reader.readInt();
        for(auto& v:s.values)v=reader.readFloat();
        if(!reader.isExhausted()||!validVocalReverbSettings(s))return false;
        // Only unpublished, detached/restoring instances may stage state.
        desired_=s;
        storeAccepted(s,appliedRequestId());
        if(isReady())applyToEngine(s);
        return true;
    }
    void setStateInformation(const void* data,int bytes) override {
        (void)restoreUnpublishedState(data,bytes);
    }
private:
    static float meterPeak(const float* samples,std::size_t frames) noexcept {
        float peak=0.f;
        for(std::size_t i=0;i<frames;++i)
            if(std::isfinite(samples[i]))
                peak=std::max(peak,std::fabs(samples[i]));
        return std::min(peak,8.f);
    }
    void clearMeters() noexcept {
        for(auto* peak:{&meterInputL_,&meterInputR_,&meterOutputL_,&meterOutputR_})
            peak->store(0.f,std::memory_order_relaxed);
    }
    std::uint64_t tailDrainLimit() const noexcept {
        const float decay=acceptedValues_[static_cast<std::size_t>(
            arverb::Parameter::DecaySeconds)].load(std::memory_order_relaxed);
        return static_cast<std::uint64_t>(rate_*(0.7+2.5*static_cast<double>(decay)));
    }
    void applyToEngine(const VocalReverbSettings& settings) noexcept {
        for(std::size_t i=0;i<settings.values.size();++i)
            engine_.setParameter(static_cast<arverb::Parameter>(i),settings.values[i]);
        engine_.setAlgorithm(static_cast<arverb::Algorithm>(settings.algorithm));
    }
    // All fields atomic: the GUI/save thread cannot race audio callback writes.
    // An odd sequence denotes a partial packet, even means complete.
    void storeAccepted(const VocalReverbSettings& s,std::uint64_t id) noexcept {
        acceptedSequence_.fetch_add(1,std::memory_order_seq_cst);
        for(std::size_t i=0;i<s.values.size();++i)
            acceptedValues_[i].store(s.values[i],std::memory_order_seq_cst);
        acceptedAlgorithm_.store(s.algorithm,std::memory_order_seq_cst);
        acceptedPreset_.store(s.preset,std::memory_order_seq_cst);
        acceptedSequence_.fetch_add(1,std::memory_order_seq_cst);
        appliedId_.store(id,std::memory_order_release);
    }
    bool readAccepted(VocalReverbSettings& v) const noexcept {
        for(int attempt=0;attempt<8;++attempt) {
            const auto before=acceptedSequence_.load(std::memory_order_seq_cst);
            if(before&1u)continue;
            for(std::size_t i=0;i<v.values.size();++i)
                v.values[i]=acceptedValues_[i].load(std::memory_order_seq_cst);
            v.algorithm=acceptedAlgorithm_.load(std::memory_order_seq_cst);
            v.preset=acceptedPreset_.load(std::memory_order_seq_cst);
            const auto after=acceptedSequence_.load(std::memory_order_seq_cst);
            if(before==after)return true;
        }
        return false;
    }
    arverb::Engine engine_;
    VocalReverbSettings desired_{}; // message-thread only
    std::array<std::atomic<float>,arverb::parameterCount> requestedValues_{};
    std::atomic<int> requestedAlgorithm_{1},requestedPreset_{0};
    std::atomic<std::uint32_t> requestedSequence_{0};
    std::atomic<std::uint64_t> requestedId_{0};
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::array<std::atomic<float>,arverb::parameterCount> acceptedValues_{};
    std::atomic<int> acceptedAlgorithm_{1},acceptedPreset_{0};
    std::atomic<std::uint32_t> acceptedSequence_{0};
    std::atomic<std::uint64_t> appliedId_{0},processedBlocks_{0};
    std::atomic<std::uint32_t> meterSubscribers_{0};
    std::atomic<std::uint64_t> meterGeneration_{0};
    std::atomic<float> meterInputL_{0.f},meterInputR_{0.f};
    std::atomic<float> meterOutputL_{0.f},meterOutputR_{0.f};
    std::uint64_t nextId_=0; // single message-thread producer
    // AUDIO-owned only: no other thread observes or resets live tail state.
    double rate_=0.;
    std::uint64_t drainFrames_=0,drainLimitFrames_=0,drainWindowFrames_=0;
    bool renderedOnAir_=false,fullyBypassed_=false,draining_=false;
    std::atomic<bool> ready_{false};
    bool published_=false;
};

} // namespace armix::builtin_fx

// The message-thread-only GUI lives in its own header; DSP, mailbox and
// session state above remain independent of any editor implementation.
#include "VocalReverbEditor.h"
