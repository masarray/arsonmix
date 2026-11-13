// SPDX-License-Identifier: AGPL-3.0-only
// Synthetic audio-thread allocation/performance fixture; never ASIO FAT proof.
#include "../engine/lms/PreparedSpeakerSystem.h"
#include <array>
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG // Always execute PCM assertions, even in release CTest.
#endif
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
static std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t n){
    ++allocations;
    if(void* p=std::malloc(n))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){
    ++allocations;
    if(void* p=std::malloc(n))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
int main(){
    using namespace armix::lms;
    SpeakerSystemRequest r;
    r.deviceEpoch=1;r.graphRevision=1;r.sampleRate=48000.;
    r.maxFrames=256;r.activeOutputChannels=16;r.ways=4;r.stereo=true;
    r.crossoverHz={80.,300.,2500.};
    for(int i=0;i<8;++i){
        const auto pin="DRIVER#A/"+std::to_string(i);
        r.verifiedPorts.push_back({pin,i});
        OutputPatch o;
        o.outputId="speaker-"+std::to_string(i);o.portIdentity=pin;
        o.sourceSide=i/4;o.section=i%4;o.delayMs=i*2.;
        o.mute=false;o.limiterEnabled=true;
        o.confirmedSpeakerProfile=true;o.ceilingDbfs=-3.;
        o.peq={{120.,1.1,2.},{800.,.75,-3.},{3500.,1.2,1.5}};
        r.outputs.push_back(o);
    }
    std::unique_ptr<PreparedSpeakerSystem> graph;
    assert(PreparedSpeakerSystem::prepare(r,graph).ok());
    std::array<float,256> sourceL{},sourceR{};
    std::vector<std::vector<float>> output(16,std::vector<float>(256));
    std::array<float*,16> pointers{};
    for(int i=0;i<16;++i)pointers[i]=output[i].data();
    for(int i=0;i<256;++i){
        sourceL[i]=std::sin(static_cast<float>(i)*.05f)*.3f;
        sourceR[i]=std::cos(static_cast<float>(i)*.07f)*.3f;
    }
    assert(graph->process(1,1,sourceL.data(),sourceR.data(),pointers.data(),16,256,true));
    const auto before=allocations.load();
    const auto started=std::chrono::steady_clock::now();
    for(int block=0;block<1500;++block)
        assert(graph->process(1,1,sourceL.data(),sourceR.data(),pointers.data(),16,256,true));
    const auto micros=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-started).count();
    assert(allocations.load()==before);
    for(int c=0;c<8;++c)for(float sample:output[c])
        assert(std::isfinite(sample)&&std::abs(sample)<1.001f);
    std::cout<<"PASS LMS: 8 output, stereo 4-way, 1500 x 256 PCM frames, "
             <<"no realtime heap, synthetic duration "<<micros<<" us\n";
}
