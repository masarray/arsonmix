// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "AutoDuckerCore.h"
#include "FxSpsc.h"
#include "../core/AutoDuckerKeyContext.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace armix::builtin_fx {
// Qualification adapter. Hidden from picker until native editor/source
// admission and accepted snapshot plumbing are qualified end-to-end.
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
        for(std::size_t i=0;i<applied_.count;++i){
            keys[i]={applied_.sources[i],armix::mixer::AutoDuckerKeyScope::find(applied_.sources[i],n)};
            valid|=keys[i].key.valid();
        }
        core_.processSources(pcm.getWritePointer(0),pcm.getWritePointer(1),n,
                            {keys.data(),applied_.count});
        gain_.store(core_.gain(),std::memory_order_relaxed);
        keyValid_.store(valid,std::memory_order_release);
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
        return ready_&&request&&s.validSources()&&AutoDuckerCore::prepareSettings(rate_,s.dynamics,e.prepared)&&queue_.push(e);
    }
    bool pollCompletion(Completion& out) noexcept {
        if(!completion_.pop(out))return false;
        if(out.applied)accepted_=out.settings;
        return true;
    }
    Settings acceptedSettings() const noexcept {return accepted_;}
    float observedGain() const noexcept {return gain_.load(std::memory_order_relaxed);}
    bool observedKeyValid() const noexcept {return keyValid_.load(std::memory_order_acquire);}
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {return queue_.empty()&&completion_.empty();}
    void getStateInformation(juce::MemoryBlock& out) override {
        juce::MemoryOutputStream w(out,false);
        w.writeInt(0x41445543);w.writeInt(2);w.writeInt(static_cast<int>(accepted_.count));
        for(std::size_t i=0;i<accepted_.count;++i)w.writeInt64(static_cast<juce::int64>(accepted_.sources[i]));
        const auto& s=accepted_.dynamics;
        for(float v:{s.thresholdDb,s.depthDb,s.attackMs,s.holdMs,s.releaseMs,s.hysteresisDb,s.highPassHz,s.lowPassHz})
            w.writeFloat(v);
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
        AutoDuckerCore::Prepared p{};
        if(!in.isExhausted()||!AutoDuckerCore::prepareSettings(ready_?rate_:48000.,d,p))return false;
        if(ready_&&!core_.prepare(rate_,d))return false;
        accepted_=applied_=s;return true;
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
    double rate_=0.;
    bool ready_=false,published_=false;
};
} // namespace armix::builtin_fx
