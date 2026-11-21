// SPDX-License-Identifier: AGPL-3.0-only
// v4 sonic rollback test: real phantom-front center, no rejected 2k/3k EQ.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuP90StereoDecoder.h"
#include "../engine/builtin_fx/sonku/SonKuP98SceneVoicing.h"
#include "../engine/builtin_fx/sonku/SonKuImmerSIBVocalRoom.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
using armix::sonku::p90::StereoDecoder;
using armix::sonku::p98::SceneVoicing;
using armix::sonku::p99::ImmerSIBVocalRoom;
constexpr double pi=3.14159265358979323846;
static float settle(int rate,int kind) {
    StereoDecoder d;assert(d.prepare(rate));
    d.enableConcertFrontBalance(true);
    float result=0.f;
    for(int i=0;i<rate;++i) {
        const float v=.10f*std::sin(2*pi*285*i/rate);
        const float g=.10f*std::sin(2*pi*2050*i/rate);
        float l=0.f,r=0.f;
        if(kind==0){l=v;r=v;} // centered vocal
        if(kind==1){l=g;r=.25f*g;} // strongly panned acoustic
        if(kind==2){l=v+g;r=v+.25f*g;} // REAL MIX: singer and guitar
        if(kind==3){l=g;r=0.f;} // hard left
        if(kind==4){l=g;r=-g;} // antiphase SIDE
        d.observe(l,r);
        if((i&15)==0) {
            const float next=d.centerBlend();
            if(i>rate/2)
                assert(std::abs(next-result)<.016f);
            result=next;
        }
    }
    return result;
}
static void acceptedWarmSource(int rate) {
    SceneVoicing source;
    assert(source.prepare(rate));
    ImmerSIBVocalRoom room;
    assert(room.prepare(rate));
    for(int i=0;i<rate/2;++i){
        const float voice=.04f*std::sin(2*pi*285*i/rate);
        const float guitar=.03f*std::sin(2*pi*2050*i/rate);
        const auto x=source.source(voice+guitar,voice+.25f*guitar,
                                   SceneVoicing::ImmerSIBDetail,true);
        assert(std::isfinite(x.left)&&std::isfinite(x.right));
        const auto y=room.source(x.left,x.right,4,true);
        const auto z=room.late(y.left,y.right);
        assert(std::isfinite(z.left)&&std::isfinite(z.right));
    }
}
static void invariantSideTransfer(int rate){
    StereoDecoder d;assert(d.prepare(rate));
    d.enableConcertFrontBalance(true);
    for(int i=0;i<rate/3;++i){
        const float v=.07f*std::sin(2*pi*285*i/rate);
        const float g=.07f*std::sin(2*pi*2050*i/rate);
        d.observe(v+g,v+.25f*g);
        if((i&15)==0)d.centerBlend();
    }
    std::array<std::array<float,8>,2> c{},l{},r{};
    for(int ear=0;ear<2;++ear)
      for(int tap=0;tap<8;++tap){
         c[ear][tap]=.03f*(ear+1)*(tap+1);
         l[ear][tap]=.02f*(ear+1)*(tap+1);
         r[ear][tap]=-.015f*(ear+1)*(tap+1);
      }
    const auto originalL=l,originalR=r;
    d.compose(c,l,r);
    for(int ear=0;ear<2;++ear)
      for(int tap=0;tap<8;++tap)
        assert(std::abs((l[ear][tap]-r[ear][tap])-
                        (originalL[ear][tap]-originalR[ear][tap]))<1.e-6f);
}
int main() {
    for(int rate:{44100,48000}){
        const float mono=settle(rate,0),pan=settle(rate,1);
        const float mix=settle(rate,2),left=settle(rate,3);
        const float anti=settle(rate,4);
        assert(mono>.96f);
        assert(pan<.03f&&left<.03f&&anti<.03f);
        assert(mix>.28f&&mix<.84f); // natural front, not all-center
        assert(mix>pan+.25f);
        acceptedWarmSource(rate);
        invariantSideTransfer(rate);
        std::printf("IMMERSIB_V4_PHANTOM_FRONT,%d,mono=%.5f,pan=%.5f,mix=%.5f,hardL=%.5f,anti=%.5f\\n",
                   rate,mono,pan,mix,left,anti);
    }
    std::puts("PASS v4: measured single HRTF front, panned source, golden P98/P99 tonal graph");
}
