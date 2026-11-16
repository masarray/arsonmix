// SPDX-License-Identifier: AGPL-3.0-only
// Product preset audition probes using the *unchanged* P9.1/P9.2 PCM core.
// Tests evidence; neither synthetic signals nor sample RMS proves luxury.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) || !SONKU_P90_TRUE_EXTERNAL_RESEARCH
#error ImerSip P9.2 audition needs the real P9 DSP switch
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace armix::sonku;
using armix::builtin_fx::imersipP92Factory;
using armix::builtin_fx::imersipP92ReferenceScene;
using armix::builtin_fx::validImerSipP92Scene;
using Stereo=std::array<std::vector<float>,2>;
constexpr double pi=3.1415926535897932384626433832795;
constexpr int frames=32768;
constexpr int skip=8192;
enum class Material { Musical, AntiPhase };

static Stereo source(int rate,Material material){
    Stereo p{std::vector<float>(frames),std::vector<float>(frames)};
    for(int i=0;i<frames;++i){
        const double t=double(i)/rate;
        if(material==Material::AntiPhase){
            const float s=static_cast<float>(
                .14*std::sin(2*pi*83*t)+.055*std::sin(2*pi*1703*t));
            p[0][i]=s;p[1][i]=-s;
        } else {
            const float mid=static_cast<float>(
                .092*std::sin(2*pi*83*t)+
                .073*std::sin(2*pi*116*t)+
                .065*std::sin(2*pi*371*t)+
                .035*std::sin(2*pi*1900*t)+
                .026*std::sin(2*pi*8500*t));
            const float side=static_cast<float>(
                .048*std::sin(2*pi*520*t)+
                .025*std::sin(2*pi*3120*t));
            p[0][i]=mid+side;
            p[1][i]=mid-side;
        }
    }
    return p;
}
static Stereo render(int rate,const Settings& scene,Stereo data){
    auto dsp=std::make_unique<SonKuSpatialCore>();
    assert(dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    assert(validImerSipP92Scene(scene));
    assert(dsp->setSettings(scene));
    for(int i=0;i<frames;i+=128)
        dsp->process(data[0].data()+i,data[1].data()+i,128);
    for(int ear=0;ear<2;++ear)
        for(const float s:data[ear])assert(std::isfinite(s)&&std::abs(s)<.981f);
    return data;
}
static double energy(const Stereo& p,bool side=false){
    double total=0.;
    for(int i=skip;i<frames;++i){
        if(side){
            const double x=.5*(p[0][i]-p[1][i]);
            total+=x*x;
        }else {
            const double l=p[0][i],r=p[1][i];
            total+=.5*(l*l+r*r);
        }
    }
    return total;
}
static double nullDelta(const Stereo& p,const Stereo& q){
    double e=0.;
    for(int i=skip;i<frames;++i)for(int ear=0;ear<2;++ear){
        const double d=p[ear][i]-q[ear][i];
        e+=.5*d*d;
    }
    return e;
}
static void verify(int rate) {
    const auto raw=source(rate,Material::Musical);
    const auto anti=source(rate,Material::AntiPhase);
    const auto plain=render(rate,imersipP92ReferenceScene(),raw);
    const double original=energy(raw);
    const double neutralRms=std::sqrt(energy(plain)/original);
    std::printf("IMERSIP_NEUTRAL,%d,rmsRatio=%.4f\n",rate,neutralRms);
    const auto preserved=imersipP92Factory(2);
    const auto liveRendered=render(rate,preserved,raw);
    const double liveRms=std::sqrt(energy(liveRendered)/original);
    assert(std::abs(preserved.stereoSpreadDegrees-150.f)<1.e-4f);
    assert(std::abs(preserved.sourceDistanceMeters-2.2f)<1.e-4f);
    assert(std::abs(preserved.ambience-.91f)<1.e-4f);
    assert(std::abs(preserved.outputTrim-
        armix::builtin_fx::imersipP92DbToLinear(4.8f))<1.e-4f);
    for(const auto& spec:armix::builtin_fx::imersipP92Presets) {
        const int id=spec.id;
        const auto s=imersipP92Factory(id);
        assert(s.renderMode==9&&!s.musicCompatibleLowFrequency&&!s.moving);
        assert(s.spacePreset>=1 && s.spacePreset<=3);
        const auto heard=render(rate,s,raw);
        const double ratio=std::sqrt(energy(heard)/original);
        const double side=energy(heard,true);
        auto silenceRoom=s;
        silenceRoom.roomAmount=0.f;
        silenceRoom.ambience=0.f;
        const auto withoutRoom=render(rate,silenceRoom,raw);
        const double roomDelta=std::sqrt(
            nullDelta(heard,withoutRoom)/energy(heard));
        const auto antiWet=render(rate,s,anti);
        const double antiRatio=std::sqrt(energy(antiWet)/energy(anti));
        const double antiSide=std::sqrt(energy(antiWet,true)/
                                      energy(anti,true));
        std::printf("IMERSIP_FACTORY_METRIC,rate=%d,id=%d,"
                    "rmsVsSource=%.4f,rmsVsNeutral=%.4f,"
                    "roomDelta=%.4f,stereoSideEnergy=%.5f,"
                    "antiphaseRms=%.4f,antiphaseSide=%.4f\n",
                    rate,id,ratio,ratio/neutralRms,roomDelta,side,
                    antiRatio,antiSide);
        assert(std::isfinite(ratio)&&std::isfinite(roomDelta));
        assert(side>1.e-5);
        assert(antiRatio>.08 && antiSide>.08); // catastrophic null veto
        if(id>=2 && id<=7){
            assert(s.sourceDistanceMeters>=1.6f); // front row is intentionally near
            assert(s.outputTrim>1.f);
            // A richer room must produce an actual, measurable wet PCM
            // difference, not just turn up a headphone-locked dry signal.
            assert(roomDelta>.045);
            // Factory candidates must not be quieter than selected P9
            // neutral. Mastered material is handled by the linked guard.
            assert(ratio>neutralRms*1.05);
        }
        if(id!=2){
            // New named scenes may not collapse into identical DSP
            // settings or a weaker version of the owner's Live Concert.
            assert(nullDelta(heard,liveRendered)>.001);
            assert(ratio>liveRms*.90); // reject another weak renamed preset
        }
        if(id==5){
            assert(std::abs(s.stereoSpreadDegrees-150.f)<1.e-4f);
            assert(std::abs(s.sourceDistanceMeters-1.6f)<1.e-4f);
            assert(std::abs(s.ambience-.97f)<1.e-4f);
        }
        if(id==7)assert(s.spacePreset==2);
        assert(s.outputTrim<=armix::builtin_fx::imersipP92DbToLinear(6.f));
    }
}
int main(){
    verify(44100);
    verify(48000);
    std::puts("PASS IMERSIP RICH SCENES: six live/room presets, phase, wet/dry, internal neutral-only guard");
}
