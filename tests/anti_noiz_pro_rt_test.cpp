// SPDX-License-Identifier: AGPL-3.0-only
// Native-session callback qualification. No production instrumentation.
#include "../engine/builtin_fx/anti_noiz/AntiNoizNativeSession.h"
#include "../tools/anti-noiz/RtAllocationProbe.h"
#include <chrono>
#include <bit>
#include <string_view>
#include <limits>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>
#include <ctime>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
using namespace armix::builtin_fx::anti_noiz;
namespace {
using Clock=std::chrono::steady_clock;
bool headroomPassed=true;
constexpr int maxBlock=256;
constexpr std::array<int,4> sizes{32,64,128,256};
void require(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
struct Denormals {
#if defined(__SSE__)
    unsigned previous=_mm_getcsr();
    Denormals(){_mm_setcsr(previous|0x8040u);}
    ~Denormals(){_mm_setcsr(previous);}
#endif
};
#ifdef __linux__
double cpuUs(){timespec t{};require(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0,"thread CPU clock");return double(t.tv_sec)*1e6+double(t.tv_nsec)*.001;}
#else
double cpuUs(){return 0;}
#endif
struct Stats {
    std::vector<double> wall,cpu;
    unsigned misses=0;double budget=0;
    void add(double w,double c,double b){wall.push_back(w);cpu.push_back(c);budget=b;if(w>b)++misses;}
    static double p99(std::vector<double>& v){std::sort(v.begin(),v.end());return v[(99*v.size()+99)/100-1];}
    void report(int rate,int channels,int count,int block,const char* phase){
        if(wall.empty())return;
        const double w=p99(wall),c=p99(cpu);
        std::cout<<"PRO_RT,"<<rate<<','<<channels<<','<<count<<','<<block<<','<<phase<<','<<wall.size()<<','<<budget<<','<<w<<','<<wall.back()<<','<<c<<','<<100*c/budget<<','<<misses<<'\n';
#if defined(ANTI_NOIZ_ALLOC_PROBE) && defined(__linux__)
        if(c>=budget*.5)headroomPassed=false;
#endif
    }
};
struct Lane {
    NativeSession live,blocked;
    std::array<float,maxBlock> l{},r{},refL{},refR{};
    std::mt19937 rng;
    Core::Settings s;
    std::uint64_t id=0,hash=14695981039346656037ull;
    NativeState::Bytes before{};
    explicit Lane(unsigned seed):rng(seed){}
    void submit(Control::Kind k){before=NativeState::encode(live.acceptedSettings());++id;for(auto* x:{&live,&blocked})require(x->submit({id,k,s}),"submit");}
    void ack(bool applied=true){Control::Completion a,b;require(live.poll(a)&&blocked.poll(b)&&a.id==id&&b.id==id&&a.applied==applied&&b.applied==applied,"ACK identity");require(NativeState::encode(a.state.settings)==NativeState::encode(b.state.settings),"ACK parity");require(NativeState::encode(live.acceptedSettings())==NativeState::encode(blocked.acceptedSettings()),"durable parity");if(!applied)require(NativeState::encode(live.acceptedSettings())==before,"rejected edit changed settings");}
};
void qualify(int rate,int channels,int count){
    [[maybe_unused]] Denormals denormals;
    std::vector<std::unique_ptr<Lane>> lanes;
    for(int i=0;i<count;++i){
        auto f=std::make_unique<Lane>(420+i);f->s.mode=Core::Mode::Voice;f->s.spectralManual=true;f->s.manual.enabled=true;f->s.manual.smartVoice=true;f->s.silenceDb=0;f->s.cleanDb=18;f->s.manual.thresholdDb=-48;f->s.manual.rangeDb=60;
        for(auto* x:{&f->live,&f->blocked}){require(x->prepare(rate,channels),"prepare");require(x->restore(NativeState::encode(f->s)),"restore");x->seal();require(x->latency()==(rate>48000?512:256),"PDC");}
        lanes.push_back(std::move(f));
    }
    rtprobe::Counts allocation{};unsigned callbacks=0,commands=0;std::uint64_t pos=0;
    std::uniform_real_distribution<float> noise(-.003f,.003f);
    auto callback=[&](int n,Stats& stat,int signal,bool bypass){
        for(auto& f:lanes){for(int i=0;i<n;++i){const double t=double(pos+i)/rate;const float voice=signal==1&&((pos+i)/(rate/5))%2==0?.07f*float(std::sin(2*std::numbers::pi*t*420)):0.f;f->l[i]=signal==2?0.f:noise(f->rng)+voice;f->r[i]=signal==2?0.f:noise(f->rng)-voice*.7f;}f->refL=f->l;f->refR=f->r;}
        rtprobe::begin();const double cpuStart=cpuUs();const auto start=Clock::now();
        for(auto& f:lanes){f->live.onBypass(bypass,bypass);if(bypass)f->live.consume();f->live.process(f->l.data(),channels==2?f->r.data():nullptr,n);}
        const auto end=Clock::now();const double cpuEnd=cpuUs();allocation+=rtprobe::end();
        stat.add(std::chrono::duration<double,std::micro>(end-start).count(),cpuEnd-cpuStart,1e6*n/rate);
        for(auto& f:lanes){f->blocked.onBypass(bypass,bypass);if(bypass)f->blocked.consume();f->blocked.process(f->refL.data(),channels==2?f->refR.data():nullptr,n);require(f->l==f->refL&&f->r==f->refR,"saturated telemetry PCM parity");for(int i=0;i<n;++i)require(std::isfinite(f->l[i])&&std::isfinite(f->r[i])&&std::abs(f->l[i])<=128&&std::abs(f->r[i])<=128,"finite bounded PCM");for(int i=0;i<n;++i){f->hash^=std::bit_cast<std::uint32_t>(f->l[i]);f->hash*=1099511628211ull;if(channels==2){f->hash^=std::bit_cast<std::uint32_t>(f->r[i]);f->hash*=1099511628211ull;}}NativeSession::Snapshot m;while(f->live.pollTelemetry(m)){};}
        pos+=n;++callbacks;
    };
    auto submit=[&](Control::Kind k){for(auto& f:lanes)f->submit(k);commands+=count;};
    auto ack=[&](bool ok=true){for(auto& f:lanes)f->ack(ok);};
    auto phase=[&](const char* name,int blocks,int signal,bool bypass,bool firstAck=false){std::array<Stats,4> stats;for(int b=0;b<blocks;++b){const int i=b%4;callback(sizes[i],stats[i],signal,bypass);if(b==0&&firstAck)ack();}for(int i=0;i<4;++i)stats[i].report(rate,channels,count,sizes[i],name);};
    submit(Control::Kind::learn);
    phase("learn",4*((3*rate+479)/480),0,false,true);
    for(auto& f:lanes)require(f->live.observed().profile&&f->live.observed().sidechainReference,"learned references");
    phase("spectral_smart",1024,1,false);
    submit(Control::Kind::audition);phase("audition",1024,1,false,true);
    for(auto& f:lanes){f->s.cleanDb=0;f->s.manual.gate=true;f->s.manual.rangeDb=96;f->s.manual.smartVoice=false;}
    submit(Control::Kind::settings);phase("gate",1024,1,false,true);
    phase("silence_tail",1024,2,false);
    for(auto& f:lanes)require(f->live.observed().residualGain==0,"digital mute after silence");
    phase("host_bypass",1024,1,true);
    std::array<Stats,4> stats;
    for(int b=0;b<1024;++b){for(auto& f:lanes){f->s.cleanDb=float(b%41);f->s.manual.gate=b%2==0;f->s.manual.smartVoice=b%3==0;f->s.manual.thresholdDb=float(-80+b%81);f->s.manual.ratio=float(1+b%20);f->s.manual.hpHz=float(20+b%481);f->s.manual.smartMarginDb=float(3+b%16);f->s.artifactControl=float(b%11)/10;f->s.vocalProtection=float((b+5)%11)/10;if(b%17==0)f->s.manual.ratio=0;}
        submit(Control::Kind::settings);const int i=b%4;callback(sizes[i],stats[i],1,false);ack(b%17!=0);}
    for(int i=0;i<4;++i)stats[i].report(rate,channels,count,sizes[i],"edits");
    require(rtprobe::empty(allocation),"callback allocation/free");
    std::cout<<"PRO_RT_PCM,"<<rate<<','<<channels<<','<<count;for(auto& f:lanes)std::cout<<','<<f->hash;std::cout<<'\n';
    std::cout<<"PRO_RT_PASS,"<<rate<<','<<channels<<','<<count<<",callbacks="<<callbacks<<",commands="<<commands<<",PCM_exact=1,allocation_probe="
#ifdef ANTI_NOIZ_ALLOC_PROBE
    <<1
#else
    <<0
#endif
    <<",alloc_free=0\n";
}
}
int main(int argc,char** argv){
    const bool enforce=argc==2&&std::string_view(argv[1])=="--enforce-headroom";
    if(argc!=1&&!enforce)return 64;
#ifdef ANTI_NOIZ_ALLOC_PROBE
    void* (*volatile alloc)(std::size_t)=std::malloc;void (*volatile release)(void*)=std::free;
    void* (*volatile cpp)(std::size_t)=::operator new;void (*volatile cppRelease)(void*) noexcept=::operator delete;
    rtprobe::begin();void* a=alloc(64);release(a);void* b=cpp(64);cppRelease(b);const auto probe=rtprobe::end();
    require(probe.cAlloc==1&&probe.cFree==1&&probe.cppAlloc==1&&probe.cppFree==1,"allocation probe self-test");
#endif
    std::cout<<"PRO_RT_SCHEMA,rate,channels,instances,block,phase,callbacks,budget_us,wall_p99_us,wall_max_us,cpu_p99_us,cpu_p99_percent,wall_misses\n";
    qualify(48000,2,1);
    for(int rate:{44100,48000,88200,96000})qualify(rate,2,4);
    qualify(48000,1,4);
    std::cout<<"PRO_RT_HEADROOM,"<<headroomPassed<<'\n';
    return !enforce||headroomPassed?0:2;
}
