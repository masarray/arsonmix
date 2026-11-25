// SPDX-License-Identifier: AGPL-3.0-only
// Standalone test: one producer and one audio consumer, no native host.
#include "../engine/builtin_fx/anti_noiz/AntiNoizControl.h"
#include "../tools/anti-noiz/RtAllocationProbe.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <thread>
#include <vector>
using namespace armix::builtin_fx::anti_noiz;
namespace {
constexpr int instances=4;
using Clock=std::chrono::steady_clock;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
Core::Settings packet(unsigned id,unsigned instance){
    Core::Settings s;
    s.mode=static_cast<Core::Mode>((id+instance)%4);
    s.cleanDb=float((id+instance*3)%25);
    s.silenceDb=float((id+instance*7)%49);
    s.musicProtect=float((id+instance)%11)/10.f;
    s.enabled=(id%13)!=0;
    if(id%17==0)s.cleanDb=std::numeric_limits<float>::quiet_NaN();
    return s;
}
bool equal(Core::Settings a,Core::Settings b){
    return a.mode==b.mode&&a.cleanDb==b.cleanDb&&a.silenceDb==b.silenceDb&&
           a.musicProtect==b.musicProtect&&a.enabled==b.enabled;
}
void concurrency(){
    constexpr unsigned count=12000;
    std::array<Control,instances> fx;
    for(auto& x:fx)check(x.prepare(48000,2),"concurrent prepare");
    std::atomic<bool> done=false;
    std::array<unsigned,instances> sent{},received{};
    std::array<Core::Settings,instances> accepted{};
    // Force actual backpressure before the audio thread starts.
    for(unsigned i=0;i<instances;++i){
        for(unsigned id=1;id<=16;++id)check(fx[i].submit({id,Control::Kind::settings,packet(id,i)}),"initial fill");
        check(!fx[i].submit({17,Control::Kind::settings,packet(17,i)}),"queue saturation");
        sent[i]=16;
    }
    rtprobe::Counts allocations{};
    unsigned callbacks=0;
    std::thread audio([&]{
        std::array<float,32> l{},r{};
        while(!done.load(std::memory_order_acquire)){
            for(auto& x:fx){
                l.fill(.001f);r.fill(-.001f);
                rtprobe::begin();x.process(l.data(),r.data(),32);allocations+=rtprobe::end();
                for(float v:l)check(std::isfinite(v),"concurrent finite PCM");
            }
            ++callbacks;
        }
    });
    unsigned rejected=0,telemetry=0,backpressure=0;
    while(received.back()<count||received[0]<count||received[1]<count||received[2]<count){
        for(unsigned i=0;i<instances;++i){
            Control::Completion ack;
            while(fx[i].poll(ack)){
                const unsigned id=++received[i];
                check(ack.id==id&&id<=sent[i],"lost/duplicate/reordered ACK");
                const auto expected=packet(id,i);
                const bool valid=id%17!=0;
                check(ack.applied==valid,"invalid command ACK mismatch");
                if(valid)accepted[i]=expected;else ++rejected;
                check(equal(ack.state.settings,accepted[i]),"torn or cross-instance settings");
                check(ack.state.latency==256&&ack.state.effectiveMode==accepted[i].mode,"ACK state mismatch");
            }
            Control::Snapshot meter;
            while(fx[i].pollTelemetry(meter)){
                ++telemetry;
                check(std::isfinite(meter.settings.cleanDb)&&meter.latency==256,"torn telemetry");
            }
            if(sent[i]<count){
                const unsigned id=sent[i]+1;
                if(fx[i].submit({id,Control::Kind::settings,packet(id,i)}))++sent[i];
                else ++backpressure;
            }
        }
        std::this_thread::yield();
    }
    done.store(true,std::memory_order_release);audio.join();
    check(rtprobe::empty(allocations),"allocation in concurrent callback");
    check(rejected==instances*(count/17),"rejected count");
    for(unsigned i=0;i<instances;++i)check(equal(fx[i].snapshot().settings,accepted[i]),"final accepted state");
    std::cout<<"CONCURRENT,instances="<<instances<<",commands="<<count*instances<<",negative_ACK="<<rejected
             <<",backpressure_retries="<<backpressure<<",audio_batches="<<callbacks<<",telemetry="<<telemetry
             <<",observed_alloc_free=0\n";
}
void multi(){
    for(unsigned rate:{48000u,96000u}){
        constexpr int block=128;
        const unsigned total=12*rate/block;
        std::array<Control,instances> fx,reference;
        std::array<std::array<float,block>,instances> l{},r{},rl{},rr{};
        std::array<std::mt19937,instances> gen{std::mt19937(1),std::mt19937(2),std::mt19937(3),std::mt19937(4)};
        std::uniform_real_distribution<float> noise(-.003f,.003f);
        std::array<Core::Settings,instances> settings{};
        for(unsigned i=0;i<instances;++i){
            settings[i].mode=Core::Mode::Voice;settings[i].silenceDb=0;
            for(auto* x:{&fx[i],&reference[i]}){
                check(x->prepare(rate,2),"multi prepare");
                check(x->submit({1,Control::Kind::settings,settings[i]}),"multi settings");
                x->consume();Control::Completion a;check(x->poll(a)&&a.applied,"multi settings ACK");
                check(x->submit({2,Control::Kind::learn,{}}),"multi Learn");
            }
        }
        std::vector<double> timing;timing.reserve(total);
        rtprobe::Counts allocations{};
        unsigned misses=0;bool learningComplete=false;
        const double budget=1e6*block/rate;
        for(unsigned b=0;b<total;++b){
            const unsigned pos=b*block;
            if(!learningComplete&&pos>=3*rate){
                for(auto& x:fx)check(x.snapshot().profile&&!x.snapshot().learning,"multi learned");
                learningComplete=true;
            }
            // Exercise one bypass and one Singing transition while other
            // instances continue Voice drift. Exact serial reference per stream.
            const bool transition=pos>=9*rate&&pos<9*rate+block;
            for(unsigned i=0;i<instances;++i){
                if(transition&&i<2){
                    if(i==0)settings[i].enabled=false;else settings[i].mode=Core::Mode::Singing;
                    for(auto* x:{&fx[i],&reference[i]})check(x->submit({3,Control::Kind::settings,settings[i]}),"multi transition");
                }
                for(int j=0;j<block;++j){
                    const double t=double(pos+unsigned(j))/rate;
                    const float scale=pos>=3*rate?1.41421356f:1.f;
                    l[i][j]=scale*noise(gen[i]);r[i][j]=scale*noise(gen[i]);
                    if(pos>=8*rate){
                        l[i][j]+=float(.02*std::sin(t*2*std::numbers::pi*(375+125*i)));
                        r[i][j]+=float(.01*std::sin(t*2*std::numbers::pi*(750+125*i)));
                    }
                }
                rl[i]=l[i];rr[i]=r[i];
            }
            rtprobe::begin();const auto start=Clock::now();
            for(unsigned i=0;i<instances;++i)fx[i].process(l[i].data(),r[i].data(),block);
            const auto stop=Clock::now();allocations+=rtprobe::end();
            const double us=std::chrono::duration<double,std::micro>(stop-start).count();
            if(pos>=3*rate){timing.push_back(us);if(us>budget)++misses;}
            for(unsigned i=0;i<instances;++i){
                reference[i].process(rl[i].data(),rr[i].data(),block);
                check(l[i]==rl[i]&&r[i]==rr[i],"instance PCM isolation");
                for(float v:l[i])check(std::isfinite(v),"multi finite PCM");
                for(auto* x:{&fx[i],&reference[i]}){
                    Control::Snapshot m;while(x->pollTelemetry(m)){}
                    Control::Completion a;
                    if(b==0)check(x->poll(a)&&a.id==2&&a.applied,"multi learn ACK");
                    if(transition&&i<2)check(x->poll(a)&&a.id==3&&a.applied,"multi transition ACK");
                }
            }
        }
        check(learningComplete&&rtprobe::empty(allocations),"multi callback allocation");
        std::sort(timing.begin(),timing.end());
        std::cout<<"MULTI,"<<rate<<",instances="<<instances<<",block="<<block<<",active_batches="<<timing.size()
                 <<",p99_us="<<timing[std::size_t((timing.size()-1)*.99)]<<",max_us="<<timing.back()
                 <<",budget_us="<<budget<<",misses="<<misses<<",PCM_exact=PASS,observed_alloc_free=0\n";
    }
}
}
int main(){concurrency();multi();return 0;}
