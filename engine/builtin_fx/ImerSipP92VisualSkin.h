// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Product-only scalable SVG art. P9.1/P9.2 audio/render/IMRS are untouched.
// All geometry and asset parsing execute on the JUCE message/editor thread.
#include <BinaryData.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace armix::builtin_fx {

// Every drawn speaker is wholly within the room including extreme edits.
// Rectangles are logical editor pixels; host/OS DPI scales the SVG, not a
// pre-downsampled 256-pixel raster. No hidden monitor speaker offscreen.
struct ImerSipP92ConcertBoxes {
    juce::Rectangle<float> leftArray,rightArray,leftSub,rightSub,leftFill,rightFill;
    static constexpr float roomLeft=16.f,roomRight=1084.f;
    static constexpr float roomTop=100.f,roomBottom=444.f;
    bool safe() const noexcept {
        auto inside=[](const juce::Rectangle<float>& r) {
            return r.getX()>=roomLeft && r.getRight()<=roomRight
                && r.getY()>=roomTop && r.getBottom()<=roomBottom
                && r.getWidth()>0.f && r.getHeight()>0.f;
        };
        if(!inside(leftArray)||!inside(rightArray)||
           !inside(leftSub)||!inside(rightSub)||
           !inside(leftFill)||!inside(rightFill))return false;
        // At the smallest stage, the nearest fill and sub must still
        // have their own space (separate PNG/SVG layers, no overlap).
        return leftSub.getRight()<=leftFill.getX()+.5f
            && rightFill.getRight()<=rightSub.getX()+.5f
            && leftArray.getRight()<=leftFill.getX()+.5f
            && rightFill.getRight()<=rightArray.getX()+.5f;
    }
};

inline ImerSipP92ConcertBoxes imersipP92ConcertBoxes(
        float stageDegrees,float distanceMetres) noexcept {
    const float stage=std::clamp(std::isfinite(stageDegrees)?
        stageDegrees:150.f,30.f,180.f);
    const float metres=std::clamp(std::isfinite(distanceMetres)?
        distanceMetres:2.2f,1.6f,7.4f);
    const float width=(stage-30.f)/150.f;
    const float far=(metres-1.6f)/5.8f;
    const float span=170.f+182.f*width;
    const float arrayW=98.f-28.f*far;
    const float arrayH=arrayW*260.f/160.f;
    const float arrayY=132.f+34.f*far;
    const float subW=112.f-32.f*far;
    const float subH=subW*150.f/180.f;
    const float subY=320.f+16.f*far;
    const float fillW=60.f-6.f*far;
    const float fillH=fillW*160.f/112.f;
    const float fillY=279.f+10.f*far;
    auto centred=[](float cx,float y,float w,float h) {
        return juce::Rectangle<float>(cx-w*.5f,y,w,h);
    };
    return {centred(550.f-span,arrayY,arrayW,arrayH),
            centred(550.f+span,arrayY,arrayW,arrayH),
            centred(550.f-span,subY,subW,subH),
            centred(550.f+span,subY,subW,subH),
            centred(466.f,fillY,fillW,fillH),
            centred(634.f,fillY,fillW,fillH)};
}

class ImerSipP92VisualSkin final : public juce::LookAndFeel_V4 {
public:
    ImerSipP92VisualSkin() {
        // True vector assets; never resample an undersized atlas.
        room_=load(BinaryData::imersip_room_svg,
                   BinaryData::imersip_room_svgSize);
        panel_=load(BinaryData::imersip_panel_svg,
                    BinaryData::imersip_panel_svgSize);
        line_=load(BinaryData::imersip_line_array_svg,
                   BinaryData::imersip_line_array_svgSize);
        sub_=load(BinaryData::imersip_subwoofer_svg,
                  BinaryData::imersip_subwoofer_svgSize);
        fill_=load(BinaryData::imersip_frontfill_svg,
                   BinaryData::imersip_frontfill_svgSize);
        setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff13272c));
        setColour(juce::ComboBox::textColourId,juce::Colour(0xffdce8e9));
        setColour(juce::ComboBox::outlineColourId,juce::Colour(0xff4b6b71));
        setColour(juce::ComboBox::arrowColourId,juce::Colour(0xff55dad0));
        setColour(juce::PopupMenu::backgroundColourId,juce::Colour(0xff17242a));
        setColour(juce::PopupMenu::textColourId,juce::Colour(0xffe0edee));
        setColour(juce::PopupMenu::highlightedBackgroundColourId,
                  juce::Colour(0xff225d65));
        setColour(juce::TextButton::buttonColourId,juce::Colour(0xff23343b));
        setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff067b78));
        setColour(juce::TextButton::textColourOffId,juce::Colour(0xffeef3f3));
    }
    bool ready() const noexcept {
        return room_&&panel_&&line_&&sub_&&fill_;
    }
    void drawRoom(juce::Graphics& g) const {
        draw(g,room_.get(),{0.f,0.f,1100.f,461.f});
    }
    void drawPanel(juce::Graphics& g) const {
        draw(g,panel_.get(),{0.f,461.f,1100.f,219.f});
    }
    void drawConcert(juce::Graphics& g,
                     const ImerSipP92ConcertBoxes& b) const {
        // All bounds are mathematically fenced within room. No cropping or
        // hidden speaker corners. No objects on top of the control desk.
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(0,95,1100,359);
        draw(g,line_.get(),b.leftArray);
        draw(g,line_.get(),b.rightArray,true);
        draw(g,sub_.get(),b.leftSub);
        draw(g,sub_.get(),b.rightSub,true);
        draw(g,fill_.get(),b.leftFill);
        draw(g,fill_.get(),b.rightFill,true);
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,
                          float sliderPos,float,float,
                          const juce::Slider::SliderStyle style,
                          juce::Slider& slider) override {
        if(style!=juce::Slider::LinearHorizontal) {
            juce::LookAndFeel_V4::drawLinearSlider(g,x,y,width,height,
                sliderPos,0.f,0.f,style,slider);
            return;
        }
        const float left=float(x+8),right=float(x+width-8);
        const float centre=float(y)+float(height)*.5f;
        const float thumb=std::clamp(sliderPos,left,right);
        g.setColour(juce::Colour(0xff091519));
        g.fillRoundedRectangle(left,centre-3.6f,right-left,7.2f,3.2f);
        g.setColour(juce::Colour(0xff4e686a));
        g.drawRoundedRectangle(left,centre-3.6f,right-left,7.2f,3.2f,1.f);
        if(thumb>left){
            g.setGradientFill(juce::ColourGradient(
                juce::Colour(0xff47e0d0),left,centre,
                juce::Colour(0xff14998f),right,centre,false));
            g.fillRoundedRectangle(left,centre-2.5f,
                thumb-left,5.f,2.f);
        }
        g.setColour(juce::Colour(0xff142c32));
        g.fillEllipse(thumb-8.f,centre-8.f,16.f,16.f);
        g.setColour(juce::Colour(0xff9acac9));
        g.drawEllipse(thumb-7.f,centre-7.f,14.f,14.f,1.f);
        g.setColour(slider.isMouseOverOrDragging()?
            juce::Colour(0xffefffff):juce::Colour(0xffbce9e4));
        g.fillEllipse(thumb-4.5f,centre-4.5f,9.f,9.f);
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,
                          float proportion,float start,float end,
                          juce::Slider&) override {
        const float size=std::min(float(width),float(height))-6.f;
        const float cx=x+width*.5f,cy=y+height*.5f;
        const float r=size*.45f;
        const float angle=start+std::clamp(proportion,0.f,1.f)*(end-start);
        g.setColour(juce::Colour(0xff0a1418));
        g.fillEllipse(cx-r-4.f,cy-r-3.f,2*r+8.f,2*r+8.f);
        g.setColour(juce::Colour(0xff4c5c63));
        g.drawEllipse(cx-r,cy-r,2*r,2*r,3.f);
        juce::Path ring;
        ring.addCentredArc(cx,cy,r,r,0.f,start,angle,true);
        g.setColour(juce::Colour(0xffdc80e1));
        g.strokePath(ring,juce::PathStrokeType(3.2f));
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff424b53),cx-r*.5f,cy-r,
            juce::Colour(0xff151d25),cx+r*.5f,cy+r,false));
        g.fillEllipse(cx-r*.80f,cy-r*.80f,1.6f*r,1.6f*r);
        g.setColour(juce::Colour(0xff70838b));
        g.drawEllipse(cx-r*.80f,cy-r*.80f,1.6f*r,1.6f*r,1.f);
        g.setColour(juce::Colour(0xffe4e5f0));
        g.drawLine(cx+std::sin(angle)*r*.34f,
                   cy-std::cos(angle)*r*.34f,
                   cx+std::sin(angle)*r*.71f,
                   cy-std::cos(angle)*r*.71f,2.7f);
    }
private:
    static std::unique_ptr<juce::Drawable> load(const void* bytes,int size) {
        return juce::Drawable::createFromImageData(
            bytes,static_cast<std::size_t>(size));
    }
    static void draw(juce::Graphics& g,const juce::Drawable* image,
                     juce::Rectangle<float> target,bool mirror=false) {
        if(!image)return;
        juce::Graphics::ScopedSaveState state(g);
        if(mirror)
            g.addTransform(juce::AffineTransform::scale(-1.f,1.f)
                .translated(target.getX()+target.getRight(),0.f));
        image->drawWithin(g,target,juce::RectanglePlacement::centred,1.f);
    }
    std::unique_ptr<juce::Drawable> room_,panel_,line_,sub_,fill_;
};
} // namespace armix::builtin_fx
