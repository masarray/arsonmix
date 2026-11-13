// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/lms/PreparedSpeakerSystem.h"
#include <array>
#ifdef NDEBUG
#undef NDEBUG // Always execute PCM assertions, even in release CTest.
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace armix::lms;
constexpr double pi=3.14159265358979323846;
SpeakerSystemRequest base(int ways=2,bool stereo=true) {
    SpeakerSystemRequest r;
    r.deviceEpoch=71;r.graphRevision=14;
    r.sampleRate=48000.;r.maxFrames=256;r.activeOutputChannels=8;
    r.ways=ways;r.stereo=stereo;
    if(ways==2)r.crossoverHz={1000.};
    if(ways==3)r.crossoverHz={150.,2000.};
    if(ways==4)r.crossoverHz={80.,300.,2500.};
    r.verifiedPorts={{"ASIO#37/OUT-1",0},{"ASIO#37/OUT-3",2},
                     {"ASIO#37/OUT-4",3},{"ASIO#37/OUT-6",5},
                     {"ASIO#37/OUT-8",7}};
    return r;
}
OutputPatch patch(std::string id,std::string pin,int side,int way,double delay=0.) {
    OutputPatch p;
    p.outputId=std::move(id);p.portIdentity=std::move(pin);
    p.sourceSide=side;p.section=way;p.delayMs=delay;
    p.mute=false;p.limiterEnabled=true;p.ceilingDbfs=0.;
    p.confirmedSpeakerProfile=true;return p;
}
std::unique_ptr<PreparedSpeakerSystem> prepared(const SpeakerSystemRequest& r) {
    std::unique_ptr<PreparedSpeakerSystem> out;
    const auto result=PreparedSpeakerSystem::prepare(r,out);
    if(!result.ok()) {
        std::cerr<<"prepare fault="<<static_cast<int>(result.fault)<<"\n";
        throw std::runtime_error("prepare failed");
    }
    assert(out&&out->epoch()==r.deviceEpoch&&out->revision()==r.graphRevision);
    assert(out->sectionCount()==static_cast<std::size_t>(r.ways*(r.stereo?2:1)));
    assert(out->destinationCount()==r.outputs.size());
    assert(out->scratchBytes()==result.scratchBytes);
    return out;
}
struct Outputs {
    std::vector<std::vector<float>> data;
    std::vector<float*> raw;
    explicit Outputs(int n):data(n,std::vector<float>(256,0.f)){
        for(auto& d:data)raw.push_back(d.data());
    }
};
void validate() {
    auto r=base();
    auto kept=prepared(r);
    auto reject=[&](SpeakerPlanFault fault){
        auto report=PreparedSpeakerSystem::prepare(r,kept);
        assert(report.fault==fault);
        assert(kept&&kept->revision()==14);
    };
    r.deviceEpoch=0;reject(SpeakerPlanFault::invalidEpoch);r.deviceEpoch=71;
    r.crossoverHz={30000.};reject(SpeakerPlanFault::invalidCrossover);r.crossoverHz={1000.};
    r.verifiedPorts[0].identity="DEMO-01";reject(SpeakerPlanFault::invalidPort);
    r.verifiedPorts[0].identity="ASIO#37/OUT-1";
    r.outputs={patch("low","invalid",0,0)};reject(SpeakerPlanFault::invalidPort);
    r.outputs={patch("low","ASIO#37/OUT-1",0,5)};reject(SpeakerPlanFault::invalidRoute);
    r.outputs={patch("low","ASIO#37/OUT-1",0,0),patch("low2","ASIO#37/OUT-1",0,0)};
    reject(SpeakerPlanFault::duplicatePort);
    r.outputs={patch("low","ASIO#37/OUT-1",0,0)};
    r.outputs[0].limiterEnabled=false;reject(SpeakerPlanFault::unconfirmedSpeaker);
    r.outputs[0].mute=true;r.outputs[0].confirmedSpeakerProfile=false;
    assert(prepared(r)->sectionCount()==4);
    r.outputs[0].delayMs=600.;reject(SpeakerPlanFault::invalidOutputDsp);
    r.outputs[0].delayMs=500.;r.scratchBudgetBytes=512;
    reject(SpeakerPlanFault::scratchBudget);
    r=base();r.verifiedPorts[1].callbackOrdinal=0;
    reject(SpeakerPlanFault::invalidPort);
}
void frequency(double hz,double low,double high) {
    auto r=base();
    r.outputs={patch("low L","ASIO#37/OUT-1",0,0),
               patch("high L","ASIO#37/OUT-3",0,1),
               patch("low R","ASIO#37/OUT-4",1,0),
               patch("high R","ASIO#37/OUT-6",1,1)};
    auto system=prepared(r);
    std::array<float,256> left{},right{};
    Outputs output(8);
    double sumLo=0.,sumHi=0.,sumInput=0.,sumCombined=0.;
    for(int block=0;block<400;++block){
        for(int i=0;i<256;++i){
            const double t=static_cast<double>(block*256+i)/r.sampleRate;
            left[i]=static_cast<float>(.25*std::sin(2*pi*hz*t));
            right[i]=left[i]*.5f;
        }
        assert(system->process(71,14,left.data(),right.data(),output.raw.data(),8,256,true));
        if(block<15)continue;
        for(int i=0;i<256;++i){
            const double lo=output.data[0][i],hi=output.data[2][i];
            sumLo+=lo*lo;sumHi+=hi*hi;sumInput+=left[i]*left[i];
            sumCombined+=(lo+hi)*(lo+hi);
            assert(std::abs(output.data[3][i]-lo*.5)<.00001);
            assert(std::abs(output.data[5][i]-hi*.5)<.00001);
        }
    }
    const auto measuredLow=std::sqrt(sumLo/sumInput);
    const auto measuredHigh=std::sqrt(sumHi/sumInput);
    const auto summed=std::sqrt(sumCombined/sumInput);
    std::cout<<"LR4 "<<hz<<"Hz low="<<measuredLow<<" high="<<measuredHigh
             <<" combined="<<summed<<"\n";
    assert(std::abs(measuredLow-low)<.03);
    assert(std::abs(measuredHigh-high)<.03);
    assert(std::abs(summed-1.)<.035);
}
void hardware_guard_and_alias() {
    auto r=base(2,false);
    r.outputs={patch("low","ASIO#37/OUT-1",0,0),
               patch("high","ASIO#37/OUT-3",0,1)};
    auto system=prepared(r);
    Outputs out(8);
    std::array<float,256> source{};source.fill(.25f);
    assert(!system->process(72,14,source.data(),nullptr,out.raw.data(),8,256,true));
    assert(!system->process(71,15,source.data(),nullptr,out.raw.data(),8,256,true));
    assert(!system->process(71,14,source.data(),nullptr,out.raw.data(),8,257,true));
    assert(!system->process(71,14,source.data(),nullptr,out.raw.data(),8,256,false));
    for(const auto sample:out.data[0])assert(sample==0.f);
    std::fill(out.data[0].begin(),out.data[0].end(),.25f);
    assert(system->process(71,14,out.raw[0],nullptr,out.raw.data(),8,256,true));
    assert(std::abs(out.data[2][100])<.2f);
    for(const auto& lane:out.data)for(const auto sample:lane)assert(std::isfinite(sample));
}
void delay_fanout_and_mute() {
    auto r=base(1,false);
    r.outputs={patch("dry","ASIO#37/OUT-1",0,0),
               patch("delayed","ASIO#37/OUT-3",0,0,1.5/48.)};
    auto system=prepared(r);
    Outputs out(8);
    std::array<float,256> impulse{};impulse[0]=.5f;
    assert(system->process(71,14,impulse.data(),nullptr,out.raw.data(),8,256,true));
    assert(std::abs(out.data[0][0]-.5f)<1e-6f);
    assert(std::abs(out.data[2][0])<1e-6f);
    assert(std::abs(out.data[2][1]-.25f)<1e-6f);
    assert(std::abs(out.data[2][2]-.25f)<1e-6f);
    r.outputs[0].mute=true;r.outputs[0].limiterEnabled=false;
    auto muted=prepared(r);
    std::fill(out.data[0].begin(),out.data[0].end(),9.f);
    assert(muted->process(71,14,impulse.data(),nullptr,out.raw.data(),8,256,true));
    for(const auto sample:out.data[0])assert(sample==0.f);
}
void digital_ceiling() {
    auto r=base(1,false);
    r.outputs={patch("full","ASIO#37/OUT-1",0,0)};
    r.outputs[0].gainDb=6.;r.outputs[0].ceilingDbfs=-12.;
    auto system=prepared(r);
    Outputs out(8);
    std::array<float,256> in{};in.fill(1.f);
    assert(system->process(71,14,in.data(),nullptr,out.raw.data(),8,256,true));
    const float ceiling=static_cast<float>(std::pow(10.,-12./20.));
    for(const float sample:out.data[0])assert(sample<=ceiling+1.e-6f);
}
void per_output_peq() {
    auto r=base(1,false);
    r.outputs={patch("peq","ASIO#37/OUT-1",0,0)};
    r.outputs[0].peq.push_back({1000.,1.,6.});
    auto system=prepared(r);
    Outputs out(8);
    std::array<float,256> in{};
    double inPower=0.,outPower=0.;
    for(int block=0;block<200;++block){
        for(int i=0;i<256;++i){
            const double time=(block*256+i)/48000.;
            in[i]=static_cast<float>(.1*std::sin(2*pi*1000.*time));
        }
        assert(system->process(71,14,in.data(),nullptr,out.raw.data(),8,256,true));
        if(block<10)continue;
        for(int i=0;i<256;++i){
            inPower+=in[i]*in[i];
            outPower+=out.data[0][i]*out.data[0][i];
        }
    }
    const auto gain=std::sqrt(outPower/inPower);
    assert(gain>1.93&&gain<2.07); // 6 dB peaking EQ, true PCM
    r.outputs[0].peq[0].q=0.;
    std::unique_ptr<PreparedSpeakerSystem> rejected=std::move(system);
    const auto bad=PreparedSpeakerSystem::prepare(r,rejected);
    assert(bad.fault==SpeakerPlanFault::invalidOutputDsp);
    assert(rejected&&rejected->destinationCount()==1); // rollback
}
int main(){
    validate();
    frequency(100.,1.,0.);
    frequency(1000.,.5,.5);
    frequency(10000.,0.,1.);
    hardware_guard_and_alias();delay_fanout_and_mute();digital_ceiling();per_output_peq();
    for(int ways=1;ways<=4;++ways)
        assert(prepared(base(ways))->sectionCount()==static_cast<std::size_t>(2*ways));
    std::cout<<"PASS LMS prepared PCM: LR4, routing, epoch, DSP and delay\n";
}
