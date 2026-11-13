// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "guitar/GuitarRigCore.h"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// M1 first-sound insert: one ordinary FxInstance, *not* a new mixer renderer.
// The amp and speaker FIR are original algorithmic DSP; commercial cabinet
// captures and NeuralAmpModeler are NOT silently embedded or represented.
namespace armix::builtin_fx {
class GuitarRigProcessor final : public juce::AudioProcessor,
    public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.guitar-rig.v1";
    static constexpr std::uint32_t stateMagic=0x47525831; // GRX1
    static constexpr int stateHeaderBytes=12;
    static constexpr int stateBlockBytes=44;
    static constexpr int crossfadeFrames=128;
    struct Completion {
        std::uint64_t requestId=0;
        armix::guitarfx::Program program{};
        bool applied=false;
    };

    GuitarRigProcessor()
      : juce::AudioProcessor(BusesProperties()
          .withInput("Input",juce::AudioChannelSet::stereo(),true)
          .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {
        setLatencySamples(armix::guitarfx::GuitarRigCore::cabinetLatencySamples);
    }
    const juce::String getName() const override {return "Guitar FX Rig";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 0.;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Studio Guitar";}
    void changeProgramName(int,const juce::String&) override {}
    // Uses existing generic FX editor ownership from the host. Its original
    // hardware PNGs are linked only into the production application.
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo() &&
               layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    void prepareToPlay(double sampleRate,int maxBlock) override {
        // Host only calls prepare while detached/quiescent. Drain accepted
        // completions before retuning the program to the NEW sample rate.
        serviceCompletions();
        Edit stale{};
        while(queued_.pop(stale)) {
            pending_.fetch_sub(1,std::memory_order_release);
            lastCompletion_={stale.requestId,stale.program,false};
        }
        fadeLeft_=0;
        ready_=maxBlock>=16&&maxBlock<=8192&&
               core_.prepare(sampleRate,accepted_);
        rate_=ready_?sampleRate:0.;
        inputPeak_.store(0.f,std::memory_order_relaxed);
        outputPeak_.store(0.f,std::memory_order_relaxed);
    }
    void releaseResources() override {ready_=false;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals denormals;
        if(!ready_||pcm.getNumChannels()<2){pcm.clear();return;}
        const int samples=pcm.getNumSamples();
        if(samples<=0)return;
        // Exactly like EQ/Compressor/Harmonice: consume once per live block.
        // The FxInstance also drains on its fully-bypassed fast path.
        // Without this, knob/preset edits stall whenever Guitar FX is ON.
        consumeRealtimeEdits();
        auto* const left=pcm.getWritePointer(0);
        auto* const right=pcm.getWritePointer(1);
        float inPeak=0.f;
        for(int i=0;i<samples;++i)
            inPeak=std::max(inPeak,std::max(std::fabs(left[i]),std::fabs(right[i])));
        // Program switches use both prepared DSP states for at most
        // 128 samples and crossfade. No heap, lock or preparation on RT.
        int processed=0;
        while(processed<samples && fadeLeft_>0) {
            float newL=left[processed],newR=right[processed];
            float oldL=newL,oldR=newR;
            core_.processStereo(&newL,&newR,1);
            beforeSwitch_.processStereo(&oldL,&oldR,1);
            const float t=1.f-static_cast<float>(fadeLeft_-1)/
                              static_cast<float>(crossfadeFrames);
            left[processed]=oldL*(1.f-t)+newL*t;
            right[processed]=oldR*(1.f-t)+newR*t;
            --fadeLeft_;++processed;
        }
        if(processed<samples)
            core_.processStereo(left+processed,right+processed,
                               static_cast<std::size_t>(samples-processed));
        float outPeak=0.f;
        for(int i=0;i<samples;++i)
            outPeak=std::max(outPeak,std::max(std::fabs(left[i]),std::fabs(right[i])));
        inputPeak_.store(inPeak,std::memory_order_relaxed);
        outputPeak_.store(outPeak,std::memory_order_relaxed);
        processedBlocks_.fetch_add(1,std::memory_order_release);
    }
    void processBlock(juce::AudioBuffer<double>& pcm,juce::MidiBuffer&) override {
        pcm.clear(); // No silently unprocessed double-precision guitar output.
    }
    void consumeRealtimeEdits() noexcept override {
        Edit next{};
        if(!ready_||fadeLeft_>0||!completed_.canPush()||!queued_.pop(next))return;
        // An immutable program is prepared by CONTROL before publication.
        beforeSwitch_=core_;
        core_=next.prepared;
        fadeLeft_=crossfadeFrames;
        (void)completed_.push({next.requestId,next.program,true});
    }

    // SINGLE message-thread producer; reject malformed programs before
    // touching the live DSP. No DSP compilation in processBlock.
    std::uint64_t nextRequestId() noexcept {
        // CONTROL only. Preserve exact monotonic identity across editor
        // close/reopen; never use zero as a potentially valid ACK.
        return requestSerial_<std::numeric_limits<std::uint64_t>::max()?
               ++requestSerial_:0;
    }
    bool queueProgram(const armix::guitarfx::Program& program,
                      std::uint64_t requestId) noexcept {
        if(!ready_||!requestId||!std::isfinite(rate_)||
           !armix::guitarfx::GuitarRigCore::supportsProgram(program))
            return false;
        Edit next{};
        next.requestId=requestId;
        next.program=program;
        if(!next.prepared.prepare(rate_,program))return false;
        // CONTROL publishes outstanding intent before AUDIO can see a packet.
        // This closes the pop->ACK gap in stateSnapshotQuiescent(); .smix
        // must never save stale accepted state while audio applies a tone.
        pending_.fetch_add(1,std::memory_order_release);
        if(queued_.push(next))return true;
        pending_.fetch_sub(1,std::memory_order_release);
        return false;
    }
    // CONTROL-only. Editor timers and Show capture share one reducer.
    // A pending GUI edit is not accepted until callback completion.
    void serviceCompletions() noexcept {
        Completion next{};
        for(int i=0;i<4&&completed_.pop(next);++i) {
            if(next.applied)accepted_=next.program;
            lastCompletion_=next;
            pending_.fetch_sub(1,std::memory_order_release);
        }
    }
    Completion lastCompletion() const noexcept {return lastCompletion_;}
    std::uint32_t pendingProgramChanges() const noexcept {
        return pending_.load(std::memory_order_acquire);
    }

    bool isReady() const noexcept {return ready_&&core_.isReady();}
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        // CONTROL only: closed editors cannot strand accepted completions.
        const_cast<GuitarRigProcessor*>(this)->serviceCompletions();
        return pending_.load(std::memory_order_acquire)==0 &&
               queued_.empty()&&completed_.empty();
    }
    double acceptedSampleRate() const noexcept {return rate_;}
    const armix::guitarfx::Program& acceptedProgram() const noexcept {
        return accepted_; // CONTROL only, after quiescent state capture.
    }
    float observedInputPeak() const noexcept {
        return inputPeak_.load(std::memory_order_relaxed);
    }
    float observedOutputPeak() const noexcept {
        return outputPeak_.load(std::memory_order_relaxed);
    }
    std::uint64_t observedBlockSerial() const noexcept {
        return processedBlocks_.load(std::memory_order_acquire);
    }

    void getStateInformation(juce::MemoryBlock& destination) override {
        serviceCompletions(); // CONTROL: completed edits now durable.
        // Existing FX lease serializes only on CONTROL while quiescent;
        // this is a portable field-by-field schema, not a memcpy of a struct.
        juce::MemoryOutputStream writer(destination,false);
        writer.writeInt(static_cast<int>(stateMagic));
        writer.writeInt(static_cast<int>(armix::guitarfx::schemaVersion));
        writer.writeInt(static_cast<int>(accepted_.count));
        for(std::size_t i=0;i<accepted_.count;++i){
            const auto& block=accepted_.blocks[i];
            writer.writeInt(static_cast<int>(block.id));
            writer.writeInt(static_cast<int>(block.module));
            writer.writeInt(block.enabled?1:0);
            for(const float value:block.values)writer.writeFloat(value);
        }
    }
    bool restoreUnpublishedState(const void* bytes,int length) {
        if(published_||!bytes||length<stateHeaderBytes||
           length>stateHeaderBytes+static_cast<int>(armix::guitarfx::maxBlocks)*
                stateBlockBytes)return false;
        juce::MemoryInputStream reader(bytes,static_cast<std::size_t>(length),false);
        if(static_cast<std::uint32_t>(reader.readInt())!=stateMagic ||
           reader.readInt()!=static_cast<int>(armix::guitarfx::schemaVersion))
            return false;
        const int count=reader.readInt();
        if(count<2||count>static_cast<int>(armix::guitarfx::maxBlocks)||
           length!=stateHeaderBytes+count*stateBlockBytes)return false;
        armix::guitarfx::Program candidate{};
        candidate.count=static_cast<std::uint8_t>(count);
        for(auto i=0;i<count;++i){
            auto& b=candidate.blocks[static_cast<std::size_t>(i)];
            const int rawId=reader.readInt();
            const int rawType=reader.readInt();
            const int rawEnabled=reader.readInt();
            if(rawId<=0 || rawEnabled<0 || rawEnabled>1)return false;
            b.id=static_cast<std::uint32_t>(rawId);
            b.module=static_cast<armix::guitarfx::Module>(rawType);
            b.enabled=rawEnabled!=0;
            for(auto& value:b.values)value=reader.readFloat();
        }
        if(!reader.isExhausted()||
           !armix::guitarfx::GuitarRigCore::supportsProgram(candidate))
            return false;
        // A future-only module may be stored disabled, but a program that
        // requires unsupported enabled DSP is never admitted as live audio.
        for(std::size_t i=0;i<candidate.count;++i)
            if(candidate.blocks[i].enabled &&
               !armix::guitarfx::GuitarRigCore::supported(candidate.blocks[i].module))
                return false;
        if(ready_){
            // Only before publication, and on CONTROL. Preserve the previous
            // accepted program and core on any failed restore.
            armix::guitarfx::GuitarRigCore replacement{};
            if(!replacement.prepare(rate_,candidate))return false;
            core_=replacement;
        }
        accepted_=candidate;
        return true;
    }
    void setStateInformation(const void* data,int size) override {
        (void)restoreUnpublishedState(data,size);
    }
private:
    struct Edit {
        std::uint64_t requestId=0;
        armix::guitarfx::Program program{};
        armix::guitarfx::GuitarRigCore prepared{};
    };
    armix::guitarfx::GuitarRigCore core_{};
    armix::guitarfx::GuitarRigCore beforeSwitch_{};
    FxSpsc<Edit,4> queued_{};
    FxSpsc<Completion,4> completed_{};
    Completion lastCompletion_{};
    // CONTROL increments before publish and retires only on ACK/cancel.
    // Tracks the in-flight gap between queue pop and completed_.push.
    std::atomic<std::uint32_t> pending_{0};
    std::uint64_t requestSerial_=0; // CONTROL only
    int fadeLeft_=0; // audio-owned
    armix::guitarfx::Program accepted_=
        armix::guitarfx::GuitarRigCore::startingProgram();
    std::atomic<float> inputPeak_{0.f},outputPeak_{0.f};
    std::atomic<std::uint64_t> processedBlocks_{0};
    double rate_=0.;
    bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
