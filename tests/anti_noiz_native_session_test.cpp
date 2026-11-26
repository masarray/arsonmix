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
    for(auto [index,value]:std::array<std::pair<int,std::uint32_t>,5>{{{0,0},{4,3},{8,4},{12,0x7fc00000u},{24,2}}}){
        auto corrupt=bytes;NativeState::put(corrupt,std::size_t(index),value);
        check(!NativeState::decode(corrupt,decoded)&&decoded.cleanDb==18,"transactional reject");
    }
    auto legacy=bytes;NativeState::put(legacy,4,1);
    check(NativeState::decode({legacy.data(),NativeState::legacySize},decoded)&&!decoded.manual.enabled&&decoded.cleanDb==18,"v1 migration");
    auto pro=s;pro.manual.enabled=true;pro.manual.gate=true;pro.manual.rangeDb=96;pro.spectralManual=true;pro.noiseOffsetDb=7;pro.cleanDb=40;
    check(NativeState::decode(NativeState::encode(pro),decoded)&&decoded.manual.enabled&&decoded.manual.gate&&decoded.manual.rangeDb==96&&decoded.cleanDb==40&&decoded.noiseOffsetDb==7,"v2 manual persistence");
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(int channels:{1,2}){
        NativeSession native;check(native.restore(bytes),"cold restore");
        check(native.prepare(rate,channels),"prepare");
        check(native.latency()==(rate>48000?512:256),"PDC");
        check(native.acceptedSettings().cleanDb==18,"cold accepted");
        native.seal();check(!native.restore(bytes)&&!native.prepare(rate,channels),"sealed lifetime");
        std::array<float,128> l{},r{};rtprobe::Counts alloc{};
        Control::Completion ack;NativeSession::Snapshot meter,last;
        auto process=[&]{rtprobe::begin();native.process(l.data(),channels==2?r.data():nullptr,128);alloc+=rtprobe::end();while(native.pollTelemetry(meter))last=meter;};
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
        check(native.poll(ack)&&!ack.applied&&!ack.state.learning,"rack reject ACK");
        for(unsigned pos=0;pos<rate/10;pos+=128){native.onBypass(true,true);native.consume();process();}
        check(last.rackBypassed&&last.learnStatus==NativeSession::LearnStatus::Cancelled,"rack cancel telemetry");
        check(native.acceptedSettings().enabled,"rack bypass does not alter durable power");
        native.onBypass(false,false);
        check(native.submit({4,Control::Kind::learn,{}}),"resume learn");
        std::mt19937 gen(9);std::uniform_real_distribution<float> noise(-.003f,.003f);
        bool sawCandidate=false;
        for(unsigned pos=0;pos<3*rate;pos+=128){
            for(int i=0;i<128;++i){l[i]=noise(gen);r[i]=noise(gen);}
            process();while(native.pollTelemetry(meter))last=meter;
            if(!pos)check(native.poll(ack)&&ack.applied,"resume ACK");
            if(last.spectrum.candidate&&last.spectrum.noiseValid){sawCandidate=true;check(last.learning&&last.spectrum.progress>0&&last.spectrum.progress<=1,"checkpoint progress");}
        }
        check(last.profile&&!last.learning&&last.learnStatus==NativeSession::LearnStatus::Accepted,"learn qualified");
        check(sawCandidate,"capture preview observed");
        const auto trusted=last.spectrum;
        check(trusted.inputValid&&trusted.noiseValid&&!trusted.candidate&&trusted.bins==native.latency()/2+1,"trusted spectrum");
        check(std::abs(trusted.binHz-float(rate)/native.latency())<.01f,"frequency mapping");
        float sum=0.f;for(int k=0;k<trusted.bins;++k){check(std::isfinite(trusted.noise[k])&&trusted.noise[k]>=0.f,"finite noise PSD");sum+=trusted.noise[k];}
        check(sum>1.e-6f&&sum<5.e-6f,"window-normalized noise power");
        // Rejected relearn must remain distinguishable from the trusted profile.
        check(native.submit({5,Control::Kind::learn,{}}),"relearn");
        for(unsigned pos=0;pos<3*rate;pos+=128){
            l.fill(0);r.fill(0);process();while(native.pollTelemetry(meter))last=meter;
            if(!pos)check(native.poll(ack)&&ack.applied,"relearn ACK");
        }
        check(last.profile&&!last.learning&&last.learnStatus==NativeSession::LearnStatus::Rejected,"relearn rejection retains profile");
        check(last.spectrum.noiseValid&&!last.spectrum.candidate&&last.spectrum.noise==trusted.noise,"rejected candidate cannot replace trusted spectrum");
        check(rtprobe::empty(alloc),"native callback allocation");
        const auto saved=NativeState::encode(native.acceptedSettings());
        native.release();check(!native.ready()&&!native.prepare(rate,channels),"published retirement");
        NativeSession fresh;check(fresh.restore(saved)&&fresh.prepare(rate,channels),"fresh epoch");
        check(!fresh.observed().profile&&fresh.observed().learnStatus==NativeSession::LearnStatus::Idle,"epoch invalidates PSD");
        check(!fresh.observed().spectrum.noiseValid&&!fresh.observed().spectrum.inputValid,"fresh epoch clears graph");
        std::cout<<"NATIVE_SESSION,"<<rate<<','<<channels<<",PDC="<<fresh.latency()<<",ACK/state/bypass/Learn/epoch PASS\n";
    }
    // Known-bin stereo tone: a drained versus a saturated telemetry queue.
    for(unsigned rate:{44100u,48000u,88200u,96000u}){
        NativeSession polled,blocked;
        for(auto* f:{&polled,&blocked}){
            check(f->prepare(rate,2),"tone prepare");
            check(f->submit({1,Control::Kind::learn,{}}),"tone learn");
        }
        const int n=polled.latency(),bin=8;
        std::array<float,128> l{},r{},otherL{},otherR{};
        NativeSession::Snapshot last,meter;
        Control::Completion a,b;rtprobe::Counts allocations{};
        for(unsigned pos=0;pos<3*rate;pos+=128){
            for(int i=0;i<128;++i){
                l[i]=.03f*float(std::sin(2.*std::numbers::pi_v<double>*bin*double(pos+i)/n));
                r[i]=-l[i];
            }
            otherL=l;otherR=r;
            rtprobe::begin();polled.process(l.data(),r.data(),128);blocked.process(otherL.data(),otherR.data(),128);allocations+=rtprobe::end();
            check(l==otherL&&r==otherR,"spectral polling exact PCM parity");
            if(!pos)check(polled.poll(a)&&blocked.poll(b)&&a.applied&&b.applied,"tone ACK");
            while(polled.pollTelemetry(meter))last=meter;
        }
        check(last.profile&&last.spectrum.noiseValid&&!last.spectrum.candidate,"tone profile accepted");
        const auto& x=last.spectrum.noise;
        check(std::max_element(x.begin(),x.begin()+last.spectrum.bins)-x.begin()==bin,"known FFT bin peak");
        float sum=0;for(int k=0;k<last.spectrum.bins;++k)sum+=x[k];
        check(std::abs(sum-.00045f)<.000015f,"known tone mean-square normalization");
        check(rtprobe::empty(allocations),"spectrum parity callback allocations");
        std::cout<<"SPECTRUM_NATIVE,"<<rate<<",bin="<<bin<<",power="<<sum<<",PCM_exact=PASS,observed_alloc_free=0\n";
    }
    NativeSession invalid;check(!invalid.prepare(32000,2)&&!invalid.prepare(48000,3),"unsupported layout/rate");
}
