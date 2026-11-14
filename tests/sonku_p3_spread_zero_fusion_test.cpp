// SPDX-License-Identifier: AGPL-3.0-only
// P3 optional optimization, equality of full first-order image HRIR
// at exactly equal L/R emitter directions. No mono assumption.
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
#include <limits>

int main(){
    using namespace armix::sonku;
    constexpr double pi=3.14159265358979323846;
    for(double rate:{44100.,48000.})for(int mode=0;mode<4;++mode){
        // 0 coherent; 1 stereo independent; 2 phase-inverted;
        // 3 correlated bass + inverted treble: music-integrity stress.
        auto slow=std::make_unique<SonKuEarlyReflections>();
        auto fast=std::make_unique<SonKuEarlyReflections>();
        assert(slow->prepare(rate)&&fast->prepare(rate));
        assert(!fast->setDirectionalZeroSpreadFusion(true)); // requires P2 mode
        assert(slow->setDirectionalHrir(true)&&fast->setDirectionalHrir(true));
        assert(fast->setDirectionalZeroSpreadFusion(true));
        assert(!slow->zeroSpreadFusionEnabled());
        assert(fast->zeroSpreadFusionEnabled());
        assert(slow->setShape(SonKuRoomShape::Square));
        assert(fast->setShape(SonKuRoomShape::Square));
        assert(slow->setAmount(.55f)&&fast->setAmount(.55f));
        assert(!fast->setDirectionalZeroSpreadFusion(false)); // active rejection
        double squared=0.,error=0.,maxDiff=0.;
        for(int i=0;i<static_cast<int>(rate/2);++i){
            const double t=i/rate;
            const float a=.10f*std::sin(2.*pi*220.*t)+
                          .045f*std::sin(2.*pi*3200.*t);
            const float b=mode==0?a:mode==1
                ?.10f*std::sin(2.*pi*470.*t)+
                  .035f*std::sin(2.*pi*7400.*t)
                :mode==2?-a
                :.10f*std::sin(2.*pi*220.*t)-
                  .045f*std::sin(2.*pi*3200.*t);
            const double angle=static_cast<double>(i)*720./rate;
            const auto x=slow->process(a,b,angle,0.f);
            const auto y=fast->process(a,b,angle,0.f);
            assert(std::isfinite(y.left)&&std::isfinite(y.right));
            for(const float d:{x.left-y.left,x.right-y.right}){
                const double v=std::abs(d);
                maxDiff=std::max(maxDiff,v);error+=v*v;
            }
            squared+=x.left*x.left+x.right*x.right;
        }
        // Exact L=-R sums to zero for spread==0: a PHYSICALLY CORRECT
        // null, not a DSP failure. Music-side preservation belongs to
        // the spatial core (P0/P1 regression), not this wet-only path.
        if(mode==2)assert(squared<1.e-6);
        else assert(squared>1.e-4);
        const double errorRms=std::sqrt(error/rate);
        assert(errorRms<3.e-6&&maxDiff<2.e-5);
        // In-flight spread change automatically DISARMS fusion forever
        // until reset, while preserving both ear histories and coefficients.
        for(int i=0;i<1000;++i){
            const float a=.04f*std::sin(static_cast<float>(2*pi*700*i/rate));
            const float b=.05f*std::cos(static_cast<float>(2*pi*960*i/rate));
            const float spread=i<500?60.f:0.f;
            const auto x=slow->process(a,b,40.,spread);
            const auto y=fast->process(a,b,40.,spread);
            assert(std::abs(x.left-y.left)<3.e-5f);
            assert(std::abs(x.right-y.right)<3.e-5f);
        }
    }
    std::puts("PASS P3 source-azimuth fusion, 44.1/48k, 4 music profiles, rotation and spread edit");
}
