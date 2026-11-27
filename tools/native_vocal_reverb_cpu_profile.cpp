// SPDX-License-Identifier: AGPL-3.0-only
// Offline, non-gating CPU measurement; this is not an ASIO hardware benchmark.
#include "arverb/Reverb.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
    using Clock=std::chrono::steady_clock;
    constexpr double pi=3.14159265358979323846;
    std::array<float,256> inL{},inR{},outL{},outR{};
    std::array<std::unique_ptr<arverb::Engine>,8> engines;
    for(auto& e:engines)e=std::make_unique<arverb::Engine>();
    std::vector<double> values;
    values.reserve(512);
    std::printf("rate,frames,instances,p50_us,p95_us,p99_us,budget_us,p99_budget_pct\n");
    for(const int rate:{44100,48000,96000,192000}) {
        for(int i=0;i<256;++i) {
            const double phase=2.*pi*static_cast<double>(i)/rate;
            inL[static_cast<std::size_t>(i)]=static_cast<float>(.12*std::sin(220.*phase)+.05*std::sin(880.*phase));
            inR[static_cast<std::size_t>(i)]=static_cast<float>(.12*std::sin(224.*phase)+.05*std::sin(915.*phase));
        }
        for(const int frames:{32,64,128,256})for(const int count:{1,4,8}) {
            for(int i=0;i<count;++i) {
                auto& e=*engines[static_cast<std::size_t>(i)];
                e.setAlgorithm(arverb::Algorithm::Plate);
                e.loadPreset(arverb::Preset::VocalPlateClean);
                e.setVoicing(5);
                e.prepare(static_cast<double>(rate),true); // actual Reverb-Sis Singer Feel product voicing
            }
            for(int n=0;n<48;++n)for(int i=0;i<count;++i)
                engines[static_cast<std::size_t>(i)]->process(inL.data(),inR.data(),outL.data(),outR.data(),static_cast<std::size_t>(frames));
            values.clear();
            for(int n=0;n<512;++n) {
                const auto start=Clock::now();
                for(int i=0;i<count;++i)
                    engines[static_cast<std::size_t>(i)]->process(inL.data(),inR.data(),outL.data(),outR.data(),static_cast<std::size_t>(frames));
                const auto end=Clock::now();
                values.push_back(std::chrono::duration<double,std::micro>(end-start).count());
            }
            std::sort(values.begin(),values.end());
            const auto pct=[&](double fraction) {
                const auto idx=static_cast<std::size_t>(std::ceil(fraction*static_cast<double>(values.size())))-1u;
                return values[std::min(idx,values.size()-1u)];
            };
            const double budget=1.e6*frames/rate;
            std::printf("%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.2f\n",
                rate,frames,count,pct(.50),pct(.95),pct(.99),budget,100.*pct(.99)/budget);
        }
    }
}
