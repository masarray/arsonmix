// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
using namespace armix::builtin_fx::de_ezzer;
int main(){
 using Clock=std::chrono::steady_clock;
 constexpr int rate=48000,repeats=10000;
 for(auto mode:{Mode::Auto,Mode::Voice})for(auto shape:{Shape::Shelf,Shape::Focused})for(int frames:{64,128,256}){
    Settings s;s.mode=mode;s.shape=shape;Core c;if(!c.setSettings(s)||!c.prepare(rate,2))return 1;
    std::vector<float> l(frames),r(frames);std::vector<double> times;times.reserve(repeats);
    std::uint32_t seed=12345;int misses=0;double peak=0;
    const double period=1e6*frames/rate;
    for(int i=0;i<repeats+200;++i){
        for(int j=0;j<frames;++j){const double t=static_cast<double>(i*frames+j)/rate;seed=1664525u*seed+1013904223u;const double noise=static_cast<double>(seed)/4294967296.*2-1;
            l[j]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+.23*std::sin(2*std::numbers::pi*4600*t)+.04*noise);
            r[j]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+.25*std::sin(2*std::numbers::pi*8000*t)-.04*noise);
        }
        const auto start=Clock::now();c.process(l.data(),r.data(),frames);const auto stop=Clock::now();
        if(i>=200){const double us=std::chrono::duration<double,std::micro>(stop-start).count();times.push_back(us);if(us>period)++misses;}
        for(int j=0;j<frames;++j){if(!std::isfinite(l[j])||!std::isfinite(r[j]))return 2;peak=std::max({peak,std::abs(static_cast<double>(l[j])),std::abs(static_cast<double>(r[j]))});}
    }
    std::sort(times.begin(),times.end());std::cout<<(mode==Mode::Auto?"Auto":"Voice")<<' '<<(shape==Shape::Shelf?"Shelf":"Focused")<<' '<<frames<<" P50/P95/P99/max us "<<times[4999]<<' '<<times[9499]<<' '<<times[9899]<<' '<<times.back()<<" misses "<<misses<<" peak "<<peak<<'\n';
 }
}
