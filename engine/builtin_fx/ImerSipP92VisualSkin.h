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
        distanceMetres:2.2f,1.f,4.f);
    const float width=(stage-30.f)/150.f;
    const float distanceRatio=(metres-1.f)/3.f;
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
          sceneLive_(load(BinaryData::scene_live_concert_png,BinaryData::scene_live_concert_pngSize)),
          sceneRoyal_(load(BinaryData::scene_royal_concert_hall_png,BinaryData::scene_royal_concert_hall_pngSize)),
          sceneArena_(load(BinaryData::scene_arena_thunder_png,BinaryData::scene_arena_thunder_pngSize)),
          sceneFront_(load(BinaryData::scene_front_row_power_png,BinaryData::scene_front_row_power_pngSize)),
          sceneSymphony_(load(BinaryData::scene_symphonic_palace_png,BinaryData::scene_symphonic_palace_pngSize)),
          sceneCinema_(load(BinaryData::scene_velvet_cinema_png,BinaryData::scene_velvet_cinema_pngSize)),
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
        return allValid({&room_,&header_,&sceneLive_,&sceneRoyal_,&sceneArena_,
          &sceneFront_,&sceneSymphony_,&sceneCinema_,
          &lineL_,&lineR_,&subL_,&subR_,
          &fillL_,&fillR_,&sideL_,&sideR_,&track_,&thumb_,&knobTeal_,
          &knobPurple_,&dropdown_,&buttonNormal_,&buttonHover_,&buttonPressed_});
    }
    void drawRoom(juce::Graphics& g) const {
        draw(g,room_,{0.f,0.f,1100.f,461.f});
    }
    bool hasScenes() const noexcept {
        return allValid({&sceneLive_,&sceneRoyal_,&sceneArena_,
                         &sceneFront_,&sceneSymphony_,&sceneCinema_});
    }
    void drawScene(juce::Graphics& g,int presetId) const {
        const juce::Image* art=&sceneLive_;
        switch(presetId){
            case 3: art=&sceneRoyal_; break;
            case 4: art=&sceneArena_; break;
            case 5: art=&sceneFront_; break;
            case 6: art=&sceneSymphony_; break;
            case 7: art=&sceneCinema_; break;
            default: break; // Custom keeps its last selected venue.
        }
        if(art->isNull()){
            drawRoom(g);
            return;
        }
        // Fit the original venue image proportionally. Scene photos already
        // include their correctly-perspectived PA and subwoofer cabinets.
        // Do not distort their aspect when Windows DPI or editor size changes.
        juce::Graphics::ScopedSaveState state(g);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.reduceClipRegion(0,0,1100,461);
        g.setColour(juce::Colour(0xff0e161c));
        g.fillRect(0,0,1100,461);
        g.drawImageWithin(*art,0,0,1100,461,
                          juce::RectanglePlacement::centred);
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
        const float left=float(x+6),right=float(x+width-6);
        const float cy=float(y)+float(height)*.5f;
        const float thumb=std::clamp(sliderPos,left,right);
        // Precise native vector rails avoid scaled PNG artefacts on HiDPI.
        g.setColour(juce::Colour(0xff394a54));
        g.fillRoundedRectangle(left,cy-2.f,right-left,4.f,2.f);
        if(thumb>left){
            g.setColour(juce::Colour(0xff4ac5bd));
            g.fillRoundedRectangle(left,cy-2.f,thumb-left,4.f,2.f);
        }
        g.setColour(juce::Colour(0xff0b2229));
        g.fillEllipse(thumb-7.5f,cy-7.5f,15.f,15.f);
        g.setColour(juce::Colour(0xff86dbd4));
        g.drawEllipse(thumb-7.f,cy-7.f,14.f,14.f,1.5f);
        g.setColour(juce::Colour(0xff9ddfd8));
        g.fillEllipse(thumb-2.f,cy-2.f,4.f,4.f);
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
        const auto bounds=juce::Rectangle<float>(.5f,.5f,
              float(width)-1.f,float(height)-1.f);
        g.setColour(juce::Colour(0xff1c2c33));
        g.fillRoundedRectangle(bounds,5.f);
        g.setColour(juce::Colour(0xff405c64));
        g.drawRoundedRectangle(bounds,5.f,1.f);
        juce::Path arrow;
        const float cx=float(width)-16.f,cy=float(height)*.52f;
        arrow.startNewSubPath(cx-4.f,cy-2.f);
        arrow.lineTo(cx,cy+2.f);
        arrow.lineTo(cx+4.f,cy-2.f);
        g.setColour(juce::Colour(0xff73c5c1));
        g.strokePath(arrow,juce::PathStrokeType(1.4f));
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& button,
                              const juce::Colour&,bool hover,bool down) override {
        const auto r=juce::Rectangle<float>(.5f,.5f,
                   float(button.getWidth()-1),float(button.getHeight()-1));
        const auto background=!button.isEnabled()?juce::Colour(0xff18252b):
            down?juce::Colour(0xff28505a):
            hover?juce::Colour(0xff29404b):juce::Colour(0xff203039);
        g.setColour(background);
        g.fillRoundedRectangle(r,5.f);
        g.setColour(button.isEnabled()
            ?juce::Colour(hover?0xff6fa6aa:0xff455963)
            :juce::Colour(0xff34454b));
        g.drawRoundedRectangle(r,5.f,1.f);
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
    const juce::Image sceneLive_,sceneRoyal_,sceneArena_,sceneFront_,sceneSymphony_,sceneCinema_;
    const juce::Image lineL_,lineR_,subL_,subR_,fillL_,fillR_,sideL_,sideR_;
    const juce::Image track_,thumb_,knobTeal_,knobPurple_,dropdown_;
    const juce::Image buttonNormal_,buttonHover_,buttonPressed_;
};
} // namespace armix::builtin_fx
