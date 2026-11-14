// SPDX-License-Identifier: AGPL-3.0-only
// P8.2 acoustic evidence from same source compiled by the Windows JUCE host.
// Parametric model evidence, NOT an empirical RIR/dearVR comparison.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) || !SONKU_P82_MULTIBAND_ROOM_RESEARCH
#error This contract must run with the P8.2 3-band room enabled
#endif
#include "../engine/builtin_fx/sonku/SonKuRoomMaterials.h"
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuEarlyReflections.h"
#include "../engine/builtin_fx/sonku/SonKuMusicalReverb.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace armix::sonku;
namespace ac=armix::sonku::room82;
constexpr float pi=3.14159265358979323846f;
constexpr auto measured=SonKuReflectionProfile::SixSurfaceKemarResearch;

static SonKuRoomGeometry geometry(ac::Family f){
    const auto& d=ac::dimensions[ac::index(f)];
    return {d.width,d.depth,1.6f,d.height,1.45f,1.45f};
}
static void materialsAndRt60(){
    assert(ac::validMaterials());
    const auto p=ac::preparePressure();
    std::array<ac::Bands,3> rt{};
    for(int room=0;room<3;++room){
        const auto fam=static_cast<ac::Family>(room);
        rt[room]=ac::eyringRt60(fam);
        assert(SonKuEarlyReflections::validGeometry(geometry(fam)));
        for(int wall=0;wall<ac::boundaries;++wall)
            for(int band=0;band<3;++band){
                const float pressure=p[room][wall][band];
                assert(std::isfinite(pressure)&&pressure>0.f&&pressure<1.f);
                assert(std::abs(pressure*pressure+
                    ac::absorption[room][wall][band]-1.f)<1e-5f);
            }
        assert(rt[room][0]>rt[room][1]);
        assert(rt[room][1]>rt[room][2]);
        for(float seconds:rt[room])assert(seconds>=.18f&&seconds<3.2f);
        std::printf("P82_RT60_MODEL,%d,LF=%.3f,MID=%.3f,HF=%.3f\n",
                    room,rt[room][0],rt[room][1],rt[room][2]);
    }
    assert(rt[0][1] < rt[1][1] && rt[1][1] < rt[2][1]);
    // These are deliberate illustrative rooms, not measured lab RT60.
    assert(rt[0][1]>.28f&&rt[0][1]<.55f);
    assert(rt[1][1]>.65f&&rt[1][1]<1.1f);
    assert(rt[2][1]>1.0f&&rt[2][1]<1.6f);
}
static double erImpulse(double rate,ac::Family family){
    SonKuEarlyReflections room;
    assert(room.prepare(rate,measured));
    const auto g=geometry(family);
    assert(room.setRealtimeRoom(g,.65f,true));
    constexpr int frames=9000;
    double energy=0.,energyAfterWall=0.;
    int first=-1;
    for(int i=0;i<frames;++i){
        const float pulse=i==0?.17f:0.f;
        auto y=room.process(pulse,pulse,38.,60.f);
        assert(std::isfinite(y.left)&&std::isfinite(y.right));
        assert(std::abs(y.left)<.98f&&std::abs(y.right)<.98f);
        const double p=y.left*y.left+y.right*y.right;
        if(p>1e-13&&first<0)first=i;
        energy+=p;
        if(i>350)energyAfterWall+=p;
    }
    std::printf("P82_ER,%.0f,%d,first=%d,total=%.10f,after350=%.10f\n",
        rate,ac::index(family),first,energy,energyAfterWall);
    assert(first>0 && first<5000);
    assert(energy>1e-7);
    assert(energyAfterWall>1e-9);
    return energyAfterWall;
}
static std::array<double,2> reverbTail(double rate,ac::Family family,float hz){
    SonKuMusicalReverb reverb;
    assert(reverb.prepare(rate));
    assert(reverb.setRealtimeProgram(
        static_cast<SonKuReverbPreset>(ac::index(family)),.7f));
    const int stimulus=static_cast<int>(rate*.13);
    const int duration=static_cast<int>(rate*1.6);
    double early=0.,late=0.;
    for(int i=0;i<duration;++i){
        const float x=i<stimulus?
            .11f*std::sin(2.f*pi*hz*i/static_cast<float>(rate)):0.f;
        const auto y=reverb.process(x,x);
        assert(std::isfinite(y.left)&&std::isfinite(y.right));
        assert(std::abs(y.left)<.99f&&std::abs(y.right)<.99f);
        const double power=y.left*y.left+y.right*y.right;
        const double time=static_cast<double>(i)/rate;
        if(time>.24&&time<.46)early+=power;
        if(time>.93&&time<1.15)late+=power;
    }
    const double ratio=late/(early+1e-20);
    std::printf("P82_FDN,%.0f,%d,%.0fHz,early=%.9f,late=%.9f,ratio=%.8f\n",
        rate,ac::index(family),static_cast<double>(hz),early,late,ratio);
    assert(early>1e-10);
    assert(std::isfinite(ratio));
    return {early,ratio};
}
static void blockParityAndDynamicRoom(double rate){
    Settings s{};
    s.spacePreset=1;s.roomAmount=.55f;s.ambience=.4f;
    s.sourceDistanceMeters=1.6f;s.startAzimuthDegrees=47.f;
    s.directElevationDegrees=17.f;
    s.stereoSpreadDegrees=65.f;
    s.moving=true;s.revolutionSeconds=2.8f;s.outputTrim=.55f;
    s.spatialMix=1.f;
    assert(valid(s));
    auto make=[&](){
        auto p=std::make_unique<SonKuSpatialCore>();
        assert(p->prepare(rate,measured));
        assert(p->setSettings(s));
        return p;
    };
    auto whole=make(),chunks=make();
    constexpr int frames=12288;
    std::vector<float> l(frames),r(frames),a(frames),b(frames);
    for(int i=0;i<frames;++i){
        const float mid=.08f*std::sin(2.f*pi*133.f*i/static_cast<float>(rate));
        const float side=.04f*std::sin(2.f*pi*2100.f*i/static_cast<float>(rate));
        l[i]=a[i]=mid+side;
        r[i]=b[i]=mid-side;
    }
    whole->process(l.data(),r.data(),frames);
    for(int i=0;i<frames;){
        const int n=std::min(frames-i,(i%3==0)?64:257);
        chunks->process(a.data()+i,b.data()+i,n);
        i+=n;
    }
    double e=0.;
    for(int i=0;i<frames;++i){
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
        assert(std::abs(l[i])<.99f&&std::abs(r[i])<.99f);
        assert(std::abs(l[i]-a[i])<3e-6f);
        assert(std::abs(r[i]-b[i])<3e-6f);
        e+=l[i]*l[i]+r[i]*r[i];
    }
    assert(e>1e-4);
}
int main(){
    materialsAndRt60();
    for(double rate:{44100.,48000.}){
        const auto small=erImpulse(rate,ac::Family::Studio);
        const auto large=erImpulse(rate,ac::Family::Hall);
        assert(std::abs(small-large)>1e-7);
        const auto studioLow=reverbTail(rate,ac::Family::Studio,125.f);
        const auto studioHigh=reverbTail(rate,ac::Family::Studio,6000.f);
        const auto hallLow=reverbTail(rate,ac::Family::Hall,125.f);
        const auto hallHigh=reverbTail(rate,ac::Family::Hall,6000.f);
        // The model predicts longer LF than HF decay within each room;
        // check that feedback really follows the prepared band-dependent law.
        assert(studioLow[1]>studioHigh[1]);
        assert(hallLow[1]>hallHigh[1]);
        assert(hallLow[1]>studioLow[1]);
        blockParityAndDynamicRoom(rate);
    }
    std::puts("PASS P8.2: six-wall distinct material bands; Eyring 3-band RT60 ordering; independent spatial ER; LF/HF diffuse decay; native PCM block parity at 44.1/48k");
}
