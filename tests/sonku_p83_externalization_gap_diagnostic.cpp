// SPDX-License-Identifier: AGPL-3.0-only
// Non-acceptance diagnostic: identify P8.2's head-locked stereo SIDE.
// A passing test proves a structural gap in P8.2, NOT externalization quality.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH
#error Diagnostic must target the actual P8.2 source profile
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using armix::sonku::SonKuSpatialCore;
using armix::sonku::Settings;
using armix::sonku::SonKuReflectionProfile;
constexpr float pi=3.14159265358979323846f;
constexpr int frames=8192;

struct Render {
    std::vector<float> left,right;
    double powerL=0.,powerR=0.;
};
static Settings settings(float azimuth,bool room){
    Settings s{};
    s.startAzimuthDegrees=azimuth;
    s.stereoSpreadDegrees=60.f;
    s.spatialMix=1.f;
    s.outputTrim=.45f;
    s.sourceDistanceMeters=1.6f;
    s.spacePreset=room?1:0;
    s.roomAmount=room?.65f:0.f;
    s.ambience=room?.35f:0.f;
    s.moving=false;
    s.directElevationDegrees=0.f;
    assert(armix::sonku::valid(s));
    return s;
}
static Render run(float azimuth,bool opposite,bool room,double rate){
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    const auto s=settings(azimuth,room);
    assert(core->setSettings(s));
    Render out{std::vector<float>(frames),std::vector<float>(frames)};
    for(int i=0;i<frames;++i){
        const float seconds=static_cast<float>(i/rate);
        const float x=.12f*std::sin(2.f*pi*1700.f*seconds)
                     +.07f*std::sin(2.f*pi*4900.f*seconds)
                     +.06f*std::sin(2.f*pi*80.f*seconds);
        out.left[i]=x;
        out.right[i]=opposite?-x:x;
    }
    core->process(out.left.data(),out.right.data(),frames);
    for(int i=frames/2;i<frames;++i){
        const double l=out.left[i],r=out.right[i];
        assert(std::isfinite(l)&&std::isfinite(r));
        assert(std::abs(l)<.98&&std::abs(r)<.98);
        out.powerL+=l*l;out.powerR+=r*r;
    }
    return out;
}
static void exposeHeadLockedSide(double rate){
    const auto front=run(0.f,true,false,rate);
    const auto right=run(90.f,true,false,rate);
    const auto rear=run(180.f,true,false,rate);
    const auto left=run(-90.f,true,false,rate);
    double maxDelta=0.,energy=0.;
    for(int i=0;i<frames;++i){
        energy+=front.left[i]*front.left[i]+front.right[i]*front.right[i];
        for(const auto* other:{&right,&rear,&left}){
            maxDelta=std::max(maxDelta,std::abs(
                static_cast<double>(front.left[i]-other->left[i])));
            maxDelta=std::max(maxDelta,std::abs(
                static_cast<double>(front.right[i]-other->right[i])));
        }
    }
    std::printf("P83_DIAGNOSTIC,%.0f,anti_phase_direct_headlocked_delta=%.10f,energy=%.6f\n",
                rate,maxDelta,energy);
    // This asserts the current limitation as an explicit REPRODUCTION.
    // A future external speaker renderer MUST FAIL this expectation when
    // actually spatializing side-only content from different directions.
    assert(energy>.1);
    assert(maxDelta<2.e-6);
}
static void verifyHorizontalEarSign(double rate){
    const auto left=run(-90.f,false,false,rate);
    const auto right=run(+90.f,false,false,rate);
    const auto front=run(0.f,false,false,rate);
    const auto rear=run(180.f,false,false,rate);
    const double leftIld=10.*std::log10(left.powerL/left.powerR);
    const double rightIld=10.*std::log10(right.powerR/right.powerL);
    const double frontIld=10.*std::log10(front.powerL/front.powerR);
    const double rearIld=10.*std::log10(rear.powerL/rear.powerR);
    std::printf("P83_EAR_SIGNS,%.0f,leftILD=%.3f,rightILD=%.3f,frontILD=%.3f,rearILD=%.3f\n",
        rate,leftIld,rightIld,frontIld,rearIld);
    assert(leftIld>2.0&&rightIld>2.0);
    assert(std::abs(frontIld)<.01&&std::abs(rearIld)<.01);
    // Good lateral ear signs DO NOT prove accurate front/rear perception.
}
static void roomCannotCureDirectSide(double rate){
    const auto noRoom=run(0.f,true,false,rate);
    const auto roomFront=run(0.f,true,true,rate);
    const auto roomRight=run(90.f,true,true,rate);
    double wetChange=0.,azDifference=0.;
    for(int i=0;i<frames;++i){
        wetChange+=std::abs(static_cast<double>(
            roomFront.left[i]-noRoom.left[i]));
        azDifference+=std::abs(static_cast<double>(
            roomFront.left[i]-roomRight.left[i]));
    }
    std::printf("P83_ROOM_EVIDENCE,%.0f,room_delta=%.6f,room_azimuth_delta=%.6f\n",
                rate,wetChange,azDifference);
    assert(wetChange>1.e-4);
    // ROOM may introduce orientation cues, but unchanged head-fixed SIDE
    // dominates the direct field. No perceptual claim is made here.
}
int main(){
    for(double rate:{44100.,48000.}){
        exposeHeadLockedSide(rate);
        verifyHorizontalEarSign(rate);
        roomCannotCureDirectSide(rate);
    }
    std::puts("PASS P8.3 DIAGNOSTIC: audible music P8.2 can remain headlocked for anti-phase SIDE at all four azimuths; lateral KEMAR ear polarity is correct; ROOM changes the mix but is not a substitute for speaker transfer");
}
