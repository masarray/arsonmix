// SPDX-License-Identifier: AGPL-3.0-only
// Real branded ImmerSIB P102.3 core-cost profiler (not ASIO/device xrun).
// CMake supplies the EXACT approved production DSP defines from ARMixConsole.
#if !defined(ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH) || !ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH || \
    !defined(ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH) || !ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH
#error "P102 CPU profile requires both approved ImmerSIB stage and EQ"
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
#include <initializer_list>
#include <memory>
#include <string_view>
#include <vector>

namespace {
using Core=armix::sonku::SonKuSpatialCore;
using Clock=std::chrono::steady_clock;
constexpr int maxFrames=512,maxInserts=4;
using Block=std::array<float,maxFrames>;
using Blocks=std::array<Block,maxInserts>;

bool measure(int rate,int frames,int inserts,int scene,bool roomOn,bool quick) {
    std::array<std::unique_ptr<Core>,maxInserts> fx{};
    Blocks sourceL{},sourceR{},left{},right{};
    for(int j=0;j<inserts;++j) {
        fx[j]=std::make_unique<Core>();
        fx[j]->optInImmerSIBStageMonitor();
        fx[j]->optInImmerSIBMusicalEq();
        if(!fx[j]->prepare(rate,armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch))
            return false;
        auto settings=armix::builtin_fx::imersipP92Factory(scene);
        if(!roomOn){ settings.spacePreset=0;settings.roomAmount=0.f;settings.ambience=0.f; }
        if(!fx[j]->setSettings(settings))return false;
        for(int i=0;i<frames;++i) {
            const double t=double(i+17*j)/double(rate);
            sourceL[j][i]=float(.064*std::sin(2.*3.141592653589793*337.*t)+
                                .048*std::sin(2.*3.141592653589793*2233.*t));
            sourceR[j][i]=float(.062*std::sin(2.*3.141592653589793*599.*t)-
                                .038*std::sin(2.*3.141592653589793*4100.*t));
        }
    }
    const int warmup=quick?128:192,measured=quick?192:320;
    std::vector<double> times;
    times.reserve(measured);
    std::uint64_t streamHash=14695981039346656037ull;
    for(int block=0;block<warmup+measured;++block) {
        // Sine generation, memcpy and measurement bookkeeping are NOT
        // charged to the realtime DSP callback window.
        for(int j=0;j<inserts;++j) {
            std::memcpy(left[j].data(),sourceL[j].data(),size_t(frames)*sizeof(float));
            std::memcpy(right[j].data(),sourceR[j].data(),size_t(frames)*sizeof(float));
        }
        const auto start=Clock::now();
        for(int j=0;j<inserts;++j)fx[j]->process(left[j].data(),right[j].data(),frames);
        const auto stop=Clock::now();
        if(block>=warmup) {
            times.push_back(std::chrono::duration<double,std::micro>(stop-start).count());
            for(int j=0;j<inserts;++j)for(int i=0;i<frames;++i)
                for(float sample:{left[j][i],right[j][i]}) {
                    if(!std::isfinite(sample))return false;
                    streamHash^=std::bit_cast<std::uint32_t>(sample);
                    streamHash*=1099511628211ull;
                }
        }
    }
    std::uint64_t hash=14695981039346656037ull;
    for(int j=0;j<inserts;++j)for(int i=0;i<frames;++i)
        for(float sample:{left[j][i],right[j][i]}) {
            if(!std::isfinite(sample))return false;
            hash^=std::bit_cast<std::uint32_t>(sample);
            hash*=1099511628211ull;
        }
    std::sort(times.begin(),times.end());
    const auto quantile=[&](double fraction) {
        return times[static_cast<std::size_t>(fraction*double(times.size()-1))];
    };
    const double deadline=1.e6*double(frames)/rate;
    const auto late=std::count_if(times.begin(),times.end(),
        [&](double t){return t>deadline;});
    std::printf("IMMERSIB_P102_CPU,engine=branded_core,rate=%d,frames=%d,inserts=%d,"
                "scene=%d,room=%s,blocks=%d,p50_us=%.2f,p95_us=%.2f,p99_us=%.2f,"
                "p99_budget_pct=%.2f,period_us=%.2f,over_budget=%lld,pcm_hash=%016llx,stream_hash=%016llx\n",
                rate,frames,inserts,scene,roomOn?"on":"off",measured,
                quantile(.50),quantile(.95),quantile(.99),
                100.*quantile(.99)/deadline,deadline,
                static_cast<long long>(late),static_cast<unsigned long long>(hash),
                static_cast<unsigned long long>(streamHash));
    return true;
}

// Root-cause attribution only: real six-surface/140-tap EARLY and eight-line
// multiband FDN LATE are measured in isolation with their approved source.
// These numbers are NOT additive percentages of the full branded graph:
// isolated caches, different feeds and independent warmup states matter.
// Never change the approved P102 coefficients or mute ROOM to claim a win.
bool attributeRoom(int rate,int frames,int scene,bool quick) {
    using namespace armix::sonku;
    auto er=std::make_unique<SonKuEarlyReflections>();
    auto fdn=std::make_unique<SonKuMusicalReverb>();
    if(!er->prepare(rate,SonKuReflectionProfile::SixSurfaceKemarResearch) ||
       !fdn->prepare(rate))return false;
    const auto s=armix::builtin_fx::imersipP92Factory(scene);
    if(s.spacePreset==0)return false;
    const float width=s.spacePreset==1?7.2f:s.spacePreset==2?14.4f:28.8f;
    const float height=s.spacePreset==1?3.2f:s.spacePreset==2?4.8f:7.2f;
    const SonKuRoomGeometry geometry{width,width,s.sourceDistanceMeters,
                                     height,1.45f,1.45f};
    const auto preset=s.spacePreset==3?SonKuReverbPreset::LargeRoom:
        s.spacePreset==2?SonKuReverbPreset::AiryChamber:
                         SonKuReverbPreset::StudioGlue;
    if(!er->setRealtimeRoom(geometry,std::min(s.roomAmount,1.f),true)||
       !fdn->setRealtimeProgram(preset,s.ambience))return false;
    Block inL{},inR{},outL{},outR{};
    for(int i=0;i<frames;++i){
        const double t=double(i)/double(rate);
        inL[i]=float(.064*std::sin(2.*3.141592653589793*337.*t)+
                     .048*std::sin(2.*3.141592653589793*2233.*t));
        inR[i]=float(.062*std::sin(2.*3.141592653589793*599.*t)-
                     .038*std::sin(2.*3.141592653589793*4100.*t));
    }
    const int warmup=quick?64:128,measured=quick?128:320;
    for(int stage=0;stage<2;++stage) {
        std::vector<double> times;
        times.reserve(measured);
        for(int block=0;block<warmup+measured;++block) {
            const auto started=Clock::now();
            for(int i=0;i<frames;++i){
                if(stage==0){
                    const auto ears=er->process(inL[i],inR[i],
                        static_cast<double>(s.startAzimuthDegrees),
                        s.stereoSpreadDegrees,s.headYawDegrees,false);
                    outL[i]=ears.left;outR[i]=ears.right;
                }else{
                    const auto ears=fdn->process(inL[i],inR[i]);
                    outL[i]=ears.left;outR[i]=ears.right;
                }
            }
            const auto stopped=Clock::now();
            if(block>=warmup)times.push_back(
                std::chrono::duration<double,std::micro>(stopped-started).count());
        }
        std::uint64_t hash=14695981039346656037ull;
        for(int i=0;i<frames;++i)for(float sample:{outL[i],outR[i]}){
            if(!std::isfinite(sample))return false;
            hash^=std::bit_cast<std::uint32_t>(sample);
            hash*=1099511628211ull;
        }
        std::sort(times.begin(),times.end());
        const auto percentile=[&](double p){
            return times[static_cast<std::size_t>(p*(times.size()-1))];
        };
        const double periodUs=1.e6*double(frames)/rate;
        std::printf("IMMERSIB_P102_ROOM_ATTR,stage=%s,rate=%d,frames=%d,scene=%d,"
                    "walls=%d,lines=%d,blocks=%d,p50_us=%.2f,p95_us=%.2f,"
                    "p99_us=%.2f,period_pct=%.2f,pcm_hash=%016llx,"
                    "note=ISOLATED_NOT_ADDITIVE\n",
                    stage==0?"early_six_surface":"late_eight_line_fdn",
                    rate,frames,scene,er->activeWallCount(),
                    SonKuMusicalReverb::lineCount,measured,
                    percentile(.50),percentile(.95),percentile(.99),
                    100.*percentile(.99)/periodUs,
                    static_cast<unsigned long long>(hash));
    }
    return true;
}

} // namespace

int main(int argc,char** argv) {
    if(argc==6 && std::string_view(argv[1])=="--case") {
        const int rate=std::atoi(argv[2]),frames=std::atoi(argv[3]);
        const int inserts=std::atoi(argv[4]),scene=std::atoi(argv[5]);
        if((rate!=44100&&rate!=48000)||frames<64||frames>maxFrames||
           inserts<1||inserts>maxInserts||scene<2||scene>8)return 2;
        return measure(rate,frames,inserts,scene,true,true)?0:3;
    }
    if(argc!=2 || (std::string_view(argv[1])!="--quick" &&
                   std::string_view(argv[1])!="--matrix" &&
                   std::string_view(argv[1])!="--attribution")) {
        std::fprintf(stderr,"Usage: ARMixImmerSIBP102CpuProfile --quick|--matrix|--attribution\n");
        return 2;
    }
    const auto mode=std::string_view(argv[1]);
    const bool quick=mode=="--quick";
    if(mode=="--attribution") {
        std::puts("IMMERSIB_P102_ATTR_SCHEMA,version=1,pcm_source=approved_unchanged,"
                  "hardware=NOT_MEASURED,scope=isolated_early_late");
        for(int rate:{44100,48000})
            for(int frames:{128,256})
                for(int scene:{2,8})
                    if(!attributeRoom(rate,frames,scene,true))return 4;
        return 0;
    }
    std::puts("IMMERSIB_P102_CPU_SCHEMA,version=1,driver_xrun=NOT_MEASURED,"
              "hardware_fat=NOT_RUN,scope=branded_SonKuSpatialCore_only");
    // Cheap practical-load sampling for Windows CI: a short buffer,
    // the current 256-frame reference, two/four simultaneous inserts,
    // and ROOM off. No hard timing threshold on shared CI runners.
    if(quick) {
        for(const auto& cfg:std::array<std::array<int,5>,5>{{
                {{48000,128,1,2,1}},{{48000,256,1,2,1}},
                {{48000,256,2,2,1}},{{48000,256,4,2,1}},
                {{48000,256,1,2,0}}}})
            if(!measure(cfg[0],cfg[1],cfg[2],cfg[3],cfg[4]!=0,true))
                return 3;
        return 0;
    }
    for(int rate:{44100,48000})
        for(int frames:{64,128,256,512})
            for(int inserts:{1,2,4})
                for(int scene:{2,8})
                    for(bool roomOn:{false,true})
                        if(!measure(rate,frames,inserts,scene,roomOn,false))return 3;
    return 0;
}
