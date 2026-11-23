// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "VocalReverbProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Only JUCE CONTROL / message thread. Never called by the ASIO PCM owner.
namespace armix::builtin_fx {
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
