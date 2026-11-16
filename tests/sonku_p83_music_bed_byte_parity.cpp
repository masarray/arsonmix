// SPDX-License-Identifier: AGPL-3.0-only
// Compile same source twice with/without P8.3 macro; cmp binary raw PCM.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto* out=std::fopen(argv[1],"wb");
    if(!out)return 2;
    for(double rate:{44100.,48000.})
    for(int room:{0,1})
    for(int opposite:{0,1}){
        auto dsp=std::make_unique<armix::sonku::SonKuSpatialCore>();
        if(!dsp->prepare(rate,armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch))return 3;
        armix::sonku::Settings s{};
        s.moving=false;s.startAzimuthDegrees=30.f;
        s.stereoSpreadDegrees=60.f;s.outputTrim=.35f;s.spatialMix=1.f;
        s.spacePreset=room;s.roomAmount=.65f;s.ambience=.35f;
        s.directElevationDegrees=0.f;
        if(!dsp->setSettings(s))return 4;
        for(int i=0;i<8192;i+=128){
            std::array<float,128> l{},r{};
            for(int k=0;k<128;++k){
                const double t=(i+k)/rate;
                const float v=.10f*std::sin(2.*3.141592653589793*83.*t)+
                              .08f*std::sin(2.*3.141592653589793*461.*t)+
                              .03f*std::sin(2.*3.141592653589793*5387.*t);
                l[k]=v;r[k]=opposite?-v:v;
            }
            dsp->process(l.data(),r.data(),128);
            if(std::fwrite(l.data(),sizeof(float),128,out)!=128 ||
               std::fwrite(r.data(),sizeof(float),128,out)!=128)return 5;
        }
    }
    return std::fclose(out)==0?0:6;
}
