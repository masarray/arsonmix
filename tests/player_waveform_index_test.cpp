// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/player/PlayerWaveformIndex.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <thread>
#include <atomic>
#include <cmath>
#include <cstdio>
using W=armix::player::PlayerWaveformIndex;
int main() {
    W index;
    std::array<float,1024> left{},right{};
    for(int i=0;i<1024;++i){
        left[static_cast<std::size_t>(i)]=
            i%4==0?.85f:.12f;
        right[static_cast<std::size_t>(i)]=-.35f;
    }
    index.observe(left.data(),right.data(),1024,0);
    for(auto v:index.snapshot())assert(v==0); // no source yet
    index.reset(4096);
    std::atomic<bool> done{false};
    std::thread worker([&]{
        for(int offset=0;offset<4096;offset+=1024)
            index.observe(left.data(),right.data(),1024,
                          static_cast<std::uint64_t>(offset));
        done.store(true,std::memory_order_release);
    });
    do {
        const auto bins=index.snapshot();
        for(auto value:bins)assert(value<=65535);
    }while(!done.load(std::memory_order_acquire));
    worker.join();
    const auto bins=index.snapshot();
    const auto expected=static_cast<unsigned>(.85f*65535);
    int hits=0;
    for(const auto value:bins){
        if(value>expected-10&&value<expected+10)++hits;
    }
    assert(hits>220); // every part of the entire song was indexed
    index.reset(48000);
    for(const auto value:index.snapshot())assert(value==0);
    left[0]=NAN;right[0]=NAN;
    index.observe(left.data(),right.data(),1,0);
    assert(index.snapshot()[0]==0); // corrupt media never emits NaN
    std::puts("M2: bounded progressive waveform, finite peaks, reset and thread-safe snapshots PASS");
}
