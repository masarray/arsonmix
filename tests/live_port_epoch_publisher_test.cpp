// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/RealtimeRoutePublisher.h"
#include "../engine/device/LiveInputPortRegistry.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
using namespace armix;
struct SpyGraph {
    static inline std::atomic<int> alive{0};
    int value=0;
    explicit SpyGraph(int v) noexcept :value(v) {alive.fetch_add(1);}
    ~SpyGraph() noexcept {alive.fetch_sub(1);}
};
using Publisher=rt::RealtimeRoutePublisher<SpyGraph,3>;
static std::unique_ptr<Publisher::Snapshot> makeGraph(std::uint64_t epoch,std::uint64_t revision,int tag) {
    auto x=std::make_unique<Publisher::Snapshot>();
    x->deviceEpoch=epoch;x->graphRevision=revision;
    x->prepared=std::make_unique<SpyGraph>(tag);
    return x;
}
int main() {
    device::LiveInputPortRegistry ports;device::LiveInputPortRegistry::Fault fault;
    constexpr std::array order{5,2,9};
    assert(device::LiveInputPortRegistry::prepare(7,order,ports,fault));
    assert(ports.epoch==7&&ports.enabledCount==3);
    assert(ports.callbackIndex(5)==0&&ports.callbackIndex(2)==1);
    assert(ports.callbackIndex(9)==2&&ports.callbackIndex(0)==-1);
    assert(ports.physicalIndex(1)==2&&ports.physicalIndex(3)==-1);
    assert(ports.matches(7,3)&&!ports.matches(7,4)&&!ports.matches(8,3));
    const auto old=ports;
    constexpr std::array duplicates{5,2,5};
    assert(!device::LiveInputPortRegistry::prepare(7,duplicates,ports,fault));
    assert(fault==device::LiveInputPortRegistry::Fault::duplicatePort);
    assert(ports.epoch==old.epoch&&ports.callbackIndex(9)==2);
    constexpr std::array invalid{-1,0};
    assert(!device::LiveInputPortRegistry::prepare(7,invalid,ports,fault));
    assert(fault==device::LiveInputPortRegistry::Fault::invalidPort);
    assert(!device::LiveInputPortRegistry::prepare(0,order,ports,fault));
    assert(fault==device::LiveInputPortRegistry::Fault::invalidEpoch);

    Publisher p;
    assert(p.publish(makeGraph(1,1,101),0,2)==Publisher::Result::staleDevice);
    assert(p.lastRevision()==0);
    assert(p.publish(makeGraph(1,1,101),0,1)==Publisher::Result::applied);
    assert(p.lastRevision()==1);
    {
        auto lease=p.acquire();
        assert(lease.get(1) && lease.get(1)->prepared->value==101);
        assert(!lease.get(2)); // epoch mismatch never exposes stale audio
        assert(p.publish(makeGraph(1,2,102),0,1)==Publisher::Result::staleRevision);
        assert(p.publish(makeGraph(1,2,102),1,1)==Publisher::Result::applied);
        assert(lease.get(1) && lease.get(1)->prepared->value==101);
        assert(p.pendingRetirements()==1);
        assert(!p.pollRetirement());
        assert(SpyGraph::alive==2);
        assert(p.publish(makeGraph(1,3,103),2,1)==Publisher::Result::applied);
        assert(p.publish(makeGraph(1,4,104),3,1)==Publisher::Result::applied);
        assert(p.pendingRetirements()==3);
        assert(p.publish(makeGraph(1,5,105),4,1)==Publisher::Result::retirementFull);
        {
            auto newer=p.acquire();
            assert(newer.get(1)&&newer.get(1)->prepared->value==104);
        }
        // Emergency unpublish CANNOT wait for the first active lease; but
        // subsequent callbacks MUST see no valid graph immediately.
        p.unpublish();
        assert(p.lastRevision()==0);
        auto silenced=p.acquire();
        assert(!silenced.get(1));
    }
    assert(p.pollRetirement());
    // The last unpublished owner was held until the callback could retire.
    assert(SpyGraph::alive==1);
    assert(p.publish(makeGraph(8,1,201),0,8)==Publisher::Result::applied);
    { auto v=p.acquire();assert(v.get(8)->prepared->value==201); }
    p.unpublish();
    assert(p.pollRetirement());
    assert(SpyGraph::alive==0);

    // A burst of consecutive reconfigurations does not leak retired plans
    // or allocate anything in the read lease (the callback API).
    for(int i=0;i<8000;++i) {
        auto next=makeGraph(9,static_cast<std::uint64_t>(i+1),i);
        assert(p.publish(std::move(next),static_cast<std::uint64_t>(i),9)==Publisher::Result::applied);
        auto lease=p.acquire();
        assert(lease.get(9)&&lease.get(9)->prepared->value==i);
    }
    p.unpublish();
    assert(p.pollRetirement());
    assert(SpyGraph::alive==0);

    // Concurrent RT reader repeatedly observes either a whole published
    // snapshot or nullptr; no old graph can be destroyed while leased.
    std::atomic<bool> running{true};std::atomic<int> observed{0};
    std::thread audio([&]{
        while(running.load(std::memory_order_relaxed)){
            auto lease=p.acquire();
            if(const auto* s=lease.get(10)){
                assert(s->deviceEpoch==10);
                assert(s->prepared->value>=0);
                observed.fetch_add(1,std::memory_order_relaxed);
            }
        }
    });
    std::uint64_t ack=0;
    for(int i=0;i<1000;++i) {
        auto state=makeGraph(10,ack+1,i);
        const auto ret=p.publish(std::move(state),ack,10);
        if(ret==Publisher::Result::applied) ++ack;
        else assert(ret==Publisher::Result::retirementFull);
        p.pollRetirement();
    }
    running.store(false,std::memory_order_release);
    audio.join();
    p.unpublish();
    assert(p.pollRetirement());
    assert(SpyGraph::alive==0);
}
