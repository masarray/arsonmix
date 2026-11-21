// SPDX-License-Identifier: AGPL-3.0-only
// ImmerSIB v5: stage/source-conscious EARLY/DIRECT calibration only.
// Never changes official P94 single-source behavior or the accepted HRTF/FX graph.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuP94FrontBackDepthCue.h"
#include "../engine/builtin_fx/sonku/SonKuP83SpeakerScene.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
using Cue=armix::sonku::p94::FrontBackDepthCue;
constexpr float pi=3.14159265358979323846f;
constexpr std::array<float,7> acceptedSpreads{112.f,106.f,124.f,104.f,118.f,108.f,134.f};

static Cue::Gains settle(Cue& cue,int rate,float left,float right,bool enabled) {
    cue.reset();
    cue.retargetStereo(left,right,enabled);
    Cue::Gains g{};
    for(int i=0;i<rate;++i) {
        const auto next=cue.next();
        assert(std::isfinite(next.direct)&&std::isfinite(next.early));
        assert(next.direct>=.9549f&&next.direct<=1.0001f);
        assert(next.early>=.9999f&&next.early<=1.2801f);
        g=next;
    }
    return g;
}
static float shape(float headAngle) {
    const float c=std::cos(headAngle*pi/180.f);
    return c*c*c*c;
}
int main(){
    for(int rate:{44100,48000}){
        Cue cue;
        assert(cue.prepare(rate));
        cue.retarget(0.f,true); // Original approved P94 still FRONT = 1.28.
        Cue::Gains legacy{};
        for(int i=0;i<rate;++i)legacy=cue.next();
        // Original P94 uses 45ms float slew; asymptotic convergence
        // is precision-limited (~1e-4), not bit-exact at 1-second.
        // Constrain gain to <0.005dB error; do not waive DRR assertions.
        assert(std::abs(legacy.direct-.955f)<5.e-4f);
        assert(std::abs(legacy.early-1.28f)<5.e-4f);
        for(float spread:acceptedSpreads){
            const auto physical=armix::sonku::p83::stereo(0.f,spread,0.f);
            const auto stage=settle(cue,rate,physical.leftHead,
                                    physical.rightHead,true);
            const float weight=.5f*(shape(physical.leftHead)+
                                    shape(physical.rightHead));
            assert(stage.direct>legacy.direct+.035f);
            assert(stage.early<legacy.early-.20f);
            assert(std::abs(stage.direct-(1.f-.045f*weight))<.0003f);
            assert(std::abs(stage.early-(1.f+.28f*weight))<.001f);
            // Equal sources must be invariant to swapping L/R (room
            // calibration never moves or crossfeeds source audio).
            const auto reversed=settle(cue,rate,physical.rightHead,
                                        physical.leftHead,true);
            assert(stage.direct==reversed.direct&&stage.early==reversed.early);
            const auto off=settle(cue,rate,physical.leftHead,
                                  physical.rightHead,false);
            assert(off.direct==1.f&&off.early==1.f);
            const double previous=20.*std::log10(double(legacy.early)/
                                                   double(legacy.direct));
            const double adjusted=20.*std::log10(double(stage.early)/
                                                   double(stage.direct));
            assert(adjusted<previous-1.8); // real width-specific ER correction
            assert(adjusted>-.001&&adjusted<.6);
        }
        // Coincident emitters reproduce P94 legacy EXACTLY.
        const auto bothFront=settle(cue,rate,0.f,0.f,true);
        assert(bothFront.direct==legacy.direct);
        assert(bothFront.early==legacy.early);
        const auto bothSide=settle(cue,rate,90.f,-90.f,true);
        assert(bothSide.direct==1.f&&bothSide.early==1.f);
        // Head yaw, not static GUI speaker art, controls head-relative cues.
        const auto turned=armix::sonku::p83::stereo(0.f,112.f,30.f);
        const auto yaw=settle(cue,rate,turned.leftHead,turned.rightHead,true);
        const float wYaw=.5f*(shape(turned.leftHead)+shape(turned.rightHead));
        assert(std::abs(yaw.early-(1.f+.28f*wYaw))<.001f);
        const auto invalid=settle(cue,rate,0.f,
                                    std::nanf(""),true);
        assert(invalid.direct==1.f&&invalid.early==1.f);
        // Fixed-speed fade in/out, no acoustic gain jumps on stage edit.
        cue.reset();
        cue.retargetStereo(-56.f,56.f,true);
        auto last=cue.next();
        for(int i=0;i<rate/4;++i){
            const auto now=cue.next();
            assert(std::abs(now.early-last.early)<.00012f);
            last=now;
        }
        std::printf("IMMERSIB_V5_P94_STEREO_STAGE,%d,legacy_er=%.6f,v5_spread112_er=%.6f, yaw30_er=%.6f,PASS\n",
                    rate,legacy.early,settle(cue,rate,-56.f,56.f,true).early,
                    yaw.early);
    }
    std::puts("PASS P94 real PA stage: 7 scenes, yaw, front/side, OFF, legacy invariant, gain slew");
}
