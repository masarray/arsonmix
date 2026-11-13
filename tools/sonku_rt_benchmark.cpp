// SPDX-License-Identifier: AGPL-3.0-only
// Scoped, runner-local reference measurement: never a hard/flaky CI threshold.
// Same 8D-SonKu core, same input blocks, same sample clock for V1 and V2.
// No wall-clock queries or I/O INSIDE the realtime processing function.
#include "../engine/builtin_fx/sonku/SonKuSpatialCore.h"
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
struct Result {double milliseconds=0.,checksum=0.,audios=0.;};
template<std::size_t block>
Result measure(double rate,float amount,float late=0.f,
               const armix::sonku::SonKuRoomGeometry* geometry=nullptr){
    using namespace armix::sonku;
    SonKuSpatialCore dsp;
    if(!dsp.prepare(rate))throw std::runtime_error("unsupported sample rate");
    Settings s{};s.moving=true;s.revolutionSeconds=10.f;
    s.spatialMix=1.f;s.stereoSpreadDegrees=60.f;
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
    auto start=std::chrono::steady_clock::now();
    double checksum=0.;
    int done=0;
    while(done<frames){
        const int take=std::min<int>(static_cast<int>(block),frames-done);
        auto l=inputL,r=inputR;
        dsp.process(l.data(),r.data(),take);
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
    return {elapsed,checksum,static_cast<double>(frames)/rate};
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
      <<" NOTE=runner-local_not_ASIO_FAT\n";
}
}
int main(){
    try{
        for(double rate:{44100.,48000.}){
            report<128>(rate);
            report<512>(rate);
        }
    }catch(const std::exception& e){
        std::cerr<<"SONKU CPU BENCH FAIL: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
