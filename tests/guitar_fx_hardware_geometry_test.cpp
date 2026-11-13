// SPDX-License-Identifier: AGPL-3.0-only
// Portable, fast proof: one hardware authoring coordinate system for paint
// composition, JUCE child bounds and HiDPI hit targets (no JUCE needed).
#include "../engine/builtin_fx/guitar/GuitarHardwareGeometry.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <array>
using namespace armix::guitarfx::art;
static bool near(float a,float b,float tolerance=1.01f) {
    return std::fabs(a-b)<tolerance;
}
int main() {
    static_assert(modules.size()==4);
    static_assert(modules[0].version==2);
    assert(!overlaps(modules[0].content,Rect{500,500,10,10}));
    for(const auto& h:modules) {
        assert(h.width>0&&h.height>0&&h.safeMargin>=3);
        assert(inside(h.content,Rect{0,0,float(h.width),float(h.height)}));
        assert(h.content.x>=h.safeMargin*.5f);
        auto picture=fit(logicalStage,h);
        assert(inside(picture,logicalStage));
        assert(near(picture.w/picture.h,float(h.width)/h.height,.001f));
        for(std::size_t i=0;i<h.knobs.size();++i) {
            const auto current=centeredControl(picture,h,h.knobs[i]);
            if(h.knobs[i].diameter==0.f)continue;
            assert(inside(current,picture));
            for(std::size_t j=0;j<i;++j) {
                if(h.knobs[j].diameter>0.f)
                    assert(!overlaps(current,centeredControl(picture,h,h.knobs[j])));
            }
            if(h.footswitch.diameter>0.f)
                assert(!overlaps(current,centeredControl(picture,h,h.footswitch)));
        }
        if(h.footswitch.diameter>0.f)
            assert(inside(centeredControl(picture,h,h.footswitch),picture));
        if(h.led.diameter>0.f)
            assert(inside(centeredControl(picture,h,h.led),picture));
        for(const auto [width,height]:std::array<std::array<int,2>,6>{{
            {{1060,640}},{{900,545}},{{1484,896}},
            {{1280,768}},{{1024,768}},{{900,700}}}}) {
            const auto view=Viewport::from(width,height);
            assert(view.scale>0.f);
            auto stage=view.map(logicalStage);
            auto mappedPicture=view.map(picture);
            assert(inside(mappedPicture,stage,1.1f));
            assert(near(mappedPicture.w/mappedPicture.h,
                        float(h.width)/h.height,.001f));
            for(const auto knob:h.knobs) {
                if(knob.diameter<=0.f)continue;
                const auto native=centeredControl(picture,h,knob);
                const auto screen=view.map(native);
                const auto px=(knob.x/h.width)*mappedPicture.w+mappedPicture.x;
                const auto py=(knob.y/h.height)*mappedPicture.h+mappedPicture.y;
                assert(near(screen.cx(),px,.15f));
                assert(near(screen.cy(),py,.15f));
                assert(inside(screen,mappedPicture,1.1f));
            }
        }
    }
    assert(modules[0].knobs[0].diameter>0.f);
    assert(modules[0].knobs[1].diameter>0.f);
    assert(modules[0].knobs[2].diameter>0.f);
    assert(modules[1].knobs[1].diameter==0.f);
    assert(modules[3].knobs[0].diameter==0.f);
    return 0;
}
