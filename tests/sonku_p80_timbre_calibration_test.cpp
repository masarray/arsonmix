// SPDX-License-Identifier: AGPL-3.0-only
// P8.0 scientifically bounded *measured KEMAR* tonal correction. Separate
// from the P7.5 anti-null fixtures. Never asserts subjective dearVR parity.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P80_MEASURED_TIMBRE_RESEARCH) || !SONKU_P80_MEASURED_TIMBRE_RESEARCH
#error Must exercise the exact experimental P8.0 DSP path
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
constexpr float kPi=3.14159265358979323846f;
static void measuredBinaural(double rate){
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    const float boost=core->p80PreparedShelfDb();
    const float relative=core->p80MeasuredLowMinusMidDb();
    assert(std::isfinite(relative)&&std::isfinite(boost));
    assert(boost>=0.f&&boost<=4.5001f);
    assert(std::abs(std::min(4.5f,std::max(0.f,-relative))-boost)<.003f);
    std::printf("P80_CALIBRATION,rate=%.0f,LF125_MINUS_MID1000_DB=%.3f,PREPARED_SHELF_DB=%.3f\n",
        rate,relative,boost);
    // 0% SPATIAL MIX must remain a true stereo identity, even with P8.
    Settings s{};s.moving=false;s.startAzimuthDegrees=0.f;
    s.spatialMix=0.f;s.outputTrim=.65f;
    assert(core->setSettings(s));
    constexpr int n=2048;std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i){l[i]=.15f*std::sin(i*.2f);r[i]=.09f*std::cos(i*.17f);}
    const auto beforeL=l,beforeR=r;
    core->process(l.data(),r.data(),n);
    for(int i=0;i<n;++i){
        assert(std::abs(l[i]-.65f*beforeL[i])<3.e-4f);
        assert(std::abs(r[i]-.65f*beforeR[i])<3.e-4f);
    }
    // Exact anti-phase survives 100% wet even with tonal EQ: P8 filters
    // only the KEMAR MID/room field, not the SIDE stereo safety path.
    s.spatialMix=1.f;s.outputTrim=.7f;s.stereoSpreadDegrees=60.f;
    assert(core->setSettings(s));
    core->reset();
    for(int i=0;i<n;++i){l[i]=.1f*std::sin(i*.16f);r[i]=-l[i];}
    const auto original=l;
    core->process(l.data(),r.data(),n);
    for(int i=0;i<n;++i){
        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
        assert(std::abs(l[i]-.7f*original[i])<6.e-5f);
        assert(std::abs(r[i]+.7f*original[i])<6.e-5f);
    }
    // Room, elevation and motion must retain chunk parity with identical
    // prepared coefficients at 64, 128 and 257 frame slices.
    s.spacePreset=1;s.sourceDistanceMeters=1.6f;s.roomAmount=.55f;
    s.ambience=.3f;s.directElevationDegrees=20.f;
    s.moving=true;s.revolutionSeconds=3.3f;
    auto one=std::make_unique<SonKuSpatialCore>();
    auto chunks=std::make_unique<SonKuSpatialCore>();
    assert(one->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    assert(chunks->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    assert(one->setSettings(s)&&chunks->setSettings(s));
    constexpr int frames=8192;
    std::vector<float> a(frames),b(frames),ca(frames),cb(frames);
    for(int i=0;i<frames;++i){
        const float m=.12f*std::sin(2.f*kPi*220.f*i/static_cast<float>(rate));
        const float side=.07f*std::sin(2.f*kPi*1900.f*i/static_cast<float>(rate));
        a[i]=ca[i]=m+side;b[i]=cb[i]=m-side;
    }
    one->process(a.data(),b.data(),frames);
    for(int j=0;j<frames;){
        const int nblock=std::min(frames-j,(j%3==0)?257:64);
        chunks->process(ca.data()+j,cb.data()+j,nblock);
        j+=nblock;
    }
    double pow=0.;
    for(int i=0;i<frames;++i){
        assert(std::isfinite(a[i])&&std::isfinite(b[i]));
        assert(std::abs(a[i])<.99f&&std::abs(b[i])<.99f);
        assert(std::abs(a[i]-ca[i])<2.e-5f&&std::abs(b[i]-cb[i])<2.e-5f);
        pow+=a[i]*a[i]+b[i]*b[i];
    }
    assert(pow>0.1);
}
int main(){
    measuredBinaural(44100.);
    measuredBinaural(48000.);
    std::puts("PASS P8.0 measured KEMAR reference, bounded stereo-common EQ, dry and anti-phase invariants, ROOM/XYZ chunk parity");
}
