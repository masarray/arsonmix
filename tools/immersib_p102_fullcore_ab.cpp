// SPDX-License-Identifier: AGPL-3.0-only
// Real P102.3 full-graph deterministic stereo PCM A/B and matched CPU timing.
// This executable compiles once against original and once against optimized
// source, retaining identical product feature macros, input & scene controls.
#if !defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) || !ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH || \
    !defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) || !ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH
#error Full core P102.3 bit-exact A/B needs both approved Stage and Musical EQ
#endif
#include "../engine/builtin_fx/ImerSipP92Scene.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

namespace {
using Core=armix::sonku::SonKuSpatialCore;
using Clock=std::chrono::steady_clock;
constexpr int maxSamples=256;
constexpr double pi=3.14159265358979323846;

bool emitPCM(const char* path) {
    FILE* output=std::fopen(path,"wb");
    if(!output)return false;
    const auto close=[&](){return std::fclose(output)==0;};
    std::array<float,maxSamples> left{},right{};
    std::uint64_t hash=14695981039346656037ull;
    std::size_t written=0;
    for(const int rate:{44100,48000}) {
        for(const int scene:{2,3,4,5,6,7,8}) {
            auto fx=std::make_unique<Core>();
            fx->optInImmerSIBStageMonitor();
            fx->optInImmerSIBMusicalEq();
            if(!fx->prepare(rate,armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch)) {
                std::fclose(output);return false;
            }
            auto settings=armix::builtin_fx::imersipP92Factory(scene);
            if(!fx->setSettings(settings)){std::fclose(output);return false;}
            // 256f and 128f alternating blocks. 12,288 frames per scene:
            // ring-wrap, ROOM on/off, dynamic yaw/spread and source types.
            for(int block=0;block<72;++block) {
                const int frames=block%3==0?128:256;
                if(block==14){
                    settings.headYawDegrees=12.f;
                    if(!fx->setSettings(settings)){std::fclose(output);return false;}
                }else if(block==22){
                    settings.headYawDegrees=0.f;
                    settings.stereoSpreadDegrees=82.f;
                    if(!fx->setSettings(settings)){std::fclose(output);return false;}
                }else if(block==30) {
                    settings.spacePreset=0;
                    settings.roomAmount=0.f;settings.ambience=0.f;
                    if(!fx->setSettings(settings)){std::fclose(output);return false;}
                }else if(block==43){
                    settings=armix::builtin_fx::imersipP92Factory(scene);
                    if(!fx->setSettings(settings)){std::fclose(output);return false;}
                }
                const int sample0=block*frames;
                for(int i=0;i<frames;++i) {
                    const double t=double(sample0+i)/double(rate);
                    const float l=float(.12*std::sin(2*pi*337*t)+
                                        .035*std::sin(2*pi*2187*t));
                    const float rr=float(.085*std::sin(2*pi*601*t)-
                                         .037*std::sin(2*pi*3887*t));
                    switch((block/9)%6){
                        case 0:left[i]=l;right[i]=rr;break;
                        case 1:left[i]=l;right[i]=-l;break;
                        case 2:left[i]=l;right[i]=0.f;break;
                        case 3:left[i]=0.f;right[i]=rr;break;
                        case 4:left[i]=right[i]=0.f;break;
                        default:left[i]=(i==0?.125f:0.f);
                                right[i]=(i==3?-.125f:0.f);break;
                    }
                }
                fx->process(left.data(),right.data(),frames);
                for(int i=0;i<frames;++i) {
                    for(const float sample:{left[i],right[i]}) {
                        if(!std::isfinite(sample) ||
                           std::fwrite(&sample,sizeof(sample),1,output)!=1){
                            std::fclose(output);return false;
                        }
                        hash^=std::bit_cast<std::uint32_t>(sample);
                        hash*=1099511628211ull;
                        ++written;
                    }
                }
            }
        }
    }
    if(!close())return false;
    std::printf("IMMERSIB_P102_FULL_CORE_PCM samples=%zu hash=%016llx "
                "scenes=7 rates=44100_48000 "
                "room_off_on=YES yaw_edit=YES stasis=TESTED\n",
                written,static_cast<unsigned long long>(hash));
    return true;
}

bool profile() {
    constexpr int frames=256,warm=48,measured=192;
    std::array<float,frames> inputL{},inputR{},l{},r{};
    std::vector<double> durations;durations.reserve(measured);
    for(const int rate:{44100,48000})for(const int scene:{2,8}){
        auto fx=std::make_unique<Core>();
        fx->optInImmerSIBStageMonitor();
        fx->optInImmerSIBMusicalEq();
        if(!fx->prepare(rate,armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch) ||
           !fx->setSettings(armix::builtin_fx::imersipP92Factory(scene)))
            return false;
        durations.clear();
        for(int block=0;block<warm+measured;++block){
            for(int i=0;i<frames;++i){
                const double t=double(i+block*frames)/double(rate);
                inputL[i]=float(.11*std::sin(2*pi*337*t));
                inputR[i]=float(.095*std::sin(2*pi*739*t));
            }
            std::memcpy(l.data(),inputL.data(),sizeof(l));
            std::memcpy(r.data(),inputR.data(),sizeof(r));
            const auto start=Clock::now();
            fx->process(l.data(),r.data(),frames);
            const auto stop=Clock::now();
            if(block>=warm)durations.push_back(
                std::chrono::duration<double,std::micro>(stop-start).count());
            for(int i=0;i<frames;++i)if(!std::isfinite(l[i])||!std::isfinite(r[i]))
                return false;
        }
        std::sort(durations.begin(),durations.end());
        const auto percentile=[&](double x){
            return durations[static_cast<std::size_t>(
                x*double(durations.size()-1))];
        };
        std::printf("IMMERSIB_P102_FULL_CORE_CPU rate=%d scene=%d "
                    "frames=%d blocks=%d p50_us=%.2f p95_us=%.2f "
                    "p99_us=%.2f scope=FULL_BRANDED_CORE_NOT_ASIO\n",
                    rate,scene,frames,measured,
                    percentile(.50),percentile(.95),percentile(.99));
    }
    return true;
}
}
int main(int argc,char** argv) {
    if(argc!=3||std::string_view(argv[1])!="--pcm")
        {std::fprintf(stderr,"usage: fullcore --pcm OUTPUTFILE\n");return 2;}
    if(!emitPCM(argv[2])){std::fputs("FULL_CORE_PCM_FAILED\n",stderr);return 3;}
    if(!profile()){std::fputs("FULL_CORE_PROFILE_FAILED\n",stderr);return 4;}
    return 0;
}
