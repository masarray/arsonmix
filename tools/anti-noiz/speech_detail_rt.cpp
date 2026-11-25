// SPDX-License-Identifier: AGPL-3.0-only
// Real-time evidence for active variable-noise Voice detail, not tonal fixture.
#include "AntiNoizCore.h"
#include "RtAllocationProbe.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
using Clock=std::chrono::steady_clock;
struct Slot{Core core;std::array<float,128> l{},r{};};
}
int main(){try{
    for(unsigned rate:{48000u,96000u}){
        std::array<Slot,4> slots{};
        Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=14;s.silenceDb=0;
        for(auto& slot:slots)require(slot.core.prepare(rate,2)&&slot.core.setSettings(s)&&slot.core.beginLearn(),"prepare");
        std::mt19937 rng(708);std::uniform_real_distribution<float> random(-1.f,1.f);
        float smooth=0.f;
        std::vector<double> times;times.reserve(rate*4/128+1);
        rtprobe::Counts allocations{};unsigned engaged=0;float maximum=0.f;
        for(unsigned phase=0;phase<2;++phase){
            const unsigned duration=(phase?4u:3u)*rate;
            for(unsigned pos=0;pos+128<=duration;pos+=128){
                for(unsigned j=0;j<128;++j){
                    const unsigned i=pos+j;
                    const float white=random(rng);smooth+=.035f*(white-smooth);
                    const float env=(i%(rate/50)<rate/200)?1.f:.08f;
                    const double t=2.*std::numbers::pi*i/rate;
                    const float voice=phase?float(.025*std::sin(375.*t)+.012*std::sin(937.5*t)+.008*std::sin(1312.5*t)):0.f;
                    const float x=env*(.12f*smooth+.006f*white)+voice;
                    for(auto& slot:slots){slot.l[j]=x;slot.r[j]=x;}
                }
                rtprobe::begin();const auto a=Clock::now();
                for(auto& slot:slots)slot.core.process(slot.l.data(),slot.r.data(),128);
                const auto b=Clock::now();allocations+=rtprobe::end();
                if(phase)times.push_back(std::chrono::duration<double,std::micro>(b-a).count());
                for(auto& slot:slots){
                    const float gain=slot.core.speechDetailRestoration();
                    require(std::isfinite(gain)&&gain>=0.f&&gain<=.120001f,"restoration cap");
                    if(phase&&gain>0.f){++engaged;maximum=std::max(maximum,gain);}
                    for(unsigned j=0;j<128;++j)require(std::isfinite(slot.l[j])&&slot.l[j]==slot.r[j],"finite linked PCM");
                }
            }
            for(auto& slot:slots)require(slot.core.hasProfile()&&!slot.core.learning()&&slot.core.latencySamples()==(rate>48000?512:256),"profile latency");
        }
        require(engaged>0,"fixture failed to engage speech detail");
        require(rtprobe::empty(allocations),"callback allocation");
        std::sort(times.begin(),times.end());const double p99=times[(times.size()-1)*99/100],budget=128.e6/rate;
        require(p99<.5*budget,"speech detail callback P99");
        const auto misses=std::count_if(times.begin(),times.end(),[&](double us){return us>budget;});
        std::cout<<"SPEECH_DETAIL_RT,"<<rate<<",instances=4,block=128,engaged="<<engaged<<",max_restoration="<<maximum
                 <<",p99_us="<<p99<<",max_us="<<times.back()<<",budget_us="<<budget<<",misses="<<misses<<",observed_alloc_free=0\n";
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
