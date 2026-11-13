// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Canonical native-pixel geometry for composited Guitar FX hardware.
// Independent of JUCE, rendering, PCM, host and display DPI.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace armix::guitarfx::art {
struct Rect { float x,y,w,h;
    constexpr float cx() const noexcept { return x+w*.5f; }
    constexpr float cy() const noexcept { return y+h*.5f; }
};
struct Anchor { float x,y,diameter; };
struct Hardware {
    const char* asset;
    int width,height;
    Rect content; // nontransparent bounding box, in native PNG pixels
    std::array<Anchor,3> knobs; // centers in *native pixels*, 0 diameter = absent
    Anchor footswitch; // independent PNG + JUCE hitbox
    Anchor led; // physical indicator location (vector controlled)
    float safeMargin;
    int version;
};
inline constexpr std::array<Hardware,4> modules {{
    {"gfx-pedal-gate-body.png",300,420,{10,7,280,406},
       {{{76,131,71},{224,131,71},{150,218,71}}},
       {150,342,72},{150,277,16},10,2},
    {"gfx-pedal-drive-body.png",300,420,{10,7,280,406},
       {{{150,147,74},{0,0,0},{0,0,0}}},
       {150,342,72},{150,277,16},10,2},
    {"gfx-amp-head.png",720,300,{5,9,710,282},
       {{{210,224,64},{360,224,64},{510,224,64}}},
       {0,0,0},{88,214,14},5,2},
    {"gfx-cabinet.png",720,300,{5,9,710,282},
       {{{0,0,0},{0,0,0},{0,0,0}}},
       {0,0,0},{0,0,0},5,2}
}};
// One 1060x640 logical coordinate space; JUCE host scaling stays untouched.
inline constexpr Rect logicalStage {218.f,158.f,824.f,366.f};
inline constexpr float logicalWidth=1060.f, logicalHeight=640.f;
struct Viewport {
    float scale=1.f,dx=0.f,dy=0.f;
    static Viewport from(int width,int height) noexcept {
        const float s=std::max(.01f,std::min(width/logicalWidth,height/logicalHeight));
        return {s,(width-logicalWidth*s)*.5f,(height-logicalHeight*s)*.5f};
    }
    Rect map(Rect r) const noexcept {
        return {dx+r.x*scale,dy+r.y*scale,r.w*scale,r.h*scale};
    }
};
inline Rect fit(Rect region,const Hardware& h) noexcept {
    const float s=std::min(region.w/h.width,region.h/h.height);
    return {region.cx()-h.width*s*.5f,region.cy()-h.height*s*.5f,
            h.width*s,h.height*s};
}
inline Rect centeredControl(Rect picture,const Hardware& h,Anchor a) noexcept {
    if(a.diameter<=0.f)return {0.f,0.f,0.f,0.f};
    const float s=picture.w/h.width;
    const float d=a.diameter*s;
    return {picture.x+a.x*s-d*.5f,picture.y+a.y*s-d*.5f,d,d};
}
inline bool inside(Rect a,Rect b,float tolerance=.51f) noexcept {
    return a.x>=b.x-tolerance && a.y>=b.y-tolerance &&
           a.x+a.w<=b.x+b.w+tolerance && a.y+a.h<=b.y+b.h+tolerance;
}
inline bool overlaps(Rect a,Rect b) noexcept {
    return a.x<b.x+b.w && a.x+a.w>b.x && a.y<b.y+b.h && a.y+a.h>b.y;
}
} // namespace armix::guitarfx::art
