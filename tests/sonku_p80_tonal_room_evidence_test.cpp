// SPDX-License-Identifier: AGPL-3.0-only
// P8.0: objective source/processed evidence from the EXACT real SonKu core.
// Synthetic tonal diagnostic, NOT a perceptual/dearVR qualification.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) || !SONKU_P80_TRANSPARENT_BASS_RESEARCH
#error Must compile the P8 acoustic research renderer
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuHrtfTonalCalibration.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using armix::sonku::Settings;
using armix::sonku::SonKuSpatialCore;
using armix::sonku::SonKuReflectionProfile;
constexpr float pi=3.14159265358979323846f;
constexpr auto research=SonKuReflectionProfile::SixSurfaceKemarResearch;

static std::unique_ptr<SonKuSpatialCore> make(double rate,const Settings& s){
    auto p=std::make_unique<SonKuSpatialCore>();
    assert(p->prepare(rate,research));
    assert(p->setSettings(s));
    return p;
}
static Settings scene(){
    Settings s{};
    s.startAzimuthDegrees=0.f;s.stereoSpreadDegrees=60.f;
    s.spatialMix=1.f;s.outputTrim=.75f;s.moving=false;
    s.spacePreset=0;s.roomAmount=.55f;s.ambience=.30f;
    return s;
}
static void exactDryBypass(double rate){
    auto s=scene();s.spatialMix=0.f;s.outputTrim=.28f;
    auto core=make(rate,s);
    // The accepted MIX control slews over ~8 ms. Establish the exact
    // accepted dry state first, without assuming an instantaneous edit.
    std::vector<float> warmL(8192),warmR(8192);
    core->process(warmL.data(),warmR.data(),static_cast<int>(warmL.size()));
    constexpr int n=1024;
    std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i){
        l[i]=.18f*std::sin(.034f*i)+.03f*std::sin(.7f*i);
        r[i]=.13f*std::cos(.023f*i)-.06f*std::sin(.55f*i);
    }
    const auto ol=l,orr=r;
    core->process(l.data(),r.data(),n);
    for(int i=0;i<n;++i){
        assert(l[i]==ol[i]);
        assert(r[i]==orr[i]);
    }
}
static void antiPhaseExactNoNull(double rate){
    for(float width:{0.f,60.f,90.f,180.f}){
        auto s=scene();s.startAzimuthDegrees=180.f;
        s.stereoSpreadDegrees=width;
        auto core=make(rate,s);
        constexpr int n=16384;
        std::vector<float> l(n),r(n);
        for(int i=0;i<n;++i){
            const float x=.16f*std::sin(2.f*pi*64.f*i/static_cast<float>(rate))
                          +.11f*std::sin(2.f*pi*3150.f*i/static_cast<float>(rate));
            l[i]=x;r[i]=-x;
        }
        const auto input=l;
        core->process(l.data(),r.data(),n);
        double p=0.,expected=0.;
        for(int i=0;i<n;++i){
            assert(std::isfinite(l[i])&&std::isfinite(r[i]));
            assert(std::abs(l[i])<.99f&&std::abs(r[i])<.99f);
            p+=l[i]*l[i];
            expected+=input[i]*input[i];
            assert(std::abs(l[i]+r[i])<2.e-5f);
            if(width==60.f){
                // At reference width 60, original stereo bass+upper SIDE
                // recombine with the SAME one-pole and remain exact.
                assert(std::abs(l[i]-.75f*input[i])<3.e-6f);
            }
        }
        assert(p>.07*expected);
    }
}
static double toneGain(double rate,float hz,bool leftOnly,bool roomOn) {
    auto s=scene();
    if(roomOn){s.spacePreset=1;s.roomAmount=.7f;s.ambience=.4f;}
    auto core=make(rate,s);
    constexpr int n=16384;
    std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i){
        const float x=.12f*std::sin(2.f*pi*hz*i/static_cast<float>(rate));
        l[i]=x;r[i]=leftOnly?0.f:x;
    }
    auto original=l;
    core->process(l.data(),r.data(),n);
    double incoming=0.,outL=0.,outR=0.,maxPeak=0.;
    for(int i=n/2;i<n;++i){
        incoming+=original[i]*original[i];
        outL+=l[i]*l[i];outR+=r[i]*r[i];
        maxPeak=std::max(maxPeak,static_cast<double>(
            std::max(std::abs(l[i]),std::abs(r[i]))));
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
    }
    const double gain=std::sqrt(outL/(incoming+1e-18));
    const double right=std::sqrt(outR/(incoming+1e-18));
    std::printf("P8_EVIDENCE,%.0f,%.0f,%s,%s,%.4f,%.4f,%.4f\n",
        rate,static_cast<double>(hz),leftOnly?"hard-left":"mono",
        roomOn?"room":"direct",gain,right,maxPeak);
    assert(maxPeak<.99);
    return gain;
}
static void roomHasActualTimedTail(double rate){
    auto off=scene(),on=off;
    on.spacePreset=1;on.roomAmount=.85f;on.ambience=.7f;
    auto dry=make(rate,off),room=make(rate,on);
    constexpr int n=16384;
    std::vector<float> l(n),r(n),a(n),b(n);
    l[0]=r[0]=a[0]=b[0]=.20f;
    dry->process(l.data(),r.data(),n);
    room->process(a.data(),b.data(),n);
    double dryLate=0.,roomLate=0.,roomSecond=0.;
    for(int i=0;i<n;++i){
        assert(std::isfinite(a[i])&&std::isfinite(b[i]));
        assert(std::abs(a[i])<.99f&&std::abs(b[i])<.99f);
        if(i>1800){
            dryLate+=l[i]*l[i]+r[i]*r[i];
            roomLate+=a[i]*a[i]+b[i]*b[i];
        }
        if(i>4000)roomSecond+=a[i]*a[i]+b[i]*b[i];
    }
    std::printf("P8_ROOM,%.0f,dry_after_1800=%.9f,room_after_1800=%.9f,late_after_4000=%.9f\n",
        rate,dryLate,roomLate,roomSecond);
    assert(dryLate<1.e-12);
    assert(roomLate>1.e-9);
    assert(roomSecond>1.e-12);
}
static void roomChunkParity(double rate) {
    auto s=scene();
    s.spacePreset=1;s.roomAmount=.6f;s.ambience=.4f;
    s.moving=true;s.startAzimuthDegrees=45.f;
    s.directElevationDegrees=15.f;
    auto whole=make(rate,s),chunk=make(rate,s);
    constexpr int n=8192;
    std::vector<float> a(n),b(n),c(n),d(n);
    for(int i=0;i<n;++i){
        const float mid=.12f*std::sin(2.f*pi*315.f*i/static_cast<float>(rate));
        const float side=.07f*std::sin(2.f*pi*2500.f*i/static_cast<float>(rate));
        a[i]=c[i]=mid+side;b[i]=d[i]=mid-side;
    }
    whole->process(a.data(),b.data(),n);
    for(int i=0;i<n;){
        const int k=std::min(n-i,(i&1)?257:64);
        chunk->process(c.data()+i,d.data()+i,k);
        i+=k;
    }
    for(int i=0;i<n;++i){
        assert(std::isfinite(a[i])&&std::isfinite(b[i]));
        assert(std::abs(a[i]-c[i])<3.e-6f);
        assert(std::abs(b[i]-d[i])<3.e-6f);
    }
}
int main(){
    std::puts("P8_SCHEMA,rate_hz,tone_hz,source_type,scene,l_gain_rms,r_gain_rms,peak");
    for(double rate:{44100.,48000.}){
        armix::sonku::SonKuHrtfTonalCalibration calibration;
        const auto bank=rate<45000.?armix::sonku::hrir::normal44100:
                                     armix::sonku::hrir::normal48000;
        assert(calibration.prepare(rate,bank));
        assert(calibration.presenceCutDb()<=-3.f &&
               calibration.presenceCutDb()>=-5.5f);
        assert(calibration.highAirDb()>=2.f &&
               calibration.highAirDb()<=4.f);
        std::printf("P8_CALIBRATION,%.0f,presence_cut_db=%.3f,high_air_db=%.3f\n",
                    rate,calibration.presenceCutDb(),calibration.highAirDb());
        exactDryBypass(rate);
        antiPhaseExactNoNull(rate);
        for(float hz:{63.f,125.f,250.f,500.f,1000.f,2000.f,4000.f,8000.f}){
            const double mono=toneGain(rate,hz,false,false);
            const double left=toneGain(rate,hz,true,false);
            if(hz==2000.f)assert(mono<1.30); // rejects P8.0 nasal 2k gain of ~1.52
            if(hz==8000.f)assert(mono>.15); // bounded air return, not a pinna-notch inversion
            assert(mono>.02&&left>.02);
            if(hz<=125.f){
                // Musical bass must not vanish when no ROOM is active.
                // Measured numerical threshold deliberately below dry -6dB;
                // per-direction/real-music fidelity is a later gate.
                assert(mono>.35);
                assert(left>.35);
            }
        }
        roomHasActualTimedTail(rate);
        roomChunkParity(rate);
    }
    std::puts("PASS P8: unaltered dry, stereo LF+HF anti-phase exact at 60deg, per-band evidence, timed 3D room and PCM partition invariance");
}
