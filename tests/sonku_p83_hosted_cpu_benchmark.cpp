// SPDX-License-Identifier: AGPL-3.0-only
// Hosted Linux CPU evidence ONLY, not physical ASIO, XRUN or JUCE host.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
namespace p83=armix::sonku::p83;
static void run(double rate,int buffer,int instances,p83::Mode mode) {
    std::array<std::unique_ptr<armix::sonku::SonKuSpatialCore>,4> cores{};
    armix::sonku::Settings settings{};
    settings.moving=false;settings.spatialMix=1.f;settings.outputTrim=.25f;
    settings.stereoSpreadDegrees=60.f;settings.spacePreset=1;
    settings.sourceDistanceMeters=1.6f;
    settings.roomAmount=.65f;settings.ambience=.3f;
    for(int j=0;j<instances;++j){
        cores[j]=std::make_unique<armix::sonku::SonKuSpatialCore>();
        if(!cores[j]->prepare(rate,armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch) ||
           !cores[j]->setSettings(settings) ||
           !cores[j]->setResearchRenderMode(mode))std::abort();
    }
    std::array<std::array<float,256>,4> left{},right{};
    std::vector<double> elapsed;
    elapsed.reserve(64);
    using Clock=std::chrono::steady_clock;
    for(int block=0;block<80;++block){
        for(int j=0;j<instances;++j)
            for(int i=0;i<buffer;++i){
                const float t=static_cast<float>((block*buffer+i)/rate);
                left[j][i]=.07f*std::sin(2.f*3.14159265f*311.f*t);
                right[j][i]=.06f*std::sin(2.f*3.14159265f*719.f*t);
            }
        const auto before=Clock::now();
        for(int j=0;j<instances;++j)
            cores[j]->process(left[j].data(),right[j].data(),buffer);
        const auto after=Clock::now();
        if(block>=16)
            elapsed.push_back(std::chrono::duration<double,std::micro>(after-before).count());
    }
    std::sort(elapsed.begin(),elapsed.end());
    const auto q=[&](double p){return elapsed[static_cast<std::size_t>(p*(elapsed.size()-1))];};
    const double deadline=1.e6*buffer/rate;
    const int overruns=static_cast<int>(std::count_if(elapsed.begin(),elapsed.end(),
                              [&](double value){return value>deadline;}));
    std::printf("P83_HOSTED_CPU,mode=%s,rate=%.0f,buffer=%d,inserts=%d,p50_us=%.2f,p95_us=%.2f,p99_us=%.2f,deadline_us=%.2f,over_deadline=%d\n",
            mode==p83::Mode::MusicBedP82?"P82":"P83",rate,buffer,instances,
            q(.5),q(.95),q(.99),deadline,overruns);
}
int main(){
    for(double rate:{44100.,48000.})
      for(int block:{64,128,256})
        for(int inserts:{1,2,4}) {
          run(rate,block,inserts,p83::Mode::MusicBedP82);
          run(rate,block,inserts,p83::Mode::SpeakerMonitor);
        }
}
