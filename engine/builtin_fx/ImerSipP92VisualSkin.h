// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Only six approved concert venue photographs; all controls rendered by JUCE.
// The rejected speaker/button/knob PNG kit must never return.
#include <BinaryData.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace armix::builtin_fx {

class ImerSipP92VisualSkin final : public juce::LookAndFeel_V4 {
public:
    // Fixed pixel master matches the 1100x461 logical stage exactly (2x).
    static constexpr int photoWidth=2200;
    static constexpr int photoHeight=922;
    ImerSipP92VisualSkin() {
        // Defer photo decode until paint(): first editor only needs one
        // scene, not six 2200x922 images. Existing ImageCache stays shared.
        setColour(juce::ComboBox::textColourId,juce::Colour(0xffdce8e9));
        setColour(juce::ComboBox::outlineColourId,juce::Colours::transparentBlack);
        setColour(juce::ComboBox::arrowColourId,juce::Colour(0xff55dad0));
        setColour(juce::PopupMenu::backgroundColourId,juce::Colour(0xff17242a));
        setColour(juce::PopupMenu::textColourId,juce::Colour(0xffe0edee));
        setColour(juce::PopupMenu::highlightedBackgroundColourId,juce::Colour(0xff225d65));
        setColour(juce::TextButton::textColourOffId,juce::Colour(0xffeef3f3));
        setColour(juce::TextButton::textColourOnId,juce::Colour(0xffefffff));
    }
    // Explicit native QA can validate all six. Constructor loads none.
    bool ready() const {
        return allValid({&sceneImage(0),&sceneImage(1),&sceneImage(2),
                         &sceneImage(3),&sceneImage(4),&sceneImage(5)});
    }
    bool hasScenes() const { return ready(); }
    std::size_t residentVenueCount() const noexcept {
        return static_cast<std::size_t>(std::count_if(
            sceneImages_.begin(),sceneImages_.end(),
            [](const juce::Image& photo){return !photo.isNull();}));
    }
    void drawScene(juce::Graphics& g,int presetId) const {
        std::size_t sceneIndex=0;
        switch(presetId){
            case 3: sceneIndex=1; break;
            case 4: sceneIndex=2; break;
            case 5: sceneIndex=3; break;
            case 6: sceneIndex=4; break;
            case 7: sceneIndex=5; break;
            case 8: sceneIndex=0; break; // XL shares Live Concert photo
            default: break; // Custom retains selected photo.
        }
        const auto& art=sceneImage(sceneIndex);
        // Fail closed on an absent/mismatched scene rather than allowing
        // centered 2:1 art to create black matte on either side.
        if(art.isNull() || art.getWidth()!=photoWidth ||
           art.getHeight()!=photoHeight) {
            jassertfalse;
            return;
        }
        // The approved 2x PNG has the exact logical viewport ratio.
        // Centred placement now fills edge-to-edge, without distortion.
        juce::Graphics::ScopedSaveState state(g);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.reduceClipRegion(0,0,1100,461);
        g.setColour(juce::Colour(0xff0e161c));
        g.fillRect(0,0,1100,461);
        g.drawImageWithin(art,0,0,1100,461,
                          juce::RectanglePlacement::centred);
    }
    void drawBypassedVenueOverlay(juce::Graphics& g) const {
        // A strong but restrained mute treatment: the six approved concert
        // photos remain visible, but stage lights and colours recede into
        // charcoal. No new PNGs, image processing, bloom or animation.
        g.setColour(juce::Colour(0xb803060a)); // ~72% near-black charcoal veil
        g.fillRect(0,0,1100,461);

        // One dominant, unmistakable state label. This deliberately has
        // more hierarchy than the original tiny 40px regular typography.
        // Keep the entire word inside the scene safe area on every DPI.
        const auto label=juce::Rectangle<int>(110,153,880,150);
        auto font=juce::Font(juce::FontOptions(76.f,juce::Font::bold));
        font.setExtraKerningFactor(0.055f);
        g.setFont(font);
        g.setColour(juce::Colour(0xb8000307));
        g.drawText("BYPASSED",label.translated(0,4),
                   juce::Justification::centred,false);
        g.setColour(juce::Colour(0xeeffa53c)); // vivid translucent amber
        g.drawText("BYPASSED",label,juce::Justification::centred,false);
    }
    void drawUnavailableVenueOverlay(juce::Graphics& g) const {
        // A retired processor lease is neither FX ON nor an accepted
        // BYPASSED state. Its last concert scene remains a recognisable
        // photograph, but the host readback is explicitly unavailable.
        g.setColour(juce::Colour(0xc003060a));
        g.fillRect(0,0,1100,461);
        auto headline=juce::Font(juce::FontOptions(49.f));
        headline.setExtraKerningFactor(0.035f);
        g.setFont(headline);
        g.setColour(juce::Colour(0xffd0dce1));
        g.drawText("FX UNAVAILABLE",110,166,880,100,
                   juce::Justification::centred,false);
        g.setFont(juce::FontOptions(20.f));
        g.setColour(juce::Colour(0xff87b8bc));
        g.drawText("REOPEN FROM FX RACK",110,267,880,45,
                   juce::Justification::centred,false);
    }
    void drawPanel(juce::Graphics& g) const {
        // Premium restraint: one exact flat matte charcoal surface,
        // no image texture, gloss, neon edges, bolts, segment borders,
        // or geometric noise. DPI-independent and low-cost to render.
        g.setColour(juce::Colour(0xff151a20));
        g.fillRect(0,461,1100,219);
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
        // Native JUCE satin-metal control. No rejected knob PNG.
        const float size=std::max(8.f,std::min(float(width),float(height))-5.f);
        const float cx=float(x)+float(width)*.5f;
        const float cy=float(y)+float(height)*.5f;
        const float radius=size*.405f;
        g.setColour(juce::Colour(0x5500050b));
        g.fillEllipse(cx-radius-2.f,cy-radius+2.f,2.f*radius+4.f,2.f*radius+4.f);
        g.setColour(juce::Colour(0xff536775));
        g.fillEllipse(cx-radius,cy-radius,2.f*radius,2.f*radius);
        g.setColour(juce::Colour(0xff0e181f));
        g.fillEllipse(cx-radius+2.f,cy-radius+2.f,2.f*radius-4.f,2.f*radius-4.f);
        const float inner=radius*.76f;
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff344550),cx,cy-inner,
            juce::Colour(0xff152129),cx,cy+inner,false));
        g.fillEllipse(cx-inner,cy-inner,2.f*inner,2.f*inner);
        const float angle=start+std::clamp(proportion,0.f,1.f)*(end-start);
        juce::Path arc;
        arc.addCentredArc(cx,cy,radius+7.f,radius+7.f,0.f,start,angle,true);
        g.setColour(juce::Colour(0xffbc8de4));
        g.strokePath(arc,juce::PathStrokeType(2.f));
        const float pointer=radius*.59f;
        g.setColour(juce::Colour(0xfff0f7f8));
        g.drawLine(cx+std::sin(angle)*pointer*.49f,
                   cy-std::cos(angle)*pointer*.49f,
                   cx+std::sin(angle)*pointer,
                   cy-std::cos(angle)*pointer,2.4f);
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
        if(button.getComponentID()=="imersip.power") {
            // Same simple IEC power-ring language as built-in Compressor/EQ.
            // Body and glyph are native JUCE; no image, font glyph, LED,
            // faux switch, label, or GUI-side invented bypass state.
            const auto bounds=juce::Rectangle<float>(.5f,.5f,
                float(button.getWidth()-1),float(button.getHeight()-1));
            const bool enabled=button.isEnabled();
            const bool on=enabled && button.getToggleState();
            const auto colour=!enabled?juce::Colour(0xff6b7981):
                on?juce::Colour(0xff73d9d0):juce::Colour(0xffa6b6bd);
            g.setColour(down?juce::Colour(0xff263941):
                       hover&&enabled?juce::Colour(0xff21323a):
                                       juce::Colour(0xff1c262d));
            g.fillRoundedRectangle(bounds,6.f);
            g.setColour(juce::Colour(hover&&enabled?0xff4c6870:0xff34464e));
            g.drawRoundedRectangle(bounds,6.f,1.f);
            const float cx=float(button.getWidth())*.5f;
            const float cy=float(button.getHeight())*.52f;
            const float radius=std::min(float(button.getWidth()),
                                        float(button.getHeight()))*.244f;
            juce::Path powerRing;
            powerRing.addCentredArc(cx,cy,radius,radius,0.f,
                .63f,juce::MathConstants<float>::twoPi-.63f,true);
            g.setColour(colour);
            g.strokePath(powerRing,juce::PathStrokeType(
                2.5f,juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
            g.drawLine(cx,cy-radius-3.f,cx,cy-1.f,2.5f);
            return;
        }
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
        for(const auto* image:images)
            if(image->isNull() || image->getWidth()!=photoWidth ||
               image->getHeight()!=photoHeight) return false;
        return true;
    }
    static juce::Image load(const void* bytes,int size) {
        return juce::ImageCache::getFromMemory(bytes,size);
    }
    // Acquired on JUCE message thread only, never from PCM callback.
    // Index maps approved seven presets onto six immutable photos.
    const juce::Image& sceneImage(std::size_t index) const {
        jassert(index<sceneImages_.size());
        auto& image=sceneImages_[index];
        if(!image.isNull())return image;
        switch(index) {
            case 0: image=load(BinaryData::scene_live_concert_png,BinaryData::scene_live_concert_pngSize);break;
            case 1: image=load(BinaryData::scene_royal_concert_hall_png,BinaryData::scene_royal_concert_hall_pngSize);break;
            case 2: image=load(BinaryData::scene_arena_thunder_png,BinaryData::scene_arena_thunder_pngSize);break;
            case 3: image=load(BinaryData::scene_front_row_power_png,BinaryData::scene_front_row_power_pngSize);break;
            case 4: image=load(BinaryData::scene_symphonic_palace_png,BinaryData::scene_symphonic_palace_pngSize);break;
            case 5: image=load(BinaryData::scene_velvet_cinema_png,BinaryData::scene_velvet_cinema_pngSize);break;
            default: jassertfalse; break;
        }
        return image;
    }
    mutable std::array<juce::Image,6> sceneImages_{};
};
} // namespace armix::builtin_fx
