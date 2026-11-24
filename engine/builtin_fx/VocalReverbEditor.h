// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "VocalReverbProcessor.h"
#if defined(ARMIX_REVERB_INTER_EMBEDDED)
#include "ARMixReverbInterBinary.h"
#endif
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
// Production and Windows QA bundle the unmodified OFL-licensed Inter font.
// Font objects are constructed exclusively on the JUCE message/UI thread.
struct MeterLayout final {
    static constexpr int groupLeft=250, groupRight=870;
    static constexpr int leftLabelX=250, leftLabelWidth=93;
    static constexpr int dotsLeftX=355, dotsRightX=569;
    static constexpr int rightLabelX=777, rightLabelWidth=93;
    static constexpr int firstDotY=113, secondDotY=148;
    static constexpr int segments=20, pitch=10, dotSize=6;
    static constexpr int labelOpticalLift=2, labelHeight=20;
    static constexpr int bankWidth=(segments-1)*pitch+dotSize;
    static constexpr int floorDb=-60, ceilingDb=0;
    static constexpr int glowMargin=2;
    static constexpr std::array<const char*,2> leftLabels{{"INPUT L","OUTPUT L"}};
    static constexpr std::array<const char*,2> rightLabels{{"INPUT R","OUTPUT R"}};

    // One geometry authority for both paint and timer invalidation.
    static juce::Rectangle<int> dirtyBounds() noexcept {
        return {dotsLeftX-glowMargin,firstDotY-glowMargin,
                dotsRightX+bankWidth-dotsLeftX+2*glowMargin,
                secondDotY+dotSize-firstDotY+2*glowMargin};
    }
    static int litCount(float db) noexcept {
        return static_cast<int>(juce::jlimit(0.f,static_cast<float>(segments),
            (db-floorDb)*segments/static_cast<float>(ceilingDb-floorDb)));
    }
    // Each LED represents the upper threshold of its 3 dB cell.
    static constexpr int tickOffset(int db) noexcept {
        const int index=(db-floorDb)*segments/(ceilingDb-floorDb)-1;
        return (index<0?0:(index>=segments?segments-1:index))*pitch+dotSize/2;
    }
};
static_assert(MeterLayout::groupLeft+MeterLayout::groupRight==1120);
static_assert(MeterLayout::dotsLeftX+MeterLayout::dotsRightX+
              MeterLayout::bankWidth==1120);
static_assert(MeterLayout::leftLabelWidth==MeterLayout::rightLabelWidth);
static_assert(MeterLayout::dotsLeftX-
    (MeterLayout::leftLabelX+MeterLayout::leftLabelWidth)==12);
static_assert(MeterLayout::rightLabelX-
    (MeterLayout::dotsRightX+19*MeterLayout::pitch+MeterLayout::dotSize)==12);
inline juce::Font uiFont(float size) {
#if defined(ARMIX_REVERB_INTER_EMBEDDED)
    static juce::Typeface::Ptr inter=juce::Typeface::createSystemTypefaceFor(
        ARMixReverbInter::InterRegular_ttf,
        static_cast<std::size_t>(ARMixReverbInter::InterRegular_ttfSize));
    if(inter!=nullptr)
        return juce::Font(juce::FontOptions(size).withTypeface(inter));
#endif
    return juce::Font(juce::FontOptions(size).withName("Inter"));
}


class DialLook final : public juce::LookAndFeel_V4 {
public:
    // Hardware-inspired controls are drawn vectorially at every DPI.
    // This is original ArSonMix artwork, not HD-CART proprietary bitmaps.
    void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,
                          float value,float start,float end,juce::Slider&) override {
        const float size=static_cast<float>(std::min(width,height));
        const float cx=x+width*.5f, cy=y+height*.5f;
        const bool hero=size>=138.f;
        const float faceR=size*(hero?.366f:.338f);
        const float bezelR=faceR+(hero?8.f:5.f);
        const float ledR=bezelR+(hero?13.f:9.f);
        const float a=start+(end-start)*juce::jlimit(0.f,1.f,value);
        // Cabinet inset; deliberately small, smooth overlapping concentric
        // layers instead of image textures loaded while the audio is running.
        g.setColour(rgb(3,6,11).withAlpha(.60f));
        g.fillEllipse(cx-bezelR-4.f,cy-bezelR-2.f,
                      2.f*(bezelR+4.f),2.f*(bezelR+4.f));
        g.setColour(rgb(12,14,19));
        g.fillEllipse(cx-bezelR,cy-bezelR,2.f*bezelR,2.f*bezelR);
        g.setColour(rgb(94,104,114).withAlpha(.70f));
        g.drawEllipse(cx-bezelR+1.f,cy-bezelR+1.f,
                      (bezelR-1.f)*2.f,(bezelR-1.f)*2.f,1.f);
        g.setColour(rgb(7,10,15));
        g.drawEllipse(cx-faceR-2.f,cy-faceR-2.f,
                      (faceR+2.f)*2.f,(faceR+2.f)*2.f,2.8f);

        // HD-CART-inspired short separated segmented LEDs, not a continuous
        // generic Slider ring. The active segment count is data-driven.
        const int steps=hero?42:25;
        const float inner=ledR-(hero?3.8f:2.5f);
        const float outer=ledR+(hero?3.8f:2.5f);
        for(int i=0;i<steps;++i) {
            const float phase=(static_cast<float>(i)+.5f)/static_cast<float>(steps);
            const float angle=start+(end-start)*phase;
            const float s=std::sin(angle),c=std::cos(angle);
            const bool lit=phase<=value;
            const auto bright=lit?rgb(122,210,250):rgb(45,62,78);
            if(lit) {
                g.setColour(bright.withAlpha(.24f));
                g.drawLine(cx+s*(inner-2.f),cy-c*(inner-2.f),
                           cx+s*(outer+2.f),cy-c*(outer+2.f),hero?6.5f:4.f);
            }
            g.setColour(bright.withAlpha(lit?.98f:.82f));
            g.drawLine(cx+s*inner,cy-c*inner,cx+s*outer,cy-c*outer,
                       hero?3.f:2.f);
        }

        const juce::Rectangle<float> face(cx-faceR,cy-faceR,faceR*2.f,faceR*2.f);
        g.setGradientFill(juce::ColourGradient(rgb(73,78,85),
                    face.getX()+8.f,face.getY()+3.f,
                    rgb(23,25,31),face.getRight(),face.getBottom(),false));
        g.fillEllipse(face);
        g.setColour(rgb(122,133,143).withAlpha(.62f));
        g.drawEllipse(face.reduced(.7f),1.f);
        const auto recessed=face.reduced(hero?4.f:2.8f);
        g.setGradientFill(juce::ColourGradient(rgb(65,70,76),
                    recessed.getCentreX()*.92f,recessed.getY(),
                    rgb(27,30,36),recessed.getRight(),recessed.getBottom(),false));
        g.fillEllipse(recessed);
        g.setColour(rgb(18,21,27));
        g.drawEllipse(recessed.reduced(1.f),2.f);
        juce::Path light;
        light.addCentredArc(cx,cy,faceR*.81f,faceR*.81f,0.f,
                            -2.25f,-.62f,true);
        g.setColour(rgb(176,189,197).withAlpha(.17f));
        g.strokePath(light,juce::PathStrokeType(hero?1.6f:1.f));
        // A real moving index mark, centred on the actual JUCE parameter.
        const float px=cx+std::sin(a)*faceR*.74f;
        const float py=cy-std::cos(a)*faceR*.74f;
        g.setColour(rgb(9,12,16));
        g.fillEllipse(px-3.8f,py-3.8f,7.6f,7.6f);
        g.setColour(rgb(226,232,235));
        const float dot=hero?4.7f:3.8f;
        g.fillEllipse(px-dot*.5f,py-dot*.5f,dot,dot);
    }

    void drawComboBox(juce::Graphics& g,int width,int height,bool pressed,
                      int,int,int,int,juce::ComboBox& box) override {
        const bool preset=box.getComponentID()=="armix.reverb.preset";
        if(preset) {
            g.setColour(rgb(10,31,48).withAlpha(.24f));
            g.fillRoundedRectangle({0.f,0.f,static_cast<float>(width),
                                    static_cast<float>(height)},3.f);
            g.setColour(rgb(94,173,209).withAlpha(.35f));
            g.drawLine(4.f,static_cast<float>(height)-1.f,
                       static_cast<float>(width)-4.f,static_cast<float>(height)-1.f,1.f);
        } else {
            g.setGradientFill(juce::ColourGradient(rgb(32,39,47),0.f,0.f,
                    rgb(12,18,26),0.f,static_cast<float>(height),false));
            g.fillRoundedRectangle({0.f,0.f,static_cast<float>(width),
                                    static_cast<float>(height)},3.f);
            g.setColour(rgb(72,97,112));
            g.drawRoundedRectangle({.5f,.5f,static_cast<float>(width)-1.f,
                                    static_cast<float>(height)-1.f},3.f,1.f);
        }
        const float cx=width-15.f, cy=height*.50f;
        juce::Path chevron;
        chevron.startNewSubPath(cx-4.f,cy-2.f);
        chevron.lineTo(cx,cy+2.f);
        chevron.lineTo(cx+4.f,cy-2.f);
        g.setColour(pressed?rgb(228,245,251):rgb(111,193,228));
        g.strokePath(chevron,juce::PathStrokeType(1.5f));
    }
    juce::Font getComboBoxFont(juce::ComboBox& box) override {
        return uiFont(box.getComponentID()=="armix.reverb.preset"?14.f:12.f);
    }
    juce::Font getLabelFont(juce::Label& label) override {
        return uiFont(juce::jlimit(10.f,25.f,label.getFont().getHeight()));
    }
    juce::Font getPopupMenuFont() override {return uiFont(12.f);}
    void positionComboBoxText(juce::ComboBox& box,juce::Label& text) override {
        // Text and chevron share the exact vertical centre and safe padding.
        text.setBounds(9,0,juce::jmax(24,box.getWidth()-40),box.getHeight());
        text.setFont(getComboBoxFont(box));
        text.setJustificationType(box.getComponentID()=="armix.reverb.preset"?
            juce::Justification::centred:juce::Justification::centredLeft);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,
                              const juce::Colour&,bool hover,bool down) override {
        const auto id=b.getComponentID();
        const auto rect=b.getLocalBounds().toFloat();
        if(id.startsWith("armix.reverb.tab.")) {
            if(b.getToggleState()) {
                g.setColour(rgb(105,195,235));
                g.fillRect(rect.getX()+13.f,rect.getBottom()-2.f,
                           rect.getWidth()-26.f,2.f);
            } else if(hover||down) {
                g.setColour(rgb(102,162,191).withAlpha(.45f));
                g.fillRect(rect.getX()+13.f,rect.getBottom()-1.f,
                           rect.getWidth()-26.f,1.f);
            }
        } else if(id=="armix.reverb.preset.previous"||
                  id=="armix.reverb.preset.next") {
            // Lucide chevron-left/right, canonical 24x24 polyline,
            // recentered for native 33px buttons; no '<'/'>' type glyphs.
            if(hover||down) {
                g.setColour(rgb(65,120,155).withAlpha(.22f));
                g.fillRoundedRectangle(rect.reduced(3.f),4.f);
            }
            const float cx=rect.getCentreX(),cy=rect.getCentreY();
            const float sign=id.endsWith("next")?1.f:-1.f;
            juce::Path chevron;
            chevron.startNewSubPath(cx-sign*2.75f,cy-5.5f);
            chevron.lineTo(cx+sign*2.75f,cy);
            chevron.lineTo(cx-sign*2.75f,cy+5.5f);
            g.setColour(hover||down?rgb(174,227,253):rgb(121,196,234));
            g.strokePath(chevron,juce::PathStrokeType(1.8f,
                juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
        } else {
            juce::LookAndFeel_V4::drawButtonBackground(g,b,
                rgb(23,32,43),hover,down);
        }
    }
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,
                        bool hover,bool down) override {
        const auto id=b.getComponentID();
        if(id=="armix.reverb.preset.previous"||
           id=="armix.reverb.preset.next")return;
        const bool tab=id.startsWith("armix.reverb.tab.");
        const auto bright=(tab&&b.getToggleState())||
                          (!tab&&(hover||down));
        g.setColour(bright?rgb(142,216,250):rgb(158,166,177));
        g.setFont(uiFont(tab?11.f:14.f));
        g.drawText(b.getButtonText(),b.getLocalBounds(),
                   juce::Justification::centred,false);
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
        title_.setText("REVERB-SIS",juce::dontSendNotification);
        title_.setFont(reverb_ui::uiFont(22.f));
        title_.setColour(juce::Label::textColourId,pale);
        title_.setComponentID("armix.reverb.title");
        addAndMakeVisible(title_);

        for(int i=0;i<8;++i)
            preset_.addItem(vocalReverbPresetNames[static_cast<std::size_t>(i)],i+1);
        preset_.addItem("Custom / saved",9);
        preset_.setSelectedId(draft_.preset>=0?draft_.preset+1:9,juce::dontSendNotification);
        preset_.setColour(juce::ComboBox::backgroundColourId,reverb_ui::rgb(12,29,44));
        preset_.setColour(juce::ComboBox::outlineColourId,reverb_ui::rgb(41,85,108));
        preset_.setColour(juce::ComboBox::textColourId,pale);
        preset_.setColour(juce::ComboBox::arrowColourId,cyan);
        preset_.setJustificationType(juce::Justification::centred);
        preset_.setLookAndFeel(&dial_);
        preset_.onChange=[this] {
            const auto i=preset_.getSelectedId();
            if(i>=1&&i<=8) {
                draft_=vocalReverbFactoryPreset(i-1);
                syncControls();
                dirty_=true;
            }
        };
        preset_.setComponentID("armix.reverb.preset");
        addAndMakeVisible(preset_);
        for(int i=0;i<4;++i)
            algorithm_.addItem(arverb::algorithmName(static_cast<arverb::Algorithm>(i)),i+1);
        algorithm_.setSelectedId(draft_.algorithm+1,juce::dontSendNotification);
        algorithm_.setColour(juce::ComboBox::backgroundColourId,reverb_ui::rgb(22,30,40));
        algorithm_.setColour(juce::ComboBox::outlineColourId,reverb_ui::rgb(48,68,82));
        algorithm_.setColour(juce::ComboBox::textColourId,pale);
        algorithm_.setColour(juce::ComboBox::arrowColourId,cyan);
        algorithm_.setLookAndFeel(&dial_);
        algorithm_.onChange=[this] {
            const int i=algorithm_.getSelectedId()-1;
            if(i>=0&&i<4){draft_.algorithm=i;markCustom();}
        };
        algorithm_.setComponentID("armix.reverb.algorithm");
        addAndMakeVisible(algorithm_);

        auto setupArrow=[this,cyan](juce::TextButton& b,int step) {
            b.setButtonText(""); // icon is native Lucide chevron, not text
            b.setLookAndFeel(&dial_);
            b.setColour(juce::TextButton::buttonColourId,reverb_ui::rgb(9,29,44));
            b.setColour(juce::TextButton::buttonOnColourId,reverb_ui::rgb(25,56,70));
            b.setColour(juce::TextButton::textColourOffId,cyan);
            b.onClick=[this,step]{selectRelativePreset(step);};
            addAndMakeVisible(b);
        };
        setupArrow(prev_,-1);setupArrow(next_,1);
        prev_.setComponentID("armix.reverb.preset.previous");
        next_.setComponentID("armix.reverb.preset.next");

        constexpr std::array<const char*,6> tabNames{{
            "MIX","SHAPE","SPACE","TONE","MOTION","DUCKER"
        }};
        for(std::size_t i=0;i<tabs_.size();++i) {
            auto& button=tabs_[i];
            button.setLookAndFeel(&dial_);
            button.setComponentID("armix.reverb.tab."+juce::String(static_cast<int>(i)));
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
            knob.setComponentID("armix.reverb.param."+juce::String(static_cast<int>(i)));
            const auto& d=arverb::parameterInfo(static_cast<arverb::Parameter>(i));
            knob.setLookAndFeel(&dial_);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,
                 (i==0||i==2)?126:103,22);
            knob.setRange(d.minimum,d.maximum,0.);
            knob.setNumDecimalPlacesToDisplay((i==1||i==5||i==6||i==7)?0:2);
            // Source-unit exact, human-readable LCD values; editable text
            // round-trips into the SAME native parameter, never UI state.
            knob.textFromValueFunction=[i](double v) {
                return formatParameter(i,v);
            };
            knob.valueFromTextFunction=[i](const juce::String& text) {
                return parseParameter(i,text);
            };
            knob.setValue(draft_.values[i],juce::dontSendNotification);
            knob.setDoubleClickReturnValue(true,d.defaultValue);
            knob.setColour(juce::Slider::textBoxTextColourId,reverb_ui::rgb(147,215,250));
            knob.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colours::transparentBlack);
            knob.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
            knob.onValueChange=[this,i]{
                draft_.values[i]=static_cast<float>(knobs_[i].getValue());
                markCustom();
            };
            addAndMakeVisible(knob);
            labels_[i].setComponentID("armix.reverb.label."+juce::String(static_cast<int>(i)));
            constexpr std::array<const char*,13> shortLabels{{
                "MIX","PRE-DELAY","DECAY","SIZE","DIFFUSION",
                "DAMPING","LOW CUT","HIGH CUT","MOD DEPTH",
                "MOD RATE","WIDTH","EARLY LEVEL","DUCKER"
            }};
            labels_[i].setText(shortLabels[i],juce::dontSendNotification);
            labels_[i].setFont(reverb_ui::uiFont(12.f));
            labels_[i].setColour(juce::Label::textColourId,reverb_ui::rgb(195,202,208));
            labels_[i].setJustificationType(juce::Justification::centred);
            addAndMakeVisible(labels_[i]);
        }
        labels_[0].setText("Dry/Wet Mix",juce::dontSendNotification);
        labels_[2].setText("Reverb Time",juce::dontSendNotification);
        setSize(1120,430);
        layoutPage();
        processor_.attachMeterView();
        startTimerHz(30);
    }
    ~VocalReverbEditor() override {
        stopTimer();
        processor_.detachMeterView();
        for(auto& k:knobs_)k.setLookAndFeel(nullptr);
        for(auto& b:tabs_)b.setLookAndFeel(nullptr);
        prev_.setLookAndFeel(nullptr);
        next_.setLookAndFeel(nullptr);
        preset_.setLookAndFeel(nullptr);
        algorithm_.setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        using namespace reverb_ui;
        const float w=static_cast<float>(getWidth());
        const float h=static_cast<float>(getHeight());
        g.fillAll(rgb(11,10,15));
        const juce::Rectangle<float> cabinet{9.f,9.f,w-18.f,h-18.f};
        g.setColour(rgb(2,4,9));g.fillRoundedRectangle(cabinet.expanded(1.f),6.f);
        g.setGradientFill(juce::ColourGradient(rgb(38,44,50),9.f,9.f,
                  rgb(10,12,18),w-9.f,h-9.f,false));
        g.fillRoundedRectangle(cabinet,6.f);
        g.setColour(rgb(91,102,109).withAlpha(.52f));
        g.drawRoundedRectangle(cabinet,6.f,1.f);

        // The reference's defining silhouette: high navy shoulders with
        // a gently lowered centre preset bay. Draw original vector geometry
        // so 100%, 125%, 150% and 200% render from the same source.
        g.setGradientFill(juce::ColourGradient(rgb(28,67,92),9.f,9.f,
                  rgb(17,44,71),9.f,64.f,false));
        g.fillRect(11.f,11.f,w-22.f,50.f);
        juce::Path body;
        body.startNewSubPath(11.f,31.f);
        body.lineTo(241.f,31.f);
        body.cubicTo(311.f,31.f,337.f,58.f,424.f,59.f);
        body.lineTo(w-424.f,59.f);
        body.cubicTo(w-337.f,58.f,w-311.f,31.f,w-241.f,31.f);
        body.lineTo(w-11.f,31.f);
        body.lineTo(w-11.f,374.f);
        body.lineTo(11.f,374.f);
        body.closeSubPath();
        g.setGradientFill(juce::ColourGradient(rgb(68,69,72),11.f,45.f,
                                                rgb(39,41,46),w-11.f,348.f,false));
        g.fillPath(body);
        g.setColour(rgb(110,124,128).withAlpha(.46f));
        juce::Path shoulder;
        shoulder.startNewSubPath(11.f,31.f);
        shoulder.lineTo(241.f,31.f);
        shoulder.cubicTo(311.f,31.f,337.f,58.f,424.f,59.f);
        shoulder.lineTo(w-424.f,59.f);
        shoulder.cubicTo(w-337.f,58.f,w-311.f,31.f,w-241.f,31.f);
        shoulder.lineTo(w-11.f,31.f);
        g.strokePath(shoulder,juce::PathStrokeType(.8f));
        g.setColour(rgb(46,99,125).withAlpha(.53f));
        g.drawLine(11.f,10.f,w-11.f,10.f,.9f);

        // Distinct HD-CART-like bottom LCD ribbon, with actual JUCE Slider
        // editable numeric text boxes over it, rather than duplicate text.
        constexpr float lcdTop=374.f;
        g.setGradientFill(juce::ColourGradient(rgb(27,85,122),11.f,lcdTop,
                                               rgb(17,49,83),11.f,407.f,false));
        g.fillRect(11.f,lcdTop,w-22.f,34.f);
        g.setColour(rgb(83,153,177).withAlpha(.50f));
        g.drawLine(11.f,lcdTop,w-11.f,lcdTop,1.f);
        g.setColour(rgb(5,10,17));g.fillRect(11.f,408.f,w-22.f,10.f);
        // Thin bounded column rules; do not make the screen a card grid.
        g.setColour(rgb(79,86,93).withAlpha(.35f));
        g.drawLine(257.f,155.f,257.f,367.f,.8f);
        g.drawLine(w-257.f,155.f,w-257.f,367.f,.8f);

        // Optical centring compensates for cap-height against circle centres.
        // Symmetric group: x250–870, midpoint exactly x560 of the panel.
        using M=reverb_ui::MeterLayout;
        // Actual processor telemetry is IN L/R and OUT L/R, not
        // independent wet/early reflection energy. Keep the labels truthful.
        g.setFont(reverb_ui::uiFont(11.f));
        for(int row=0;row<2;++row) {
            const int dotY=row==0?M::firstDotY:M::secondDotY;
            const int textY=dotY+M::dotSize/2-M::labelHeight/2-
                            M::labelOpticalLift;
            g.setColour(rgb(180,189,198));
            g.drawText(M::leftLabels[static_cast<std::size_t>(row)],
                M::leftLabelX,textY,M::leftLabelWidth,M::labelHeight,
                juce::Justification::centredRight,false);
            g.drawText(M::rightLabels[static_cast<std::size_t>(row)],
                M::rightLabelX,textY,M::rightLabelWidth,M::labelHeight,
                juce::Justification::centredLeft,false);
        }
        drawMeters(g);
        g.setColour(rgb(139,151,161));
        g.setFont(reverb_ui::uiFont(9.f));
        for(const int x : {M::dotsLeftX,M::dotsRightX}) {
            for(const int db : {-60,-24,-6,0}) {
                const int center=x+M::tickOffset(db);
                g.drawText(juce::String(db),center-11,128,22,11,
                           juce::Justification::centred,false);
            }
        }

        g.setColour(rgb(75,90,99).withAlpha(.67f));
        g.drawLine(291.f,221.f,w-291.f,221.f,.8f);
        g.setColour(rgb(106,187,224).withAlpha(.26f));
        g.drawLine(291.f,222.f,w-291.f,222.f,.7f);
        // Lower 10px cabinet lip is intentionally empty: no clipped footer.
    }

    void resized() override {
        title_.setBounds(47,65,247,32);
        preset_.setBounds(424,17,272,34);
        prev_.setBounds(383,17,33,34);
        next_.setBounds(getWidth()-383-33,17,33,34);
        algorithm_.setBounds(934,78,139,28);
        for(std::size_t i=0;i<tabs_.size();++i)
            tabs_[i].setBounds(280+static_cast<int>(i)*94,183,92,36);
        layoutPage();
    }

private:
    static constexpr std::array<std::array<int,5>,6> pages_{{
        {{1,11,10,6,7}},   // MIX: initial delays, wet body and air
        {{3,4,11,8,9}},   // SHAPE: size, density, reflection, motion
        {{1,3,4,10,12}},  // SPACE: perception and field
        {{5,6,7,10,11}},  // TONE: damp, band limits and wet blend
        {{8,9,4,3,10}},   // MOTION: controlled gentle animation
        {{12,1,4,11,10}}  // DUCKER: vocal clarity and early energy
    }};
    // Defined once: the same formatter covers visible readouts and a
    // user's typed values. Native DSP always owns the real parameter units.
    static juce::String formatParameter(std::size_t i,double v) {
        switch(i) {
            case 0: case 4: case 10: case 11: case 12:
                return juce::String(v*100.0,0)+" %";
            case 1: return juce::String(v,0)+" ms";
            case 2: return juce::String(v,2)+" s";
            case 3: return juce::String(v,2)+"x";
            case 5: case 6: case 7:
                return v>=1000.0?juce::String(v/1000.0,1)+" kHz":
                                  juce::String(v,0)+" Hz";
            case 8: return juce::String(v,2)+" ms";
            case 9: return juce::String(v,2)+" Hz";
        }
        return juce::String(v,2);
    }
    static double parseParameter(std::size_t i,const juce::String& value) {
        const double v=value.trim().getDoubleValue();
        if(!std::isfinite(v))return 0.;
        if(i==0||i==4||i==10||i==11||i==12)
            return value.containsChar('%')||v>2.0?v/100.0:v;
        if((i==5||i==6||i==7)&&value.containsIgnoreCase("khz"))
            return v*1000.0;
        return v;
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
        repaint(juce::Rectangle<int>(30,103,1050,295));
    }
    void syncControls() {
        algorithm_.setSelectedId(draft_.algorithm+1,juce::dontSendNotification);
        for(std::size_t i=0;i<knobs_.size();++i)
            knobs_[i].setValue(draft_.values[i],juce::dontSendNotification);
        repaint();
    }
    void layoutPage() {
        // A stable six-tab selector; both hero controls never disappear.
        // The five small controls are re-used, never duplicated and never
        // animated at audio rate. Input focus remains on native JUCE widgets.
        for(std::size_t i=0;i<knobs_.size();++i) {
            knobs_[i].setVisible(false);
            labels_[i].setVisible(false);
        }
        knobs_[2].setBounds(48,108,185,290);
        labels_[2].setBounds(40,125,201,23);
        knobs_[0].setBounds(getWidth()-48-185,108,185,290);
        labels_[0].setBounds(getWidth()-40-201,125,201,23);
        knobs_[2].setVisible(true);labels_[2].setVisible(true);
        knobs_[0].setVisible(true);labels_[0].setVisible(true);
        for(std::size_t n=0;n<5;++n) {
            const auto idx=static_cast<std::size_t>(
                pages_[static_cast<std::size_t>(page_)][n]);
            const int x=277+static_cast<int>(n)*115;
            knobs_[idx].setBounds(x,221,106,174);
            labels_[idx].setBounds(x-1,224,108,21);
            knobs_[idx].setVisible(true);
            labels_[idx].setVisible(true);
        }
        for(std::size_t i=0;i<tabs_.size();++i)
            tabs_[i].setToggleState(static_cast<int>(i)==page_,juce::dontSendNotification);
        repaint();
    }
    void drawMeters(juce::Graphics& g) {
        using namespace reverb_ui;
        // The single native PCM graph supplies all four channel peaks.
        // Zero fake animation or audio-loop graphics/heap allocation.
        for(int ch=0;ch<4;++ch) {
            const float peak=displayPeaks_[static_cast<std::size_t>(ch)];
            using M=reverb_ui::MeterLayout;
            const float db=peak<=1.e-6f?static_cast<float>(M::floorDb):
                20.f*std::log10(peak);
            const int lit=M::litCount(db);
            const int x=ch%2==0?M::dotsLeftX:M::dotsRightX;
            const int y=ch/2==0?M::firstDotY:M::secondDotY;
            for(int segment=0;segment<M::segments;++segment) {
                const bool on=segment<lit;
                const auto bright=segment>=18?rgb(241,148,116):
                    (segment>=15?rgb(223,193,119):rgb(112,203,245));
                const float sx=static_cast<float>(x+segment*M::pitch);
                g.setColour(on?bright:rgb(27,35,44));
                g.fillEllipse(sx,static_cast<float>(y),
                              static_cast<float>(M::dotSize),static_cast<float>(M::dotSize));
                if(on) {
                    g.setColour(bright.withAlpha(.25f));
                    g.drawEllipse(sx-1.f,static_cast<float>(y)-1.f,
                                  static_cast<float>(M::dotSize+2),
                                  static_cast<float>(M::dotSize+2),1.f);
                }
            }
        }
    }
    // Preserve DSP ACK/readback as a contextual tooltip rather than
    // permanently printing transport/debug status on the faceplate.
    void updateFeedbackTooltip() {
        const int next=!processor_.isReady()?0:(inFlight_||dirty_?1:2);
        if(next==feedbackState_)return;
        feedbackState_=next;
        preset_.setTooltip(next==0?"Audio engine offline":
            (next==1?"Changes awaiting audio callback":
                     "Parameters confirmed by the audio engine"));
    }
    void timerCallback() override {
        if(inFlight_ && processor_.appliedRequestId()>=inFlightId_)inFlight_=false;
        if(!processor_.isReady()) {
            updateFeedbackTooltip();
            displayPeaks_.fill(0.f);repaint(reverb_ui::MeterLayout::dirtyBounds());
            return;
        }
        if(dirty_&&!inFlight_) {
            const auto id=processor_.queueSettings(draft_);
            if(id){inFlightId_=id;inFlight_=true;dirty_=false;}
        }
        updateFeedbackTooltip();
        const auto fresh=processor_.meterPeaks();
        for(std::size_t i=0;i<displayPeaks_.size();++i)
            displayPeaks_[i]=std::max(fresh[i],displayPeaks_[i]*.82f);
        repaint(reverb_ui::MeterLayout::dirtyBounds());
    }
    VocalReverbProcessor& processor_;
    VocalReverbSettings draft_;
    reverb_ui::DialLook dial_;
    juce::Label title_;
    juce::ComboBox preset_,algorithm_;
    juce::TextButton prev_,next_;
    std::array<juce::TextButton,6> tabs_;
    std::array<juce::Slider,arverb::parameterCount> knobs_;
    std::array<juce::Label,arverb::parameterCount> labels_;
    std::array<float,4> displayPeaks_{};
    std::uint64_t inFlightId_=0;
    int page_=0;
    bool inFlight_=false,dirty_=false;
    int feedbackState_=-1;
};
inline juce::AudioProcessorEditor* VocalReverbProcessor::createEditor() {
    return new VocalReverbEditor(*this);
}
} // namespace armix::builtin_fx
