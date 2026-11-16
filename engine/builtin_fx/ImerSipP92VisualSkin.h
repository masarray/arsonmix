// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// ImerSip concert UI: independent full-resolution embedded RGBA PNGs.
// No SVGs or atlas crops. UI-only -- SonKu P9.1/P9.2 PCM, ACK, IMRS unchanged.
#include <BinaryData.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace armix::builtin_fx {

struct ImerSipP92ConcertBoxes {
    juce::Rectangle<float> leftArray,rightArray,leftSub,rightSub;
    juce::Rectangle<float> leftFill,rightFill,leftSide,rightSide;
    static constexpr float roomLeft=16.f,roomRight=1084.f;
    static constexpr float roomTop=100.f,roomBottom=444.f;
    bool safe() const noexcept {
        const auto inside=[](const juce::Rectangle<float>& r) {
            return r.getX()>=roomLeft && r.getRight()<=roomRight
                && r.getY()>=roomTop && r.getBottom()<=roomBottom
                && r.getWidth()>0.f && r.getHeight()>0.f;
        };
        if(!inside(leftArray)||!inside(rightArray)||
           !inside(leftSub)||!inside(rightSub)||
           !inside(leftFill)||!inside(rightFill)||
           !inside(leftSide)||!inside(rightSide)) return false;
        // The nearest cabinets must retain their own screen-space footprint.
        return leftSub.getRight()<=leftFill.getX()+.5f &&
            rightFill.getRight()<=rightSub.getX()+.5f &&
            leftArray.getRight()<=leftFill.getX()+.5f &&
            rightFill.getRight()<=rightArray.getX()+.5f &&
            leftSide.getRight()<=leftSub.getX()+.5f &&
            rightSub.getRight()<=rightSide.getX()+.5f;
    }
};

inline ImerSipP92ConcertBoxes imersipP92ConcertBoxes(
        float stageDegrees,float distanceMetres) noexcept {
    const float stage=std::clamp(std::isfinite(stageDegrees)?
        stageDegrees:150.f,30.f,180.f);
    const float metres=std::clamp(std::isfinite(distanceMetres)?
        distanceMetres:2.2f,1.6f,10.f);
    const float width=(stage-30.f)/150.f;
    const float distanceRatio=(metres-1.6f)/8.4f;
    const float span=170.f+182.f*width;
    const float arrayW=98.f-28.f*distanceRatio;
    const float arrayH=arrayW*260.f/160.f;
    const float arrayY=132.f+34.f*distanceRatio;
    const float subW=112.f-32.f*distanceRatio;
    const float subH=subW*150.f/180.f;
    const float subY=320.f+16.f*distanceRatio;
    const float fillW=60.f-6.f*distanceRatio;
    const float fillH=fillW*160.f/112.f;
    const float fillY=279.f+10.f*distanceRatio;
    const float sideW=40.f-5.f*distanceRatio;
    const float sideH=52.f-5.f*distanceRatio;
    const auto centred=[](float cx,float y,float w,float h) {
        return juce::Rectangle<float>(cx-w*.5f,y,w,h);
    };
    return {
        centred(550.f-span,arrayY,arrayW,arrayH),
        centred(550.f+span,arrayY,arrayW,arrayH),
        centred(550.f-span,subY,subW,subH),
        centred(550.f+span,subY,subW,subH),
        centred(466.f,fillY,fillW,fillH),
        centred(634.f,fillY,fillW,fillH),
        centred(550.f-span-100.f,270.f,sideW,sideH),
        centred(550.f+span+100.f,270.f,sideW,sideH)
    };
}

class ImerSipP92VisualSkin final : public juce::LookAndFeel_V4 {
public:
    ImerSipP92VisualSkin()
        : room_(load(BinaryData::room_background_png,BinaryData::room_background_pngSize)),
          header_(load(BinaryData::header_overlay_png,BinaryData::header_overlay_pngSize)),
          lineL_(load(BinaryData::line_array_left_png,BinaryData::line_array_left_pngSize)),
          lineR_(load(BinaryData::line_array_right_png,BinaryData::line_array_right_pngSize)),
          subL_(load(BinaryData::subwoofer_left_png,BinaryData::subwoofer_left_pngSize)),
          subR_(load(BinaryData::subwoofer_right_png,BinaryData::subwoofer_right_pngSize)),
          fillL_(load(BinaryData::front_fill_left_png,BinaryData::front_fill_left_pngSize)),
          fillR_(load(BinaryData::front_fill_right_png,BinaryData::front_fill_right_pngSize)),
          sideL_(load(BinaryData::side_fill_left_png,BinaryData::side_fill_left_pngSize)),
          sideR_(load(BinaryData::side_fill_right_png,BinaryData::side_fill_right_pngSize)),
          track_(load(BinaryData::slider_track_png,BinaryData::slider_track_pngSize)),
          thumb_(load(BinaryData::slider_thumb_png,BinaryData::slider_thumb_pngSize)),
          knobTeal_(load(BinaryData::knob_teal_png,BinaryData::knob_teal_pngSize)),
          knobPurple_(load(BinaryData::knob_master_purple_png,BinaryData::knob_master_purple_pngSize)),
          dropdown_(load(BinaryData::dropdown_background_png,BinaryData::dropdown_background_pngSize)),
          buttonNormal_(load(BinaryData::button_normal_png,BinaryData::button_normal_pngSize)),
          buttonHover_(load(BinaryData::button_hover_png,BinaryData::button_hover_pngSize)),
          buttonPressed_(load(BinaryData::button_pressed_png,BinaryData::button_pressed_pngSize)) {
        setColour(juce::ComboBox::textColourId,juce::Colour(0xffdce8e9));
        setColour(juce::ComboBox::outlineColourId,juce::Colours::transparentBlack);
        setColour(juce::ComboBox::arrowColourId,juce::Colour(0xff55dad0));
        setColour(juce::PopupMenu::backgroundColourId,juce::Colour(0xff17242a));
        setColour(juce::PopupMenu::textColourId,juce::Colour(0xffe0edee));
        setColour(juce::PopupMenu::highlightedBackgroundColourId,juce::Colour(0xff225d65));
        setColour(juce::TextButton::textColourOffId,juce::Colour(0xffeef3f3));
        setColour(juce::TextButton::textColourOnId,juce::Colour(0xffefffff));
    }
    bool ready() const noexcept {
        return allValid({&room_,&header_,&lineL_,&lineR_,&subL_,&subR_,
          &fillL_,&fillR_,&sideL_,&sideR_,&track_,&thumb_,&knobTeal_,
          &knobPurple_,&dropdown_,&buttonNormal_,&buttonHover_,&buttonPressed_});
    }
    void drawRoom(juce::Graphics& g) const {
        draw(g,room_,{0.f,0.f,1100.f,461.f});
    }
    void drawHeader(juce::Graphics& g) const {
        draw(g,header_,{0.f,0.f,1100.f,73.f});
    }
    void drawPanel(juce::Graphics& g) const {
        // Premium restraint: one exact flat matte charcoal surface,
        // no image texture, gloss, neon edges, bolts, segment borders,
        // or geometric noise. DPI-independent and low-cost to render.
        g.setColour(juce::Colour(0xff151a20));
        g.fillRect(0,461,1100,219);
    }
    void drawConcert(juce::Graphics& g,const ImerSipP92ConcertBoxes& b) const {
        if(!b.safe())return;
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(0,95,1100,359);
        draw(g,sideL_,b.leftSide);
        draw(g,sideR_,b.rightSide);
        draw(g,lineL_,b.leftArray);
        draw(g,lineR_,b.rightArray);
        draw(g,subL_,b.leftSub);
        draw(g,subR_,b.rightSub);
        draw(g,fillL_,b.leftFill);
        draw(g,fillR_,b.rightFill);
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,
                          float sliderPos,float minSliderPos,float maxSliderPos,
                          const juce::Slider::SliderStyle style,
                          juce::Slider& slider) override {
        if(style!=juce::Slider::LinearHorizontal){
            juce::LookAndFeel_V4::drawLinearSlider(g,x,y,width,height,
                sliderPos,minSliderPos,maxSliderPos,style,slider);
            return;
        }
        const auto left=float(x+8),right=float(x+width-8);
        const float centre=float(y)+float(height)*.5f;
        draw(g,track_,{left,centre-6.f,right-left,12.f});
        const float thumb=std::clamp(sliderPos,left,right);
        if(thumb>left){
            g.setColour(juce::Colour(0xff3bccbd));
            g.drawLine(left+3.f,centre,thumb,centre,2.2f);
        }
        draw(g,thumb_,{thumb-9.f,centre-9.f,18.f,18.f});
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,
                          float proportion,float start,float end,
                          juce::Slider&) override {
        const float size=std::min(float(width),float(height))-4.f;
        const float cx=float(x)+float(width)*.5f;
        const float cy=float(y)+float(height)*.5f;
        draw(g,knobPurple_,{cx-size*.5f,cy-size*.5f,size,size});
        const float angle=start+std::clamp(proportion,0.f,1.f)*(end-start);
        // Only the pointer/arc moves. The PNG's shadow and metal body never rotate.
        const float radius=size*.31f;
        juce::Path arc;
        arc.addCentredArc(cx,cy,size*.47f,size*.47f,0.f,start,angle,true);
        g.setColour(juce::Colour(0xffd995ef));
        g.strokePath(arc,juce::PathStrokeType(2.3f));
        g.setColour(juce::Colour(0xffeafff8));
        g.drawLine(cx+std::sin(angle)*radius*.43f,
                   cy-std::cos(angle)*radius*.43f,
                   cx+std::sin(angle)*radius,
                   cy-std::cos(angle)*radius,2.5f);
    }
    void drawComboBox(juce::Graphics& g,int width,int height,bool,
                      int,int,int,int,juce::ComboBox&) override {
        draw(g,dropdown_,{0.f,0.f,float(width),float(height)});
        juce::Path chevron;
        chevron.startNewSubPath(float(width-24),float(height)*.42f);
        chevron.lineTo(float(width-19),float(height)*.57f);
        chevron.lineTo(float(width-14),float(height)*.42f);
        g.setColour(juce::Colour(0xff77dcd5));
        g.strokePath(chevron,juce::PathStrokeType(1.6f));
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& button,
                              const juce::Colour&,bool hover,bool down) override {
        const auto& sprite=down?buttonPressed_:(hover?buttonHover_:buttonNormal_);
        draw(g,sprite,{0.f,0.f,float(button.getWidth()),float(button.getHeight())});
    }
private:
    static bool allValid(std::initializer_list<const juce::Image*> images) noexcept {
        for(const auto* image:images)if(image->isNull())return false;
        return true;
    }
    static juce::Image load(const void* bytes,int size) {
        return juce::ImageCache::getFromMemory(bytes,size);
    }
    static void draw(juce::Graphics& g,const juce::Image& img,
                     juce::Rectangle<float> target) {
        if(img.isNull())return;
        juce::Graphics::ScopedSaveState state(g);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImageWithin(img,juce::roundToInt(target.getX()),
            juce::roundToInt(target.getY()),juce::roundToInt(target.getWidth()),
            juce::roundToInt(target.getHeight()),juce::RectanglePlacement::centred);
    }
    const juce::Image room_,header_;
    const juce::Image lineL_,lineR_,subL_,subR_,fillL_,fillR_,sideL_,sideR_;
    const juce::Image track_,thumb_,knobTeal_,knobPurple_,dropdown_;
    const juce::Image buttonNormal_,buttonHover_,buttonPressed_;
};
} // namespace armix::builtin_fx
