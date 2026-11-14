// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/player/PlayerCrossfadeCurve.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
using Curve=armix::player::PlayerCrossfadeCurve;
int main() {
    const Curve curve;
    for(int total: {1,2,64,4320,24000,144000}) {
        const auto begin=curve.gains(0,total),end=curve.gains(total,total);
        assert(begin.oldGain==1.f && begin.newGain==0.f);
        assert(std::abs(end.oldGain)<0.00001f &&
               std::abs(end.newGain-1.f)<0.00001f);
        float previousOld=1.f,previousNew=0.f;
        for(int pos=0;pos<=total;++pos) {
            const auto g=curve.gains(pos,total);
            assert(std::isfinite(g.oldGain)&&std::isfinite(g.newGain));
            assert(g.oldGain<=previousOld+1e-6f);
            assert(g.newGain>=previousNew-1e-6f);
            assert(g.oldGain>=-1e-6f && g.newGain>=0.f);
            assert(std::abs(g.oldGain*g.oldGain+
                            g.newGain*g.newGain-1.f)<0.002f);
            previousOld=g.oldGain;previousNew=g.newGain;
        }
        assert(curve.gains(total*2,total).newGain==1.f);
    }
    const auto midpoint=curve.gains(500,1000);
    assert(std::abs(midpoint.oldGain-0.7071f)<0.004f);
    assert(std::abs(midpoint.newGain-0.7071f)<0.004f);
    std::puts("M3: bounded equal-power curve, endpoints and finite gain PASS");
}
