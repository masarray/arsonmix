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
    static constexpr int sessionStateVersion=2; // v1 single-tone restore retained
    static constexpr std::uint8_t useCurrentBank=255;
    static constexpr int crossfadeFrames=128;
    struct Completion {
        std::uint64_t requestId=0;
        armix::guitarfx::Program program{};
        bool applied=false;
        std::uint8_t bank=0;
    };

    GuitarRigProcessor()
      : juce::AudioProcessor(BusesProperties()
          .withInput("Input",juce::AudioChannelSet::stereo(),true)
          .withOutput("Output",juce::AudioChannelSet::stereo(),true)) {
        setLatencySamples(armix::guitarfx::GuitarRigCore::processingLatencySamples);
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
            lastCompletion_={stale.requestId,stale.program,false,stale.bank};
        }
        fadeLeft_=0;
        fullyBypassed_=false;
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
    void onRealtimeBypass(bool /*requested*/,bool fully) noexcept override {
        // The host calls this from the SINGLE callback before either its wet
        // processBlock or its fully-bypassed CPU-saving fast path. A Guitar FX
        // crossfade must not wait forever for PCM that is deliberately skipped.
        if(fully&&!fullyBypassed_) {
            // With no wet PCM consumed, a 32-sample cabinet delay would
            // otherwise replay an old note on re-enable. Discard the stale
            // history only when the host has finished fading to dry.
            core_.reset();
            beforeSwitch_.reset();
        }
        fullyBypassed_=fully;
        if(fully)fadeLeft_=0;
    }
    void consumeRealtimeEdits() noexcept override {
        Edit next{};
        if(!ready_||fadeLeft_>0||!completed_.canPush()||!queued_.pop(next))return;
        // An immutable program is prepared by CONTROL before publication.
        // Reuse compatible delay/envelope history; unmodified filters are
        // not restarted cold every time the player adjusts a small knob.
        // Once fully bypassed, processBlock is intentionally skipped by the
        // host: install the new state without a crossfade that cannot finish.
        if(!fullyBypassed_)beforeSwitch_=core_;
        (void)next.prepared.inheritCompatibleHistory(core_);
        core_=next.prepared;
        fadeLeft_=fullyBypassed_?0:crossfadeFrames;
        (void)completed_.push({next.requestId,next.program,true,next.bank});
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
                      std::uint64_t requestId,
                      std::uint8_t bank=useCurrentBank) noexcept {
        const auto selected=bank==useCurrentBank?activeBank_:bank;
        if(selected>1 ||!ready_||!requestId||!std::isfinite(rate_)||
           !armix::guitarfx::GuitarRigCore::supportsProgram(program))
            return false;
        Edit next{};
        next.requestId=requestId;
        next.program=program;
        next.bank=selected;
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
            if(next.applied) {
                accepted_=next.program;
                bankPrograms_[next.bank]=next.program;
                activeBank_=next.bank;
            }
            lastCompletion_=next;
            pending_.fetch_sub(1,std::memory_order_release);
        }
    }
    Completion lastCompletion() const noexcept {return lastCompletion_;}
    std::uint8_t activeBank() const noexcept {return activeBank_;}
    const armix::guitarfx::Program& acceptedBankProgram(
            std::uint8_t bank) const noexcept {
        // CONTROL only; callers must pass 0 or 1.
        return bankPrograms_[bank<2?bank:0];
    }
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
        serviceCompletions(); // CONTROL only; host holds FX snapshot gate.
        // GRX1 v2: magic, storage version, active bank, then two bounded
        // program blocks. Stable field-by-field schema, not struct memcpy.
        // Legacy v1 single-tone snapshots are accepted below.
        juce::MemoryOutputStream writer(destination,false);
        writer.writeInt(static_cast<int>(stateMagic));
        writer.writeInt(sessionStateVersion);
        writer.writeInt(static_cast<int>(activeBank_));
        for(const auto& bank:bankPrograms_){
            writer.writeInt(static_cast<int>(bank.count));
            for(std::size_t i=0;i<bank.count;++i) {
                const auto& block=bank.blocks[i];
                writer.writeInt(static_cast<int>(block.id));
                writer.writeInt(static_cast<int>(block.module));
                writer.writeInt(block.enabled?1:0);
                for(float value:block.values)writer.writeFloat(value);
            }
        }
    }
    bool restoreUnpublishedState(const void* bytes,int length) {
        constexpr int maxBytes=stateHeaderBytes+2*4+
            2*static_cast<int>(armix::guitarfx::maxBlocks)*stateBlockBytes;
        if(published_||!bytes||length<stateHeaderBytes||length>maxBytes)
            return false;
        juce::MemoryInputStream reader(bytes,static_cast<std::size_t>(length),false);
        if(static_cast<std::uint32_t>(reader.readInt())!=stateMagic)
            return false;
        const int storageVersion=reader.readInt();
        if(storageVersion!=1 && storageVersion!=sessionStateVersion)return false;
        int selected=0;
        if(storageVersion==sessionStateVersion) {
            selected=reader.readInt();
            if(selected<0||selected>1)return false;
        }
        std::array<armix::guitarfx::Program,2> candidateBanks{};
        int byteCount=storageVersion==1?8:stateHeaderBytes;
        const int entries=storageVersion==1?1:2;
        for(int bank=0;bank<entries;++bank){
            const int count=reader.readInt();
            byteCount+=4;
            if(count<2||count>static_cast<int>(armix::guitarfx::maxBlocks))
                return false;
            // Reject corrupt sizes before reading any DSP fields.
            if(length<byteCount+count*stateBlockBytes)return false;
            auto& candidate=candidateBanks[static_cast<std::size_t>(bank)];
            candidate.count=static_cast<std::uint8_t>(count);
            for(int i=0;i<count;++i){
                auto& block=candidate.blocks[static_cast<std::size_t>(i)];
                const int rawId=reader.readInt();
                const int rawType=reader.readInt();
                const int rawEnabled=reader.readInt();
                if(rawId<=0||rawEnabled<0||rawEnabled>1)return false;
                block.id=static_cast<std::uint32_t>(rawId);
                block.module=static_cast<armix::guitarfx::Module>(rawType);
                block.enabled=rawEnabled!=0;
                for(auto& value:block.values)value=reader.readFloat();
            }
            byteCount+=count*stateBlockBytes;
            if(!armix::guitarfx::GuitarRigCore::supportsProgram(candidate))
                return false;
        }
        if(byteCount!=length||!reader.isExhausted())return false;
        if(storageVersion==1)candidateBanks[1]=candidateBanks[0];
        const auto& chosen=candidateBanks[static_cast<std::size_t>(selected)];
        if(ready_){
            // CONTROL and unpublished only. No partial commit on bad state.
            armix::guitarfx::GuitarRigCore replacement{};
            if(!replacement.prepare(rate_,chosen))return false;
            core_=replacement;
        }
        bankPrograms_=candidateBanks;
        activeBank_=static_cast<std::uint8_t>(selected);
        accepted_=chosen;
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
        std::uint8_t bank=0;
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
    bool fullyBypassed_=false; // AUDIO callback only, host bypass hint
    armix::guitarfx::Program accepted_=
        armix::guitarfx::GuitarRigCore::startingProgram();
    std::array<armix::guitarfx::Program,2> bankPrograms_{{
        armix::guitarfx::GuitarRigCore::startingProgram(),
        armix::guitarfx::GuitarRigCore::startingProgram()
    }};
    std::uint8_t activeBank_=0; // CONTROL only, after ACK
    std::atomic<float> inputPeak_{0.f},outputPeak_{0.f};
    std::atomic<std::uint64_t> processedBlocks_{0};
    double rate_=0.;
    bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
