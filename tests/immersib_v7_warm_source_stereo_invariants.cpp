// SPDX-License-Identifier: AGPL-3.0-only
// A completed stereo master is not a vocal stem: supplemental warmth must
// NOT leak hard-left guitar into right virtual PA. Own P98 filter regression.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuP98SceneVoicing.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
using armix::sonku::p98::SceneVoicing;
constexpr double PI=3.1415926535897932384626433832795;
static void test(int sr,int scene,bool enabled,int pattern){
    SceneVoicing original,concert;
    assert(original.prepare(sr)&&concert.prepare(sr));
    concert.enableImmerSIBMusicalBalance(true);
    constexpr int n=32768;
    double energyDifference=0.,earBalanceDifference=0.;
    for(int i=0;i<n;++i){
        const float x=.18f*float(std::sin(2.*PI*285.*i/sr));
        const float l=(pattern==1)?0.f:x;
        const float r=(pattern==0)?0.f:(pattern==3?-x:x);
        const auto oa=original.source(l,r,scene,enabled);
        const auto ob=concert.source(l,r,scene,enabled);
        const auto a=original.finish(oa.left,oa.right);
        const auto b=concert.finish(ob.left,ob.right);
        assert(std::isfinite(a.left)&&std::isfinite(a.right));
        assert(std::isfinite(b.left)&&std::isfinite(b.right));
        if(i<n/2)continue;
        if(!enabled){
            assert(a.left==b.left && a.right==b.right);
            continue;
        }
        // An ORIGINAL hard-left input already receives approved P98
        // MID voicing from both speakers; v7 must add NO MORE to RIGHT.
        if(pattern==0)assert(a.right==b.right);
        if(pattern==1)assert(a.left==b.left);
        // True mono vocal must remain exactly symmetric.
        if(pattern==2)assert(b.left==b.right);
        // Anti-phase L/R must remain anti-phase; no new mono leakage.
        if(pattern==3)assert(std::abs(b.left+b.right)<1.e-6f);
        energyDifference+=(double(b.left-a.left)*double(b.left-a.left)
                           +double(b.right-a.right)*double(b.right-a.right));
        earBalanceDifference+=double(b.left-b.right)*(b.left-b.right);
    }
    if(enabled)assert(energyDifference>1.e-5);
    assert(std::isfinite(earBalanceDifference));
}
int main(){
    for(int sr:{44100,48000})for(int scene:{4,5})
        for(bool room:{false,true})for(int pattern=0;pattern<4;++pattern)
            test(sr,scene,room,pattern);
    std::puts("IMMERSIB_V7_P98_PAN_PRESERVING_WARM_SOURCE_PASS");
}
