// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Native port of the owner-approved Studio Rack V6 Canvas prototype.
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
// Studio Rack V6 uses a clean satin faceplate, not row-correlated noise.
inline double noise(int x,int y) {
    const auto n=std::sin(x*127.1+y*311.7)*43758.5453;
    return n-std::floor(n);
}
inline juce::Image makeMetal() {
    juce::Image im(juce::Image::RGB,metalW,metalH,false);
    juce::Image::BitmapData pixels(im,juce::Image::BitmapData::writeOnly);
    // V6 two-circle radial gradient: (560,75,25) to (560,100,620).
    // Solve |point - centre(t)| = radius(t); then interpolate RGB stops.
    constexpr double dr=595.,dy=25.,a=dr*dr-dy*dy;
    const auto blend=[](double u,double v,double f){return u+(v-u)*f;};
    for(int y=0;y<metalH;++y) {
        const double yy=(y+.5)*.5,py=yy-75.;
        const double b=2.*(25.*dr+py*dy);
        const double dark=.188235294*clamp((yy-187.5)/159.5);
        for(int x=0;x<metalW;++x) {
            const double xx=(x+.5)*.5,px=xx-560.;
            const double c=625.-px*px-py*py;
            const double q=clamp((-b+std::sqrt(std::max(0.,b*b-4.*a*c)))/(2.*a));
            const double pos=q<.44?q/.44:q<.76?(q-.44)/.32:(q-.76)/.24;
            const int first=q<.44?0:q<.76?1:2;
            constexpr int stops[4][3]={{68,70,70},{54,56,57},{41,43,45},{29,32,35}};
            const double lightPos=clamp((xx*1120.+(yy-26.)*304.)/(1120.*1120.+304.*304.));
            const double white=(5./255.)*(1.-clamp(lightPos/.22));
            const double black=(7./255.)*clamp((lightPos-.78)/.22);
            const auto ch=[&](int k){return channel((blend(stops[first][k],stops[first+1][k],pos)*(1.-dark)*(1.-white)+255.*white)*(1.-black));};
            pixels.setPixelColour(x,y,juce::Colour::fromRGB(ch(0),ch(1),ch(2)));
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
        const double rough=face?.48:bevel?.32:.62;
        const double spec=std::pow(ndh,2./(rough*rough))*(face?13.:bevel?43.:10.);
        const double ao=.72+.28*clamp(h/.19), grazing=std::pow(1.-ndv,3)*6.;
        const double grain=(noise(px,py)-.5)*(face?3.5:2.2);
        const double micro=face?std::sin(r*410.+noise(px,py)*1.1)*.45:std::cos(phi*112.)*1.5;
        const double b=((face?39.:bevel?16.:9.)+(face?36.:bevel?44.:23.)*ndl+
            spec+grazing)*ao+grain+micro+(face?(-wy*.9-wx*.6)*10.:0.);
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
// Single geometry authority for approved V6 tangential ring and QA.
// Canvas units; actual dial hit region is intentionally separate.
struct DialGeometry {
    bool hero;
    constexpr int count() const noexcept { return hero?23:21; }
    constexpr float scale() const noexcept { return (hero?326.f:134.f)/480.f; }
    constexpr float length() const noexcept { return hero?18.6f:20.f; }
    constexpr float height() const noexcept { return hero?7.4f:9.8f; }
    constexpr float inset() const noexcept { return hero?1.15f:1.35f; }
    constexpr float ring() const noexcept { return .925f*float(radius)+height()*.5f+1.4f+(hero?7.4f:7.6f); }
    constexpr float bezelGap() const noexcept { return (ring()-.925f*float(radius)-height()*.5f-1.4f)*scale(); }
    float chordGap() const noexcept { return (2.f*ring()*std::sin(juce::MathConstants<float>::pi*1.5f/(2.f*(count()-1)))-(length()+3.f))*scale(); }
    float extent() const noexcept { return (ring()+height()*.5f+1.4f+5.f)*scale(); }
    static juce::AffineTransform tickTransform(float a,float x,float y) noexcept {
        // Long axis = (cos a,sin a). At twelve o'clock (2pi) horizontal;
        // at three o'clock (pi/2) vertical. No erroneous extra quarter turn.
        return juce::AffineTransform::rotation(a).translated(x,y);
    }
};
inline void drawKnob(juce::Graphics& g,float x,float y,bool hero,float value,float start,float end) {
    const DialGeometry d{hero};
    const float v=juce::jlimit(0.f,1.f,value);
    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::translation(-float(cx),-float(cy)).scaled(d.scale()).translated(x,y));
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImageAt(assets().knob,0,0);
    constexpr float X=float(cx),Y=float(cy),R=float(radius);
    const float l=d.length(),h=d.height(),in=d.inset();
    for(int i=0;i<d.count();++i) {
        const float phase=float(i)/float(d.count()-1), a=start+(end-start)*phase;
        const float xx=X+std::sin(a)*d.ring(), yy=Y-std::cos(a)*d.ring();
        const bool lit=v>0.f&&phase<=v+1.e-7f;
        juce::Graphics::ScopedSaveState tickState(g);
        g.addTransform(DialGeometry::tickTransform(a,xx,yy));
        const juce::Rectangle<float> led(-l*.5f,-h*.5f,l,h);
        g.setColour(colour(0x0a1116));g.fillRoundedRectangle(led.expanded(1.5f,1.4f),1.5f);
        if(lit) {
            // Bounded soft halo, no runtime blur or unbounded dirty region.
            for(int halo=5;halo>=1;--halo) {
                g.setColour(colour(0x34d9ff).withAlpha(.025f+.012f*(5-halo)));
                g.fillRoundedRectangle(led.expanded(float(halo)),1.2f+halo);
            }
        }
        g.setColour(colour(lit?0x54dfff:0x30434e));g.fillRoundedRectangle(led,1.2f);
        juce::ColourGradient core(colour(lit?0xd7fbff:0x48616e),0.f,-h*.5f,
                                  colour(lit?0x59cfff:0x263640),0.f,h*.5f,false);
        core.addColour(.45,colour(lit?0xa1edff:0x30434d));
        g.setGradientFill(core);g.fillRoundedRectangle(led.reduced(in),.8f);
    }
    const float a=start+(end-start)*v, px=X+std::sin(a)*.665f*R,py=Y-std::cos(a)*.665f*R;
    juce::ColourGradient glow(colour(0x2fd6ff).withAlpha(.34f),px,py,
        colour(0x2fd6ff).withAlpha(0.f),px+12.f,py,true);
    g.setGradientFill(glow);g.fillEllipse(disc(px,py,12.f));
    g.setColour(colour(0x122a32));g.fillEllipse(disc(px,py,6.8f));
    juce::ColourGradient dotGradient(colour(0xefffff),px-1.8f,py-2.2f,
                                    colour(0x159cc4),px+6.2f,py,true);
    dotGradient.addColour(.26,colour(0xc7fbff));
    dotGradient.addColour(.72,colour(0x51dcff));
    g.setGradientFill(dotGradient);g.fillEllipse(disc(px,py-.12f,5.f));
}
} // namespace armix::builtin_fx::reverb_ui::graphite
