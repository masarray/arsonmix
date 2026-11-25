// SPDX-License-Identifier: AGPL-3.0-only
// Targeted four-instance tonal callback stress, independent of generic noise RT.
#include "AntiNoizCore.h"
#include "RtAllocationProbe.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
namespace {
using Clock=std::chrono::steady_clock;
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
struct Slot {
    Core core;
    std::array<float,128> l{},r{};
};
void report(unsigned rate,const char* stage,std::vector<double>& times,rtprobe::Counts counts){
    require(!times.empty()&&rtprobe::empty(counts),"empty measurement or callback allocation");
    std::sort(times.begin(),times.end());
    const auto p99=times[(times.size()-1)*99/100];
    const double budget=128.e6/rate;
    const auto misses=std::count_if(times.begin(),times.end(),[&](double us){return us>budget;});
    // CI scheduling can produce outliers; retain maximum/misses as evidence,
    // enforce a substantial P99 budget instead of pretending hardware FAT.
    require(p99<budget*.5,"tonal P99 exceeds half callback budget");
    std::cout<<"TONAL_RT,"<<rate<<','<<stage<<",instances=4,block=128,batches="<<times.size()
             <<",p99_us="<<p99<<",max_us="<<times.back()<<",budget_us="<<budget
             <<",misses="<<misses<<",observed_alloc_free=0\n";
}
}
int main(){try{
    for(unsigned rate:{48000u,96000u}){
        std::array<Slot,4> slots{};
        Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=14;s.silenceDb=0;
        for(auto& slot:slots){
            require(slot.core.prepare(rate,2)&&slot.core.setSettings(s)&&slot.core.beginLearn(),"prepare Learn");
        }
        std::vector<double> learn,active;learn.reserve(rate*3/128+1);active.reserve(rate*4/128+1);
        rtprobe::Counts lc{},ac{};
        double reference=0.,output=0.;
        for(unsigned phase=0;phase<2;++phase){
            const unsigned duration=(phase?4u:3u)*rate;
            for(unsigned pos=0;pos+128<=duration;pos+=128){
                for(auto& slot:slots)for(unsigned j=0;j<128;++j){
                    const double t=2.*std::numbers::pi*(pos+j)/rate;
                    const float line=float(.05*std::sin(2.5*29*t)); // 72.5Hz exact grid
                    const float other=phase?float(.08*std::sin(230.*t)):0.f;
                    slot.l[j]=line+other;slot.r[j]=line+other;
                }
                const bool learning=slots[0].core.learning();
                rtprobe::begin();const auto start=Clock::now();
                for(auto& slot:slots)slot.core.process(slot.l.data(),slot.r.data(),128);
                const auto stop=Clock::now();const auto counts=rtprobe::end();
                const auto us=std::chrono::duration<double,std::micro>(stop-start).count();
                if(!phase){lc+=counts;if(learning)learn.push_back(us);}
                else{ac+=counts;active.push_back(us);}
                for(const auto& slot:slots)for(unsigned j=0;j<128;++j){
                    require(std::isfinite(slot.l[j])&&slot.l[j]==slot.r[j],"invalid stereo output");
                    if(phase&&pos>=2*rate){reference+=.05*.05/2+.08*.08/2;output+=double(slot.l[j])*slot.l[j];}
                }
            }
            for(auto& slot:slots)require(slot.core.hasProfile()&&!slot.core.learning()&&slot.core.latencySamples()==(rate>48000?512:256),"Learn/latency invariant");
        }
        require(output>0.&&output<reference*1.1,"active tonal output range");
        report(rate,"learn",learn,lc);report(rate,"active",active,ac);
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
