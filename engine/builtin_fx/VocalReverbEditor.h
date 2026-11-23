// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "VocalReverbProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>

// An original ArSonMix instrument faceplate, geometry inspired by the
// user's compact HD-CART reference. No LiquidSonics art, trademarks, logos,
// screenshots or images are embedded. One native JUCE editor only.
namespace armix::builtin_fx {
namespace reverb_ui {
inline juce::Colour rgb(int r,int g,int b) {return juce::Colour::fromRGB(
    static_cast<juce::uint8>(r),static_cast<juce::uint8>(g),
    static_cast<juce::uint8>(b));}
constexpr float pi=juce::MathConstants<float>::pi;

class DialLook final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,
                          float value,float start,float end,juce::Slider&) override {
        const float diameter=static_cast<float>(std::min(width,height));
        const float cx=x+width*.5f;
        const float cy=y+height*.5f;
        const bool hero=diameter>=130.f;
        const float radius=diameter*(hero?.397f:.348f);
        const float ringRadius=radius+(hero?13.f:9.f);
        const float thickness=hero?3.2f:2.7f;
        const auto angle=start+(end-start)*value;
        g.setColour(rgb(5,9,14).withAlpha(.77f));
        g.fillEllipse(cx-radius-7.f,cy-radius-5.f,(radius+7.f)*2.f,(radius+7.f)*2.f);
        g.setColour(rgb(52,69,82));
        g.drawEllipse(cx-radius-4.f,cy-radius-4.f,(radius+4.f)*2.f,(radius+4.f)*2.f,1.f);
        juce::Path track;
        track.addCentredArc(cx,cy,ringRadius,ringRadius,0.f,start,end,true);
        g.setColour(rgb(39,57,70));
        g.strokePath(track,juce::PathStrokeType(thickness,juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
        juce::Path active;
        active.addCentredArc(cx,cy,ringRadius,ringRadius,0.f,start,angle,true);
        g.setColour(rgb(56,152,197).withAlpha(.28f));
        g.strokePath(active,juce::PathStrokeType(thickness+7.f,juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour(rgb(102,205,246));
        g.strokePath(active,juce::PathStrokeType(thickness,juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        const int tickCount=hero?38:25;
        for(int i=0;i<=tickCount;++i) {
            const float p=static_cast<float>(i)/static_cast<float>(tickCount);
            const float a=start+(end-start)*p;
            const float dot=hero?2.2f:1.65f;
            const float tx=cx+std::sin(a)*(ringRadius+3.f);
            const float ty=cy-std::cos(a)*(ringRadius+3.f);
            g.setColour(p<=value?rgb(130,217,255):rgb(45,62,77));
            g.fillEllipse(tx-dot*.5f,ty-dot*.5f,dot,dot);
        }
        const auto face=juce::Rectangle<float>(cx-radius,cy-radius,radius*2.f,radius*2.f);
        g.setGradientFill(juce::ColourGradient(rgb(71,78,85),
            face.getX(),face.getY(),rgb(27,31,38),
            face.getRight(),face.getBottom(),false));
        g.fillEllipse(face);
        g.setColour(rgb(98,110,118).withAlpha(.75f));
        g.drawEllipse(face.reduced(1.f),1.f);
        g.setColour(rgb(8,12,17).withAlpha(.8f));
        g.drawEllipse(face.reduced(4.f),2.f);
        g.setColour(rgb(108,128,146).withAlpha(.28f));
        juce::Path bevel;
        bevel.addCentredArc(cx,cy,radius*.8f,radius*.8f,0.f,
                            -2.25f,-.60f,true);
        g.strokePath(bevel,juce::PathStrokeType(1.2f));
        const float pointerR=radius*.74f;
        const float px=cx+std::sin(angle)*pointerR;
        const float py=cy-std::cos(angle)*pointerR;
        g.setColour(rgb(210,228,237));
        g.fillEllipse(px-(hero?3.3f:2.8f),py-(hero?3.3f:2.8f),
                      hero?6.6f:5.6f,hero?6.6f:5.6f);
        g.setColour(rgb(7,14,20).withAlpha(.65f));
        g.drawEllipse(px-2.f,py-2.f,4.f,4.f,1.f);
    }
};
} // namespace reverb_ui

class VocalReverbEditor final:public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit VocalReverbEditor(VocalReverbProcessor& processor)
        :juce::AudioProcessorEditor(processor),processor_(processor) {
        setOpaque(true);
        if(!processor_.acceptedSettings(draft_))draft_=vocalReverbFactoryPreset(0);
        const auto pale=reverb_ui::rgb(209,224,233);
        const auto cyan=reverb_ui::rgb(107,201,243);
        title_.setText("ARSONMIX  /  VOCAL REVERB",juce::dontSendNotification);
        title_.setFont(juce::Font(juce::FontOptions(17.f)));
        title_.setColour(juce::Label::textColourId,pale);
        addAndMakeVisible(title_);

        status_.setFont(juce::Font(juce::FontOptions(11.f)));
        status_.setColour(juce::Label::textColourId,reverb_ui::rgb(139,219,194));
        status_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(status_);

        for(int i=0;i<8;++i)
            preset_.addItem(vocalReverbPresetNames[static_cast<std::size_t>(i)],i+1);
        preset_.addItem("Custom / saved",9);
        preset_.setSelectedId(draft_.preset>=0?draft_.preset+1:9,juce::dontSendNotification);
        preset_.setColour(juce::ComboBox::backgroundColourId,reverb_ui::rgb(12,29,44));
        preset_.setColour(juce::ComboBox::outlineColourId,reverb_ui::rgb(41,85,108));
        preset_.setColour(juce::ComboBox::textColourId,pale);
        preset_.setColour(juce::ComboBox::arrowColourId,cyan);
        preset_.setJustificationType(juce::Justification::centred);
        preset_.onChange=[this] {
            const auto i=preset_.getSelectedId();
            if(i>=1&&i<=8) {
                draft_=vocalReverbFactoryPreset(i-1);
                syncControls();
                dirty_=true;
            }
        };
        addAndMakeVisible(preset_);
        for(int i=0;i<4;++i)
            algorithm_.addItem(arverb::algorithmName(static_cast<arverb::Algorithm>(i)),i+1);
        algorithm_.setSelectedId(draft_.algorithm+1,juce::dontSendNotification);
        algorithm_.setColour(juce::ComboBox::backgroundColourId,reverb_ui::rgb(22,30,40));
        algorithm_.setColour(juce::ComboBox::outlineColourId,reverb_ui::rgb(48,68,82));
        algorithm_.setColour(juce::ComboBox::textColourId,pale);
        algorithm_.setColour(juce::ComboBox::arrowColourId,cyan);
        algorithm_.onChange=[this] {
            const int i=algorithm_.getSelectedId()-1;
            if(i>=0&&i<4){draft_.algorithm=i;markCustom();}
        };
        addAndMakeVisible(algorithm_);

        auto setupArrow=[this,cyan](juce::TextButton& b,int step) {
            b.setButtonText(step<0?"<":">");
            b.setColour(juce::TextButton::buttonColourId,reverb_ui::rgb(9,29,44));
            b.setColour(juce::TextButton::buttonOnColourId,reverb_ui::rgb(25,56,70));
            b.setColour(juce::TextButton::textColourOffId,cyan);
            b.onClick=[this,step]{selectRelativePreset(step);};
            addAndMakeVisible(b);
        };
        setupArrow(prev_,-1);setupArrow(next_,1);

        constexpr std::array<const char*,4> tabNames{{
            "MIX","SPACE","TONE","MOTION"
        }};
        for(std::size_t i=0;i<tabs_.size();++i) {
            auto& button=tabs_[i];
            button.setButtonText(tabNames[i]);
            button.setColour(juce::TextButton::buttonColourId,reverb_ui::rgb(27,33,41));
            button.setColour(juce::TextButton::buttonOnColourId,reverb_ui::rgb(17,48,65));
            button.setColour(juce::TextButton::textColourOffId,reverb_ui::rgb(165,180,193));
            button.setColour(juce::TextButton::textColourOnId,cyan);
            button.setClickingTogglesState(false);
            button.onClick=[this,i]{page_=static_cast<int>(i);layoutPage();};
            addAndMakeVisible(button);
        }
        for(std::size_t i=0;i<knobs_.size();++i) {
            auto& knob=knobs_[i];
            const auto& d=arverb::parameterInfo(static_cast<arverb::Parameter>(i));
            knob.setLookAndFeel(&dial_);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,84,20);
            knob.setRange(d.minimum,d.maximum,0.);
            knob.setValue(draft_.values[i],juce::dontSendNotification);
            knob.setDoubleClickReturnValue(true,d.defaultValue);
            knob.setColour(juce::Slider::textBoxTextColourId,reverb_ui::rgb(154,214,247));
            knob.setColour(juce::Slider::textBoxBackgroundColourId,reverb_ui::rgb(7,21,32));
            knob.setColour(juce::Slider::textBoxOutlineColourId,reverb_ui::rgb(30,60,76));
            knob.onValueChange=[this,i]{
                draft_.values[i]=static_cast<float>(knobs_[i].getValue());
                markCustom();
            };
            addAndMakeVisible(knob);
            labels_[i].setText(juce::String(d.name).toUpperCase(),juce::dontSendNotification);
            labels_[i].setFont(juce::Font(juce::FontOptions(11.f)));
            labels_[i].setColour(juce::Label::textColourId,reverb_ui::rgb(195,202,208));
            labels_[i].setJustificationType(juce::Justification::centred);
            addAndMakeVisible(labels_[i]);
        }
        labels_[0].setText("DRY / WET MIX",juce::dontSendNotification);
        labels_[2].setText("REVERB TIME",juce::dontSendNotification);
        setSize(1120,430);
        layoutPage();
        startTimerHz(30);
    }
    ~VocalReverbEditor() override {
        stopTimer();
        for(auto& k:knobs_)k.setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        using namespace reverb_ui;
        const float w=static_cast<float>(getWidth());
        const float h=static_cast<float>(getHeight());
        g.fillAll(rgb(13,13,16));
        // Dark cabinet bevel with a deeply blue top recess.
        g.setGradientFill(juce::ColourGradient(rgb(52,56,61),0.f,50.f,
                                                rgb(18,20,26),w,h,false));
        g.fillRoundedRectangle({9.f,9.f,w-18.f,h-18.f},7.f);
        g.setColour(rgb(77,84,89).withAlpha(.6f));
        g.drawRoundedRectangle({9.f,9.f,w-18.f,h-18.f},7.f,1.f);
        g.setGradientFill(juce::ColourGradient(rgb(11,37,56),0.f,9.f,
                                                rgb(5,24,42),0.f,61.f,false));
        g.fillRoundedRectangle({11.f,11.f,w-22.f,58.f},5.f);
        g.setColour(rgb(58,117,144).withAlpha(.75f));
        g.drawLine(11.f,68.f,w-11.f,68.f,1.f);
        g.setGradientFill(juce::ColourGradient(rgb(43,46,51),0.f,75.f,
                                                rgb(23,26,33),0.f,355.f,false));
        g.fillRect(11.f,70.f,w-22.f,285.f);
        // Quiet lower LCD bar, exactly aligned with real knob readouts.
        g.setGradientFill(juce::ColourGradient(rgb(11,43,66),0.f,354.f,
                                                rgb(5,27,47),0.f,407.f,false));
        g.fillRect(11.f,355.f,w-22.f,52.f);
        g.setColour(rgb(40,99,128));g.drawLine(11.f,355.f,w-11.f,355.f);
        g.setColour(rgb(16,20,25));g.fillRect(11.f,408.f,w-22.f,9.f);
        g.setColour(rgb(75,100,118).withAlpha(.65f));
        g.drawLine(246.f,152.f,246.f,345.f,.7f);
        g.drawLine(875.f,152.f,875.f,345.f,.7f);
        g.setColour(rgb(131,163,178));g.setFont(juce::Font(juce::FontOptions(11.f)));
        g.drawText("NATIVE  |  ZERO-ADDED LATENCY",33,127,220,16,
                   juce::Justification::centredLeft,false);
        g.setFont(juce::Font(juce::FontOptions(10.f)));
        g.drawText("INPUT  L / R",365,98,90,16,juce::Justification::centredLeft,false);
        g.drawText("OUTPUT L / R",365,123,90,16,juce::Justification::centredLeft,false);
        drawMeters(g);
        g.setColour(rgb(165,185,195));
        g.setFont(juce::Font(juce::FontOptions(10.f)));
        g.drawText("DSP  ·  STEREO  ·  " + juce::String(arverb::algorithmName(
            static_cast<arverb::Algorithm>(draft_.algorithm))).toUpperCase(),
            37,370,295,22,juce::Justification::centredLeft,false);
        g.drawText("SING  /  LIVE  /  RECORD",875,370,198,22,
                   juce::Justification::centredRight,false);
        g.setColour(rgb(93,175,214).withAlpha(.64f));
        g.drawLine(276.f,205.f,844.f,205.f,1.f);
        g.setFont(juce::Font(juce::FontOptions(11.f)));
        g.setColour(rgb(107,201,243));
        g.drawText(pageDescription(),292,337,524,16,juce::Justification::centred,false);
    }

    void resized() override {
        title_.setBounds(37,78,310,34);
        preset_.setBounds(432,20,256,32);
        prev_.setBounds(383,18,37,36);
        next_.setBounds(700,18,37,36);
        algorithm_.setBounds(910,103,162,30);
        status_.setBounds(789,368,78,23);
        for(std::size_t i=0;i<tabs_.size();++i)
            tabs_[i].setBounds(299+static_cast<int>(i)*142,168,133,31);
        layoutPage();
    }

private:
    static constexpr std::array<std::array<int,5>,4> pages_{{
        {{1,11,10,6,7}},  // vocal live essentials: predelay, early, width, filters
        {{1,3,4,11,10}},  // geometry, density and stereo field
        {{5,6,7,11,10}},  // HF decay + wet EQ
        {{8,9,12,3,4}}   // modulation and automatic vocal clarity
    }};
    juce::String pageDescription() const {
        switch(page_) {
            case 0:return "PRE-DELAY       EARLY ENERGY       STEREO FIELD       BODY       AIR";
            case 1:return "GEOMETRY       ROOM SIZE       DENSITY       REFLECTIONS       WIDTH";
            case 2:return "HF DAMPING       LOW CUT       HIGH CUT       EARLY       WIDTH";
            default:return "MOD DEPTH       MOD SPEED       VOCAL DUCK       SIZE       DIFFUSION";
        }
    }
    void selectRelativePreset(int delta) {
        const int current=draft_.preset<0?0:draft_.preset;
        const int index=(current+delta+8)%8;
        draft_=vocalReverbFactoryPreset(index);
        preset_.setSelectedId(index+1,juce::dontSendNotification);
        syncControls();
        dirty_=true;
    }
    void markCustom() {
        draft_.preset=-1;
        preset_.setSelectedId(9,juce::dontSendNotification);
        dirty_=true;
        repaint(juce::Rectangle<int>(29,363,842,34));
    }
    void syncControls() {
        algorithm_.setSelectedId(draft_.algorithm+1,juce::dontSendNotification);
        for(std::size_t i=0;i<knobs_.size();++i)
            knobs_[i].setValue(draft_.values[i],juce::dontSendNotification);
        repaint();
    }
    void layoutPage() {
        for(std::size_t i=0;i<knobs_.size();++i) {
            knobs_[i].setVisible(false);
            labels_[i].setVisible(false);
        }
        // Two hero controls remain visible across every tab, like HD-CART.
        knobs_[2].setBounds(48,175,185,180);
        labels_[2].setBounds(43,151,195,21);
        knobs_[0].setBounds(889,175,184,180);
        labels_[0].setBounds(882,151,199,21);
        knobs_[2].setVisible(true);labels_[2].setVisible(true);
        knobs_[0].setVisible(true);labels_[0].setVisible(true);
        for(std::size_t n=0;n<5;++n) {
            const auto idx=static_cast<std::size_t>(
                pages_[static_cast<std::size_t>(page_)][n]);
            const int x=278+static_cast<int>(n)*115;
            knobs_[idx].setBounds(x,227,105,108);
            labels_[idx].setBounds(x-6,207,118,18);
            knobs_[idx].setVisible(true);
            labels_[idx].setVisible(true);
        }
        for(std::size_t i=0;i<tabs_.size();++i)
            tabs_[i].setToggleState(static_cast<int>(i)==page_,juce::dontSendNotification);
        repaint();
    }
    void drawMeters(juce::Graphics& g) {
        using namespace reverb_ui;
        for(int ch=0;ch<4;++ch) {
            const float peak=displayPeaks_[static_cast<std::size_t>(ch)];
            const float db=peak<=1.e-6f?-60.f:20.f*std::log10(peak);
            const int lit=static_cast<int>(juce::jlimit(0.f,20.f,
                                                       (db+60.f)/66.f*20.f));
            const int x=462+(ch%2)*244;
            const int y=102+(ch/2)*26;
            for(int s=0;s<20;++s) {
                const bool on=s<lit;
                const auto bright=s>=17?rgb(241,144,113):
                    (s>=14?rgb(220,193,113):rgb(109,200,241));
                g.setColour(on?bright:rgb(24,40,52));
                g.fillEllipse(static_cast<float>(x+s*10),static_cast<float>(y),6.f,6.f);
                if(on) {
                    g.setColour(bright.withAlpha(.24f));
                    g.drawEllipse(static_cast<float>(x+s*10)-1.f,
                                  static_cast<float>(y)-1.f,8.f,8.f,1.f);
                }
            }
        }
    }
    void timerCallback() override {
        if(inFlight_ && processor_.appliedRequestId()>=inFlightId_)inFlight_=false;
        if(!processor_.isReady()) {
            status_.setText("OFFLINE",juce::dontSendNotification);
            displayPeaks_.fill(0.f);repaint(juce::Rectangle<int>(450,95,465,50));
            return;
        }
        if(dirty_&&!inFlight_) {
            const auto id=processor_.queueSettings(draft_);
            if(id){inFlightId_=id;inFlight_=true;dirty_=false;}
        }
        status_.setText(inFlight_?"APPLYING":(dirty_?"QUEUED":"PCM OK"),
                        juce::dontSendNotification);
        const auto fresh=processor_.meterPeaks();
        for(std::size_t i=0;i<displayPeaks_.size();++i)
            displayPeaks_[i]=std::max(fresh[i],displayPeaks_[i]*.82f);
        repaint(juce::Rectangle<int>(450,95,465,52));
    }
    VocalReverbProcessor& processor_;
    VocalReverbSettings draft_;
    reverb_ui::DialLook dial_;
    juce::Label title_,status_;
    juce::ComboBox preset_,algorithm_;
    juce::TextButton prev_,next_;
    std::array<juce::TextButton,4> tabs_;
    std::array<juce::Slider,arverb::parameterCount> knobs_;
    std::array<juce::Label,arverb::parameterCount> labels_;
    std::array<float,4> displayPeaks_{};
    std::uint64_t inFlightId_=0;
    int page_=0;
    bool inFlight_=false,dirty_=false;
};
inline juce::AudioProcessorEditor* VocalReverbProcessor::createEditor() {
    return new VocalReverbEditor(*this);
}
} // namespace armix::builtin_fx
