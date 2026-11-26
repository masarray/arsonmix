// SPDX-License-Identifier: AGPL-3.0-only
// Same driver compiled against pinned pre-P10 and candidate cores.
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
using namespace armix::sonku;
int main(int argc,char** argv){
    assert(argc==2);
    auto* out=std::fopen(argv[1],"wb");assert(out);
    constexpr double pi=3.14159265358979323846;
    constexpr int block=128;
    for(int brand=0;brand<2;++brand){
        auto dsp=std::make_unique<SonKuSpatialCore>();
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
        if(brand==0)dsp->optInP99ForSonKuInsert();
#if defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) && ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH
        if(brand==1)dsp->optInImmerSIBStageMonitor();
#endif
#endif
        for(int rate:{44100,48000}){
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
            assert(dsp->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch));
#else
            assert(dsp->prepare(rate));
#endif
            Settings s{};
            s.revolutionSeconds=24.f;s.stereoSpreadDegrees=180.f;
            s.spacePreset=3;s.sourceDistanceMeters=2.7f;
            s.roomAmount=1.f;s.ambience=.77f;s.outputTrim=1.8f;
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
            s.renderMode=9;s.sceneVoicing=brand==0?1:0;
#endif
            for(int stage=0;stage<9;++stage){
                // Static and moving, zero spread, direction reversal, XYZ edits,
                // room tail transition, dry, reset and return to wet.
                if(stage==1){s.moving=false;s.startAzimuthDegrees=180.f;}
                if(stage==2){s.stereoSpreadDegrees=0.f;s.startAzimuthDegrees=-90.f;}
                if(stage==3){s.stereoSpreadDegrees=127.f;s.startAzimuthDegrees=35.f;
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
                    s.directElevationDegrees=15.f;s.hrtfElevationTrimDegrees=-5.f;s.headYawDegrees=25.f;
#endif
                }
                if(stage==4){s.moving=true;s.clockwise=false;s.revolutionSeconds=.5f;}
                if(stage==5){s.spacePreset=0;s.sourceDistanceMeters=1.6f;
#if defined(SONKU_P72_NATIVE_XYZ_RT) && SONKU_P72_NATIVE_XYZ_RT
                    s.directElevationDegrees=0.f;s.hrtfElevationTrimDegrees=0.f;
#endif
                }
                if(stage==6){s.spatialMix=0.f;s.moving=false;}
                if(stage==7){s.spatialMix=1.f;s.spacePreset=3;s.sourceDistanceMeters=2.7f;dsp->reset();}
                if(stage==8){s.startAzimuthDegrees=0.f;
#if defined(SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH) && SONKU_P99_MUSICAL_VOCAL_DEPTH_RESEARCH
                    s.renderMode=0;s.sceneVoicing=0;
#endif
                }
                assert(valid(s)&&dsp->setSettings(s));
                std::vector<double> timings;
                timings.reserve(128);
                std::array<float,257> l{},r{};
                int done=0;
                // Non-power-of-two blocks exercise every write-ring offset.
                while(done<8192){
                    const int cap=(done%3==0?64:done%3==1?block:257);
                    const int n=std::min(cap,8192-done);
                    for(int i=0;i<n;++i){
                        const double t=double(stage*8192+done+i)/rate;
                        const float m=.065f*std::sin(2*pi*320*t)+.032f*std::sin(2*pi*1900*t);
                        const float side=.025f*std::sin(2*pi*3831*t);
                        l[i]=m+side;r[i]=(stage%3==0?m-side:stage%3==1?l[i]:-l[i]);
                    }
                    const auto begin=std::chrono::steady_clock::now();
                    dsp->process(l.data(),r.data(),n);
                    const auto end=std::chrono::steady_clock::now();
                    // Normalize informational timings by processed samples.
                    timings.push_back(std::chrono::duration<double,std::micro>(end-begin).count()/n);
                    for(int i=0;i<n;++i){
                        assert(std::isfinite(l[i])&&std::isfinite(r[i]));
                        assert(std::fwrite(&l[i],sizeof(float),1,out)==1);
                        assert(std::fwrite(&r[i],sizeof(float),1,out)==1);
                    }
                    done+=n;
                }
                std::sort(timings.begin(),timings.end());
                auto q=[&](double p){return timings[std::min(timings.size()-1,
                    static_cast<std::size_t>(std::ceil(p*timings.size())-1))];};
                std::printf("P10,%d,%d,%d,%zu,%.6f,%.6f,%.6f\n",
                    brand,rate,stage,timings.size(),q(.50),q(.95),q(.99));
            }
        }
    }
    assert(std::fclose(out)==0);
}
