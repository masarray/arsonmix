// SPDX-License-Identifier: AGPL-3.0-only
// Offline executable: no measurement hooks or allocator overrides in DSP.
#ifdef ANTI_NOIZ_MATCHED_BRIDGE
#include "RtQualificationBridge.h"
#else
#include "AntiNoizControl.h"
#endif
#include "RtAllocationProbe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <bit>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <vector>
#ifdef ANTI_NOIZ_MATCHED_BRIDGE
using namespace armix::anti_noiz_qualification;
#else
using namespace armix::builtin_fx::anti_noiz;
#endif
namespace {
using Clock=std::chrono::steady_clock;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void probeSelfTest(){
#ifdef ANTI_NOIZ_ALLOC_PROBE
    // Volatile function pointers prevent dead allocation elimination.
    void* (*volatile alloc)(std::size_t)=std::malloc;
    void (*volatile release)(void*)=std::free;
    void* (*volatile cpp)(std::size_t)=::operator new;
    void (*volatile cppRelease)(void*) noexcept=::operator delete;
    rtprobe::begin();
    void* a=alloc(64);release(a);
    void* b=cpp(64);cppRelease(b);
    void* c=::operator new(128,std::align_val_t(64));::operator delete(c,std::align_val_t(64));
    const auto result=rtprobe::end();
    require(result.cAlloc==1&&result.cFree==1&&result.cppAlloc==2&&result.cppFree==2,"allocation probe self-test failed");
#endif
}
struct Fixture {
    Control fx;
    Control::Completion ack{};
    Control::Snapshot meter{};
    Core::Settings settings{};
    std::array<float,1024> l{},r{};
    std::mt19937 gen{19231};
    std::uniform_real_distribution<float> noise{-.003f,.003f};
    std::array<float,2> prev{};
    rtprobe::Counts allocations{};
    std::uint64_t hash=14695981039346656037ull,callbacks=0;
    std::uint64_t nextId=1;
    std::FILE* capture=nullptr;
    std::array<float,4096> captureFrames{};
    void prepare(unsigned rate,Core::Mode mode){
        require(fx.prepare(rate,2),"prepare");
        settings.mode=mode;settings.silenceDb=0;
        submit(Control::Kind::settings);
    }
    void submit(Control::Kind kind){require(fx.submit({nextId++,kind,settings}),"command submit");}
    void expect(bool applied=true){
        require(fx.poll(ack)&&ack.id==nextId-1&&ack.applied==applied,"callback command ACK");
    }
    double process(int n){
        if(capture)for(int j=0;j<n;++j){
            captureFrames[4*j]=l[j];captureFrames[4*j+1]=r[j];
        }
        rtprobe::begin();
        const auto start=Clock::now();fx.process(l.data(),r.data(),n);const auto stop=Clock::now();
        allocations+=rtprobe::end();++callbacks;
        if(capture){
            for(int j=0;j<n;++j){captureFrames[4*j+2]=l[j];captureFrames[4*j+3]=r[j];}
            require(std::fwrite(captureFrames.data(),sizeof(float),std::size_t(4*n),capture)==std::size_t(4*n),"PCM capture write");
        }
        while(fx.pollTelemetry(meter)){}
        for(int j=0;j<n;++j)for(float x:{l[j],r[j]}){
            require(std::isfinite(x)&&std::abs(x)<=128.f,"nonfinite or unbounded output");
            hash^=std::bit_cast<std::uint32_t>(x);hash*=1099511628211ull;
        }
        return std::chrono::duration<double,std::micro>(stop-start).count();
    }
    void fillNoise(int n){for(int j=0;j<n;++j){l[j]=noise(gen);r[j]=noise(gen);}}
    void learn(unsigned rate,int block){
        fillNoise(block);process(block);expect();
        submit(Control::Kind::learn);
        for(unsigned pos=0;pos<3*rate;){
            const int n=int(std::min<unsigned>(block,3*rate-pos));fillNoise(n);process(n);
            if(!pos)expect();
            pos+=unsigned(n);
        }
        require(fx.snapshot().profile&&!fx.snapshot().learning,"learn did not complete");
    }
};
void stress(){
    constexpr std::array<int,7> blocks{1,32,127,256,1024,64,511};
    for(unsigned rate:{44100u,48000u,88200u,96000u}){
        Fixture f;f.prepare(rate,Core::Mode::Voice);f.learn(rate,127);
        // 30 seconds simulated per rate. Callback partitions, accepted and
        // rejected control packets, Learn cancellation, bypass and nonfinite
        // input are all exercised; no wall-clock sleep or hardware dependency.
        unsigned pos=0,next=rate*3,phase=0;std::size_t b=0;
        while(pos<30*rate){
            const int n=int(std::min<unsigned>(blocks[b++%blocks.size()],30*rate-pos));
            bool command=false,applied=true;
            if(pos>=next){
                next+=3*rate;++phase;
                switch(phase){
                    case 1:f.settings.mode=Core::Mode::Singing;break;
                    case 2:f.settings.mode=Core::Mode::Music;break;
                    case 3:f.settings.mode=Core::Mode::Voice;f.settings.enabled=false;break;
                    case 4:f.settings.enabled=true;break;
                    case 5:f.settings.cleanDb=std::numeric_limits<float>::quiet_NaN();applied=false;break;
                    case 6:f.settings.cleanDb=14;f.submit(Control::Kind::learn);command=true;break;
                    case 7:f.settings.enabled=false;break;
                    case 8:f.settings.enabled=true;f.settings.mode=Core::Mode::AutoProtect;break;
                    default:f.settings.cleanDb=0;f.settings.silenceDb=0;break;
                }
                if(!command)f.submit(Control::Kind::settings);
                command=true;
            }
            f.fillNoise(n);
            for(int j=0;j<n;++j){
                const double t=double(pos+unsigned(j))/rate;
                f.l[j]+=float(.003*std::sin(t*2*std::numbers::pi*375));
                f.r[j]+=float(.002*std::sin(t*2*std::numbers::pi*750));
            }
            if(b%113==0)f.l[0]=std::numeric_limits<float>::quiet_NaN();
            if(b%127==0)f.r[0]=std::numeric_limits<float>::infinity();
            f.process(n);if(command)f.expect(applied);
            pos+=unsigned(n);
        }
        require(rtprobe::empty(f.allocations),"allocation/free in lifecycle callback");
        require(f.fx.snapshot().latency==(rate>48000?512:256),"latency changed");
        std::cout<<"STRESS,"<<rate<<','<<f.callbacks<<','<<f.allocations.cppAlloc<<','<<f.allocations.cppFree<<','<<f.allocations.cAlloc<<','<<f.allocations.cFree<<','<<f.hash<<'\n';
    }
}
void benchmark(const char* prefix=nullptr){
    for(unsigned rate:{44100u,48000u,88200u,96000u})for(int block:{32,64,128,256,1024})for(int scenario=0;scenario<3;++scenario){
        Fixture f;
        const auto name=scenario==0?"weak-auto":scenario==1?"coherent":"spectral";
        std::unique_ptr<std::FILE,decltype(&std::fclose)> capture(nullptr,&std::fclose);
        if(prefix){
            const std::string path=std::string(prefix)+"-"+std::to_string(rate)+"-"+std::to_string(block)+"-"+name+".f32";
            capture.reset(std::fopen(path.c_str(),"wb"));require(bool(capture),"PCM capture open");
            f.capture=capture.get();
        }
        f.prepare(rate,scenario==0?Core::Mode::AutoProtect:Core::Mode::Voice);f.learn(rate,block);
        std::vector<double> elapsed;elapsed.reserve(10*rate/unsigned(block)+1);
        std::uint64_t misses=0;
        double inputLate=0,outputLate=0;
        for(unsigned pos=0;pos<10*rate;){
            const int n=int(std::min<unsigned>(block,10*rate-pos));
            for(int j=0;j<n;++j)for(int c=0;c<2;++c){
                const float x=f.noise(f.gen);float y=x;
                const unsigned sample=pos+unsigned(j);
                const double t=double(sample)/rate;
                if(scenario==0){
                    const float amp=sample<3*rate?.001f:sample<6*rate?.008f:.04f;
                    y+=amp*float(std::sin(t*2*std::numbers::pi*(c?750:375)));
                }else{
                    y=scenario==1?std::sqrt(2.f)*x:std::sqrt(1.55f)*(x-.28f*f.prev[c]);
                    if(sample>=8*rate)y+=float(.05*std::sin(t*2*std::numbers::pi*(c?750:375)));
                }
                f.prev[c]=x;
                (c?f.r:f.l)[j]=y;
                if(scenario&&sample>=6*rate&&sample<8*rate)inputLate+=double(y)*y;
            }
            const double us=f.process(n);elapsed.push_back(us);
            if(us>1e6*n/rate)++misses;
            for(int j=0;j<n;++j){
                const unsigned sample=pos+unsigned(j);
                if(scenario&&sample>=6*rate&&sample<8*rate)outputLate+=double(f.l[j])*f.l[j]+double(f.r[j])*f.r[j];
            }
            pos+=unsigned(n);
        }
        require(rtprobe::empty(f.allocations),"allocation/free in benchmark callback");
        if(capture){f.capture=nullptr;require(std::fclose(capture.release())==0,"PCM capture close");}
        std::sort(elapsed.begin(),elapsed.end());
        auto q=[&](double p){return elapsed[std::size_t((elapsed.size()-1)*p)];};
        const double attenuation=scenario?10*std::log10(inputLate/outputLate):0.;
        std::cout<<"RT,"<<rate<<','<<block<<','<<name<<','<<elapsed.size()<<','<<q(.5)<<','<<q(.99)<<','<<q(.999)<<','<<elapsed.back()<<','<<1e6*block/rate<<','<<misses<<','<<f.allocations.cppAlloc<<','<<f.allocations.cppFree<<','<<f.allocations.cAlloc<<','<<f.allocations.cFree<<','<<f.hash<<','<<attenuation<<'\n';
    }
}
}
int main(int argc,char** argv){try{
    require(argc==2||(argc==3&&std::string_view(argv[1])=="--benchmark"),"use --stress or --benchmark [capture-prefix]");
    probeSelfTest();
#ifdef ANTI_NOIZ_ALLOC_PROBE
    std::cout<<"ALLOC_PROBE,C++ new/delete and linked C allocator references,positive control PASS\n";
#else
    std::cout<<"ALLOC_PROBE,disabled in sanitizer lane\n";
#endif
    const std::string_view arg=argv[1];
    if(arg=="--stress")stress();else if(arg=="--benchmark")benchmark(argc==3?argv[2]:nullptr);else require(false,"unknown mode");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
