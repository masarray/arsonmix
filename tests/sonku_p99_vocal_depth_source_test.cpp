// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuP99MusicalVocalDepth.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using armix::sonku::p99::MusicalVocalDepth;

static double rmsRatio(int rate,int scene,int stage,double hz,bool phaseReversed=false,
                       float level=.07f) {
    MusicalVocalDepth e;
    assert(e.prepare(rate));
    constexpr double pi=3.1415926535897932384626433832795;
    double before=0.,after=0.;
    for(int i=0;i<rate;++i) {
        const float x=level*std::sin(2.*pi*hz*double(i)/rate);
        const float y=phaseReversed?-x:x;
        const auto v=e.source(x,y,scene,true);
        const auto a=e.early(x,y);
        const auto z=e.late(x,y);
        const float out=stage==0?v.left:stage==1?a.left:z.left;
        assert(std::isfinite(out) && std::abs(out)<1.f);
        if(scene==0)assert(out==x);
        if(phaseReversed && stage==0)assert(out==x);
        if(i>rate/2){before+=double(x)*x;after+=double(out)*out;}
    }
    return std::sqrt(after/before);
}
// Actual DSP-to-EXISTING-room input, not arbitrary post-output EQ. The
// musical vocal-body band must excite the room at 320Hz without boosting
// cymbals/sibilants at 7.6k. Both room paths must retain original SIDE.
static double roomRatio(int rate,int scene,bool early,double hz,
                        bool antiphase=false,float level=.07f){
    MusicalVocalDepth e;
    assert(e.prepare(rate));
    constexpr double pi=3.1415926535897932384626433832795;
    double before=0.,after=0.;
    for(int i=0;i<rate;++i){
        const float x=level*std::sin(2.*pi*hz*double(i)/rate);
        const float y=antiphase?-x:x;
        (void)e.source(x,y,scene,true);
        const auto wet=early?e.roomEarlyFeed(x,y):e.roomLateFeed(x,y);
        assert(std::isfinite(wet.left)&&std::isfinite(wet.right));
        assert(std::abs(wet.left)<1.f && std::abs(wet.right)<1.f);
        if(scene==0 || antiphase || level<.0001f){
            assert(wet.left==x && wet.right==y);
        }
        // A coherent center-only room-body feed MUST NOT change SIDE
        // or reintroduce stereo channels crossing from a side signal.
        assert(std::abs((wet.left-wet.right)-(x-y))<1.e-6f);
        if(i>rate/2){before+=double(x)*x;after+=double(wet.left)*wet.left;}
    }
    return std::sqrt(after/before);
}
int main(){
    MusicalVocalDepth unprepared;
    assert(!unprepared.prepare(96000));
    for(int rate:{44100,48000}){
        for(int stage=0;stage<3;++stage)
            for(double hz:{260.,1800.,7600.})
                assert(std::abs(rmsRatio(rate,0,stage,hz)-1.)<1.e-8);

        // Two musical frequency regions: warmth survives and articulation
        // becomes more audible; a 7.6 kHz sibilance shelf is NEVER added.
        const auto warmth=rmsRatio(rate,1,0,260.);
        const auto vocal=rmsRatio(rate,1,0,1800.);
        const auto airy=rmsRatio(rate,1,0,7600.);
        const auto studio=rmsRatio(rate,2,0,1800.);
        const auto cinema=rmsRatio(rate,3,0,1800.);
        // The user asked for a clearly stronger step beyond P9.9.
        // Raise LOWER audibility bounds instead of only relaxing safety.
        assert(warmth>1.102 && warmth<1.12);
        assert(vocal>1.145 && vocal<1.25);
        assert(airy>.98 && airy<1.045);
        assert(vocal>cinema && cinema>studio && studio>1.007);
        // No invented vocal image when all signal is stereo SIDE.
        assert(std::abs(rmsRatio(rate,1,0,1800.,true)-1.)<1.e-7);
        // Real first images become a little more lateral only where
        // SIDE exists, without moving/summing the center image.
        const auto earlySide=rmsRatio(rate,1,1,1800.,true);
        const auto earlyCenter=rmsRatio(rate,1,1,1800.);
        assert(earlySide>1.008 && earlySide<1.15);
        assert(std::abs(earlyCenter-1.)<1.e-8);
        // A modest midrange *late* reduction rather than cutting full
        // stereo ambience; antiphase late is untouched.
        const auto centerLate=rmsRatio(rate,1,2,1800.);
        const auto sideLate=rmsRatio(rate,1,2,1800.,true);
        assert(centerLate<.998 && centerLate>.86);
        assert(std::abs(sideLate-1.)<1.e-8);
        // Near-silent centered leakage cannot activate a loud voice gate.
        assert(rmsRatio(rate,1,0,1800.,false,.00008f)<1.012);
        const auto earlyWarm=roomRatio(rate,1,true,320.);
        const auto lateWarm=roomRatio(rate,1,false,320.);
        const auto studioWarm=roomRatio(rate,2,true,320.);
        const auto cinemaWarm=roomRatio(rate,3,true,320.);
        // Audible ROOM warmth: early response stronger than late, Signature
        // strongest, Cinema intermediate, Studio subtle. No excess HF.
        assert(earlyWarm>1.50 && earlyWarm<1.60);
        assert(lateWarm>1.22 && lateWarm<1.30);
        assert(earlyWarm>cinemaWarm && cinemaWarm>studioWarm);
        assert(studioWarm>1.035);
        assert(roomRatio(rate,1,true,7600.)<1.035);
        assert(roomRatio(rate,1,false,7600.)<1.025);
        assert(std::abs(roomRatio(rate,1,true,320.,true)-1.)<1.e-7);
        assert(std::abs(roomRatio(rate,1,false,320.,true)-1.)<1.e-7);
        assert(std::abs(roomRatio(rate,1,true,320.,false,.00008f)-1.)<1.e-7);
        assert(std::abs(roomRatio(rate,0,true,320.)-1.)<1.e-7);
        // Switching to neutral MUST return exact samples from that call.
        MusicalVocalDepth e;
        assert(e.prepare(rate));
        for(int i=0;i<rate;++i) {
            const float x=.07f*std::sin(2.*3.14159265358979323846*
                                         1800.*double(i)/rate);
            (void)e.source(x,x,1,true);
            (void)e.early(x,-x);
            (void)e.late(x,x);
        }
        const auto neutral=e.source(.084f,-.027f,0,true);
        const auto unchangedEarly=e.early(.084f,-.027f);
        const auto unchangedLate=e.late(.084f,-.027f);
        const auto neutralRoomEarly=e.roomEarlyFeed(.084f,-.027f);
        const auto neutralRoomLate=e.roomLateFeed(.084f,-.027f);
        for(const auto result:{neutral,unchangedEarly,unchangedLate,
                               neutralRoomEarly,neutralRoomLate}){
            assert(result.left==.084f && result.right==-.027f);
        }
        const auto roomOff=e.source(.084f,-.027f,1,false);
        assert(roomOff.left==.084f && roomOff.right==-.027f);
        for(const auto out:{e.roomEarlyFeed(.084f,-.027f),
                            e.roomLateFeed(.084f,-.027f)})
            assert(out.left==.084f && out.right==-.027f);
        std::printf(
           "P99_FOCUS,%d,warmth=%.6f,voice=%.6f,sibilance=%.6f,"
           "studio=%.6f,cinema=%.6f,earlySide=%.6f,"
           "lateCenter=%.6f,lateSide=%.6f,"
           "ER320=%.6f,TAIL320=%.6f,STUDIO_ER320=%.6f,CINEMA_ER320=%.6f\n",
           rate,warmth,vocal,airy,studio,cinema,
           earlySide,centerLate,sideLate,earlyWarm,lateWarm,
           studioWarm,cinemaWarm);
    }
    std::puts("PASS P9.9: musical vocal body, dynamic center focus, "
              "true directional early stereo and no high treble harshness");
}
