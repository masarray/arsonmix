// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/RealtimeHealthHistogram.h"
#include "../engine/core/FxTimingSampler.h"
#include <cstdlib>
#include <cmath>
static void check(bool ok) { if(!ok)std::abort(); }
int main()
{
    armix::rt::FxTimingSampler timing;timing.seed(12345);
    int last=-1,n=0;std::uint64_t sum=0,frames=0,bypass=0,maxNs=0;
    for(int i=0;i<10000;++i)if(timing.shouldSample()){
        if(last>=0)check(i-last>=24&&i-last<=40);
        last=i;++n;const auto ns=static_cast<std::uint64_t>(1000+i);
        timing.record(ns,64,(n%3)==0);
        sum+=ns;frames+=64;bypass+=(n%3)==0;maxNs=ns;
    }
    const auto fx=timing.snapshot();
    check(n>=250&&n<=417&&fx.samples==static_cast<std::uint64_t>(n)&&
        fx.frames==frames&&fx.totalNs==sum&&fx.maxNs==maxNs&&fx.lastNs==maxNs&&fx.fullyBypassed==bypass);
    armix::rt::RealtimeHealthHistogram histogram;
    auto empty=histogram.snapshot();
    check(empty.total==0 && empty.p95Ceiling==0.);
    for(int i=0;i<9500;++i)histogram.observe(200,10000);
    for(int i=0;i<400;++i)histogram.observe(4000,10000);
    for(int i=0;i<100;++i)histogram.observe(12000,10000);
    histogram.observe(100,0);
    histogram.observe(100,NAN);
    auto snap=histogram.snapshot();
    check(snap.total==10000);
    check(snap.counts[1]==9500); // 2% deadline
    check(snap.counts[7]==400);  // 40% deadline
    check(snap.counts[12]==100); // exceeded deadline
    check(snap.p95Ceiling==0.02);
    check(snap.p99Ceiling==0.40);
    armix::rt::RealtimeHealthHistogram periodic;
    for(int i=0;i<256;++i)periodic.observe(i%4==2?400:100,333);
    const auto phase=periodic.snapshot();
    for(unsigned p=0;p<4;++p){
        check(phase.phaseObserved[p]==64);
        check(phase.phaseOverBudget[p]==(p==2?64u:0u));
    }
    armix::rt::RealtimeHealthHistogram overloaded;
    for(int i=0;i<100;++i)overloaded.observe(25000,10000);
    auto over=overloaded.snapshot();
    check(over.p95Ceiling<0 && over.p99Ceiling<0);
}
