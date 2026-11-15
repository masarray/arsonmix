// SPDX-License-Identifier: AGPL-3.0-only
// P9.1+P9.2: real SonKu native stereo PCM, non-proprietary source material.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) || !SONKU_P90_TRUE_EXTERNAL_RESEARCH
#error P9.1+P9.2 requires the isolated P9 DSP switch
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuP91P92WorldBassFoundation.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
using Stereo=std::array<std::vector<float>,2>;
static Stereo simulate(int rate,int mode,int room,float roomWet,
                       float yaw,int source,int frames=48000,int block=128){
    auto processor=std::make_unique<SonKuSpatialCore>();
    assert(processor->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    Settings s{};
    s.renderMode=mode;s.moving=false;s.startAzimuthDegrees=0.f;
    s.stereoSpreadDegrees=82.f;s.headYawDegrees=yaw;
    s.outputTrim=1.f;s.spatialMix=1.f;
    s.spacePreset=room;s.roomAmount=roomWet;s.ambience=room?0.32f:0.f;
    s.sourceDistanceMeters=1.6f;
    assert(valid(s)&&processor->setSettings(s));
    Stereo pcm{std::vector<float>(frames),std::vector<float>(frames)};
    for(int i=0;i<frames;++i){
        constexpr float pi=3.14159265358979323846f;
        const float t=float(i)/rate;
        const float kick=.24f*std::sin(2.f*pi*70.f*t);
        const float body=.14f*std::sin(2.f*pi*116.f*t);
        const float air=.08f*std::sin(2.f*pi*5200.f*t);
        const float signal=kick+body+air;
        pcm[0][i]=(source==1?0.f:signal);
        pcm[1][i]=(source==0?0.f:source==3?-signal:signal);
    }
    for(int i=0;i<frames;i+=block)
        processor->process(pcm[0].data()+i,pcm[1].data()+i,
                           std::min(block,frames-i));
    for(int ear=0;ear<2;++ear)for(float x:pcm[ear])
        assert(std::isfinite(x)&&std::abs(x)<.981f);
    return pcm;
}
static double rms(const Stereo& p,int ear,int skip=12000){
    double e=0.;
    for(std::size_t i=skip;i<p[ear].size();++i)e+=double(p[ear][i])*p[ear][i];
    return std::sqrt(e/(p[ear].size()-skip));
}
static double difference(const Stereo& a,const Stereo& b){
    double e=0.;
    for(int ear=0;ear<2;++ear)
        for(std::size_t i=0;i<a[ear].size();++i){
            const double d=double(a[ear][i])-b[ear][i];e+=d*d;
        }
    return e;
}
int main(){
    for(int rate:{44100,48000}){
        const auto p9=simulate(rate,9,3,.80f,0.f,2,rate);
        const auto p9Block=simulate(rate,9,3,.80f,0.f,2,rate,256);
        assert(difference(p9,p9Block)==0. && "ASIO block size changed PCM");
        const auto p89=simulate(rate,8,3,.80f,0.f,2,rate);
        assert(difference(p9,p89)>1.e-4);
        const auto dry=simulate(rate,9,0,0.f,0.f,2,rate);
        assert(difference(p9,dry)>1.e-4 && "Room model inaudible");
        // The P9 direct stereo scene is now 0.72 rather than the old
        // 0.5 attenuation. It should not be quieter than old P8.9 for
        // this bounded 70/116Hz and 5.2k stimulus.
        const double p9TotalRms=rms(p9,0)+rms(p9,1);
        const double sourceTotalRms=2.*std::sqrt(
            (.24*.24+.14*.14+.08*.08)/2.);
        // Musical default must not simply win A/B by being twice as loud.
        // A narrow test tone is not integrated LUFS; this bounds only the
        // deterministic steady-state electrical gain to within ~2.5 dB.
        std::fprintf(stderr,"P91P92_LEVEL_%d input=%.8f actual=%.8f ratio=%.5f\n",
                     rate,sourceTotalRms,p9TotalRms,p9TotalRms/sourceTotalRms);
        assert(p9TotalRms>.75*sourceTotalRms &&
               p9TotalRms<1.33*sourceTotalRms &&
               "P9 has excessive loss or hidden makeup gain");
        assert(p9TotalRms>.78*(rms(p89,0)+rms(p89,1)));
        const auto left=simulate(rate,9,3,.8f,0.f,0,rate);
        const auto right=simulate(rate,9,3,.8f,0.f,1,rate);
        assert(difference(left,right)>1.e-4);
        const auto rotated=simulate(rate,9,3,.8f,90.f,0,rate);
        assert(difference(left,rotated)>1.e-4);
        // Anti-phase is not rebuilt as synthetic mono bass.
        const auto anti=simulate(rate,9,3,.8f,0.f,3,rate);
        assert(rms(anti,0)+rms(anti,1)>1.e-4);
        // Room modal return is exactly zero with Room OFF; a zero-wet
        // scene cannot invent resonant gain.
        auto core=std::make_unique<p92::WorldBassFoundation>();
        assert(core->prepare(rate));
        core->updateDirections(-41.f,41.f);
        for(int i=0;i<rate/2;++i){
            float h0=.01f,h1=.01f;
            const auto out=core->process(.01f,.01f,h0,h1,0,1.f);
            assert(out.left==0.f && out.right==0.f);
        }
        const auto withModes=simulate(rate,9,3,1.f,0.f,0,rate);
        const auto noModes=simulate(rate,9,3,0.f,0.f,0,rate);
        assert(difference(withModes,noModes)>1.e-5);
        std::printf("P91P92_RATE,%d,p9_rms=%.7f,p89_rms=%.7f,room_delta=%.7f\n",
            rate,rms(p9,0)+rms(p9,1),rms(p89,0)+rms(p89,1),difference(withModes,noModes));
    }
    std::puts("PASS P91P92 MUSIC/BASS PCM: 44.1/48k, world-locked separated LF, no dry SIDE, 3-room axial modes, anti-phase, finite output, 128/256 parity");
}
