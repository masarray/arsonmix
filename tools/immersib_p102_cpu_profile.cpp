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
    const int warmup=quick?48:96,measured=quick?128:320;
    std::vector<double> times;
    times.reserve(measured);
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
        if(block>=warmup)
            times.push_back(std::chrono::duration<double,std::micro>(stop-start).count());
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
                "p99_budget_pct=%.2f,period_us=%.2f,over_budget=%lld,pcm_hash=%016llx\n",
                rate,frames,inserts,scene,roomOn?"on":"off",measured,
                quantile(.50),quantile(.95),quantile(.99),
                100.*quantile(.99)/deadline,deadline,
                static_cast<long long>(late),static_cast<unsigned long long>(hash));
    return true;
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=2 || (std::string_view(argv[1])!="--quick" &&
                   std::string_view(argv[1])!="--matrix")) {
        std::fprintf(stderr,"Usage: ARMixImmerSIBP102CpuProfile --quick|--matrix\n");
        return 2;
    }
    const bool quick=std::string_view(argv[1])=="--quick";
    std::puts("IMMERSIB_P102_CPU_SCHEMA,version=1,driver_xrun=NOT_MEASURED,"
              "hardware_fat=NOT_RUN,scope=branded_SonKuSpatialCore_only");
    if(quick)return measure(48000,256,1,2,true,true)?0:3;
    for(int rate:{44100,48000})
        for(int frames:{64,128,256,512})
            for(int inserts:{1,2,4})
                for(int scene:{2,8})
                    for(bool roomOn:{false,true})
                        if(!measure(rate,frames,inserts,scene,roomOn,false))return 3;
    return 0;
}
