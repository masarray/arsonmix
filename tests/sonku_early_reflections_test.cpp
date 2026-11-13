// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuEarlyReflections.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
int main(){
    using armix::sonku::SonKuEarlyReflections;
    SonKuEarlyReflections invalid;
    assert(!invalid.prepare(96000.));
    assert(!invalid.prepare(std::numeric_limits<double>::quiet_NaN()));
    for(double rate:{44100.,48000.}){
        SonKuEarlyReflections a,b;
        assert(a.prepare(rate)&&b.prepare(rate));
        assert(!a.active());
        assert(!a.setAmount(-.01f));
        assert(!a.setAmount(std::numeric_limits<float>::infinity()));
        assert(a.setAmount(.55f)&&b.setAmount(.55f));
        double echo=0.,earDifference=0.;
        constexpr int count=4096;
        for(int i=0;i<count;++i){
            const float left=i==0?1.f:0.f;
            const auto x=a.process(left,0.f,90.,0.f);
            const auto y=b.process(left,0.f,90.,0.f);
            assert(std::isfinite(x.left)&&std::isfinite(x.right));
            assert(std::abs(x.left-y.left)<1.e-7f);
            assert(std::abs(x.right-y.right)<1.e-7f);
            if(i<180)assert(x.left==0.f && x.right==0.f);
            if(i>180){
                echo+=std::abs(x.left)+std::abs(x.right);
                earDifference+=std::abs(x.left-x.right);
            }
        }
        assert(echo>.004 && earDifference>.001);
        // Switching the optional room off must settle to bit-exact silence;
        // the dry measured HRTF never borrows anything from stale room state.
        assert(a.setAmount(0.f));
        for(int i=0;i<6000;++i){
            auto tail=a.process(0.f,0.f,180.,0.f);
            if(i>5000)assert(tail.left==0.f&&tail.right==0.f);
        }
        assert(!a.active());
    }
    return 0;
}
