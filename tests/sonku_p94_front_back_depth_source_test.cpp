// SPDX-License-Identifier: AGPL-3.0-only
// Real P9.4 native source test; NO audio hardware or browser mocks.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P94_FRONT_BACK_DEPTH_RESEARCH) || !SONKU_P94_FRONT_BACK_DEPTH_RESEARCH
#error P9.4 source test must not silently run against P9.1-only DSP
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuP94FrontBackDepthCue.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
using Stereo=std::array<std::vector<float>,2>;
static void depthWeights(int rate){
    p94::FrontBackDepthCue cue;
    assert(cue.prepare(rate));
    auto settle=[&](float headAzimuth,bool enabled){
        cue.retarget(headAzimuth,enabled);
        p94::FrontBackDepthCue::Gains g{};
        for(int i=0;i<rate;++i)g=cue.next();
        return g;
    };
    const auto front=settle(0.f,true);
    assert(front.direct>.950f&&front.direct<.957f);
    assert(front.early>1.275f&&front.early<1.281f);
    const double shift=20.*std::log10(front.direct/front.early);
    assert(shift< -2.45 && shift> -2.7);
    const auto right=settle(90.f,true);
    assert(right.direct==1.f&&right.early==1.f);
    const auto rear=settle(180.f,true);
    assert(std::abs(rear.direct-front.direct)<.0001f);
    assert(std::abs(rear.early-front.early)<.0001f);
    const auto left=settle(-90.f,true);
    assert(left.direct==1.f&&left.early==1.f);
    const auto disabled=settle(0.f,false);
    assert(disabled.direct==1.f&&disabled.early==1.f);
    for(int i=0;i<360;i++){
        cue.retarget(float(i),true);
        const auto g=cue.next();
        assert(std::isfinite(g.direct)&&std::isfinite(g.early));
        assert(g.direct>=.9549f&&g.direct<=1.f);
        assert(g.early>=1.f&&g.early<=1.2801f);
    }
    // The model is relative to head direction, never rotated with the GUI
    // ellipse. An observer turning +90 maps world FRONT to head LEFT.
    const auto turned=settle(-90.f,true);
    assert(turned.direct==1.f&&turned.early==1.f);
    std::printf("P94_WEIGHT,%d,front_direct=%.6f,front_er=%.6f,DRR_shift_dB=%.3f\n",
                rate,front.direct,front.early,shift);
}
static Stereo run(int rate,float azimuth,bool room,int frames,int block,bool anti){
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    Settings s{};
    s.renderMode=9;
    s.moving=false;
    s.startAzimuthDegrees=azimuth;
    s.stereoSpreadDegrees=82.f;
    s.outputTrim=1.f;
    s.spatialMix=1.f;
    s.sourceDistanceMeters=1.6f;
    s.spacePreset=room?3:0;
    s.roomAmount=room?.78f:0.f;
    s.ambience=room?.33f:0.f;
    assert(valid(s)&&core->setSettings(s));
    Stereo a{std::vector<float>(frames),std::vector<float>(frames)};
    constexpr float pi=3.14159265358979323846f;
    for(int i=0;i<frames;++i){
        const float t=float(i)/rate;
        const float kick=.17f*std::sin(2*pi*73*t);
        const float bass=.12f*std::sin(2*pi*120*t);
        const float vocal=.10f*std::sin(2*pi*970*t);
        const float air=.055f*std::sin(2*pi*5200*t);
        const float sig=kick+bass+vocal+air;
        a[0][i]=sig;
        a[1][i]=anti?-sig:(kick+bass+vocal*.86f-air*.72f);
    }
    for(int i=0;i<frames;i+=block)
        core->process(a[0].data()+i,a[1].data()+i,std::min(block,frames-i));
    for(int ear=0;ear<2;++ear)for(float v:a[ear])
        assert(std::isfinite(v)&&std::abs(v)<.981f);
    return a;
}
static double rms(const Stereo& a,int trim){
    double sum=0.;
    for(int c=0;c<2;++c)for(int i=trim;i<int(a[c].size());++i)
        sum+=double(a[c][i])*a[c][i];
    return std::sqrt(sum/(2.*double(int(a[0].size())-trim)));
}
static double difference(const Stereo& a,const Stereo& b){
    double e=0.;
    for(int c=0;c<2;++c)for(std::size_t i=0;i<a[c].size();++i){
        const double d=double(a[c][i])-b[c][i];e+=d*d;
    }
    return e;
}
int main(){
    for(int rate:{44100,48000}){
        depthWeights(rate);
        constexpr std::array<float,4> cardinal{0.f,90.f,180.f,270.f};
        for(float angle:cardinal){
            const auto ref=run(rate,angle,true,rate,64,false);
            const auto block=run(rate,angle,true,rate,256,false);
            assert(difference(ref,block)==0. && "Audio output depends on host block size");
            const auto dry=run(rate,angle,false,rate,64,false);
            assert(rms(ref,rate/4)>.015 && rms(dry,rate/4)>.015);
            // No static front/back input stereo collapse in the full path.
            const auto anti=run(rate,angle,true,rate/2,128,true);
            assert(rms(anti,rate/10)>.0001);
            std::printf("P94_PCM,%d,az=%g,room_rms=%.6f,off_rms=%.6f\n",
                rate,double(angle),rms(ref,rate/4),rms(dry,rate/4));
        }
    }
    std::puts("PASS P94 front/back cue: no geometry change, 2.54dB DRR proxy, room OFF neutral, 44.1/48k PCM, 64/256 exact parity, phase and peak guard");
}
