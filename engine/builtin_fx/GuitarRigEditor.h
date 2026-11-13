// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GuitarRigProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#if defined(ARMIX_GFX_EMBED_PNG_SKINS)
#include "BinaryData.h"
#endif

// Single native JUCE editor opened by the ALREADY EXISTING FxInstance
// vendor-editor lease. No second WebView, callback, device or mixer.
// Decorative original PNGs are embedded into ARMixAssets ONLY in the
// production app; test binaries use a lightweight drawn fallback.
namespace armix::builtin_fx {
class GuitarRigEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer {
    using Program=armix::guitarfx::Program;
    using Module=armix::guitarfx::Module;
    struct HardwareKnobs final : juce::LookAndFeel_V4 {
        juce::Image image;
        void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,
                              float pos,float start,float end,
                              juce::Slider& slider) override {
            if(!image.isValid()) {
                juce::LookAndFeel_V4::drawRotarySlider(g,x,y,w,h,pos,start,end,slider);
                return;
            }
            juce::Graphics::ScopedSaveState saved(g);
            const float cx=static_cast<float>(x+w/2),cy=static_cast<float>(y+h/2);
            const float radians=start+pos*(end-start);
            g.addTransform(juce::AffineTransform::rotation(radians,cx,cy));
            g.drawImageWithin(image,x+3,y+3,w-6,h-6,
                              juce::RectanglePlacement::centred);
        }
    };
public:
    explicit GuitarRigEditor(GuitarRigProcessor& processor)
        : juce::AudioProcessorEditor(processor),processor_(processor) {
        setOpaque(true);
        draft_=processor_.acceptedProgram();
        accepted_=draft_;
        loadArt();
        knobsLook_.image=knob_;
        setSize(1060,640);
        preset_.addItem("Clean Studio",1);
        preset_.addItem("Warm Blues",2);
        preset_.addItem("Vintage Crunch",3);
        preset_.addItem("Modern Lead",4);
        preset_.setSelectedId(1,juce::dontSendNotification);
        preset_.onChange=[this]{choosePreset(preset_.getSelectedId());};
        addAndMakeVisible(preset_);
        for(std::size_t i=0;i<4;++i){
            const auto* name=names_[i];
            focusButtons_[i].setButtonText(name);
            focusButtons_[i].onClick=[this,i]{
                focus_=i;refreshWidgets();repaint();
            };
            addAndMakeVisible(focusButtons_[i]);
            power_[i].setButtonText("ON");
            power_[i].setClickingTogglesState(false);
            power_[i].onClick=[this,i]{
                if(draft_.count<=i)return;
                draft_.blocks[i].enabled=!draft_.blocks[i].enabled;
                dirty_=true;refreshWidgets();repaint();
            };
            addAndMakeVisible(power_[i]);
        }
        for(std::size_t i=0;i<3;++i){
            sliders_[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            sliders_[i].setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            sliders_[i].setRange(0.,1.,0.005);
            sliders_[i].setLookAndFeel(&knobsLook_);
            sliders_[i].onValueChange=[this,i] {
                if(refreshing_ || focus_>=draft_.count)return;
                draft_.blocks[focus_].values[i]=static_cast<float>(
                    sliders_[i].getValue());
                dirty_=true;repaint();
            };
            addAndMakeVisible(sliders_[i]);
        }
        status_.setJustificationType(juce::Justification::centredRight);
        status_.setColour(juce::Label::textColourId,
                          juce::Colour::fromRGB(213,170,112));
        addAndMakeVisible(status_);
        refreshWidgets();
        startTimerHz(15); // UI only, never a second audio timer/callback.
    }
    ~GuitarRigEditor() override {
        stopTimer();
        for(auto& slider:sliders_)slider.setLookAndFeel(nullptr);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(18,21,26));
        const auto gold=juce::Colour::fromRGB(211,159,94);
        const auto muted=juce::Colour::fromRGB(155,166,179);
        g.setColour(juce::Colour::fromRGB(33,39,46));
        g.fillRect(0,0,getWidth(),64);
        g.fillRect(0,64,getWidth(),92);
        g.fillRect(0,156,204,getHeight()-156);
        g.setColour(juce::Colour::fromRGB(58,64,71));
        g.drawLine(0.f,64.f,static_cast<float>(getWidth()),64.f,1.f);
        g.drawLine(0.f,156.f,static_cast<float>(getWidth()),156.f,1.f);
        g.drawLine(204.f,156.f,204.f,static_cast<float>(getHeight()),1.f);
        g.setColour(gold);
        g.setFont(juce::Font(juce::FontOptions(20.f,juce::Font::bold)));
        g.drawText("ArSonMix  /  GUITAR FX RIG",18,17,390,31,
                   juce::Justification::centredLeft);
        g.setColour(muted);
        g.setFont(juce::Font(juce::FontOptions(11.f)));
        g.drawText("TONE",18,77,162,25,juce::Justification::centredLeft);
        g.drawText("SIGNAL CHAIN  •  click hardware to focus",224,70,540,20,
                   juce::Justification::centredLeft);
        g.drawText("FACTORY TONES",18,177,171,24,
                   juce::Justification::centredLeft);
        // Focus shell deliberately uses only ONE major visual device.
        const auto stage=juce::Rectangle<int>(226,177,getWidth()-250,286);
        g.setColour(juce::Colour::fromRGB(36,39,44));
        g.fillRoundedRectangle(stage.toFloat(),12.f);
        g.setColour(juce::Colour::fromRGB(72,70,67));
        g.drawRoundedRectangle(stage.toFloat(),12.f,1.f);
        const auto hero=focus_==2?amp_:focus_==3?cab_:pedal_;
        const auto art=stage.reduced(20,20);
        if(hero.isValid())
            g.drawImageWithin(hero,art.getX(),art.getY(),art.getWidth(),
                art.getHeight(),juce::RectanglePlacement::centred);
        else {
            g.setColour(juce::Colour::fromRGB(71,62,54));
            g.fillRoundedRectangle(art.toFloat().reduced(25.f,14.f),15.f);
            g.setColour(gold);
            g.setFont(juce::Font(juce::FontOptions(26.f)));
            g.drawText(names_[focus_],art,juce::Justification::centred);
        }
        // Bitmap artwork never contains parameter legends or live values.
        g.setColour(gold);
        g.setFont(juce::Font(juce::FontOptions(14.f)));
        g.drawText(names_[focus_],246,184,290,27,
                   juce::Justification::centredLeft);
        g.setColour(muted);
        g.setFont(juce::Font(juce::FontOptions(12.f)));
        for(std::size_t i=0;i<activeControlCount();++i){
            const int cx=300+static_cast<int>(i)*188;
            g.drawText(controlNames_[focus_][i],cx-40,549,110,22,
                       juce::Justification::centred);
            g.setColour(gold);
            g.drawText(juce::String(draft_.blocks[focus_].values[i]*100.f,0)+"%",
                       cx-40,570,110,18,juce::Justification::centred);
            g.setColour(muted);
        }
        if(focus_==3){
            g.setFont(juce::Font(juce::FontOptions(12.f)));
            g.drawText("ORIGINAL 65-TAP CABINET FIR  ·  32-SAMPLE DSP PDC",
                       305,495,570,29,juce::Justification::centred);
        }
        g.setColour(juce::Colour::fromRGB(51,57,65));
        g.fillRect(205,getHeight()-33,getWidth()-205,33);
        g.setColour(muted);
        const auto serial=processor_.observedBlockSerial();
        const bool live=serial>0 && nowMs_-lastBlockMs_<900;
        g.drawText(live?"PCM CALLBACK OBSERVED  ·  audio input not independently verified":
                   "PCM NOT OBSERVED  ·  no simulated audio meters",
                   225,getHeight()-31,620,28,juce::Justification::centredLeft);
        g.setColour(live?juce::Colour::fromRGB(98,197,141):
                         juce::Colour::fromRGB(108,113,119));
        g.fillEllipse(static_cast<float>(getWidth()-42),
                      static_cast<float>(getHeight()-23),10.f,10.f);
    }
    void resized() override {
        preset_.setBounds(getWidth()-280,16,258,33);
        status_.setBounds(16,207,175,46);
        for(std::size_t i=0;i<4;++i){
            const int x=228+static_cast<int>(i)*199;
            focusButtons_[i].setBounds(x,104,151,39);
            power_[i].setBounds(x+155,108,38,30);
        }
        for(std::size_t i=0;i<3;++i)
            sliders_[i].setBounds(259+static_cast<int>(i)*188,473,116,80);
    }
private:
    void timerCallback() override {
        processor_.serviceCompletions();
        const auto receipt=processor_.lastCompletion();
        if(waitingId_ && receipt.requestId==waitingId_){
            if(receipt.applied){
                accepted_=receipt.program;
            }else{
                dirty_=true; // retain unsent draft after failed/cancelled command
            }
            waitingId_=0;
        }
        if(dirty_&&!waitingId_&&processor_.isReady()&&
           processor_.observedBlockSerial()>0) {
            const auto id=processor_.nextRequestId();
            if(id && processor_.queueProgram(draft_,id)){
                waitingId_=id;dirty_=false;
            }
        }
        const auto serial=processor_.observedBlockSerial();
        nowMs_=juce::Time::getMillisecondCounter();
        if(serial!=lastSerial_){lastSerial_=serial;lastBlockMs_=nowMs_;}
        juce::String message;
        if(waitingId_)message="APPLYING  ·  awaiting PCM ACK";
        else if(dirty_)message="PENDING  ·  no live PCM";
        else message=(nowMs_-lastBlockMs_<900)?"DSP ACCEPTED":"DSP OFFLINE";
        status_.setText(message,juce::dontSendNotification);
        repaint();
    }
    void loadArt() {
#if defined(ARMIX_GFX_EMBED_PNG_SKINS)
        amp_=juce::ImageFileFormat::loadFrom(
            BinaryData::amp_head_class_a_png,
            static_cast<std::size_t>(BinaryData::amp_head_class_a_pngSize));
        cab_=juce::ImageFileFormat::loadFrom(
            BinaryData::cabinet_dual_12_png,
            static_cast<std::size_t>(BinaryData::cabinet_dual_12_pngSize));
        pedal_=juce::ImageFileFormat::loadFrom(
            BinaryData::pedal_overdrive_png,
            static_cast<std::size_t>(BinaryData::pedal_overdrive_pngSize));
        knob_=juce::ImageFileFormat::loadFrom(
            BinaryData::knob_brushed_metal_png,
            static_cast<std::size_t>(BinaryData::knob_brushed_metal_pngSize));
#endif
    }
    std::size_t activeControlCount() const noexcept {
        return focus_==2?3u:focus_==3?0u:1u;
    }
    void refreshWidgets(){
        refreshing_=true;
        for(std::size_t i=0;i<4;++i){
            power_[i].setButtonText(draft_.blocks[i].enabled?"ON":"OFF");
            focusButtons_[i].setColour(juce::TextButton::buttonColourId,
                i==focus_?juce::Colour::fromRGB(93,67,43):
                          juce::Colour::fromRGB(49,55,61));
        }
        for(std::size_t i=0;i<3;++i){
            sliders_[i].setVisible(i<activeControlCount());
            sliders_[i].setValue(draft_.blocks[focus_].values[i],
                                  juce::dontSendNotification);
        }
        refreshing_=false;
    }
    void choosePreset(int id){
        if(id<1||id>4)return;
        Program next=armix::guitarfx::GuitarRigCore::startingProgram();
        const auto level=static_cast<std::size_t>(2);
        switch(id){
          case 1:next.blocks[level].values[0]=0.20f;break;
          case 2:next.blocks[level].values[0]=0.43f;
                 next.blocks[1].enabled=true;
                 next.blocks[1].values[0]=0.22f;break;
          case 3:next.blocks[level].values[0]=0.62f;
                 next.blocks[1].enabled=true;
                 next.blocks[1].values[0]=0.45f;break;
          case 4:next.blocks[level].values[0]=0.91f;
                 next.blocks[1].enabled=true;
                 next.blocks[1].values[0]=0.73f;break;
        }
        if(armix::guitarfx::validate(next)!=armix::guitarfx::ProgramError::none)
            return;
        draft_=next;
        dirty_=true;
        refreshWidgets();repaint();
    }
    GuitarRigProcessor& processor_;
    HardwareKnobs knobsLook_{};
    juce::Image amp_,cab_,pedal_,knob_;
    juce::ComboBox preset_;
    juce::Label status_;
    std::array<juce::TextButton,4> focusButtons_{};
    std::array<juce::TextButton,4> power_{};
    std::array<juce::Slider,3> sliders_{};
    Program draft_{},accepted_{};
    std::size_t focus_=2;
    std::uint64_t waitingId_=0,lastSerial_=0;
    juce::uint32 nowMs_=0,lastBlockMs_=0;
    bool refreshing_=false,dirty_=false;
    static constexpr std::array<const char*,4> names_{
        "GATE","OVERDRIVE","AMPLIFIER","CABINET"};
    static constexpr std::array<std::array<const char*,3>,4> controlNames_{{
        {"THRESHOLD","",""},
        {"DRIVE","",""},
        {"GAIN","TONE","MASTER"},
        {"","",""}
    }};
};
inline juce::AudioProcessorEditor* GuitarRigProcessor::createEditor(){
    return new GuitarRigEditor(*this);
}
} // namespace armix::builtin_fx
