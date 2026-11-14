// SPDX-License-Identifier: AGPL-3.0-only
// Exact 140-tap native-room FIR invariance across ring wrap, moving HRIR,
// two independent stereo emitters and dynamic spread changes.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuEarlyReflections.h"
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
int main(){
    using namespace armix::sonku;
    constexpr double pi=3.14159265358979323846;
    for(double rate:{44100.,48000.}){
        for(float spread:{0.f,60.f,120.f}){
            for(int mode=0;mode<3;++mode){
                auto ref=std::make_unique<SonKuEarlyReflections>();
                auto fast=std::make_unique<SonKuEarlyReflections>();
                assert(ref->prepare(rate)&&fast->prepare(rate));
                assert(!fast->setDirectionalContiguousHrir(true));
                assert(ref->setDirectionalHrir(true));
                assert(fast->setDirectionalHrir(true));
                assert(fast->setDirectionalContiguousHrir(true));
                assert(fast->contiguousHrirEnabled());
                assert(ref->setShape(SonKuRoomShape::Square));
                assert(fast->setShape(SonKuRoomShape::Square));
                assert(ref->setAmount(.55f)&&fast->setAmount(.55f));
                assert(!fast->setDirectionalContiguousHrir(false));
                double energy=0.;
                // 24576 crosses ring wrap x3; at least one frame enters
                // each of the fast/fallback windows at both sample rates.
                for(int i=0;i<24576;++i){
                    const double t=i/rate;
                    const float a=mode==2?(i%1973==0?.30f:0.f):
                        .14f*std::sin(2*pi*220*t)+
                        .047f*std::sin(2*pi*3200*t);
                    const float b=mode==2?(i%1613==0?.22f:0.f):
                        (mode==0? a : .12f*std::sin(2*pi*470*t)-
                                        .057f*std::sin(2*pi*3200*t));
                    const float az=static_cast<float>(360.*i/12288.);
                    const float activeSpread=(i>12000&&i<16000)
                        ?90.f:spread;
                    const auto original=ref->process(a,b,az,activeSpread);
                    const auto optimized=fast->process(a,b,az,activeSpread);
                    assert(std::isfinite(optimized.left));
                    assert(std::isfinite(optimized.right));
                    assert(std::abs(original.left-optimized.left)<2.e-6f);
                    assert(std::abs(original.right-optimized.right)<2.e-6f);
                    energy+=original.left*original.left+
                            original.right*original.right;
                }
                if(mode!=0 || spread!=0.f)assert(energy>.0001);
                assert(fast->contiguousHrirEnabled());
                assert(fast->setAmount(0.f));
                assert(ref->setAmount(0.f));
                for(int i=0;i<9000;++i){
                    auto x=ref->process(0.f,0.f,0.,spread);
                    auto y=fast->process(0.f,0.f,0.,spread);
                    assert(std::abs(x.left-y.left)<1.e-6f);
                    assert(std::abs(x.right-y.right)<1.e-6f);
                }
                assert(!fast->active());
                assert(fast->setDirectionalContiguousHrir(false));
            }
        }
        // The same opt-in mode must also work through the native core.
        auto core=std::make_unique<SonKuSpatialCore>();
        assert(core->prepare(rate));
        assert(core->setDirectionalEarlyReflectionsHrir(true));
        assert(core->setDirectionalContiguousHrir(true));
        assert(core->setEarlyReflectionsShape(SonKuRoomShape::Square));
        assert(core->setEarlyReflectionsAmount(.55f));
        Settings s{};s.moving=true;s.stereoSpreadDegrees=60.f;
        assert(core->setSettings(s));
        float l[128]{},r[128]{};
        l[0]=r[0]=.13f;
        core->process(l,r,128);
        for(int i=0;i<128;++i)
            assert(std::isfinite(l[i])&&std::isfinite(r[i]));
    }
    std::puts("PASS P4 full-140 KEMAR FIR PCM, 44.1/48k, 0/60/120 spread, 3 ring wraps");
}
