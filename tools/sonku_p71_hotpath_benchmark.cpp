// SPDX-License-Identifier: AGPL-3.0-only
// Exact static source/scenes for old P7 vs P7.1 warmup-split FIR.
// This is an informational Linux runner process-time experiment: NOT ASIO.
#ifndef SONKU_P7_MEASURED_ELEVATION_RESEARCH
#error P7 HRTF bank must be used in both identical comparison binaries
#endif
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
int main(){
    using namespace armix::sonku;
    constexpr int block=128,frames=48000,instances=4;
    constexpr float pi=3.14159265358979323846f;
    std::array<std::unique_ptr<SonKuSpatialCore>,instances> dsps{};
    for(auto& dsp:dsps){
        dsp=std::make_unique<SonKuSpatialCore>();
        if(!dsp->prepare(48000.,SonKuReflectionProfile::SixSurfaceKemarResearch))
            return 2;
        Settings s{};
        s.spacePreset=1;s.roomAmount=.65f;s.ambience=.2f;
        s.stereoSpreadDegrees=60.f;s.startAzimuthDegrees=35.f;
        s.moving=true;s.revolutionSeconds=4.f;s.outputTrim=.65f;
        if(!dsp->setSettings(s))return 3;
    }
    std::array<float,block> l{},r{};
    for(int k=0;k<block;++k){
        const float t=static_cast<float>(k)/48000.f;
        l[k]=.11f*std::sin(2.f*pi*220.f*t)+
             .05f*std::sin(2.f*pi*3200.f*t);
        r[k]=.11f*std::sin(2.f*pi*220.f*t)-
             .05f*std::sin(2.f*pi*3200.f*t);
    }
    // Separate non-timed warmup; all FIR delay epochs and room transition
    // have reached steady state before measured PCM callback starts.
    for(int sample=0;sample<frames;sample+=block)
        for(auto& dsp:dsps){auto a=l,b=r;dsp->process(a.data(),b.data(),block);}
    std::vector<double> us;
    us.reserve(frames/block);
    auto first=std::chrono::steady_clock::now();
    for(int sample=0;sample<frames;sample+=block){
        const auto start=std::chrono::steady_clock::now();
        for(auto& dsp:dsps){
            auto a=l,b=r;
            dsp->process(a.data(),b.data(),block);
            if(!std::isfinite(a[0])||!std::isfinite(b[0]))return 4;
        }
        const auto end=std::chrono::steady_clock::now();
        us.push_back(std::chrono::duration<double,std::micro>(end-start).count());
    }
    const auto last=std::chrono::steady_clock::now();
    std::sort(us.begin(),us.end());
    const auto quantile=[&](float q){
        const size_t k=std::clamp(static_cast<size_t>(std::ceil(q*us.size())),
                                  size_t(1),us.size());
        return us[k-1];
    };
    const double total=std::chrono::duration<double,std::milli>(last-first).count();
    std::printf("SONKU_P71_HOTPATH block=128 rate=48000 inserts=4 spread=60 "
                "total_ms=%.3f p50_us=%.3f p95_us=%.3f p99_us=%.3f\n",
                total,quantile(.50f),quantile(.95f),quantile(.99f));
}
