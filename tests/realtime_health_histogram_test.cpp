// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/RealtimeHealthHistogram.h"
#include <cstdlib>
#include <cmath>
static void check(bool ok) { if(!ok)std::abort(); }
int main()
{
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
}
