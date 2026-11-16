// SPDX-License-Identifier: AGPL-3.0-only
// SonKu P9 measured stereo kernel / actual native PCM regression.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#if !defined(SONKU_P90_TRUE_EXTERNAL_RESEARCH) || !SONKU_P90_TRUE_EXTERNAL_RESEARCH
#error P9 must be explicitly enabled for this test
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include "../engine/builtin_fx/sonku/SonKuP90StereoDecoder.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
using Stereo=std::array<std::vector<float>,2>;
constexpr float pi=3.14159265358979323846f;
static Stereo run(int mode,int rate,int block,int room,float distance,
                  float width,float yaw,int material,int frames=4096) {
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    Settings p{};
    p.moving=false;
    p.startAzimuthDegrees=20.f;
    p.stereoSpreadDegrees=width;
    p.headYawDegrees=yaw;
    p.spacePreset=room;
    p.sourceDistanceMeters=distance;
    p.roomAmount=room?0.8f:0.f;
    p.ambience=room?0.45f:0.f;
    p.hrtfElevationTrimDegrees=-8.f;
    p.renderMode=mode;
    p.outputTrim=.6f;
    p.spatialMix=1.f;
    assert(valid(p));
    assert(core->setSettings(p));
    Stereo out{std::vector<float>(frames),std::vector<float>(frames)};
    for(int i=0;i<frames;++i) {
        const float t=float(i)/float(rate);
        const float mid=.13f*std::sin(2*pi*3100.f*t)+
                        .1f*std::sin(2*pi*6700.f*t)+
                        .12f*std::sin(2*pi*140.f*t);
        const float side=.11f*std::sin(2*pi*9300.f*t);
        switch(material){
        case 0: out[0][i]=out[1][i]=mid;break; // coherent center
        case 1: out[0][i]=mid+side;out[1][i]=-(mid+side);break; // anti-phase
        case 2: out[0][i]=mid+side;out[1][i]=0.f;break; // left only
        case 3: out[0][i]=0.f;out[1][i]=mid+side;break; // right only
        default: out[0][i]=mid+side;out[1][i]=mid-side;
        }
        if(i==13 && material==2)out[0][i]+=.1f;
        if(i==13 && material==3)out[1][i]+=.1f;
    }
    for(int i=0;i<frames;i+=block)
        core->process(out[0].data()+i,out[1].data()+i,std::min(block,frames-i));
    return out;
}
static double energy(const Stereo& pcm) {
    double e=0.;
    for(std::size_t k=0;k<pcm[0].size();++k){
        const float l=pcm[0][k],r=pcm[1][k];
        assert(std::isfinite(l)&&std::isfinite(r));
        assert(std::abs(l)<.981f && std::abs(r)<.981f);
        e+=double(l)*l+double(r)*r;
    }
    return e;
}
static double delta(const Stereo& a,const Stereo& b) {
    assert(a[0].size()==b[0].size());
    double e=0;
    for(std::size_t k=0;k<a[0].size();++k)
        for(int ear=0;ear<2;++ear){
            const double d=double(a[ear][k])-b[ear][k];
            e+=d*d;
        }
    return e;
}
static void algebraicTransferGate(){
    using Kernel=std::array<std::array<float,8>,2>;
    Kernel center{},left{},right{};
    for(int ear=0;ear<2;++ear)for(int k=0;k<8;++k){
        center[ear][k]=float(k+1)*.003f;
        left[ear][k]=float(ear+1)*float(k+1)*.011f;
        right[ear][k]=float(ear+2)*float(k+1)*.006f;
    }
    const auto oldL=left,oldR=right;
    p90::StereoDecoder decoder;
    assert(decoder.prepare(48000.));
    // First transient protection: exact centered MID from corrected pair.
    decoder.compose(center,left,right);
    for(int ear=0;ear<2;++ear)for(int k=0;k<8;++k){
        assert(std::abs((left[ear][k]+right[ear][k])-2*center[ear][k])<1.e-6f);
        assert(std::abs((left[ear][k]-right[ear][k])-
                        (oldL[ear][k]-oldR[ear][k]))<1.e-6f);
    }
    // Correlation transitions are bounded, no NaN or phase-inverted SIDE.
    for(int i=0;i<24000;++i){
        decoder.observe(i%2?0.1f:-0.1f,0.f);
        if(i%16==0){
            left=oldL;right=oldR;
            decoder.compose(center,left,right);
        }
    }
    assert(decoder.centerBlend()<.02f);
}
int main(){
    algebraicTransferGate();
    for(int rate:{44100,48000}){
        for(float width:{60.f,104.f,180.f})
            for(int material:{0,1,2,3,4}){
                const auto p9=run(9,rate,64,0,1.6f,width,0.f,material);
                const auto p9b=run(9,rate,256,0,1.6f,width,0.f,material);
                const auto p89=run(8,rate,128,0,1.6f,width,0.f,material);
                assert(delta(p9,p9b)==0. && "callback block size changed PCM");
                assert(energy(p9)>1.e-7 && "P9 lost stereo input");
                // Direct stereo always differs from fused+head-locked P8.9.
                assert(delta(p9,p89)>1.e-10);
            }
        for(float yaw:{-90.f,0.f,90.f,180.f}){
            const auto left=run(9,rate,128,3,6.7f,104.f,yaw,2,8192);
            const auto right=run(9,rate,128,3,6.7f,104.f,yaw,3,8192);
            const auto anti=run(9,rate,128,3,6.7f,104.f,yaw,1,8192);
            assert(energy(left)>1.e-7&&energy(right)>1.e-7&&energy(anti)>1.e-7);
            assert(delta(left,right)>1.e-6);
            if(yaw==0.f){
                const auto rotated=run(9,rate,128,3,6.7f,104.f,90.f,2,8192);
                assert(delta(left,rotated)>1.e-6 && "head yaw not spatializing treble");
            }
        }
        const auto near=run(9,rate,64,3,1.6f,104.f,0.f,4,8192);
        const auto far=run(9,rate,256,3,6.7f,104.f,0.f,4,8192);
        const auto veryFar=run(9,rate,128,3,10.f,104.f,0.f,4,8192);
        assert(delta(near,far)>1.e-7&&delta(far,veryFar)>1.e-7);
        assert(energy(veryFar)>1.e-6);
        auto s=Settings{};s.renderMode=9;s.spacePreset=3;s.sourceDistanceMeters=6.7f;
        assert(valid(s));
        s.hrtfElevationTrimDegrees=-35.f;
        assert(!valid(s));
        std::printf("P90_RATE,%d,near=%.7f,far=%.7f,10m=%.7f\n",
                    rate,energy(near),energy(far),energy(veryFar));
    }
    std::puts("PASS P90 NATIVE PCM: real 2x2 directional speaker HRIR, invariant anti-phase transfer, coherent MID, 44.1/48k, 64/128/256, yaw and distance");
}
