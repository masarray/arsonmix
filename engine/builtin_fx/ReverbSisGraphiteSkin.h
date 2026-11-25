// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Port of the owner-approved Graphite Ice Blue isolated C++ trial.
// Only called by the JUCE message thread; immutable shared raster assets,
// independent of processor state, display scale and parameter automation.
namespace armix::builtin_fx::reverb_ui::graphite {
constexpr int bodyW=480, bodyH=444, metalW=2240, metalH=860;
constexpr double cx=240., cy=444.*.515, radius=139., referenceDial=326.;
inline juce::Colour colour(int hex) {
    return juce::Colour(0xff000000u|static_cast<juce::uint32>(hex));
}
inline double clamp(double x,double a=0.,double b=1.) { return std::clamp(x,a,b); }
struct Vec { double x,y,z; };
inline Vec unit(Vec v) {
    const auto n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    return {v.x/n,v.y/n,v.z/n};
}
inline double dot(Vec a,Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline juce::uint8 channel(double x) {
    return static_cast<juce::uint8>(std::lround(clamp(x,0.,255.)));
}
inline std::uint32_t hash32(std::uint32_t v) {
    v^=v>>16; v*=0x7feb352du; v^=v>>15; v*=0x846ca68bu; return v^(v>>16);
}
inline double rand01(std::uint32_t v) { return double(hash32(v)&0xffffffu)/16777215.; }
inline double smoothNoise(int x,int y,int span) {
    const int k=x/span;
    double t=double(x%span)/span; t=t*t*(3.-2.*t);
    const auto a=rand01(std::uint32_t(y)*92821u+std::uint32_t(k)*137631u);
    const auto b=rand01(std::uint32_t(y)*92821u+std::uint32_t(k+1)*137631u);
    return a+(b-a)*t;
}
inline double noise(int x,int y) {
    const auto n=std::sin(x*127.1+y*311.7)*43758.5453;
    return n-std::floor(n);
}
inline juce::Image makeMetal() {
    juce::Image im(juce::Image::RGB,metalW,metalH,false);
    juce::Image::BitmapData pixels(im,juce::Image::BitmapData::writeOnly);
    // Separate row-coherent grain and grazing illumination; evaluate broad
    // horizontal sheen only once per column rather than once per pixel.
    std::array<double,metalW> sheen{};
    for(int x=0;x<metalW;++x) {
        const double u=x/double(metalW-1);
        sheen[static_cast<std::size_t>(x)]=5.8*std::exp(-std::pow((u-.24)/.39,2))+
                                           2.6*std::exp(-std::pow((u-.81)/.23,2));
    }
    for(int y=0;y<metalH;++y) {
        const double v=y/double(metalH-1);
        const double row=(rand01(std::uint32_t(y)*1973u+71u)-.5)*4.6;
        const double edge=2.*std::exp(-std::pow((v-.14)/.16,2));
        for(int x=0;x<metalW;++x) {
            const double grain=row+(smoothNoise(x,y,150)-.5)*3.8+
                (smoothNoise(x,y,28)-.5)*1.7+
                (rand01(std::uint32_t(x)*713u+std::uint32_t(y)*16091u)-.5)*.8;
            const double b=46.-11.*v+sheen[static_cast<std::size_t>(x)]+edge+grain;
            pixels.setPixelColour(x,y,juce::Colour::fromRGB(channel(b-2),channel(b),channel(b+3)));
        }
    }
    return im;
}
// Highest ray intersection with the radial profile, in orthographic top view.
// Invert its monotonic descending upper branch analytically instead of the
// trial's repeated height marching + binary search. Same profile and slope.
inline std::array<double,2> surface(double r) {
    constexpr double p[][2]={{.065,.87},{.21,.842},{.27,.817},
        {.305,.788},{.335,.749},{.352,.696},{.372,0.}};
    if(r>.87) return {-1.,0.};
    for(int i=6;i>=1;--i) {
        if(r<=p[i-1][1]) {
            const double slope=(p[i][1]-p[i-1][1])/(p[i][0]-p[i-1][0]);
            return {p[i-1][0]+(r-p[i-1][1])/slope,slope};
        }
    }
    return {-1.,0.};
}
inline juce::Rectangle<float> disc(float x,float y,float r) {
    return {x-r,y-r,2.f*r,2.f*r};
}
inline juce::Image makeKnob() {
    juce::Image im(juce::Image::ARGB,bodyW,bodyH,true);
    {
        juce::Graphics g(im);
        const float x=static_cast<float>(cx), y=static_cast<float>(cy), s=static_cast<float>(radius);
        // Cairo radial shadow starts at .54R; replicate stop positions in
        // JUCE's zero-origin radial gradient, then the metal collar.
        juce::ColourGradient shadow(juce::Colours::black.withAlpha(.9f),x,y+s*.04f,
            juce::Colours::transparentBlack,x+s*1.04f,y+s*.04f,true);
        shadow.addColour(.54/1.04,juce::Colours::black.withAlpha(.9f));
        shadow.addColour((.54+.58*.50)/1.04,juce::Colours::black.withAlpha(.82f));
        shadow.addColour((.54+.83*.50)/1.04,juce::Colours::black.withAlpha(.27f));
        g.setGradientFill(shadow);g.fillRect(im.getBounds());
        juce::ColourGradient collar(colour(0x0b0d0f),x,y-s*.93f,
            colour(0x35383a),x,y+s*.93f,false);
        collar.addColour(.48,colour(0x111315));
        g.setGradientFill(collar);g.fillEllipse(disc(x,y,s*.925f));
        g.setColour(colour(0x0b0d0e));g.fillEllipse(disc(x,y,s*.894f));
    }
    juce::Image::BitmapData pixels(im,juce::Image::BitmapData::readWrite);
    const Vec light=unit({-.55,-.60,.82}), half=unit({light.x,light.y,light.z+1.});
    for(int py=int(std::floor(cy-radius));py<int(std::ceil(cy+radius));++py)
      for(int px=int(std::floor(cx-radius));px<int(std::ceil(cx+radius));++px) {
        const double wx=(px+.5-cx)/radius, wy=(py+.5-cy)/radius, r=std::hypot(wx,wy);
        const auto hit=surface(r);
        if(hit[0]<0.)continue;
        const double h=hit[0], phi=std::atan2(wy,wx), nx=r>1.e-8?wx/r:0., ny=r>1.e-8?wy/r:0.;
        const double t=h<.27?std::sin(phi*112.)*.085*(1.-clamp((h-.20)/.07)):0.;
        const Vec n=unit({nx-ny*t,ny+nx*t,-hit[1]});
        const double ndl=std::max(0.,dot(n,light)),ndh=std::max(0.,dot(n,half)),ndv=std::max(0.,n.z);
        const bool face=h>.335, bevel=h>.27&&!face;
        const double rough=face?.53:bevel?.30:.62;
        const double spec=std::pow(ndh,2./(rough*rough))*(face?9.:bevel?38.:10.);
        const double ao=.72+.28*clamp(h/.19), grazing=std::pow(1.-ndv,3)*6.;
        const double grain=(noise(px,py)-.5)*(face?3.5:2.2);
        const double micro=face?std::sin(r*410.+noise(px,py)*1.1)*.45:std::cos(phi*112.)*1.5;
        const double b=((face?15.:bevel?11.:9.)+(face?20.:bevel?32.:23.)*ndl+
            spec+grazing)*ao+grain+micro+(face?(-wy*.9-wx*.6)*4.:0.);
        pixels.setPixelColour(px,py,juce::Colour::fromRGB(channel(b*.96),channel(b),channel(b*1.055)));
    }
    return im;
}
struct Assets {
    juce::Image knob,metal;
    Assets():knob(makeKnob()),metal(makeMetal()) {}
};
// One bounded cache for all Reverb-Sis instances (~8.6 MB logical pixel
// payload, backend-dependent stride). No per-dial, per-frame or RT rebuild.
// Only accessed from native editor construction/paint on the message thread.
inline const Assets& assets() { static const Assets cache; return cache; }
inline void drawMetal(juce::Graphics& g,const juce::Path& path,int width,int height) {
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(path);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(assets().metal,juce::Rectangle<float>(0.f,0.f,float(width),float(height)));
}
inline void stroke(juce::Graphics& g,float x,float y,float x2,float y2,float w,juce::Colour c) {
    juce::Path p; p.startNewSubPath(x,y);p.lineTo(x2,y2);
    g.setColour(c);g.strokePath(p,juce::PathStrokeType(w,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
}
inline void drawKnob(juce::Graphics& g,float x,float y,float size,float value,float start,float end) {
    const float s=size/static_cast<float>(referenceDial), v=juce::jlimit(0.f,1.f,value);
    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::translation(-float(cx),-float(cy)).scaled(s).translated(x,y));
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImageAt(assets().knob,0,0);
    constexpr float X=float(cx),Y=float(cy),R=float(radius);
    for(int i=0;i<27;++i) {
        const float phase=i/26.f, a=start+(end-start)*phase;
        const float sn=std::sin(a), cs=-std::cos(a), xx=X+sn*1.10f*R,yy=Y+cs*1.10f*R;
        const bool lit=v>0.f&&phase<=v+1.e-7f;
        const auto tick=[&](float w,juce::Colour c) {
            stroke(g,xx-sn*3.5f,yy-cs*3.5f,xx+sn*3.5f,yy+cs*3.5f,w,c);
        };
        tick(5.5f,colour(0x090b0d));
        if(lit)for(int j=9;j>=3;j-=2)tick(float(j),colour(0x40aef7).withAlpha(.035f+(9-j)*.008f));
        tick(2.3f,colour(lit?0xb1e6ff:0x454b50));
    }
    const float a=start+(end-start)*v, px=X+std::sin(a)*.59f*R,py=Y-std::cos(a)*.59f*R;
    g.setColour(colour(0x080a0b));g.fillEllipse(disc(px,py,7.3f));
    g.setGradientFill(juce::ColourGradient(colour(0x141719),px,py-6.f,
        colour(0x5d6265),px,py+6.f,false));g.fillEllipse(disc(px,py,6.2f));
    juce::ColourGradient glow(colour(0x43bfff).withAlpha(.5f),px,py,
        colour(0x43bfff).withAlpha(0.f),px+13.f,py,true);
    glow.addColour(2./13.,colour(0x43bfff).withAlpha(.5f));
    g.setGradientFill(glow);g.fillEllipse(disc(px,py,13.f));
    juce::ColourGradient led(colour(0xe7f8ff),px-1.3f,py-1.5f,
        colour(0x59a8d9),px+4.1f,py,true);
    led.addColour(.6,colour(0xb5e7ff));
    g.setGradientFill(led);g.fillEllipse(disc(px,py-.25f,4.1f));
}
} // namespace armix::builtin_fx::reverb_ui::graphite
