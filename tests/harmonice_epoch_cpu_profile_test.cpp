// SPDX-License-Identifier: AGPL-3.0-only
// Non-gating, same-runner old/new pitch-epoch workload comparison.
// Timing is synthetic PSOLA-only, not full mixer, driver or vocal naturalness.
#include "../engine/builtin_fx/HarmonyPitchSynchronous.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>
namespace {
using Clock=std::chrono::steady_clock;
constexpr int block=64,warmup=128,measured=1024;
constexpr double pi2=6.283185307179586;
struct Result {
    double p50=0.,p95=0.,p99=0.,p999=0.,maximum=0.,checksum=0.;
};
template <bool Fast>
Result measure(double rate,double f0){
    using Impl=armix::harmony::PitchSynchronousVoice<16384,6,Fast>;
    Impl shifter;
    std::array<float,16384> ring{};
    std::array<double,measured> times{};
    const int frames=(warmup+measured)*block;
    std::vector<float> input(static_cast<std::size_t>(frames));
    for(int t=0;t<frames;++t){
        const double phase=pi2*f0*t/rate;
        input[static_cast<std::size_t>(t)]=static_cast<float>(
            .17*std::sin(phase)+.06*std::sin(2.*phase+.3)+
            .028*std::sin(3.*phase+.17));
    }
    double sum=0.;
    for(int b=0;b<warmup+measured;++b){
        const auto before=Clock::now();
        for(int j=0;j<block;++j){
            const int t=b*block+j;
            const std::size_t at=static_cast<std::size_t>(t)&16383;
            ring[at]=input[static_cast<std::size_t>(t)];
            for(int v=0;v<5;++v){
                const auto output=shifter.process(
                    v,static_cast<std::uint64_t>(t),at,ring,
                    static_cast<float>(rate/f0),1.2f,
                    static_cast<float>(rate*.032)+static_cast<float>(v*rate*.004));
                if(!std::isfinite(output.sample)){
                    std::cerr<<"PSOLA nonfinite\n";std::exit(2);
                }
                sum+=static_cast<double>(output.sample);
            }
        }
        const auto after=Clock::now();
        if(b>=warmup)
            times[static_cast<std::size_t>(b-warmup)]=
                std::chrono::duration<double,std::micro>(after-before).count();
    }
    std::sort(times.begin(),times.end());
    const auto at=[&](double p)->double{
        return times[static_cast<std::size_t>(std::ceil(p*measured)-1)];
    };
    return {at(.5),at(.95),at(.99),at(.999),times.back(),sum};
}
void compare(double rate,double f0){
    // Order is constant, same binary and runner; scheduling noise remains.
    const auto original=measure<false>(rate,f0);
    const auto optimized=measure<true>(rate,f0);
    if(std::abs(original.checksum-optimized.checksum)>1.e-9){
        std::cerr<<"SAME INPUT PSOLA reference checksum changed\n";
        std::exit(2);
    }
    const double deadline=1.e6*block/rate;
    std::cout<<std::fixed<<std::setprecision(2)
        <<"PSOLA_EPOCH_SYNTHETIC_ONLY rate="<<rate<<" f0="<<f0
        <<" block="<<block<<" reference_p50_us="<<original.p50
        <<" optimized_p50_us="<<optimized.p50
        <<" reference_p95_us="<<original.p95
        <<" optimized_p95_us="<<optimized.p95
        <<" reference_p99_us="<<original.p99
        <<" optimized_p99_us="<<optimized.p99
        <<" reference_p99_9_us="<<original.p999
        <<" optimized_p99_9_us="<<optimized.p999
        <<" reference_max_us="<<original.maximum
        <<" optimized_max_us="<<optimized.maximum
        <<" optimized_p99_9_deadline_pct="<<100.*optimized.p999/deadline
        <<" hardware_ASIO_qualified=false\n";
}
}
int main(){
    for(double rate:{48000.,96000.})
      for(double f0:{82.406889,220.})
        compare(rate,f0);
    std::cout<<"PSOLA same-source reference/optimized checksums match; "
                "CPU comparisons non-gating\n";
}
