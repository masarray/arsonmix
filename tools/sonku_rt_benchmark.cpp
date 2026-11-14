// SPDX-License-Identifier: AGPL-3.0-only
// Scoped, runner-local reference measurement: never a hard/flaky CI threshold.
// Same 8D-SonKu core, same input blocks, same sample clock for V1 and V2.
// No wall-clock queries or I/O INSIDE the realtime processing function.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <array>
#include <algorithm>
#include <vector>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
struct Result {
    double milliseconds=0.,checksum=0.,audios=0.;
    double p50us=0.,p95us=0.,p99us=0.;
};
template<std::size_t block>
Result measure(double rate,float amount,float late=0.f,
               const armix::sonku::SonKuRoomGeometry* geometry=nullptr,
               bool directional=false,float spread=60.f,
               bool fused=false){
    using namespace armix::sonku;
    SonKuSpatialCore dsp;
    if(!dsp.prepare(rate))throw std::runtime_error("unsupported sample rate");
    if(directional && !dsp.setDirectionalEarlyReflectionsHrir(true))
        throw std::runtime_error("directional room not prepared");
    if(fused && !dsp.setDirectionalZeroSpreadFusion(true))
        throw std::runtime_error("P3 zero-spread fusion not prepared");
    Settings s{};s.moving=true;s.revolutionSeconds=10.f;
    s.spatialMix=1.f;s.stereoSpreadDegrees=spread;
    if(!dsp.setSettings(s)||
       (geometry && !dsp.setAcousticRoomGeometry(*geometry))||
       !dsp.setEarlyReflectionsAmount(amount)||
       !dsp.setMusicalReverbAmount(late))
        throw std::runtime_error("invalid DSP setup");
    std::array<float,block> inputL{},inputR{};
    for(std::size_t n=0;n<block;++n){
        inputL[n]=std::sin(n*.091f)*.18f;
        inputR[n]=std::sin(n*.071f)*.16f;
    }
    const int frames=static_cast<int>(rate)*3; // three virtual seconds
    // Instrumentation lives outside the DSP callback. It allocates once
    // before processing and stores one duration only AFTER each callback.
    // Informational hosted-runner timing is not ASIO deadline evidence.
    std::vector<double> callbackUs;
    callbackUs.reserve((frames+static_cast<int>(block)-1)/static_cast<int>(block));
    auto start=std::chrono::steady_clock::now();
    double checksum=0.;
    int done=0;
    while(done<frames){
        const int take=std::min<int>(static_cast<int>(block),frames-done);
        auto l=inputL,r=inputR;
        const auto blockStart=std::chrono::steady_clock::now();
        dsp.process(l.data(),r.data(),take);
        const auto blockEnd=std::chrono::steady_clock::now();
        callbackUs.push_back(std::chrono::duration<double,std::micro>(
            blockEnd-blockStart).count());
        for(int i=0;i<take;++i){
            if(!std::isfinite(l[i])||!std::isfinite(r[i]))
                throw std::runtime_error("nonfinite benchmark output");
            checksum+=std::abs(l[i])+std::abs(r[i]);
        }
        done+=take;
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    if(!(elapsed>0.)||!(checksum>1.))throw std::runtime_error("empty benchmark");
    if(callbackUs.empty())throw std::runtime_error("missing callback samples");
    std::sort(callbackUs.begin(),callbackUs.end());
    const auto percentile=[&](double q) noexcept {
        const auto index=static_cast<std::size_t>(
            std::ceil(q*static_cast<double>(callbackUs.size())))-1u;
        return callbackUs[std::min(index,callbackUs.size()-1)];
    };
    const double p50=percentile(.50),p95=percentile(.95),p99=percentile(.99);
    if(!(p50>0. && p50<=p95 && p95<=p99) || !std::isfinite(p99))
        throw std::runtime_error("invalid callback distribution");
    return {elapsed,checksum,static_cast<double>(frames)/rate,
            p50,p95,p99};
}
template<std::size_t block>
void report(double rate){
    const auto baseline=measure<block>(rate,0.f);
    const auto room=measure<block>(rate,.55f);
    const auto musical=measure<block>(rate,.55f,.42f);
    const armix::sonku::SonKuRoomGeometry large{28.8f,28.8f,8.f};
    const auto space=measure<block>(rate,.55f,0.f,&large);
    std::cout<<std::fixed<<std::setprecision(3)
      <<"SONKU_CPU rate="<<rate<<" block="<<block
      <<" v1_ms="<<baseline.milliseconds
      <<" v2_ms="<<room.milliseconds
      <<" v3_ms="<<musical.milliseconds
      <<" v4_large_far_ms="<<space.milliseconds
      <<" v3_vs_v2="<<musical.milliseconds/room.milliseconds
      <<" ratio="<<room.milliseconds/baseline.milliseconds
      <<" v1_cores="<<(baseline.milliseconds/1000.)/baseline.audios
      <<" v2_cores="<<(room.milliseconds/1000.)/room.audios
      <<" v3_cores="<<(musical.milliseconds/1000.)/musical.audios
      <<" v1_p50us="<<baseline.p50us
      <<" v1_p95us="<<baseline.p95us
      <<" v1_p99us="<<baseline.p99us
      <<" v2_p99us="<<room.p99us
      <<" v3_p99us="<<musical.p99us
      <<" v3_p99_budget_ratio="<<musical.p99us/
        (1000000.*static_cast<double>(block)/rate)
      <<" NOTE=runner-local_not_ASIO_FAT\n";
}
}
int main(){
    try{
        for(double rate:{44100.,48000.}){
            report<64>(rate);
            report<128>(rate);
            report<256>(rate);
            report<512>(rate);
        }
        const auto light=measure<128>(48000.,.55f);
        const auto reflected=measure<128>(48000.,.55f,0.f,nullptr,true);
        std::cout<<std::fixed<<std::setprecision(3)
          <<"SONKU_P2_CPU rate=48000 block=128 light_ms="<<light.milliseconds
          <<" measured_hrir_ms="<<reflected.milliseconds
          <<" measured_over_light="<<reflected.milliseconds/light.milliseconds
          <<" measured_p99us="<<reflected.p99us
          <<" NOTE=offline_not_ASIO\n";
        // Matched 0-degree-source-separation AB: identical input,
        // floating timing outside callback. Full 140-tap HRTF retained.
        const auto p2=measure<128>(48000.,.55f,0.f,nullptr,true,0.f,false);
        const auto p3=measure<128>(48000.,.55f,0.f,nullptr,true,0.f,true);
        const auto p3Stereo=measure<128>(48000.,.55f,0.f,nullptr,true,60.f,true);
        std::cout<<std::fixed<<std::setprecision(3)
          <<"SONKU_P3_CPU rate=48000 block=128 spread=0"
          <<" p2_ms="<<p2.milliseconds
          <<" p3_ms="<<p3.milliseconds
          <<" p2_over_p3="<<p2.milliseconds/p3.milliseconds
          <<" p2_p99us="<<p2.p99us
          <<" p3_p99us="<<p3.p99us
          <<" spread60_no_fusion_ms="<<p3Stereo.milliseconds
          <<" NOTE=runner_local_not_ASIO\n";
    }catch(const std::exception& e){
        std::cerr<<"SONKU CPU BENCH FAIL: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
