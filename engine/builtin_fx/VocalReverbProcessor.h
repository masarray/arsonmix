// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../../third_party/arsonverb/include/arverb/Reverb.hpp"
#include "FxSpsc.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace armix::builtin_fx {
struct VocalReverbSettings {
    std::array<float, arverb::parameterCount> values{};
    int algorithm=1; // Plate
    int preset=0;    // -1 means edited
};
inline bool validVocalReverbSettings(const VocalReverbSettings& v) noexcept {
    if(v.algorithm<0||v.algorithm>=static_cast<int>(arverb::algorithmCount)||
       v.preset < -1 || v.preset >= 8) return false;
    for(std::size_t i=0;i<v.values.size();++i) {
        const auto& d=arverb::parameterInfo(static_cast<arverb::Parameter>(i));
        if(!std::isfinite(v.values[i]) || v.values[i]<d.minimum ||
           v.values[i]>d.maximum) return false;
    }
    return true;
}
inline constexpr std::array<const char*,8> vocalReverbPresetNames{{
    "Krenyess Vocal", "Sweet Karaoke", "Crisp Pop Plate", "Silky Ballad",
    "Tight Live Chamber", "Air Vocal Hall", "Warm Vintage Plate", "Large Stage Hall"
}};
inline VocalReverbSettings vocalReverbFactoryPreset(int index) {
    using P=arverb::Preset;
    constexpr std::array<P,8> origins{{
        P::VocalPlateClean,P::VocalPlate,P::VocalPlateClean,P::AiryBallad,
        P::VocalChamberTight,P::VocalWideAir,P::VocalPlateWarm,P::VocalHallBallad
    }};
    if(index<0||index>=static_cast<int>(origins.size()))index=0;
    VocalReverbSettings result;
    result.values=arverb::presetValues(origins[static_cast<std::size_t>(index)]);
    result.algorithm=static_cast<int>(arverb::presetAlgorithm(origins[static_cast<std::size_t>(index)]));
    result.preset=index;
    using Param=arverb::Parameter;
    auto put=[&](Param key,float value) {result.values[static_cast<std::size_t>(key)]=value;};
    switch(index) {
        case 0: // clear bright vocal sheen; preserve intelligibility
            put(Param::Mix,.18f);put(Param::PreDelayMs,52.f);
            put(Param::DecaySeconds,2.05f);put(Param::Diffusion,.89f);
            put(Param::DampingHz,8900.f);put(Param::LowCutHz,300.f);
            put(Param::HighCutHz,14200.f);put(Param::Width,1.24f);
            put(Param::EarlyLevel,.16f);put(Param::DuckAmount,.43f);break;
        case 1:
            put(Param::Mix,.17f);put(Param::PreDelayMs,44.f);
            put(Param::DecaySeconds,1.7f);put(Param::LowCutHz,240.f);
            put(Param::HighCutHz,11200.f);put(Param::DuckAmount,.43f);break;
        case 2:
            put(Param::Mix,.18f);put(Param::PreDelayMs,38.f);
            put(Param::DecaySeconds,1.55f);put(Param::Diffusion,.94f);
            put(Param::HighCutHz,12700.f);put(Param::DuckAmount,.52f);break;
        case 3:
            put(Param::Mix,.22f);put(Param::PreDelayMs,67.f);
            put(Param::DecaySeconds,3.1f);put(Param::HighCutHz,10400.f);break;
        case 4:
            put(Param::Mix,.13f);put(Param::PreDelayMs,22.f);
            put(Param::DecaySeconds,.95f);put(Param::DuckAmount,.49f);break;
        case 5:
            put(Param::Mix,.17f);put(Param::PreDelayMs,62.f);
            put(Param::DecaySeconds,2.65f);put(Param::LowCutHz,340.f);
            put(Param::HighCutHz,13000.f);put(Param::DuckAmount,.51f);break;
        case 6:
            put(Param::Mix,.19f);put(Param::HighCutHz,7300.f);
            put(Param::DampingHz,5300.f);break;
        case 7:
            put(Param::Mix,.21f);put(Param::PreDelayMs,76.f);
            put(Param::DecaySeconds,4.1f);put(Param::DuckAmount,.62f);break;
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
        storeAccepted(vocalReverbFactoryPreset(0),0);
    }
    const juce::String getName() const override {return "Vocal Reverb";}
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
        engine_.prepare(rate); // CONTROL/host suspended; only place for allocation
        ready_=true;
    }
    void releaseResources() override {ready_=false;}
    bool isReady() const noexcept{return ready_;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        auto* left=pcm.getWritePointer(0);
        auto* right=pcm.getWritePointer(1);
        engine_.process(left,right,left,right,static_cast<std::size_t>(pcm.getNumSamples()));
        processedBlocks_.fetch_add(1,std::memory_order_release);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void consumeRealtimeEdits() noexcept override {
        Edit edit{};
        if(!ready_||!queued_.pop(edit))return;
        applyToEngine(edit.settings);
        storeAccepted(edit.settings,edit.id); // PCM callback is the authority
        pendingEdits_.fetch_sub(1,std::memory_order_release);
    }
    // Message-thread single producer. Returns zero when no edit was accepted.
    std::uint64_t queueSettings(const VocalReverbSettings& v) noexcept {
        if(!ready_||!validVocalReverbSettings(v))return 0;
        const auto id=nextId_+1;
        // Include the interval after FIFO pop but before audio-applied ACK.
        // Without this reservation .smix can capture stale accepted controls.
        pendingEdits_.fetch_add(1,std::memory_order_acq_rel);
        if(!queued_.push({id,v})) {
            pendingEdits_.fetch_sub(1,std::memory_order_release);
            return 0;
        }
        nextId_=id;
        return id;
    }
    std::uint64_t appliedRequestId() const noexcept {
        return appliedId_.load(std::memory_order_acquire);
    }
    std::uint64_t processedBlocks() const noexcept {
        return processedBlocks_.load(std::memory_order_acquire);
    }
    bool acceptedSettings(VocalReverbSettings& v) const noexcept {
        return readAccepted(v);
    }
    void sealForRealtime() noexcept override {published_=true;}
    bool stateSnapshotQuiescent() const noexcept override {
        return pendingEdits_.load(std::memory_order_acquire)==0 &&
               queued_.empty() &&
               !(acceptedSequence_.load(std::memory_order_acquire)&1u);
    }
    void getStateInformation(juce::MemoryBlock& out) override {
        VocalReverbSettings accepted;
        if(!stateSnapshotQuiescent()||!readAccepted(accepted)||
           !validVocalReverbSettings(accepted)){out.reset();return;}
        juce::MemoryOutputStream writer(out,false);
        writer.writeInt(0x41565242); // AVRB, distinct from factory VST3
        writer.writeInt(1);
        writer.writeInt(accepted.preset);
        writer.writeInt(accepted.algorithm);
        for(const auto v:accepted.values)writer.writeFloat(v);
        writer.flush();
    }
    bool restoreUnpublishedState(const void* data,int bytes) {
        if(published_||!data||bytes!=stateBytes||!queued_.empty())return false;
        juce::MemoryInputStream reader(data,static_cast<std::size_t>(bytes),false);
        if(reader.readInt()!=0x41565242||reader.readInt()!=1)return false;
        VocalReverbSettings s;
        s.preset=reader.readInt();s.algorithm=reader.readInt();
        for(auto& v:s.values)v=reader.readFloat();
        if(!reader.isExhausted()||!validVocalReverbSettings(s))return false;
        // Only unpublished, detached/restoring instances may stage state.
        storeAccepted(s,appliedRequestId());
        if(ready_)applyToEngine(s);
        return true;
    }
    void setStateInformation(const void* data,int bytes) override {
        (void)restoreUnpublishedState(data,bytes);
    }
private:
    struct Edit {std::uint64_t id;VocalReverbSettings settings;};
    void applyToEngine(const VocalReverbSettings& settings) noexcept {
        for(std::size_t i=0;i<settings.values.size();++i)
            engine_.setParameter(static_cast<arverb::Parameter>(i),settings.values[i]);
        engine_.setAlgorithm(static_cast<arverb::Algorithm>(settings.algorithm));
    }
    // All fields atomic: the GUI/save thread cannot race audio callback writes.
    // An odd sequence denotes a partial packet, even means complete.
    void storeAccepted(const VocalReverbSettings& s,std::uint64_t id) noexcept {
        acceptedSequence_.fetch_add(1,std::memory_order_acq_rel);
        for(std::size_t i=0;i<s.values.size();++i)
            acceptedValues_[i].store(s.values[i],std::memory_order_relaxed);
        acceptedAlgorithm_.store(s.algorithm,std::memory_order_relaxed);
        acceptedPreset_.store(s.preset,std::memory_order_relaxed);
        appliedId_.store(id,std::memory_order_relaxed);
        acceptedSequence_.fetch_add(1,std::memory_order_release);
    }
    bool readAccepted(VocalReverbSettings& v) const noexcept {
        for(int attempt=0;attempt<8;++attempt) {
            const auto before=acceptedSequence_.load(std::memory_order_acquire);
            if(before&1u)continue;
            for(std::size_t i=0;i<v.values.size();++i)
                v.values[i]=acceptedValues_[i].load(std::memory_order_relaxed);
            v.algorithm=acceptedAlgorithm_.load(std::memory_order_relaxed);
            v.preset=acceptedPreset_.load(std::memory_order_relaxed);
            const auto after=acceptedSequence_.load(std::memory_order_acquire);
            if(before==after)return true;
        }
        return false;
    }
    arverb::Engine engine_;
    FxSpsc<Edit,32> queued_{};
    std::array<std::atomic<float>,arverb::parameterCount> acceptedValues_{};
    std::atomic<int> acceptedAlgorithm_{1},acceptedPreset_{0};
    std::atomic<std::uint32_t> acceptedSequence_{0};
    std::atomic<std::uint64_t> appliedId_{0},processedBlocks_{0};
    std::atomic<std::uint32_t> pendingEdits_{0};
    std::uint64_t nextId_=0; // single message-thread producer
    bool ready_=false,published_=false;
};

class VocalReverbEditor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit VocalReverbEditor(VocalReverbProcessor& p)
        :juce::AudioProcessorEditor(p),processor_(p) {
        setOpaque(true);
        if(!processor_.acceptedSettings(draft_))draft_=vocalReverbFactoryPreset(0);
        title_.setText("ArSonMix  /  VOCAL REVERB",juce::dontSendNotification);
        title_.setColour(juce::Label::textColourId,juce::Colour::fromRGB(226,234,244));
        title_.setFont(juce::Font(juce::FontOptions(16.f)));
        addAndMakeVisible(title_);
        status_.setJustificationType(juce::Justification::centredRight);
        status_.setColour(juce::Label::textColourId,juce::Colour::fromRGB(119,188,162));
        addAndMakeVisible(status_);
        for(int i=0;i<8;++i)preset_.addItem(vocalReverbPresetNames[static_cast<std::size_t>(i)],i+1);
        preset_.addItem("Custom / saved",9);
        preset_.setSelectedId(draft_.preset>=0?draft_.preset+1:9,juce::dontSendNotification);
        preset_.onChange=[this] {
            const auto id=preset_.getSelectedId();
            if(id>=1&&id<=8){draft_=vocalReverbFactoryPreset(id-1);syncControls();}
            dirty_=true;
        };
        addAndMakeVisible(preset_);
        for(int i=0;i<4;++i)algorithm_.addItem(arverb::algorithmName(static_cast<arverb::Algorithm>(i)),i+1);
        algorithm_.setSelectedId(draft_.algorithm+1,juce::dontSendNotification);
        algorithm_.onChange=[this] {
            draft_.algorithm=algorithm_.getSelectedId()-1;
            markCustom();
        };
        addAndMakeVisible(algorithm_);
        for(std::size_t i=0;i<knobs_.size();++i) {
            auto& knob=knobs_[i];
            const auto& spec=arverb::parameterInfo(static_cast<arverb::Parameter>(i));
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,58,17);
            knob.setRange(spec.minimum,spec.maximum,0);
            knob.setValue(draft_.values[i],juce::dontSendNotification);
            knob.setColour(juce::Slider::rotarySliderFillColourId,juce::Colour::fromRGB(96,194,191));
            knob.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour::fromRGB(51,65,81));
            knob.setColour(juce::Slider::thumbColourId,juce::Colour::fromRGB(209,221,232));
            knob.setColour(juce::Slider::textBoxTextColourId,juce::Colour::fromRGB(224,236,244));
            knob.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour::fromRGB(26,34,46));
            knob.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
            knob.onValueChange=[this,i] {
                draft_.values[i]=static_cast<float>(knobs_[i].getValue());
                markCustom();
            };
            addAndMakeVisible(knob);
            labels_[i].setText(juce::String(spec.name),juce::dontSendNotification);
            labels_[i].setJustificationType(juce::Justification::centred);
            labels_[i].setColour(juce::Label::textColourId,juce::Colour::fromRGB(162,178,195));
            labels_[i].setFont(juce::Font(juce::FontOptions(11.f)));
            addAndMakeVisible(labels_[i]);
        }
        setSize(816,346);
        startTimerHz(30);
    }
    ~VocalReverbEditor() override {stopTimer();}
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(19,25,34));
        g.setColour(juce::Colour::fromRGB(35,47,61));
        g.drawLine(16.f,58.f,static_cast<float>(getWidth()-16),58.f);
        g.drawLine(16.f,200.f,static_cast<float>(getWidth()-16),200.f);
    }
    void resized() override {
        title_.setBounds(18,10,290,27);
        preset_.setBounds(309,12,204,28);
        algorithm_.setBounds(525,12,115,28);
        status_.setBounds(656,12,143,28);
        constexpr int column=111,begin=18;
        for(int i=0;i<13;++i) {
            const int row=i<7?0:1;
            const int item=row==0?i:i-7;
            const int offset=row==0?begin:begin+54;
            const int x=offset+item*column;
            const int y=row==0?67:209;
            knobs_[static_cast<std::size_t>(i)].setBounds(x,y,92,92);
            labels_[static_cast<std::size_t>(i)].setBounds(x-6,y+90,104,19);
        }
    }
private:
    void markCustom() {
        draft_.preset=-1;
        preset_.setSelectedId(9,juce::dontSendNotification);
        dirty_=true;
    }
    void syncControls() {
        algorithm_.setSelectedId(draft_.algorithm+1,juce::dontSendNotification);
        for(std::size_t i=0;i<knobs_.size();++i)
            knobs_[i].setValue(draft_.values[i],juce::dontSendNotification);
    }
    void timerCallback() override {
        if(inFlight_&&processor_.appliedRequestId()>=inFlightId_)inFlight_=false;
        if(!processor_.isReady()){status_.setText("OFFLINE",juce::dontSendNotification);return;}
        if(dirty_&&!inFlight_) {
            const auto id=processor_.queueSettings(draft_);
            if(id){inFlight_=true;inFlightId_=id;dirty_=false;}
        }
        status_.setText(inFlight_?"APPLYING":(dirty_?"QUEUED":"PCM APPLIED"),
                        juce::dontSendNotification);
    }
    VocalReverbProcessor& processor_;
    VocalReverbSettings draft_;
    juce::Label title_,status_;
    juce::ComboBox preset_,algorithm_;
    std::array<juce::Slider,arverb::parameterCount> knobs_{};
    std::array<juce::Label,arverb::parameterCount> labels_{};
    std::uint64_t inFlightId_=0;
    bool inFlight_=false,dirty_=false;
};
inline juce::AudioProcessorEditor* VocalReverbProcessor::createEditor() {
    return new VocalReverbEditor(*this);
}
} // namespace armix::builtin_fx
