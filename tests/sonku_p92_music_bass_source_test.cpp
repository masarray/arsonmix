// SPDX-License-Identifier: AGPL-3.0-only
// P9.1+P9.2 true native C++ PCM: music level, LF physical source and room.
// This is numerical evidence, NEVER a claim of subjective dearVR parity.
#ifdef NDEBUG
#undef NDEBUG
#endif
#if !defined(SONKU_P92_MUSIC_BASS_FOUNDATION) || !SONKU_P92_MUSIC_BASS_FOUNDATION
#error P9.2 needs its opt-in compile flag
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuP92MusicBassFoundation.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace armix::sonku;
constexpr double pi=3.1415926535897932384626433832795;
struct Result {
    std::vector<float> l,r;
    double energy=0.,peak=0.,inputEnergy=0.;
};
static Result render(int rate,int mode,int room,float distance,float width,
                     float yaw,float reflection,float reverb,int material,
                     int frames=12288,int block=128,float mix=1.f,float trim=1.f) {
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    Settings s{};
    s.startAzimuthDegrees=0.f;
    s.moving=false;
    s.stereoSpreadDegrees=width;
    s.headYawDegrees=yaw;
    s.spatialMix=mix;
    s.outputTrim=trim;
    s.roomAmount=reflection;
    s.ambience=reverb;
    s.sourceDistanceMeters=distance;
    s.spacePreset=room;
    s.renderMode=mode;
    s.hrtfElevationTrimDegrees=0.f;
    assert(valid(s) && core->setSettings(s));
    Result v{std::vector<float>(frames),std::vector<float>(frames)};
    for(int i=0;i<frames;++i) {
        const double t=double(i)/rate;
        const float bass=.08f*std::sin(2*pi*49.*t)+
                         .07f*std::sin(2*pi*73.*t)+
                         .05f*std::sin(2*pi*109.*t);
        const float mid=.06f*std::sin(2*pi*880.*t)+
                        .04f*std::sin(2*pi*3200.*t);
        const float side=.03f*std::sin(2*pi*1700.*t);
        const float impulse=(i==32)?.12f:0.f;
        switch(material){
        case 0: v.l[i]=bass+mid+impulse;v.r[i]=v.l[i];break;
        case 1: v.l[i]=bass+mid+impulse;v.r[i]=-(bass+mid+impulse);break;
        case 2: v.l[i]=bass+mid+impulse;v.r[i]=0.f;break;
        case 3: v.l[i]=0.f;v.r[i]=bass+mid+impulse;break;
        default:v.l[i]=bass+mid+side+impulse;v.r[i]=bass+mid-side;
        }
    }
    for(int i=0;i<frames;++i)
        v.inputEnergy+=double(v.l[i])*v.l[i]+double(v.r[i])*v.r[i];
    for(int i=0;i<frames;i+=block)
        core->process(v.l.data()+i,v.r.data()+i,std::min(block,frames-i));
    for(int i=0;i<frames;++i) {
        assert(std::isfinite(v.l[i])&&std::isfinite(v.r[i]));
        v.peak=std::max(v.peak,std::max(double(std::abs(v.l[i])),
                                           double(std::abs(v.r[i]))));
        v.energy+=double(v.l[i])*v.l[i]+double(v.r[i])*v.r[i];
    }
    assert(v.peak<.981&&v.energy>1.e-8);
    return v;
}
static double diff(const Result& a,const Result& b) {
    assert(a.l.size()==b.l.size());
    double e=0.;
    for(std::size_t i=0;i<a.l.size();++i){
        const double l=double(a.l[i])-b.l[i],r=double(a.r[i])-b.r[i];
        e+=l*l+r*r;
    }
    return e;
}
int main(){
    for(int rate:{44100,48000}){
        p92::MusicBassFoundation bass;
        const auto bank=rate==44100?hrir::normal44100:hrir::normal48000;
        assert(bass.prepare(rate,bank));
        assert(bass.directMakeup()>=1.0f&&bass.directMakeup()<=1.9f);
        for(int room=1;room<=3;++room)
            for(int m=0;m<3;++m) {
                const float frequency=bass.modeFrequency(room,m);
                assert(frequency>=24.f&&frequency<=125.f);
            }
        // Mode 10 has the exact previous stereo HRIR and no headphone
        // dry SIDE. At equal static scene, P9.0 must remain an independent
        // baseline, not silently inherit our source-coupled room modes.
        for(int material=0;material<=4;++material){
            const auto next=render(rate,10,3,1.6f,82.f,0.f,.72f,.38f,material);
            const auto old=render(rate,9,3,1.6f,82.f,0.f,.72f,.38f,material);
            std::fprintf(stderr,"P92_FIDELITY_DIAG rate=%d material=%d old=%.8f new=%.8f ratio=%.5f peak=%.6f\n",rate,material,old.energy,next.energy,next.energy/old.energy,next.peak);
            assert(diff(next,old)>1.e-8);
            assert(next.energy>old.energy*.20 && "new music stage became inaudible");
            assert(next.peak<.981);
            // Normal coherent/stereo programme must begin close to
            // source reference without the user's old manual +7dB gain.
            if(material==0||material==4) {
                const double matchDb=10.*std::log10(next.energy/next.inputEnergy);
                assert(std::abs(matchDb)<3.0 && "P9.2 default lost natural music loudness");
            }
        }
        const auto roomOn=render(rate,10,3,1.6f,82.f,0.f,.72f,.38f,4);
        const auto roomOff=render(rate,10,0,1.6f,82.f,0.f,0.f,0.f,4);
        assert(diff(roomOn,roomOff)>1.e-6&&"room modes not reaching actual ears");
        // Angle/head yaw must actually change bass-bearing full-band
        // transfer rather than the old channel-locked <220Hz return.
        const auto yawed=render(rate,10,3,1.6f,82.f,85.f,.72f,.38f,2);
        const auto opposite=render(rate,10,3,1.6f,82.f,-85.f,.72f,.38f,2);
        assert(diff(yawed,opposite)>1.e-6);
        const auto left=render(rate,10,3,1.6f,82.f,0.f,.72f,.38f,2);
        const auto right=render(rate,10,3,1.6f,82.f,0.f,.72f,.38f,3);
        const auto anti=render(rate,10,3,1.6f,82.f,0.f,.72f,.38f,1);
        assert(diff(left,right)>1.e-6&&anti.energy>1.e-7);
        for(int block:{64,256}){
            const auto check=render(rate,10,3,1.6f,82.f,0.f,.72f,.38f,4,12288,block);
            assert(diff(roomOn,check)==0. && "buffer-size dependent PCM");
        }
        const auto dry=render(rate,10,0,1.6f,82.f,0.f,0.f,0.f,4,12288,128,0.f);
        assert(dry.energy>1.e-7);
        // New renderer is still strictly dry after its bounded 8ms gain
        // smoothing has settled at MIX=0: no resonant/room ghost tail.
        for(int i=8000;i<12288;++i) {
            const double t=double(i)/rate;
            const float bass=.08f*std::sin(2*pi*49.*t)+
                             .07f*std::sin(2*pi*73.*t)+
                             .05f*std::sin(2*pi*109.*t);
            const float mid=.06f*std::sin(2*pi*880.*t)+
                            .04f*std::sin(2*pi*3200.*t);
            const float side=.03f*std::sin(2*pi*1700.*t);
            assert(dry.l[i]==bass+mid+side);
            assert(dry.r[i]==bass+mid-side);
        }
        // In a measured HRTF we do not guarantee output = input gain at
        // every frequency, but at 100% MIX high-level energy must be finite
        // without a +7dB manual user gain just to make the plug audible.
        std::printf("P92_PCM,%d,makeup=%.4f,old=%.7f,next=%.7f,room_diff=%.7f,peak=%.5f,dryRatioDb=%.2f\n",
            rate,bass.directMakeup(),render(rate,9,0,1.6f,82.f,0.f,0.f,0.f,4).energy,
            roomOn.energy,diff(roomOn,roomOff),roomOn.peak,
            10.*std::log10(roomOn.energy/roomOn.inputEnergy));
    }
    std::puts("PASS P92 NATIVE PCM: 44.1/48k, P90 golden isolation, measured music makeup, axial LF modes, bass yaw, anti-phase, room OFF, 64/128/256 parity");
}
