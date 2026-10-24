// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/RealtimeControl.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>
using namespace armix::rt;
int main(){
    SpscQueue<int,8> q;
    int value=-1;
    assert(!q.tryPop(value));
    for(int i=0;i<7;i++) assert(q.tryPush(i));
    assert(!q.tryPush(999));
    for(int i=0;i<7;i++){assert(q.tryPop(value));assert(value==i);}
    assert(!q.tryPop(value));
    for(int i=0;i<120000;i++){assert(q.tryPush(i));assert(q.tryPop(value));assert(value==i);}

    ParameterBank<32> params;
    float read=0;std::uint32_t revision=0;
    assert(!params.readChanged(5,revision,read));
    params.publish(5,0.1f);params.publish(5,0.3f);params.publish(5,0.7f);
    assert(params.readChanged(5,revision,read)&&std::fabs(read-0.7f)<0.00001f);
    assert(!params.readChanged(5,revision,read));
    // Concurrent ordered SPSC sequence verifies visibility across 2 native threads.
    SpscQueue<std::uint32_t,1024> concurrent;
    constexpr std::uint32_t count=250000;
    std::atomic<bool> bad{false};
    std::thread producer([&]{for(std::uint32_t i=0;i<count;){if(concurrent.tryPush(i))++i;else std::this_thread::yield();}});
    std::thread consumer([&]{for(std::uint32_t i=0;i<count;){std::uint32_t v=0;if(concurrent.tryPop(v)){if(v!=i)bad=true;++i;}else std::this_thread::yield();}});
    producer.join();consumer.join();assert(!bad);
    std::cout<<"PASS: SPSC capacity, wrap, 250000 ordered interthread events, parameter coalescing\n";
}
