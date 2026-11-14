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
    // Conservative bounded tail for the longest enabled room program. OFF
    // still performs no FDN work; this reports potential host tail ownership.
    double getTailLengthSeconds() const override {return 2.6;}
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
        // P6 is opt-in at build time on an unreleased test binary only.
        // The render profile is immutable across native audio callbacks.
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
        // Only an explicit native experimental build prepares six surfaces.
        constexpr auto profile=armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch;
#elif defined(ARMIX_SONKU_EXPERIMENTAL_DIRECTIONAL_RT) && ARMIX_SONKU_EXPERIMENTAL_DIRECTIONAL_RT
        constexpr auto profile=armix::sonku::SonKuReflectionProfile::DirectionalKemar;
#else
        constexpr auto profile=armix::sonku::SonKuReflectionProfile::Lightweight;
#endif
        ready_=core_.prepare(rate,profile);
        observedAzimuthDegrees_.store(accepted_.startAzimuthDegrees,std::memory_order_relaxed);
        if(ready_)ready_=core_.setSettings(accepted_);
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        core_.process(pcm.getWritePointer(0),pcm.getWritePointer(1),pcm.getNumSamples());
        observedAzimuthDegrees_.store(static_cast<float>(core_.azimuthDegrees()),
            std::memory_order_relaxed);
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
    armix::sonku::SonKuReflectionProfile preparedReflectionProfile() const noexcept {
        return core_.preparedReflectionProfile();
    }
    float observedAzimuthDegrees() const noexcept {
        return observedAzimuthDegrees_.load(std::memory_order_relaxed);
    }
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return queue_.empty()&&complete_.empty();
    }
    // CONTROL only; state is accepted, not speculative or PCM-owned.
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MemoryOutputStream out(dest,false);
        out.writeInt(0x534F4E4B); // SONK
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
        out.writeInt(3);
#else
        out.writeInt(2);
#endif
        out.writeFloat(accepted_.startAzimuthDegrees);
        out.writeFloat(accepted_.revolutionSeconds);
        out.writeFloat(accepted_.stereoSpreadDegrees);
        out.writeFloat(accepted_.spatialMix);
        out.writeFloat(accepted_.outputTrim);
        out.writeByte(accepted_.moving?1:0);
        out.writeByte(accepted_.clockwise?1:0);
        out.writeInt(accepted_.spacePreset);
        out.writeFloat(accepted_.sourceDistanceMeters);
        out.writeFloat(accepted_.roomAmount);
        out.writeFloat(accepted_.ambience);
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
        out.writeFloat(accepted_.directElevationDegrees);
#endif
    }
    bool restoreUnpublishedState(const void* data,int size) {
        if(published_||!data||
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
           (size!=30&&size!=46&&size!=50)||
#else
           (size!=30&&size!=46)||
#endif
           !queue_.empty()||!complete_.empty())return false;
        juce::MemoryInputStream in(data,static_cast<std::size_t>(size),false);
        if(in.readInt()!=0x534F4E4B)return false;
        const int version=in.readInt();
        if((version!=1&&version!=2
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
             &&version!=3
#endif
            ) || (version==1&&size!=30)||(version==2&&size!=46)
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
            ||(version==3&&size!=50)
#endif
            )return false;
        armix::sonku::Settings next{};
        next.startAzimuthDegrees=in.readFloat();
        next.revolutionSeconds=in.readFloat();
        next.stereoSpreadDegrees=in.readFloat();
        next.spatialMix=in.readFloat();
        next.outputTrim=in.readFloat();
        const int moving=in.readByte(),clockwise=in.readByte();
        if(moving>1||clockwise>1)return false;
        next.moving=moving!=0;
        next.clockwise=clockwise!=0;
        if(version>=2) {
            next.spacePreset=in.readInt();
            next.sourceDistanceMeters=in.readFloat();
            next.roomAmount=in.readFloat();
            next.ambience=in.readFloat();
        }
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
        if(version==3)next.directElevationDegrees=in.readFloat();
#endif
        // v1 OFF, v2 original scene; experimental v3 includes direct
        // elevation, and is rejected by ordinary public v2 builds.
        // Never accept a partial or malformed native FX state.
        if(!in.isExhausted())return false;
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
    std::atomic<float> observedAzimuthDegrees_{0.f};
    bool ready_=false;
    bool published_=false;
};
} // namespace armix::builtin_fx
