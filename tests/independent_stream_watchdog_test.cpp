// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/IndependentStreamWatchdog.h"
#include <cstdint>
#include <cstdlib>
static void check(bool ok){if(!ok)std::abort();}
int main()
{
    using W=armix::device::IndependentStreamWatchdog;
    using S=W::Stall;
    W w;
    check(!w.snapshot().armed);
    check(w.observe(1000,0,0)==S::none);
    check(w.observe(3999,0,0)==S::none);
    check(w.observe(4000,0,0)==S::both);
    check(w.snapshot().captureSilentMs==3000);
    check(w.observe(4001,1,1)==S::both); // sticky even after late callback
    w.reset(); check(!w.snapshot().armed);
    check(w.observe(0,0,0)==S::none);
    for(std::uint64_t t=100;t<3000;t+=100)
        check(w.observe(t,0,t/100)==S::none);
    check(w.observe(3000,0,30)==S::capture);
    check(w.snapshot().renderSilentMs==0);
    w.reset();
    check(w.observe(200,0,0)==S::none);
    for(std::uint64_t t=300;t<3200;t+=100)
        check(w.observe(t,t/100,0)==S::none);
    check(w.observe(3200,32,0)==S::render);
    w.reset();
    check(w.observe(100,0,0)==S::none);
    for(std::uint64_t t=200;t<100000;t+=100)
        check(w.observe(t,t*2,t*3)==S::none);
    check(w.snapshot().captureSilentMs==0 && w.snapshot().renderSilentMs==0);
    w.reset();
    check(w.observe(9000,UINT64_MAX,UINT64_MAX)==S::none);
    check(w.observe(9200,0,0)==S::none); // counter wrap
    check(w.observe(100,1,1)==S::none); // monotonic clock reset
    check(w.observe(3099,1,1)==S::none);
    check(w.observe(3100,1,1)==S::both);
    w.reset();
    check(w.observe(80000,0,0)==S::none);
    check(w.observe(80001,3,4)==S::none);
    return 0;
}
