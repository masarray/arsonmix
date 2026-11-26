// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "anti_noiz/AntiNoizNativeSession.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
namespace armix::builtin_fx {
// Actual JUCE adapter for the existing native FX registry and approved
// inline editor. No new render graph or private audio callback.
class AntiNoizProcessor final : public juce::AudioProcessor,
                               public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.anti-noiz.v1";
    using Core=anti_noiz::Core;
    using Control=anti_noiz::Control;
    using Completion=Control::Completion;
    using Snapshot=anti_noiz::NativeSession::Snapshot;
    using LearnStatus=anti_noiz::NativeSession::LearnStatus;
    AntiNoizProcessor():juce::AudioProcessor(BusesProperties()
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)){}
    const juce::String getName() const override {return "Anti Noiz";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return session_.rate()>0?double(getLatencySamples())/session_.rate():0.;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Current";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return false;} // existing approved WebView owner, not a second editor
    juce::AudioProcessorEditor* createEditor() override {return nullptr;}
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        auto in=b.getMainInputChannelSet();
        return in==b.getMainOutputChannelSet()&&(in==juce::AudioChannelSet::mono()||in==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double rate,int frames) override {
        if(sealed_)return; // published graph leases are replaced, not re-prepared
        if(frames<=0){session_.release();return;}
        if(!session_.prepare(rate,getTotalNumInputChannels(),sourceRole_))return;
        setRateAndBufferSizeDetails(rate,frames);setLatencySamples(session_.latency());
    }
    void releaseResources() override {session_.release();}
    bool setUnpublishedSourceRole(Core::SourceRole role) noexcept {
        if(sealed_||session_.ready()||static_cast<unsigned>(role)>4u)return false;
        sourceRole_=role;return true;
    }
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!session_.ready()||pcm.getNumChannels()!=session_.channels()||pcm.getNumSamples()<=0)return;
        session_.process(pcm.getWritePointer(0),pcm.getNumChannels()==2?pcm.getWritePointer(1):nullptr,pcm.getNumSamples());
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {session_.consume();}
    bool requiresContinuousBypassProcessing() const noexcept override {return true;}
    void onRealtimeBypass(bool requested,bool fully) noexcept override {session_.onBypass(requested,fully);}
    void sealForRealtime() noexcept override {sealed_=true;session_.seal();}
    bool stateSnapshotQuiescent() const noexcept override {return session_.quiescent();}
    bool isReady() const noexcept {return session_.ready();}
    Core::Settings acceptedSettings() const noexcept {return session_.acceptedSettings();}
    bool queueSettings(Core::Settings s,std::uint64_t id) noexcept {return session_.submit({id,Control::Kind::settings,s});}
    bool queueLearn(std::uint64_t id) noexcept {return session_.submit({id,Control::Kind::learn,{}});}
    bool queueClearProfile(std::uint64_t id) noexcept {return session_.submit({id,Control::Kind::clearProfile,{}});}
    bool queueAudition(std::uint64_t id) noexcept {return session_.submit({id,Control::Kind::audition,{}});}
    bool pollCompletion(Control::Completion& c) noexcept {return session_.poll(c);}
    bool pollTelemetry(Snapshot& s) noexcept {return session_.pollTelemetry(s);}
    // CONTROL-only bounded drain. No DSP state read on this thread.
    Snapshot readObservation() noexcept {
        Snapshot s;
        for(int i=0;i<2;++i)if(!session_.pollTelemetry(s))break;
        return session_.observed();
    }
    void getStateInformation(juce::MemoryBlock& out) override {
        out.reset();if(!session_.quiescent())return;
        const auto bytes=anti_noiz::NativeState::encode(session_.acceptedSettings());
        out.append(bytes.data(),bytes.size());
    }
    bool restoreUnpublishedState(const void* data,int bytes) noexcept {
        if(!data||(bytes!=static_cast<int>(anti_noiz::NativeState::size)&&bytes!=static_cast<int>(anti_noiz::NativeState::legacySize)&&bytes!=static_cast<int>(anti_noiz::NativeState::v2Size)))return false;
        return session_.restore({static_cast<const std::uint8_t*>(data),static_cast<std::size_t>(bytes)});
    }
    void setStateInformation(const void* data,int bytes) override {(void)restoreUnpublishedState(data,bytes);}
private:
    anti_noiz::NativeSession session_;
    Core::SourceRole sourceRole_=Core::SourceRole::Unknown;
    bool sealed_=false;
};
}
