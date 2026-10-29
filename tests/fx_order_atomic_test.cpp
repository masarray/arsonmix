// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/FxOrderPlan.h"
#include "../engine/core/FxMoveRamp.h"
#include "../engine/core/FxStructuralEditPlan.h"
#include <memory>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <vector>

// These checks must execute in MSVC Release builds with NDEBUG.
static void check(bool ok) { if (!ok) { std::fputs("FX_ORDER_ASSERT_FAIL\n",stderr); std::abort(); } }
int main()
{
    using Plan = armix::dsp::FxOrderPlan;
    static_assert (Plan::canonical == 0x76543210ULL);
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                   "Realtime order publication requires lock-free x64 atomic.");
    // Both halves meet at exact digital zero across the graph-order seam.
    using Ramp = armix::dsp::FxMoveRamp;
    for (int frames : {32, 64, 128, 480, 1024})
    {
        check(Ramp::gain(1,0,frames)==1.f);
        check(Ramp::gain(1,frames-1,frames)==0.f);
        check(Ramp::gain(2,0,frames)==0.f);
        check(Ramp::gain(2,frames-1,frames)==1.f);
        check(Ramp::gain(0,17,frames)==1.f);
        for(int i=1;i<frames;i++)
        {
            check(Ramp::gain(1,i,frames)<=Ramp::gain(1,i-1,frames));
            check(Ramp::gain(2,i,frames)>=Ramp::gain(2,i-1,frames));
        }
    }
    // Entire insert identities survive every arbitrary move. No duplicates.
    for (int count=2; count<=8; ++count)
        for (int from=0; from<count; ++from)
            for (int to=0; to<count; ++to)
            {
                auto next=Plan::moved(Plan::canonical,count,from,to);
                std::array<bool,8> found {};
                for (int i=0; i<count; ++i)
                {
                    const int physical=Plan::physicalAt(next,i);
                    check(physical>=0 && physical<count && !found[physical]);
                    found[physical]=true;
                }
                check(Plan::physicalAt(next,to)==from);
                check(Plan::moved(next,count,to,from)==Plan::canonical);
            }
    // B2: all structural edits preserve the *logical* vendor instance order,
    // including MOVE followed by REMOVE, and APPEND without old instance copies.
    using Structural=armix::dsp::FxStructuralEditPlan;
    for(int count=0;count<=8;++count)
    {
        for(int from=0;from<count;++from)
        for(int to=0;to<count;++to)
        {
            const auto order=Plan::moved(Plan::canonical,count,from,to);
            for(int removed=-1;removed<count;++removed)
            for(int append=0;append<=1;++append)
            {
                const auto plan=Structural::make(order,count,removed,append);
                const int next=count-(removed>=0?1:0)+append;
                if(next>8){check(!plan.valid);continue;}
                check(plan.valid&&plan.count==next);
                int step=0;
                std::array<bool,8> used{};
                for(int i=0;i<count;++i)
                {
                    const int p=Plan::physicalAt(order,i);
                    if(i==removed)continue;
                    check(plan.sourcePhysical[step++]==p);
                    check(!used[p]);used[p]=true;
                }
                if(append)check(plan.sourcePhysical[step++]==-1);
                check(step==next);
            }
        }
        // No move still must support an empty rack and a full rack.
        const auto all=Structural::make(Plan::canonical,count,-1,count<8?1:0);
        check(all.valid&&all.count==count+(count<8?1:0));
    }
    check(!Structural::make(Plan::canonical,8,-1,1).valid);
    check(!Structural::make(Plan::canonical,1,1,0).valid);
    check(!Structural::make(Plan::canonical,-1,-1,0).valid);
    check(!Structural::make(Plan::canonical,3,-2,0).valid);
    check(!Structural::make(Plan::canonical,2,-1,2).valid);
    // Shared vendor ownership: old graph retirement cannot destroy a reused FX.
    int retired=0;
    auto vendor=std::shared_ptr<int>(new int(7),[&](int* p){++retired;delete p;});
    auto oldGraph=vendor;
    auto newGraph=vendor;
    vendor.reset();
    oldGraph.reset();
    check(retired==0 && *newGraph==7);
    newGraph.reset();
    check(retired==1);

    // A callback never observes a partially written permutation.
    std::atomic<std::uint64_t> order {Plan::canonical};
    std::atomic<bool> running {true};
    std::thread reader([&]{
        while (running.load(std::memory_order_acquire))
        {
            const auto snapshot=order.load(std::memory_order_acquire);
            std::array<bool,8> seen {};
            for(int i=0;i<8;++i)
            {
                const int p=Plan::physicalAt(snapshot,i);
                check(p>=0 && p<8 && !seen[p]);
                seen[p]=true;
            }
        }
    });
    for(int i=0;i<20000;i++){
        const auto old=order.load(std::memory_order_acquire);
        order.store(Plan::moved(old,8,i%8,(i*7+3)%8),std::memory_order_release);
    }
    running.store(false,std::memory_order_release);
    reader.join();
}
