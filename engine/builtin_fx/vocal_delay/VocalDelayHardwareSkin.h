// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
// Original ArSonMix mixer artwork: site/preview-v2/assets/mixer-gain-knob-brushed-steel.png
// Source blob 7bae3ba396c9c952d7e549e7236bf4ece257d147, embedded byte-identically.
// Single immutable decoded asset; no audio callback access.
namespace armix::builtin_fx::vocal_delay::hardware {
inline const juce::Image& knob(){
    static const auto im=[]{
        juce::MemoryOutputStream bytes;
        if(!juce::Base64::convertFromBase64(bytes,"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"))return juce::Image{};
        return juce::ImageFileFormat::loadFrom(bytes.getData(),bytes.getDataSize());
    }();return im;
}
inline juce::Colour col(unsigned rgb){return juce::Colour(0xff000000u|rgb);}
inline juce::Font font(float size){return juce::Font(juce::FontOptions(size).withName("Segoe UI"));}
inline void inset(juce::Graphics& g,juce::Rectangle<float> r,float radius=4.f){
    g.setColour(col(0x15171a));g.fillRoundedRectangle(r,radius);
    g.setColour(col(0x51575c));g.drawRoundedRectangle(r, radius,1.f);
    g.setColour(col(0x06090a));g.drawRoundedRectangle(r.reduced(1.f),radius-1.f,1.f);
}
inline const juce::Image& metal(){
    static const auto im=[]{
        juce::Image a(juce::Image::RGB,1240,600,false);
        juce::Image::BitmapData pix(a,juce::Image::BitmapData::writeOnly);
        for(int y=0;y<600;++y)for(int x=0;x<1240;++x){
            const unsigned n=(unsigned(x)*1664525u+unsigned(y)*1013904223u);
            const float t=float(y)/600.f;
            const int v=int(47.f-20.f*t+4.f*std::sin(float(y)*1.7f)+float((n>>22)&3u));
            pix.setPixelColour(x,y,juce::Colour::fromRGB(juce::uint8(v),juce::uint8(v+2),juce::uint8(v+4)));
        }return a;
    }();return im;
}
inline void chassis(juce::Graphics& g,int w,int h){
    g.fillAll(col(0x141719));g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(metal(),juce::Rectangle<float>(5,5,float(w-10),float(h-10)));
    g.setColour(col(0x777e82));g.drawRoundedRectangle(5,5,float(w-10),float(h-10),5,1);
    g.setColour(col(0x101417));g.drawRoundedRectangle(7,7,float(w-14),float(h-14),4,1);
    for(auto x:{11.f,float(w-11)})for(auto y:{11.f,float(h-11)}){
        g.setColour(col(0x141719));g.fillEllipse(x-2.8f,y-2.8f,5.6f,5.6f);
        g.setColour(col(0x70777b));g.drawLine(x-1.5f,y+1.f,x+1.5f,y-1.f,.8f);
    }
}
class Look final:public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float start,float end,juce::Slider& s) override{
        const float r=float(juce::jmin(w,h))*.43f,cx=float(x)+float(w)*.5f,cy=float(y)+float(h)*.5f;
        if(r<10)return;
        const float radius=r*.88f;
        const auto a=start+pos*(end-start);
        const int segments=r>35?25:15;
        for(int i=0;i<segments;++i){
            const float p=float(i)/float(segments-1),angle=start+p*(end-start);
            const float sx=cx+std::sin(angle)*r,sy=cy-std::cos(angle)*r;
            const float ex=cx+std::sin(angle)*(r-4.f),ey=cy-std::cos(angle)*(r-4.f);
            g.setColour(col(0x101416));g.drawLine(sx,sy,ex,ey,3.4f);
            g.setColour(col(s.isEnabled()&&p<=pos&&pos>0?0x8ce7d1:0x70777b));g.drawLine(sx,sy,ex,ey,1.6f);
        }
        g.setColour(juce::Colours::black.withAlpha(.55f));g.fillEllipse(cx-radius,cy-radius+3,2*radius,2*radius);
        const auto& im=knob();
        if(im.isValid()){g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
            g.drawImage(im,juce::Rectangle<float>(cx-radius,cy-radius,2*radius,2*radius));}
        else{g.setColour(col(0x555d63));g.fillEllipse(cx-radius,cy-radius,2*radius,2*radius);}
        juce::Path needle;needle.addRoundedRectangle(-1.7f,-radius*.69f,3.4f,radius*.31f,1.5f);
        needle.applyTransform(juce::AffineTransform::rotation(a).translated(cx,cy));
        g.setColour(col(0x081513));g.strokePath(needle,juce::PathStrokeType(2.f));
        g.setColour(col(s.isEnabled()?0xb5ffdf:0x7b8588));g.fillPath(needle);
    }
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour& colour,bool over,bool down) override{
        const auto r=b.getLocalBounds().toFloat().reduced(1.f);
        const auto base=colour.isTransparent()?col(0x393e42):colour;
        inset(g,r,4);
        g.setGradientFill(juce::ColourGradient(base.brighter(over?.28f:.14f),0,r.getY(),base.darker(down?.40f:.22f),0,r.getBottom(),false));
        g.fillRoundedRectangle(r.reduced(2.f),2.5f);
    }
    juce::Font getTextButtonFont(juce::TextButton&,int height) override{return font(float(juce::jmin(12,height-5)));}
    void drawComboBox(juce::Graphics& g,int width,int height,bool pressed,int,int,int,int,juce::ComboBox&) override{
        inset(g,{1,1,float(width-2),float(height-2)},4);
        g.setColour(col(pressed?0xc2f8e2:0x87c5b4));
        juce::Path arrow;arrow.startNewSubPath(float(width-16),float(height)*.43f);
        arrow.lineTo(float(width-11),float(height)*.60f);arrow.lineTo(float(width-6),float(height)*.43f);
        g.strokePath(arrow,juce::PathStrokeType(1.4f));
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override{return font(12.f);}
};
} // namespace armix::builtin_fx::vocal_delay::hardware
