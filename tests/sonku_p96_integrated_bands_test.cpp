// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;

static void measure(int rate,int angle,int room,float meters) {
    auto core=std::make_unique<SonKuSpatialCore>();
    assert(core->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
    Settings s{};
    s.renderMode=9;
    s.moving=false;
    s.startAzimuthDegrees=static_cast<float>(angle);
    s.stereoSpreadDegrees=82.f;
    s.spatialMix=1.f;
    s.outputTrim=1.2f;
    s.spacePreset=room;
    s.sourceDistanceMeters=meters;
    s.roomAmount=0.f; // ROOM geometry ON; wet OFF isolates measured direct
    s.ambience=0.f;
    assert(valid(s) && core->setSettings(s));
    const int n=rate;
    constexpr double pi=3.1415926535897932384626433832795;
    std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i) {
        const double t=static_cast<double>(i)/rate;
        const float x=.08f*std::sin(2*pi*70*t)+
                      .055f*std::sin(2*pi*800*t)+
                      .028f*std::sin(2*pi*7500*t);
        l[i]=x;
        r[i]=.8f*x+.008f*std::sin(2*pi*1770*t);
    }
    for(int i=0;i<n;i+=256)
        core->process(l.data()+i,r.data()+i,std::min(256,n-i));
    std::uint64_t hash=14695981039346656037ull;
    for(int i=0;i<n;++i){
        assert(std::isfinite(l[i]) && std::isfinite(r[i]));
        assert(std::abs(l[i])<=.9805f && std::abs(r[i])<=.9805f);
        for(float value:{l[i],r[i]}){
            hash^=std::bit_cast<std::uint32_t>(value);
            hash*=1099511628211ull;
        }
    }
    constexpr std::array<int,3> freqs{70,800,7500};
    std::array<double,3> amps{};
    for(int b=0;b<3;++b){
        double sinSum=0.,cosSum=0.;
        for(int i=n/2;i<n;++i){
            const double phase=2*pi*freqs[b]*i/rate;
            sinSum+=l[i]*std::sin(phase);
            cosSum+=l[i]*std::cos(phase);
        }
        amps[b]=2.*std::hypot(sinSum,cosSum)/(n-n/2);
        assert(std::isfinite(amps[b]) && amps[b]>0.);
    }
    std::printf("P96_FULL,%d,%d,%d,%.1f,%.9f,%.9f,%.9f,%016llx\n",
                rate,angle,room,meters,amps[0],amps[1],amps[2],
                static_cast<unsigned long long>(hash));
}
int main(){
    for(int rate:{44100,48000})
        for(int angle:{0,180}){
            measure(rate,angle,0,10.f); // ROOM OFF exact hash parity
            measure(rate,angle,3,2.f);  // <=2m exact hash parity
            measure(rate,angle,3,10.f); // far stage spectral retention
        }
}
