// SPDX-License-Identifier: AGPL-3.0-only
// Offline callback microbenchmark, not an ASIO or scheduling qualification.
#include "../../engine/builtin_fx/anti_noiz/AntiNoizControl.h"
#include <array>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
int main(){
    using Clock=std::chrono::steady_clock;
    for(unsigned rate:{48000u,96000u})for(int block:{32,64,128,256,1024})for(auto mode:{Core::Mode::AutoProtect,Core::Mode::Voice}){
        Control fx;if(!fx.prepare(rate,2))return 1;
        Core::Settings settings;settings.mode=mode;
        if(!fx.submit({1,Control::Kind::settings,settings}))return 1;
        fx.consume();Control::Completion ack;if(!fx.poll(ack)||!ack.applied)return 1;
        if(!fx.submit({2,Control::Kind::learn,{}}))return 1;
        std::mt19937 rng(2026);std::uniform_real_distribution<float> noise(-.003f,.003f);
        std::array<float,1024> l{},r{},sourceL{},sourceR{};Control::Snapshot meter;
        for(unsigned i=0;i<3*rate;i+=unsigned(block)){
            for(int j=0;j<block;++j){l[j]=noise(rng);r[j]=noise(rng);}
            fx.process(l.data(),r.data(),block);while(fx.pollTelemetry(meter)){}
        }
        if(!fx.poll(ack)||!ack.applied||!fx.snapshot().profile)return 1;
        const unsigned count=std::max(2000u,rate*3/unsigned(block));
        std::vector<double> durations;durations.reserve(count);double checksum=0;
        for(unsigned i=0;i<count;++i){
            for(int j=0;j<block;++j){const double t=double(i*unsigned(block)+unsigned(j))/rate;
                sourceL[j]=noise(rng)+float(.08*std::sin(t*2*std::numbers::pi*375));
                sourceR[j]=noise(rng)+float(.06*std::sin(t*2*std::numbers::pi*750));}
            l=sourceL;r=sourceR;
            const auto start=Clock::now();fx.process(l.data(),r.data(),block);const auto stop=Clock::now();
            durations.push_back(std::chrono::duration<double,std::micro>(stop-start).count());
            while(fx.pollTelemetry(meter)){}checksum+=l[0];
        }
        std::sort(durations.begin(),durations.end());const double budget=1e6*block/rate;
        const double p50=durations[(count-1)/2],p99=durations[std::size_t((count-1)*.99)],peak=durations.back();
        std::cout<<rate<<','<<block<<','<<(mode==Core::Mode::Voice?"voice":"auto")<<','<<p50<<','<<p99<<','<<peak<<','<<budget<<','<<checksum<<'\n';
    }
}
