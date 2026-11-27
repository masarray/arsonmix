// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "VocalReverbProcessor.h"
#include "ReverbSisGraphiteSkin.h"
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
    static constexpr int width=1120,height=430, shoulderY=24;
    static constexpr int headerCentreY=29, chromeCentreY=27, lcdTop=347,lcdBottom=404;
    static constexpr int chromeSize=26,chromeGap=8,chromeRight=25;
    static constexpr int smallCaptionY=224,algorithmWidth=166,algorithmCentreX=983;
    static constexpr int lcdCentreY=376,lcdHeight=26;
    static constexpr int heroTop=129,heroCentreY=236,heroDial=212,heroWidth=220;
    static constexpr int smallTop=243,smallCentreY=295,smallDial=104,smallWidth=116;
    static constexpr int heroCentreX=145,smallFirstCentre=316,smallPitch=122;
    static constexpr int controlBottom=lcdCentreY+lcdHeight/2;
    static constexpr int tabY=170,tabHeight=35,tabPitch=98,tabWidth=96;
    static constexpr int tabLeft=(width-(5*tabPitch+tabWidth))/2;
    static constexpr int tabRuleY=tabY+tabHeight-2;
    static_assert(controlBottom<=lcdBottom && lcdCentreY-lcdHeight/2>=lcdTop);
    static_assert(chromeCentreY-chromeSize/2>=0 && chromeCentreY+chromeSize/2<60);
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
    static constexpr int leftLabelX=250, leftLabelWidth=85;
    static constexpr int dotsLeftX=355, dotsRightX=569;
    static constexpr int rightLabelX=785, rightLabelWidth=85;
    static constexpr int firstDotY=97, secondDotY=126, tickY=108;
    static constexpr int segments=20, pitch=10, dotSize=6;
    static constexpr int labelOpticalLift=2, labelHeight=20;
    static constexpr int bankWidth=(segments-1)*pitch+dotSize;
    static constexpr int floorDb=-60, ceilingDb=0;
    static constexpr int glowMargin=2;
    static constexpr std::array<int,3> scaleMarks{{-24,-12,0}};
    static constexpr int scaleLabelWidth=36, originLabelWidth=16;
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
    // Fill away from the centre on both sides; 0 signal at the origin,
    // 0 dBFS at full scale (outer end). Each cell spans 3 dB.
    static constexpr int tickOffset(int db) noexcept {
        const int index=(db-floorDb)*segments/(ceilingDb-floorDb)-1;
        return (index<0?0:(index>=segments?segments-1:index))*pitch+dotSize/2;
    }
    static constexpr int segmentX(bool right,int segment) noexcept {
        return right?dotsRightX+segment*pitch:dotsLeftX+(segments-1-segment)*pitch;
    }
    static constexpr int scaleX(bool right,int db) noexcept {
        return right?dotsRightX+tickOffset(db):dotsLeftX+bankWidth-tickOffset(db);
    }
};
static_assert(MeterLayout::groupLeft+MeterLayout::groupRight==1120);
static_assert(MeterLayout::dotsLeftX+MeterLayout::dotsRightX+
              MeterLayout::bankWidth==1120);
static_assert(MeterLayout::leftLabelWidth==MeterLayout::rightLabelWidth);
static_assert(MeterLayout::dotsLeftX-
    (MeterLayout::leftLabelX+MeterLayout::leftLabelWidth)==20);
static_assert(MeterLayout::rightLabelX-
    (MeterLayout::dotsRightX+19*MeterLayout::pitch+MeterLayout::dotSize)==20);
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
    // Shared procedural surface; native JUCE Slider still owns gesture,
    // skew, accessibility, text editing and callback ACK.
    void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,
                          float value,float start,float end,juce::Slider& slider) override {
        const auto id=slider.getComponentID();
        graphite::drawKnob(g,x+width*.5f,y+height*.5f,
            id=="armix.reverb.param.0"||id=="armix.reverb.param.2",value,start,end);
    }

    void drawComboBox(juce::Graphics& g,int width,int height,bool pressed,
                      int,int,int,int,juce::ComboBox& box) override {
        const bool preset=box.getComponentID()=="armix.reverb.preset";
        if(preset) {
            // Open luminous preset bay, as in V6. Keep native dropdown
            // arrow and keyboard focus rather than removing affordance.
            if(pressed||box.hasKeyboardFocus(true)) {
                g.setColour(rgb(119,209,245).withAlpha(.12f));
                g.fillRoundedRectangle({0.f,0.f,float(width),float(height)},3.f);
            }
        } else {
            g.setGradientFill(juce::ColourGradient(rgb(32,38,43),0.f,0.f,
                    rgb(21,26,30),0.f,static_cast<float>(height),false));
            g.fillRoundedRectangle({0.f,0.f,static_cast<float>(width),
                                    static_cast<float>(height)},3.f);
            g.setColour(rgb(80,90,97));
            g.drawRoundedRectangle({.5f,.5f,static_cast<float>(width)-1.f,
                                    static_cast<float>(height)-1.f},3.f,1.f);
        }
        const float cx=width-15.f, cy=height*.50f;
        juce::Path chevron;
        chevron.startNewSubPath(cx-4.f,cy-2.f);
        chevron.lineTo(cx,cy+2.f);
        chevron.lineTo(cx+4.f,cy-2.f);
        g.setColour(pressed?rgb(228,245,251):rgb(185,238,255));
        g.strokePath(chevron,juce::PathStrokeType(1.5f));
    }
    juce::Font getComboBoxFont(juce::ComboBox& box) override {
        return uiFont(box.getComponentID()=="armix.reverb.preset"?16.f:13.f);
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
                g.setColour(juce::Colours::white.withAlpha(.035f));
                g.fillRect(rect);
                g.setColour(rgb(184,241,255).withAlpha(.75f));
                g.fillRect(rect.getX()+13.f,rect.getBottom()-1.f,rect.getWidth()-26.f,1.f);
            } else if(hover||down) {
                g.setColour(rgb(102,162,191).withAlpha(.45f));
                g.fillRect(rect.getX()+13.f,rect.getBottom()-1.f,
                           rect.getWidth()-26.f,1.f);
            }
        } else if(id=="armix.reverb.preset.save") {
            // Compact neutral graphite action: unlike Power, Save never
            // implies a persistent on/off state. Disabled is visibly muted.
            const bool enabled=b.isEnabled();
            const auto r=rect.reduced(.6f);
            const auto top=enabled?(down?0x1c303b:hover?0x40535f:0x3a4751):0x262c31;
            const auto bottom=enabled?(down?0x17242c:hover?0x293941:0x2b3941):0x20252a;
            const auto border=enabled?(down?0x8bc2db:hover?0x7399ac:0x53636c):0x3e494f;
            g.setGradientFill(juce::ColourGradient(graphite::colour(top),0.f,r.getY(),
                graphite::colour(bottom),0.f,r.getBottom(),false));
            g.fillRoundedRectangle(r,4.f);
            g.setColour(graphite::colour(border));
            g.drawRoundedRectangle(r,4.f,1.f);
            g.setColour(juce::Colour(enabled?0x20ffffffu:0x0cffffffu));
            g.drawHorizontalLine(2,4.f,r.getRight()-4.f);
            if(enabled&&b.hasKeyboardFocus(false)) {
                g.setColour(juce::Colour(0xff9cdbe7));
                g.drawRoundedRectangle(r.reduced(1.f),3.f,1.f);
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
            g.setColour(hover||down?rgb(174,227,253):rgb(185,238,255));
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
        if(id=="armix.reverb.preset.save") {
            // Lucide Save, 24x24 viewBox, 2px rounded outline. Render at
            // 20px viewBox, optically balanced with Power and Close; no font/SVG loader.
            // https://lucide.dev/icons/save
            juce::Path icon;
            icon.startNewSubPath(19.f,21.f);
            icon.lineTo(5.f,21.f);
            icon.cubicTo(3.8954f,21.f,3.f,20.1046f,3.f,19.f);
            icon.lineTo(3.f,5.f);
            icon.cubicTo(3.f,3.8954f,3.8954f,3.f,5.f,3.f);
            icon.lineTo(16.f,3.f);
            icon.lineTo(21.f,8.f);
            icon.lineTo(21.f,19.f);
            icon.cubicTo(21.f,20.1046f,20.1046f,21.f,19.f,21.f);
            icon.closeSubPath();
            icon.startNewSubPath(17.f,21.f);
            icon.lineTo(17.f,13.f);
            icon.lineTo(7.f,13.f);
            icon.lineTo(7.f,21.f);
            icon.startNewSubPath(7.f,3.f);
            icon.lineTo(7.f,8.f);
            icon.lineTo(15.f,8.f);
            const auto ink=!b.isEnabled()?0x75828a:down?0xe7f8ff:hover?0xb1e6ff:0xe0edf3;
            g.setColour(graphite::colour(ink));
            g.strokePath(icon,juce::PathStrokeType(2.f,
                juce::PathStrokeType::curved,juce::PathStrokeType::rounded),
                juce::AffineTransform::scale(20.f/24.f)
                    .translated((b.getWidth()-20.f)*.5f,(b.getHeight()-20.f)*.5f));
            return;
        }
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
        g.setColour(bright?rgb(184,241,255):rgb(169,174,176));
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
        for(std::size_t i=0;i<knobs_.size();++i)
            knobs_[i].setEnabled(available&&!(i==0&&processor_.wetReturnMode()));
        preset_.setEnabled(available);algorithm_.setEnabled(available);
        prev_.setEnabled(available);next_.setEnabled(available);
        save_.setEnabled(available&&static_cast<bool>(saveUserPreset_));
        repaint();
    }
    explicit VocalReverbEditor(VocalReverbProcessor& processor)
        :juce::AudioProcessorEditor(processor),processor_(processor) {
        setOpaque(true);
        draft_=processor_.desiredSettings();
        const auto pale=reverb_ui::rgb(200,242,255);
        const auto cyan=reverb_ui::rgb(107,201,243);
        title_.setText(processor_.wetReturnMode()?"REVERB-SIS RETURN":"REVERB-SIS",juce::dontSendNotification);
        title_.setFont(reverb_ui::uiFont(22.f));
        title_.setColour(juce::Label::textColourId,reverb_ui::rgb(241,245,247));
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
        save_.setButtonText(""); // Icon only; retain accessible title + tooltip.
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
            knob.setColour(juce::Slider::textBoxTextColourId,reverb_ui::rgb(188,239,255));
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
                "Mix","Pre-delay","Decay","Size","Diffusion",
                "Damping","Low Cut","High Cut","Mod Depth",
                "Mod Rate","Width","Early Level","Ducker"
            }};
            labels_[i].setText(shortLabels[i],juce::dontSendNotification);
            labels_[i].setFont(reverb_ui::uiFont(12.f));
            labels_[i].setColour(juce::Label::textColourId,reverb_ui::rgb(231,240,244));
            labels_[i].setJustificationType(juce::Justification::centred);
            labels_[i].setInterceptsMouseClicks(false,false);
            addAndMakeVisible(labels_[i]);
        }
        labels_[0].setText(processor_.wetReturnMode()?"Wet Only":"Dry/Wet Mix",juce::dontSendNotification);
        if(processor_.wetReturnMode()){
            knobs_[0].setValue(1.,juce::dontSendNotification);
            knobs_[0].setEnabled(false);
            knobs_[0].setTooltip("100% wet; use the return fader or monitor send to set level");
        }
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
        const float w=static_cast<float>(getWidth()),h=static_cast<float>(getHeight());
        g.fillAll(rgb(23,26,29));
        const juce::Rectangle<float> cabinet{0.f,0.f,w,h};
        juce::Graphics::ScopedSaveState clip(g);
        juce::Path perimeter;perimeter.addRoundedRectangle(cabinet,5.f);
        g.reduceClipRegion(perimeter);
        juce::ColourGradient header(graphite::colour(0x0c1c28),0.f,0.f,
                                    graphite::colour(0x0c1b25),w,0.f,false);
        header.addColour(.5,graphite::colour(0x183c51));
        g.setGradientFill(header);g.fillRect(0.f,0.f,w,72.f);
        juce::Path body;
        body.startNewSubPath(0.f,float(F::shoulderY));body.lineTo(190.f,float(F::shoulderY));
        body.cubicTo(290.f,24.f,320.f,60.f,425.f,60.f);body.lineTo(695.f,60.f);
        body.cubicTo(800.f,60.f,830.f,24.f,930.f,24.f);body.lineTo(w,24.f);
        body.lineTo(w,float(F::lcdTop));body.lineTo(0.f,float(F::lcdTop));body.closeSubPath();
        graphite::drawMetal(g,body,getWidth(),getHeight());
        g.setColour(graphite::colour(0x929a9b).withAlpha(.28f));
        g.strokePath(body,juce::PathStrokeType(.7f));
        const float lcdTop=float(F::lcdTop),lcdHeight=float(F::lcdBottom-F::lcdTop);
        juce::ColourGradient lcd(graphite::colour(0x0b1d2c),0.f,lcdTop,
                                 graphite::colour(0x103246),0.f,float(F::lcdBottom),false);
        lcd.addColour(.48,graphite::colour(0x164864));
        g.setGradientFill(lcd);g.fillRect(0.f,lcdTop,w,lcdHeight);
        juce::ColourGradient sheen(juce::Colours::black.withAlpha(.26f),0.f,0.f,
                                   juce::Colours::black.withAlpha(.26f),w,0.f,false);
        sheen.addColour(.38,juce::Colours::white.withAlpha(.02f));
        sheen.addColour(.60,juce::Colours::white.withAlpha(.02f));
        g.setGradientFill(sheen);g.fillRect(0.f,lcdTop,w,lcdHeight);
        g.setColour(graphite::colour(0x040b11));g.drawHorizontalLine(F::lcdTop,0.f,w);
        g.setColour(graphite::colour(0x4d87a1).withAlpha(.32f));g.drawHorizontalLine(F::lcdBottom-1,0.f,w);
        // Single perimeter; no additional inset cabinet, right strip or footer artifact.
        g.setColour(graphite::colour(0x66717a));g.drawRoundedRectangle(cabinet.reduced(.5f),5.f,1.f);
        g.setColour(graphite::colour(0xb5c4cc));g.setFont(uiFont(10.f));
        g.drawText("S T U D I O  R E V E R B",49,83,200,15,juce::Justification::centredLeft,false);
        juce::ColourGradient tabFace(graphite::colour(0x1f2123),0.f,float(F::tabY),
             graphite::colour(0x242729),0.f,float(F::tabY+F::tabHeight),false);
        g.setGradientFill(tabFace);g.fillRoundedRectangle(float(F::tabLeft),float(F::tabY),
             float(F::width-2*F::tabLeft),float(F::tabHeight),5.f);
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
            g.setColour(rgb(209,222,230));
            g.drawText(M::leftLabels[static_cast<std::size_t>(row)],
                M::leftLabelX,textY,M::leftLabelWidth,M::labelHeight,
                juce::Justification::centredRight,false);
            g.drawText(M::rightLabels[static_cast<std::size_t>(row)],
                M::rightLabelX,textY,M::rightLabelWidth,M::labelHeight,
                juce::Justification::centredLeft,false);
        }
        drawMeters(g);
        g.setColour(rgb(183,200,211));
        g.setFont(reverb_ui::uiFont(9.f));
        for(bool right:{false,true}) {
            for(const int db : M::scaleMarks) {
                const int center=M::scaleX(right,db);
                g.drawText(db==0?juce::String("0 dBFS"):juce::String(db),
                           center-M::scaleLabelWidth/2,M::tickY,M::scaleLabelWidth,11,
                           juce::Justification::centred,false);
            }
        }
        // Central 0 is explicitly an amplitude origin, not 0 dBFS.
        g.drawText("0",F::width/2-M::originLabelWidth/2,M::tickY,M::originLabelWidth,11,
                   juce::Justification::centred,false);

        // The active tab underline and quiet baseline share one pixel band.
        g.setColour(rgb(116,121,123).withAlpha(.42f));
        g.fillRect(F::tabLeft,F::tabRuleY,F::width-2*F::tabLeft,2);
        // V6 cabinet lip below the LCD remains a clean solid surface.
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
        title_.setBounds(47,57,247,25);
        const int top=F::chromeCentreY-F::chromeSize/2;
        close_.setBounds(getWidth()-F::chromeRight-F::chromeSize,top,F::chromeSize,F::chromeSize);
        power_.setBounds(close_.getX()-F::chromeGap-F::chromeSize,top,F::chromeSize,F::chromeSize);
        save_.setBounds(power_.getX()-F::chromeGap-F::chromeSize,top,F::chromeSize,F::chromeSize);
        preset_.setBounds(424,13,272,32);
        prev_.setBounds(383,15,28,28);
        next_.setBounds(getWidth()-383-28,15,28,28);
        algorithm_.setBounds(F::algorithmCentreX-F::algorithmWidth/2,68,F::algorithmWidth,29);
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
            knobs_[i].setValue(i==0&&processor_.wetReturnMode()?1.f:draft_.values[i],juce::dontSendNotification);
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
        knobs_[2].setBounds(F::heroBounds(F::heroCentreX));
        labels_[2].setBounds(F::heroCentreX-100,109,200,19);
        knobs_[0].setBounds(F::heroBounds(getWidth()-F::heroCentreX));
        labels_[0].setBounds(getWidth()-F::heroCentreX-100,109,200,19);
        knobs_[2].setVisible(true);labels_[2].setVisible(true);
        knobs_[0].setVisible(true);labels_[0].setVisible(true);
        for(std::size_t n=0;n<5;++n) {
            const auto idx=static_cast<std::size_t>(
                pages_[static_cast<std::size_t>(page_)][n]);
            const int centre=F::smallFirstCentre+static_cast<int>(n)*F::smallPitch;
            knobs_[idx].setBounds(F::smallBounds(centre));
            labels_[idx].setBounds(centre-54,F::smallCaptionY,108,19);
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
            const bool right=ch%2!=0;
            const int y=ch/2==0?M::firstDotY:M::secondDotY;
            for(int segment=0;segment<M::segments;++segment) {
                const bool on=segment<lit;
                const auto bright=segment>=18?rgb(241,148,116):
                    (segment>=15?rgb(217,164,65):rgb(159,228,255));
                const float sx=static_cast<float>(M::segmentX(right,segment));
                g.setColour(on?bright:rgb(48,55,58));
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
