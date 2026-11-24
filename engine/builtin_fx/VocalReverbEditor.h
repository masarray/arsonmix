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
#include <functional>
#include <utility>
#include <vector>

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
// Fixed compact faceplate. Dial artwork, hit regions and editable LCDs have
// separate geometry: changing the LCD never moves or rescales a dial.
struct FaceplateLayout final {
    static constexpr int width=1120,height=430, shoulderY=47;
    static constexpr int headerCentreY=34, chromeCentreY=29, lcdTop=374,lcdBottom=height-12;
    static constexpr int smallCaptionY=214,algorithmWidth=139,algorithmCentreX=980;
    static constexpr int lcdCentreY=393,lcdHeight=26;
    static constexpr int heroTop=124,heroCentreY=250,heroDial=184,heroWidth=202;
    static constexpr int smallTop=221,smallCentreY=297,smallDial=106,smallWidth=114;
    static constexpr int smallFirstCentre=330,smallPitch=115;
    static constexpr int controlBottom=lcdCentreY+lcdHeight/2;
    static constexpr int tabY=164,tabHeight=34,tabPitch=94,tabWidth=92;
    static constexpr int tabLeft=(width-(5*tabPitch+tabWidth))/2;
    static constexpr int tabRuleY=tabY+tabHeight-2;
    static_assert(controlBottom<=lcdBottom && lcdCentreY-lcdHeight/2>=lcdTop);
    static_assert(chromeCentreY-12>=11 && chromeCentreY+12<shoulderY);
    static_assert(smallWidth<smallPitch);
    static juce::Rectangle<int> heroBounds(int centreX) {
        return {centreX-heroWidth/2,heroTop,heroWidth,controlBottom-heroTop};
    }
    static juce::Rectangle<int> smallBounds(int centreX) {
        return {centreX-smallWidth/2,smallTop,smallWidth,controlBottom-smallTop};
    }
    static juce::Slider::SliderLayout sliderLayout(juce::Slider& slider,bool hero) {
        const int top=hero?heroTop:smallTop;
        const int size=hero?heroDial:smallDial;
        const int centre=hero?heroCentreY:smallCentreY;
        const int textWidth=hero?126:102;
        juce::Slider::SliderLayout layout;
        layout.sliderBounds={(slider.getWidth()-size)/2,centre-top-size/2,size,size};
        layout.textBoxBounds={(slider.getWidth()-textWidth)/2,
                              lcdCentreY-top-lcdHeight/2,textWidth,lcdHeight};
        return layout;
    }
};
struct MeterLayout final {
    static constexpr int groupLeft=250, groupRight=870;
    static constexpr int leftLabelX=250, leftLabelWidth=93;
    static constexpr int dotsLeftX=355, dotsRightX=569;
    static constexpr int rightLabelX=777, rightLabelWidth=93;
    static constexpr int firstDotY=97, secondDotY=132, tickY=112;
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


// JUCE calls valueChanged synchronously even when its listener notification
// is async (wheel, typed value, keyboard). The processor must own the final
// intent BEFORE the window can close. Silent preset sync never calls this.
class LiveReverbKnob final : public juce::Slider {
public:
    std::function<void()> publishValue;
    void valueChanged() override {
        if(publishValue)publishValue();
    }
};
class DialLook final : public juce::LookAndFeel_V4 {
public:
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override {
        using F=FaceplateLayout;
        const auto id=slider.getComponentID();
        const bool hero=id=="armix.reverb.param.0"||id=="armix.reverb.param.2";
        const auto width=hero?F::heroWidth:F::smallWidth;
        const auto top=hero?F::heroTop:F::smallTop;
        if(id.startsWith("armix.reverb.param.")&&slider.getWidth()==width&&
           slider.getHeight()==F::controlBottom-top)
            return F::sliderLayout(slider,hero);
        return juce::LookAndFeel_V4::getSliderLayout(slider);
    }
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
        const bool preset=box.getComponentID()=="armix.reverb.preset";
        const int left=preset?30:9;
        text.setBounds(left,0,juce::jmax(24,box.getWidth()-(preset?60:40)),box.getHeight());
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
        } else if(id=="armix.reverb.power"||id=="armix.reverb.close") {
            // Match the authoritative /preview/ mixer-contextual-buttons.css
            // .armix-builtin-fx-power / -close, not a Reverb-specific theme.
            const auto r=rect.reduced(.6f);
            const auto state=b.getProperties().getWithDefault("armixFxState","unknown").toString();
            juce::uint32 top=0xff3a4751,bottom=0xff2b3941,border=0xff4b5d66;
            if(id.endsWith("close")) {
                top=down?0xffef3e50: hover?0xff633840:0xff3e5866;
                bottom=down?0xffc52d40: hover?0xff48292f:0xff293e4b;
                border=down?0xffee8b93: hover?0xffb27078:0xff607d8c;
            } else if(state=="on") {
                top=0xff335c51;bottom=0xff204539;border=0xff428969;
            } else if(state=="off") {
                top=0xff533b42;bottom=0xff3a3039;border=0xff83525a;
            } else if(state=="rack-off") {
                top=0xff544737;bottom=0xff41382d;border=0xff846948;
            }
            g.setGradientFill(juce::ColourGradient(juce::Colour(top),0.f,r.getY(),
                juce::Colour(bottom),0.f,r.getBottom(),false));
            g.fillRoundedRectangle(r,4.f);
            g.setColour(juce::Colour(border));
            g.drawRoundedRectangle(r,4.f,1.f);
            g.setColour(juce::Colour(0x20ffffff));
            g.drawHorizontalLine(2,4.f,r.getRight()-4.f);
            if(b.hasKeyboardFocus(false)) {
                g.setColour(juce::Colour(0xff9cdbe7));
                g.drawRoundedRectangle(r.reduced(1.f),3.f,1.f);
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
        if(id=="armix.reverb.close") {
            // Exact CSS / GEQ31 Close SVG, rendered at the approved 22px.
            juce::Path x;
            x.startNewSubPath(6.4f,19.f);
            x.lineTo(5.f,17.6f);x.lineTo(10.6f,12.f);
            x.lineTo(5.f,6.4f);x.lineTo(6.4f,5.f);
            x.lineTo(12.f,10.6f);x.lineTo(17.6f,5.f);
            x.lineTo(19.f,6.4f);x.lineTo(13.4f,12.f);
            x.lineTo(19.f,17.6f);x.lineTo(17.6f,19.f);
            x.lineTo(12.f,13.4f);x.closeSubPath();
            g.setColour(juce::Colour(0xfff4f8fa));
            g.fillPath(x,juce::AffineTransform::scale(22.f/24.f)
                .translated((b.getWidth()-22.f)*.5f,(b.getHeight()-22.f)*.5f));
            return;
        }
        if(id=="armix.reverb.power") {
            // IEC power ring, same 18px visual scale as native PEQ/Compressor
            // web chrome; vector avoids font-dependent U+23FB rendering.
            const auto state=b.getProperties().getWithDefault("armixFxState","unknown").toString();
            const auto ink=juce::Colour(state=="on"?0xff76f0b9:
                state=="off"?0xffe99b9f:state=="rack-off"?0xfff0c483:0xff8498a4);
            const float cx=b.getWidth()*.5f,cy=b.getHeight()*.5f+1.f,radius=6.f;
            juce::Path ring;
            ring.addCentredArc(cx,cy,radius,radius,0.f,.63f,
                juce::MathConstants<float>::twoPi-.63f,true);
            juce::Path stem;
            stem.startNewSubPath(cx,cy-9.f);stem.lineTo(cx,cy-1.f);
            const auto stroke=juce::PathStrokeType(1.65f,
                juce::PathStrokeType::curved,juce::PathStrokeType::rounded);
            if(state=="on") {
                g.setColour(ink.withAlpha(.18f));
                g.strokePath(ring,juce::PathStrokeType(3.6f));
                g.strokePath(stem,juce::PathStrokeType(3.6f));
            }
            g.setColour(ink);g.strokePath(ring,stroke);g.strokePath(stem,stroke);
            return;
        }
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
    struct HostBypassState {
        bool valid=false;
        bool slotBypassed=false;
        bool rackBypassed=false;
        bool globalBypassed=false;
        bool effectiveBypassed() const noexcept {
            return valid&&(slotBypassed||rackBypassed||globalBypassed);
        }
    };
    enum class HostOverlay {none,bypassed,unavailable};
    struct UserPresetEntry {juce::String id,name;};
    void bindUserPresetService(
        std::function<std::vector<UserPresetEntry>()> enumerate,
        std::function<juce::String(const juce::String&)> save,
        std::function<bool(const juce::String&)> recall) {
        listUserPresets_=std::move(enumerate);
        saveUserPreset_=std::move(save);
        recallUserPreset_=std::move(recall);
        refreshPresetList();
        save_.setEnabled(static_cast<bool>(saveUserPreset_)&&hostOverlay_!=HostOverlay::unavailable);
    }
    // The retained host is the only Power authority; no local DSP bypass.
    void bindHostPower(std::function<bool(bool)> request,
                       std::function<HostBypassState()> readback) {
        requestBypass_=std::move(request);
        bindHostBypassReadback(std::move(readback));
    }
    void bindHostBypassReadback(std::function<HostBypassState()> readback) {
        readHostBypass_=std::move(readback);
        synchronizeHostBypass();
    }
    HostOverlay hostOverlay() const noexcept {return hostOverlay_;}
    void synchronizeHostBypass() {
        const auto state=readHostBypass_?readHostBypass_():HostBypassState{};
        const auto next=!readHostBypass_?HostOverlay::none:
            !state.valid?HostOverlay::unavailable:
            state.effectiveBypassed()?HostOverlay::bypassed:HostOverlay::none;
        const bool canToggle=state.valid&&requestBypass_&&
                             !state.rackBypassed&&!state.globalBypassed;
        const bool active=state.valid&&!state.effectiveBypassed();
        const juce::String visual=!state.valid?"unknown":
            (state.rackBypassed||state.globalBypassed)?"rack-off":
            state.slotBypassed?"off":"on";
        if(power_.getProperties().getWithDefault("armixFxState","unknown").toString()!=visual) {
            power_.getProperties().set("armixFxState",visual);
            power_.repaint();
        }
        power_.setEnabled(canToggle);
        power_.setToggleState(active,juce::dontSendNotification);
        const juce::String tooltip=!state.valid?"FX Power unavailable; use FX Rack":
            state.globalBypassed?"Enable global FX in mixer":
            state.rackBypassed?"Enable FX Rack in mixer":
            state.slotBypassed?"Enable Reverb-Sis":"Bypass Reverb-Sis";
        if(power_.getTooltip()!=tooltip)power_.setTooltip(tooltip);
        if(next==hostOverlay_)return;
        hostOverlay_=next;
        // Allow editing a valid bypassed insert; a removed/replaced lease
        // is unavailable, not bypassed and not a new target.
        const bool available=next!=HostOverlay::unavailable;
        for(auto& knob:knobs_)knob.setEnabled(available);
        preset_.setEnabled(available);algorithm_.setEnabled(available);
        prev_.setEnabled(available);next_.setEnabled(available);
        save_.setEnabled(available&&static_cast<bool>(saveUserPreset_));
        repaint();
    }
    explicit VocalReverbEditor(VocalReverbProcessor& processor)
        :juce::AudioProcessorEditor(processor),processor_(processor) {
        setOpaque(true);
        draft_=processor_.desiredSettings();
        const auto pale=reverb_ui::rgb(209,224,233);
        const auto cyan=reverb_ui::rgb(107,201,243);
        title_.setText("REVERB-SIS",juce::dontSendNotification);
        title_.setFont(reverb_ui::uiFont(22.f));
        title_.setColour(juce::Label::textColourId,pale);
        title_.setComponentID("armix.reverb.title");
        addAndMakeVisible(title_);
        power_.setComponentID("armix.reverb.power");
        power_.setButtonText({});
        power_.setTitle("FX Power");
        power_.getProperties().set("armixFxState","unknown");
        power_.setLookAndFeel(&dial_);
        power_.setClickingTogglesState(false);
        power_.setEnabled(false);
        power_.setTooltip("FX Power unavailable; use FX Rack");
        power_.onClick=[this] {
            if(!requestBypass_||!readHostBypass_)return;
            const auto before=readHostBypass_();
            if(before.valid&&!before.rackBypassed&&!before.globalBypassed)
                (void)requestBypass_(!before.slotBypassed);
            synchronizeHostBypass();
        };
        addAndMakeVisible(power_);
        close_.setComponentID("armix.reverb.close");
        close_.setButtonText({});
        close_.setTitle("Close Reverb-Sis");
        close_.setLookAndFeel(&dial_);
        close_.setTooltip("Close Reverb-Sis");
        close_.onClick=[this] {
            if(auto* window=findParentComponentOfClass<juce::DocumentWindow>())
                window->closeButtonPressed();
        };
        addAndMakeVisible(close_);

        for(int i=0;i<reverbSisPresetCount;++i)
            preset_.addItem(vocalReverbPresetNames[static_cast<std::size_t>(i)],i+1);
        preset_.addItem("Custom / saved",reverbSisCustomItem);
        preset_.setSelectedId(draft_.preset>=0?draft_.preset+1:reverbSisCustomItem,juce::dontSendNotification);
        preset_.setColour(juce::ComboBox::backgroundColourId,reverb_ui::rgb(12,29,44));
        preset_.setColour(juce::ComboBox::outlineColourId,reverb_ui::rgb(41,85,108));
        preset_.setColour(juce::ComboBox::textColourId,pale);
        preset_.setColour(juce::ComboBox::arrowColourId,cyan);
        preset_.setJustificationType(juce::Justification::centred);
        preset_.setLookAndFeel(&dial_);
        preset_.onChange=[this] {
            const int i=preset_.getSelectedId();
            if(i>=1&&i<=reverbSisPresetCount) {
                selectedUserId_.clear();
                draft_=vocalReverbFactoryPreset(i-1);
                syncControls();
                submitDraft();
            } else if(i>=userPresetFirstId) {
                const auto index=static_cast<std::size_t>(i-userPresetFirstId);
                if(index<userPresets_.size()&&recallUserPreset_&&
                   recallUserPreset_(userPresets_[index].id)) {
                    selectedUserId_=userPresets_[index].id;
                    draft_=processor_.desiredSettings();
                    syncControls();
                    updateFeedbackTooltip();
                } else selectCurrentPreset();
            } else selectCurrentPreset();
        };
        preset_.setComponentID("armix.reverb.preset");
        addAndMakeVisible(preset_);
        save_.setComponentID("armix.reverb.preset.save");
        save_.setButtonText("SAVE");
        save_.setTitle("Save Reverb-Sis user preset");
        save_.setTooltip("Save current confirmed settings to My Presets");
        save_.setLookAndFeel(&dial_);
        save_.setEnabled(false);
        save_.onClick=[this]{promptSavePreset();};
        addAndMakeVisible(save_);
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
            // Relative drag, no click-to-jump or release-only control.
            // A full normalized sweep takes a deliberate 280 logical px;
            // use the native Slider's same path for mouse, wheel and keys.
            knob.setChangeNotificationOnlyOnRelease(false);
            knob.setMouseDragSensitivity(280);
            knob.setVelocityBasedMode(false);
            knob.setScrollWheelEnabled(true);
            knob.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,
                                     juce::MathConstants<float>::pi*2.75f,true);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow,false,
                 (i==0||i==2)?126:103,26);
            knob.setRange(d.minimum,d.maximum,0.);
            // More resolution where singers normally work; endpoints stay
            // the actual DSP bounds, with no hidden quantisation.
            switch(static_cast<arverb::Parameter>(i)) {
                case arverb::Parameter::PreDelayMs: knob.setSkewFactorFromMidPoint(55.0);break;
                case arverb::Parameter::DecaySeconds: knob.setSkewFactorFromMidPoint(2.5);break;
                case arverb::Parameter::DampingHz: knob.setSkewFactorFromMidPoint(5000.0);break;
                case arverb::Parameter::LowCutHz: knob.setSkewFactorFromMidPoint(220.0);break;
                case arverb::Parameter::HighCutHz: knob.setSkewFactorFromMidPoint(7000.0);break;
                case arverb::Parameter::ModRateHz: knob.setSkewFactorFromMidPoint(.35);break;
                default: break;
            }
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
            knob.publishValue=[this,i]{
                const float value=static_cast<float>(knobs_[i].getValue());
                if(draft_.values[i]==value)return;
                draft_.values[i]=value;
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
            labels_[i].setInterceptsMouseClicks(false,false);
            addAndMakeVisible(labels_[i]);
        }
        labels_[0].setText("Dry/Wet Mix",juce::dontSendNotification);
        labels_[2].setText("Reverb Time",juce::dontSendNotification);
        setSize(reverb_ui::FaceplateLayout::width,reverb_ui::FaceplateLayout::height);
        layoutPage();
        lastMeterGeneration_=processor_.meterGeneration();
        processor_.attachMeterView();
        startTimerHz(30);
    }
    ~VocalReverbEditor() override {
        stopTimer();
        processor_.detachMeterView();
        for(auto& k:knobs_){
            k.publishValue={};
            k.setLookAndFeel(nullptr);
        }
        for(auto& b:tabs_)b.setLookAndFeel(nullptr);
        prev_.setLookAndFeel(nullptr);
        next_.setLookAndFeel(nullptr);
        preset_.setLookAndFeel(nullptr);
        algorithm_.setLookAndFeel(nullptr);
        power_.setLookAndFeel(nullptr);
        close_.setLookAndFeel(nullptr);
        save_.setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        using namespace reverb_ui;
        using F=FaceplateLayout;
        constexpr float shoulderY=static_cast<float>(F::shoulderY);
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
        body.startNewSubPath(11.f,shoulderY);
        body.lineTo(241.f,shoulderY);
        body.cubicTo(311.f,shoulderY,337.f,58.f,424.f,59.f);
        body.lineTo(w-424.f,59.f);
        body.cubicTo(w-337.f,58.f,w-311.f,shoulderY,w-241.f,shoulderY);
        body.lineTo(w-11.f,shoulderY);
        body.lineTo(w-11.f,374.f);
        body.lineTo(11.f,374.f);
        body.closeSubPath();
        g.setGradientFill(juce::ColourGradient(rgb(68,69,72),11.f,45.f,
                                                rgb(39,41,46),w-11.f,348.f,false));
        g.fillPath(body);
        g.setColour(rgb(110,124,128).withAlpha(.46f));
        juce::Path shoulder;
        shoulder.startNewSubPath(11.f,shoulderY);
        shoulder.lineTo(241.f,shoulderY);
        shoulder.cubicTo(311.f,shoulderY,337.f,58.f,424.f,59.f);
        shoulder.lineTo(w-424.f,59.f);
        shoulder.cubicTo(w-337.f,58.f,w-311.f,shoulderY,w-241.f,shoulderY);
        shoulder.lineTo(w-11.f,shoulderY);
        g.strokePath(shoulder,juce::PathStrokeType(.8f));
        g.setColour(rgb(46,99,125).withAlpha(.53f));
        g.drawLine(11.f,10.f,w-11.f,10.f,.9f);

        // Distinct HD-CART-like bottom LCD ribbon, with actual JUCE Slider
        // editable numeric text boxes over it, rather than duplicate text.
        constexpr float lcdTop=static_cast<float>(F::lcdTop);
        g.setGradientFill(juce::ColourGradient(rgb(27,85,122),11.f,lcdTop,
                                               rgb(17,49,83),11.f,static_cast<float>(F::lcdBottom-1),false));
        // Fill the entire inner faceplate to the cabinet inset; no black
        // spacer below the LCD. Preserve numeric control positions.
        g.fillRect(11.f,lcdTop,w-22.f,static_cast<float>(F::lcdBottom-F::lcdTop));
        g.setColour(rgb(83,153,177).withAlpha(.50f));
        g.drawLine(11.f,lcdTop,w-11.f,lcdTop,1.f);
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
                g.drawText(juce::String(db),center-11,M::tickY,22,11,
                           juce::Justification::centred,false);
            }
        }

        // The active tab underline and quiet baseline share one pixel band.
        g.setColour(rgb(75,90,99).withAlpha(.67f));
        g.fillRect(F::tabLeft,F::tabRuleY,F::width-2*F::tabLeft,2);
        // Lower 10px cabinet lip is intentionally empty: no clipped footer.
    }

    void paintOverChildren(juce::Graphics& g) override {
        if(hostOverlay_==HostOverlay::none)return;
        // GEQ31 / ImmerSIB visual language: actual children are dimmed,
        // amber bold status and shadow; header controls stay undimmed.
        const bool bypassed=hostOverlay_==HostOverlay::bypassed;
        juce::Graphics::ScopedSaveState saved(g);
        g.setColour(juce::Colour(0xb803060a));
        g.fillRect(11,60,getWidth()-22,reverb_ui::FaceplateLayout::lcdBottom-60);
        auto font=juce::Font(juce::FontOptions(
            bypassed?51.f:34.f,juce::Font::bold));
        font.setExtraKerningFactor(.055f);
        g.setFont(font);
        const juce::Rectangle<int> label(0,164,getWidth(),122);
        const auto text=bypassed?"BYPASSED":"FX UNAVAILABLE";
        g.setColour(juce::Colour(0xb8000307));
        g.drawText(text,label.translated(0,3),juce::Justification::centred,false);
        g.setColour(bypassed?juce::Colour(0xeeffa53c):juce::Colour(0xffd0dce1));
        g.drawText(text,label,juce::Justification::centred,false);
    }
    void resized() override {
        using F=reverb_ui::FaceplateLayout;
        title_.setBounds(47,65,247,32);
        // Same right-side cluster as PEQ, Compressor and Harmonice;
        // Owner optical correction: lift the compact chrome five pixels
        // off the preset centre; keep the entire cluster on blue shoulder.
        close_.setBounds(getWidth()-25-24,F::chromeCentreY-12,24,24);
        power_.setBounds(close_.getX()-8-26,F::chromeCentreY-12,26,24);
        preset_.setBounds(424,17,272,34);
        prev_.setBounds(383,17,33,34);
        next_.setBounds(getWidth()-383-33,17,33,34);
        save_.setBounds(next_.getRight()+8,F::chromeCentreY-12,54,24);
        algorithm_.setBounds(F::algorithmCentreX-F::algorithmWidth/2,78,F::algorithmWidth,28);
        for(std::size_t i=0;i<tabs_.size();++i)
            tabs_[i].setBounds(F::tabLeft+static_cast<int>(i)*F::tabPitch,
                               F::tabY,F::tabWidth,F::tabHeight);
        layoutPage();
    }
    // Drag only bare faceplate header, never sliders, dropdowns or buttons.
    void mouseDown(const juce::MouseEvent& event) override {
        draggingWindow_=event.y<60&&event.mods.isLeftButtonDown()&&
                        findParentComponentOfClass<juce::DocumentWindow>()!=nullptr;
        if(draggingWindow_)
            windowDragger_.startDraggingComponent(getTopLevelComponent(),event);
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if(draggingWindow_)
            windowDragger_.dragComponent(getTopLevelComponent(),event,nullptr);
    }
    void mouseUp(const juce::MouseEvent&) override {draggingWindow_=false;}

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
    static constexpr int userPresetFirstId=1000;
    void selectCurrentPreset() {
        int choice=draft_.preset>=0?draft_.preset+1:reverbSisCustomItem;
        for(std::size_t i=0;i<userPresets_.size();++i)
            if(userPresets_[i].id==selectedUserId_)choice=userPresetFirstId+static_cast<int>(i);
        preset_.setSelectedId(choice,juce::dontSendNotification);
    }
    void refreshPresetList() {
        userPresets_=listUserPresets_?listUserPresets_():std::vector<UserPresetEntry>{};
        preset_.clear(juce::dontSendNotification);
        for(int i=0;i<reverbSisPresetCount;++i)
            preset_.addItem(vocalReverbPresetNames[static_cast<std::size_t>(i)],i+1);
        preset_.addItem("Custom / saved",reverbSisCustomItem);
        if(!userPresets_.empty())preset_.addSectionHeading("My Presets");
        for(std::size_t i=0;i<userPresets_.size();++i)
            preset_.addItem(userPresets_[i].name,userPresetFirstId+static_cast<int>(i));
        selectCurrentPreset();
    }
    void promptSavePreset() {
        if(!saveUserPreset_||hostOverlay_==HostOverlay::unavailable)return;
        auto* dialog=new juce::AlertWindow("Save Reverb-Sis preset",
            "Save your current reverb settings to My Presets",
            juce::MessageBoxIconType::NoIcon,this);
        dialog->addTextEditor("presetName","","Preset name:");
        dialog->addButton("Save",1,juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
        const juce::Component::SafePointer<VocalReverbEditor> weak(this);
        dialog->enterModalState(true,juce::ModalCallbackFunction::create(
            [weak,dialog](int result) {
                if(result!=1||weak==nullptr||!weak->saveUserPreset_)return;
                const auto id=weak->saveUserPreset_(dialog->getTextEditorContents("presetName").trim());
                if(id.isEmpty())return;
                weak->selectedUserId_=id;
                weak->refreshPresetList();
            }),true);
    }
    void selectRelativePreset(int delta) {
        const int current=draft_.preset<0?0:draft_.preset;
        const int index=(current+delta+reverbSisPresetCount)%reverbSisPresetCount;
        selectedUserId_.clear();
        draft_=vocalReverbFactoryPreset(index);
        preset_.setSelectedId(index+1,juce::dontSendNotification);
        syncControls();
        submitDraft();
    }
    void markCustom() {
        selectedUserId_.clear();
        draft_.preset=-1;
        preset_.setSelectedId(reverbSisCustomItem,juce::dontSendNotification);
        submitDraft();
        // Native slider and ComboBox already invalidate their own pixels.
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
        using F=reverb_ui::FaceplateLayout;
        knobs_[2].setBounds(F::heroBounds(140));
        labels_[2].setBounds(40,125,200,23);
        knobs_[0].setBounds(F::heroBounds(getWidth()-140));
        labels_[0].setBounds(getWidth()-240,125,200,23);
        knobs_[2].setVisible(true);labels_[2].setVisible(true);
        knobs_[0].setVisible(true);labels_[0].setVisible(true);
        for(std::size_t n=0;n<5;++n) {
            const auto idx=static_cast<std::size_t>(
                pages_[static_cast<std::size_t>(page_)][n]);
            const int centre=F::smallFirstCentre+static_cast<int>(n)*F::smallPitch;
            knobs_[idx].setBounds(F::smallBounds(centre));
            labels_[idx].setBounds(centre-54,F::smallCaptionY,108,21);
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
            using M=reverb_ui::MeterLayout;
            const int lit=displaySegments_[static_cast<std::size_t>(ch)];
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
    void submitDraft() {
        (void)processor_.queueSettings(draft_);
        updateFeedbackTooltip();
    }
    // Preserve DSP ACK/readback as a contextual tooltip rather than
    // permanently printing transport/debug status on the faceplate.
    void updateFeedbackTooltip() {
        const int next=!processor_.isReady()?0:(processor_.settingsPending()?1:2);
        if(next==feedbackState_)return;
        feedbackState_=next;
        preset_.setTooltip(next==0?"Audio engine offline":
            (next==1?"Changes awaiting audio callback":
                     "Parameters confirmed by the audio engine"));
    }
    void timerCallback() override {
        synchronizeHostBypass();
        updateFeedbackTooltip();
        const bool ready=processor_.isReady();
        const auto generation=processor_.meterGeneration();
        const auto fresh=ready&&generation!=lastMeterGeneration_?
            processor_.meterPeaks():std::array<float,4>{};
        lastMeterGeneration_=generation;
        bool changed=false;
        for(std::size_t i=0;i<displayPeaks_.size();++i) {
            displayPeaks_[i]=ready?std::max(fresh[i],displayPeaks_[i]*.82f):0.f;
            const auto peak=displayPeaks_[i];
            const int lit=reverb_ui::MeterLayout::litCount(
                peak<=1.e-6f?-60.f:20.f*std::log10(peak));
            changed|=lit!=displaySegments_[i];
            displaySegments_[i]=lit;
        }
        if(changed)repaint(reverb_ui::MeterLayout::dirtyBounds());
    }
    VocalReverbProcessor& processor_;
    VocalReverbSettings draft_;
    reverb_ui::DialLook dial_;
    juce::Label title_;
    juce::ComboBox preset_,algorithm_;
    juce::TextButton prev_,next_,power_,close_,save_;
    std::vector<UserPresetEntry> userPresets_;
    juce::String selectedUserId_;
    std::function<std::vector<UserPresetEntry>()> listUserPresets_;
    std::function<juce::String(const juce::String&)> saveUserPreset_;
    std::function<bool(const juce::String&)> recallUserPreset_;
    juce::ComponentDragger windowDragger_;
    bool draggingWindow_=false;
    std::array<juce::TextButton,6> tabs_;
    std::array<reverb_ui::LiveReverbKnob,arverb::parameterCount> knobs_;
    std::array<juce::Label,arverb::parameterCount> labels_;
    std::array<float,4> displayPeaks_{};
    std::array<int,4> displaySegments_{};
    std::uint64_t lastMeterGeneration_=0;
    int page_=0;
    int feedbackState_=-1;
    std::function<HostBypassState()> readHostBypass_;
    std::function<bool(bool)> requestBypass_;
    HostOverlay hostOverlay_=HostOverlay::none;
};
inline juce::AudioProcessorEditor* VocalReverbProcessor::createEditor() {
    return new VocalReverbEditor(*this);
}
} // namespace armix::builtin_fx
