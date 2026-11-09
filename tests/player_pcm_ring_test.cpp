// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/player/PreparedStereoPcmRing.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <new>
#include <thread>
#include <atomic>
static std::size_t allocations=0;
void* operator new(std::size_t n) {
    ++allocations;
    if(auto* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
int main() {
    armix::player::PreparedStereoPcmRing ring;
    assert(!ring.prepare(1));
    assert(ring.prepare(16));
    assert(ring.capacityFrames()==16);
    std::array<float,32> left{},right{};
    for(int i=0;i<32;++i) {
        left[i]=static_cast<float>(i+1);
        right[i]=static_cast<float>(-(i+1));
    }
    assert(ring.write(left.data(),right.data(),12)==12);
    std::array<float,24> outL{},outR{};
    const auto callbackAlloc=allocations;
    assert(ring.read(outL.data(),outR.data(),8)==8);
    assert(allocations==callbackAlloc);
    for(int i=0;i<8;++i) {
        assert(outL[i]==i+1 && outR[i]==-(i+1));
    }
    // FIFO wraps without overwriting unread PCM or swapping L/R.
    assert(ring.write(left.data()+12,right.data()+12,14)==12);
    assert(ring.bufferedFrames()==16);
    assert(ring.write(left.data()+26,right.data()+26,1)==0);
    const auto before=allocations;
    assert(ring.read(outL.data(),outR.data(),16)==16);
    assert(allocations==before);
    for(int i=0;i<16;++i) {
        assert(outL[i]==i+9 && outR[i]==-(i+9));
    }
    assert(ring.bufferedFrames()==0);
    // Decoder/worker starvation cannot hold the ASIO callback.
    const auto beforeUnderrun=allocations;
    assert(ring.read(outL.data(),outR.data(),24)==0);
    assert(allocations==beforeUnderrun);
    assert(ring.underrunFrames()==24);
    for(int i=0;i<24;++i)assert(outL[i]==0.f && outR[i]==0.f);
    // Partial source buffer outputs zero-padded tail, not stale old frames.
    assert(ring.write(left.data(),right.data(),3)==3);
    assert(ring.read(outL.data(),outR.data(),8)==3);
    assert(ring.underrunFrames()==29);
    for(int i=0;i<3;++i)assert(outL[i]==i+1 && outR[i]==-(i+1));
    for(int i=3;i<8;++i)assert(outL[i]==0.f && outR[i]==0.f);
    // Deterministic threaded SPSC integrity: callback stays allocation free
    // even when the decoding producer runs concurrently at a different rate.
    armix::player::PreparedStereoPcmRing concurrent;
    assert(concurrent.prepare(256));
    constexpr int frames=8192;
    std::atomic<bool> done{false};
    std::atomic<int> produced{0};
    std::thread worker([&] {
        std::array<float,32> l{},r{};
        int offset=0;
        while(offset<frames) {
            for(int i=0;i<32;++i) {
                l[i]=static_cast<float>(offset+i+1);
                r[i]=-l[i];
            }
            const auto n=concurrent.write(l.data(),r.data(),32);
            if(n!=32) {std::this_thread::yield();continue;}
            offset+=32;produced.store(offset,std::memory_order_relaxed);
        }
        done.store(true,std::memory_order_release);
    });
    std::array<float,32> l{},r{};
    int consumed=0;
    while(consumed<frames) {
        // Only call read when a FULL prepared block exists, otherwise a
        // genuine underrun should zero-fill without skipping future frames.
        if(concurrent.bufferedFrames()<32) {
            std::this_thread::yield();continue;
        }
        assert(concurrent.read(l.data(),r.data(),32)==32);
        for(int i=0;i<32;++i) {
            assert(l[i]==static_cast<float>(consumed+i+1));
            assert(r[i]==-l[i]);
        }
        consumed+=32;
    }
    worker.join();
    assert(done.load(std::memory_order_acquire));
    assert(produced.load(std::memory_order_relaxed)==frames);
    assert(concurrent.underrunFrames()==0);
}
