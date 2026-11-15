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
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
        audioSettings_=accepted_;
        modeFade_=1.f;
        modePhase_=ModePhase::Stable;
        modeTransitionPending_.store(false,std::memory_order_release);
        modeStep_=static_cast<float>(1./(rate*.012));
#endif
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        core_.process(pcm.getWritePointer(0),pcm.getWritePointer(1),pcm.getNumSamples());
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
        // No extra PCM graph: click-safe fade OUT before changing the
        // authoritative FIR speaker transfer, then IN on subsequent blocks.
        // An APPLIED completion is published only AFTER core.setSettings().
        if(modePhase_!=ModePhase::Stable){
            auto* l=pcm.getWritePointer(0);
            auto* r=pcm.getWritePointer(1);
            for(int i=0;i<pcm.getNumSamples();++i){
                modeFade_=std::clamp(modeFade_+
                    (modePhase_==ModePhase::FadingOut?-modeStep_:modeStep_),
                    0.f,1.f);
                l[i]*=modeFade_;
                r[i]*=modeFade_;
            }
            if(modePhase_==ModePhase::FadingOut && modeFade_==0.f){
                const bool ok=core_.setSettings(stagedModeEdit_.settings);
                if(ok)audioSettings_=stagedModeEdit_.settings;
                // Completion slot was reserved at queue consumption; no
                // other edit is consumed during this fixed fade transaction.
                complete_.push({stagedModeEdit_.requestId,
                                stagedModeEdit_.settings,ok});
                modePhase_=ModePhase::FadingIn;
            }else if(modePhase_==ModePhase::FadingIn && modeFade_==1.f){
                modePhase_=ModePhase::Stable;
                modeTransitionPending_.store(false,std::memory_order_release);
            }
        }
#endif
        observedAzimuthDegrees_.store(static_cast<float>(core_.azimuthDegrees()),
            std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {
        Edit next;
        if(!ready_||!complete_.canPush())return;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
        if(modePhase_!=ModePhase::Stable)return;
#endif
        if(!queue_.pop(next))return;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
        // Logical/unpublished FX has NO callback: apply directly so
        // an offline .smix edit can be saved instead of hanging in fade.
        if(published_ &&
           (next.settings.renderMode!=audioSettings_.renderMode ||
            next.settings.musicCompatibleLowFrequency!=
                audioSettings_.musicCompatibleLowFrequency ||
            next.settings.headYawDegrees!=audioSettings_.headYawDegrees)){
            stagedModeEdit_=next;
            modeTransitionPending_.store(true,std::memory_order_release);
            modePhase_=ModePhase::FadingOut;
            return; // ACK only after the actual audio-side mode switch
        }
#endif
        const bool ok=core_.setSettings(next.settings);
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
        if(ok)audioSettings_=next.settings;
#endif
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
        return queue_.empty()&&complete_.empty()
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
            && !modeTransitionPending_.load(std::memory_order_acquire)
#endif
            ;
    }
    // CONTROL only; state is accepted, not speculative or PCM-owned.
    void getStateInformation(juce::MemoryBlock& dest) override {
        juce::MemoryOutputStream out(dest,false);
        out.writeInt(0x534F4E4B); // SONK
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT
        out.writeInt(10); // 63 bytes v9 layout, opt-in mode 8
#else
        out.writeInt(9); // v8 59-byte state + 4-byte HRTF trim
#endif
#else
        out.writeInt(8); // same 59-byte layout; modes 0/3/4/5/6
#endif
#else
        out.writeInt(7); // 59-byte layout; mode 5, old sessions intact
#endif
#else
        out.writeInt(6); // unchanged 59-byte state; P8.5 is opt-in mode 4
#endif
#else
        out.writeInt(5); // same 59-byte layout as v4, new P8.4 mode
#endif
#else
        out.writeInt(4);
#endif
#else
        out.writeInt(3);
#endif
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
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
        out.writeInt(accepted_.renderMode);
        out.writeByte(accepted_.musicCompatibleLowFrequency?1:0);
        out.writeFloat(accepted_.headYawDegrees);
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT
        out.writeFloat(accepted_.hrtfElevationTrimDegrees);
#endif
    }
    bool restoreUnpublishedState(const void* data,int size) {
        if(published_||!data||
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
#if defined(ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT
           (size!=30&&size!=46&&size!=50&&size!=59&&size!=63)||
#else
           (size!=30&&size!=46&&size!=50&&size!=59)||
#endif
#else
           (size!=30&&size!=46&&size!=50)||
#endif
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
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
             &&version!=4
#if defined(ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT
             &&version!=5
#if defined(ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT
             &&version!=6
#if defined(ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT
             &&version!=7
#if defined(ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT
             &&version!=8
#if defined(ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT
             &&version!=9
#if defined(ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT
             &&version!=10
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
            ) || (version==1&&size!=30)||(version==2&&size!=46)
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
            ||(version==3&&size!=50)
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
            ||(version==4&&size!=59)
#if defined(ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT
            ||(version==5&&size!=59)
#if defined(ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT
            ||(version==6&&size!=59)
#if defined(ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT
            ||(version==7&&size!=59)
#if defined(ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT
            ||(version==8&&size!=59)
#if defined(ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT
            ||(version==9&&size!=63)
#if defined(ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT
            ||(version==10&&size!=63)
#endif
#endif
#endif
#endif
#endif
#endif
#endif
#endif
            )return false;
        armix::sonku::Settings next{};
        next.startAzimuthDegrees=in.readFloat();
        next.revolutionSeconds=in.readFloat();
        next.stereoSpreadDegrees=in.readFloat();
        next.spatialMix=in.readFloat();
        next.outputTrim=in.readFloat();
        const int moving=in.readByte(),clockwise=in.readByte();
        // JUCE readByte() may return a SIGNED char. Reject -1/0xFF,
        // not just values above 1, for every v1/v2/v3/v4 restoration.
        if((moving!=0&&moving!=1)||(clockwise!=0&&clockwise!=1))
            return false;
        next.moving=moving!=0;
        next.clockwise=clockwise!=0;
        if(version>=2) {
            next.spacePreset=in.readInt();
            next.sourceDistanceMeters=in.readFloat();
            next.roomAmount=in.readFloat();
            next.ambience=in.readFloat();
        }
#if defined(ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT) && ARMIX_SONKU_EXPERIMENTAL_NATIVE_XYZ_RT
        if(version>=3)next.directElevationDegrees=in.readFloat();
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
        if(version>=4){
            next.renderMode=in.readInt();
            const int music=in.readByte();
            if(music!=0&&music!=1)return false;
            next.musicCompatibleLowFrequency=music!=0;
            next.headYawDegrees=in.readFloat();
#if defined(ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT
            // Critical headphone FAT v4 backward-safety: old P8.3
            // physical/mono session must NOT silently restore the
            // owner-rejected renderer in the new research candidate.
            // Explicitly migrate old v4 non-P8.2 to golden Music Bed.
            if(version==4) {
                // All legacy v4 Speaker LF flags are discarded, even for
                // a v4 P8.2 MusicBed session. P8.4 v5 exposes NO LF480 path.
                next.musicCompatibleLowFrequency=false;
                if(next.renderMode!=0) {
                    next.renderMode=0;
                    next.headYawDegrees=0.f;
                }
            }
            if(version==5 &&
               (next.renderMode!=0 && next.renderMode!=3))
                return false;
            if(version==5 && next.musicCompatibleLowFrequency)
                return false;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT
            // Preserve legacy P8.4 mode=3 on v5 restore. Never revive v4
            // rejected physical modes; v6 permits only music modes 0/3/4.
            if(version==6 && next.renderMode!=0 && next.renderMode!=3 && next.renderMode!=4)
                return false;
            if(version==6 && next.musicCompatibleLowFrequency)return false;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT
            // P8.6 mode 5 is opt-in only. v6 mode 4 is never migrated.
            if(version==7 && next.renderMode!=0 && next.renderMode!=3 &&
               next.renderMode!=4 && next.renderMode!=5)return false;
            if(version==7 && next.musicCompatibleLowFrequency)return false;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT
            if(version==8 && next.renderMode!=0 && next.renderMode!=3 &&
               next.renderMode!=4 && next.renderMode!=5 && next.renderMode!=6)return false;
            if(version==8 && next.musicCompatibleLowFrequency)return false;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT
            if(version==9 && next.renderMode!=0 && next.renderMode!=3 && next.renderMode!=4 && next.renderMode!=5 && next.renderMode!=6 && next.renderMode!=7)return false;
            if(version==9 && next.musicCompatibleLowFrequency)return false;
#if defined(ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT
            if(version==10 && next.renderMode!=0 && next.renderMode!=3 &&
               next.renderMode!=4 && next.renderMode!=5 && next.renderMode!=6 &&
               next.renderMode!=7 && next.renderMode!=8)return false;
            if(version==10 && next.musicCompatibleLowFrequency)return false;
#endif
#endif
#endif
#endif
#endif
#endif
        }
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT
        if(version>=9)next.hrtfElevationTrimDegrees=in.readFloat();
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
#if defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
    enum class ModePhase : std::uint8_t { Stable, FadingOut, FadingIn };
    Edit stagedModeEdit_{};
    armix::sonku::Settings audioSettings_{};
    ModePhase modePhase_=ModePhase::Stable;
    std::atomic<bool> modeTransitionPending_{false};
    float modeFade_=1.f,modeStep_=0.f;
#endif
    bool ready_=false;
    bool published_=false;
};
} // namespace armix::builtin_fx
