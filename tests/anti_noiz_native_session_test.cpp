// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/anti_noiz/AntiNoizNativeSession.h"
#include "../tools/anti-noiz/RtAllocationProbe.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>
#include <limits>
using namespace armix::builtin_fx::anti_noiz;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
int main(){
    Core::Settings s;s.mode=Core::Mode::Voice;s.cleanDb=18;s.silenceDb=0;s.musicProtect=.75f;
    const auto bytes=NativeState::encode(s);
    Core::Settings decoded;check(NativeState::decode(bytes,decoded)&&decoded.cleanDb==18,"state codec");
    for(std::size_t n=0;n<bytes.size();++n)check(!NativeState::decode({bytes.data(),n},decoded),"truncation");
    for(auto [index,value]:std::array<std::pair<int,std::uint32_t>,5>{{{0,0},{4,2},{8,4},{12,0x7fc00000u},{24,2}}}){
        auto corrupt=bytes;NativeState::put(corrupt,std::size_t(index),value);
        check(!NativeState::decode(corrupt,decoded)&&decoded.cleanDb==18,"transactional reject");
    }
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(int channels:{1,2}){
        NativeSession native;check(native.restore(bytes),"cold restore");
        check(native.prepare(rate,channels),"prepare");
        check(native.latency()==(rate>48000?512:256),"PDC");
        check(native.acceptedSettings().cleanDb==18,"cold accepted");
        native.seal();check(!native.restore(bytes)&&!native.prepare(rate,channels),"sealed lifetime");
        std::array<float,128> l{},r{};rtprobe::Counts alloc{};
        auto process=[&]{rtprobe::begin();native.process(l.data(),channels==2?r.data():nullptr,128);alloc+=rtprobe::end();};
        Control::Completion ack;Control::Snapshot meter,last;
        s.cleanDb=14;
        check(native.submit({1,Control::Kind::settings,s})&&!native.quiescent(),"queue");
        check(!native.submit({2,Control::Kind::learn,{}})&&!native.poll(ack),"single pending");
        check(native.acceptedSettings().cleanDb==18,"not optimistic");
        process();check(native.poll(ack)&&ack.applied&&ack.id==1&&native.quiescent(),"applied ACK");
        check(native.acceptedSettings().cleanDb==14,"accepted settings");
        check(native.submit({2,Control::Kind::learn,{}}),"learn request");
        process();check(native.poll(ack)&&ack.applied&&ack.state.learning,"learn started");
        native.onBypass(true,true);native.consume();process();
        check(native.submit({3,Control::Kind::learn,{}}),"bypass request");
        native.onBypass(true,true);native.consume();process();
        check(native.poll(ack)&&!ack.applied&&!ack.state.learning&&ack.state.rackBypassed&&ack.state.learnStatus==Core::LearnStatus::Cancelled,"rack cancel/reject");
        check(native.acceptedSettings().enabled,"rack bypass does not alter durable power");
        native.onBypass(false,false);
        check(native.submit({4,Control::Kind::learn,{}}),"resume learn");
        std::mt19937 gen(9);std::uniform_real_distribution<float> noise(-.003f,.003f);
        for(unsigned pos=0;pos<3*rate;pos+=128){
            for(int i=0;i<128;++i){l[i]=noise(gen);r[i]=noise(gen);}
            process();while(native.pollTelemetry(meter))last=meter;
            if(!pos)check(native.poll(ack)&&ack.applied,"resume ACK");
        }
        check(last.profile&&!last.learning&&last.learnStatus==Core::LearnStatus::Accepted,"learn qualified");
        // Rejected relearn must remain distinguishable from the trusted profile.
        check(native.submit({5,Control::Kind::learn,{}}),"relearn");
        for(unsigned pos=0;pos<3*rate;pos+=128){
            l.fill(0);r.fill(0);process();while(native.pollTelemetry(meter))last=meter;
            if(!pos)check(native.poll(ack)&&ack.applied,"relearn ACK");
        }
        check(last.profile&&!last.learning&&last.learnStatus==Core::LearnStatus::Rejected,"relearn rejection retains profile");
        check(rtprobe::empty(alloc),"native callback allocation");
        const auto saved=NativeState::encode(native.acceptedSettings());
        native.release();check(!native.ready()&&!native.prepare(rate,channels),"published retirement");
        NativeSession fresh;check(fresh.restore(saved)&&fresh.prepare(rate,channels),"fresh epoch");
        check(!fresh.observed().profile&&fresh.observed().learnStatus==Core::LearnStatus::Idle,"epoch invalidates PSD");
        std::cout<<"NATIVE_SESSION,"<<rate<<','<<channels<<",PDC="<<fresh.latency()<<",ACK/state/bypass/Learn/epoch PASS\n";
    }
    NativeSession invalid;check(!invalid.prepare(32000,2)&&!invalid.prepare(48000,3),"unsupported layout/rate");
}
