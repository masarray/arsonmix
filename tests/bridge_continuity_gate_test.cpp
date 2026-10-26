// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/BridgeContinuityGate.h"
#include <cstdint>
#include <cstdlib>
static void check(bool ok) { if (!ok) std::abort(); }
using G=armix::device::BridgeContinuityGate;
using F=G::Fault;
static void sample(G& g,std::uint64_t ms,std::uint64_t frames,
                   std::uint64_t lost=0,std::uint64_t prime=0,
                   std::uint64_t overflow=0)
{
    g.observe(ms,{frames,lost,prime,overflow});
}
int main()
{
    G gate;
    check(!gate.snapshot().armed);
    sample(gate,0,0);
    for(std::uint64_t t=100;t<=6000;t+=100)
    {
        sample(gate,t,t*48);
        check(gate.snapshot().fault==F::none);
    }
    gate.reset();
    // Initial 1.5s of source acquisition is allowed even at 100% priming.
    for(std::uint64_t t=0;t<=1500;t+=100)
        sample(gate,t,t*48,0,t*48);
    check(gate.snapshot().fault==F::none);
    // One bad 1s window, then healthy, must not flap hardware.
    for(std::uint64_t t=1600;t<=2600;t+=100)
        sample(gate,t,t*48,(t-1500)*48/50,72000);
    check(gate.snapshot().fault==F::none);
    for(std::uint64_t t=2700;t<=3700;t+=100)
        sample(gate,t,t*48,1056,72000);
    check(gate.snapshot().consecutiveBadWindows==0);

    // Recurrent dropout while callback/render frame count keeps advancing.
    gate.reset();sample(gate,0,0);
    for(std::uint64_t t=100;t<=1600;t+=100)sample(gate,t,t*48);
    for(std::uint64_t t=1700;t<=4700;t+=100)
        sample(gate,t,t*48,(t-1600)*48/40);
    check(gate.snapshot().fault==F::underflow);
    check(gate.snapshot().consecutiveBadWindows>=3);
    check(gate.snapshot().lossBasisPoints>=200);
    // Counter restart begins another generation, never reuses old latch.
    sample(gate,5000,240000,0);
    check(gate.snapshot().fault==F::none);

    // Real bridge behavior: a single underflow frame then repeated priming.
    // A repeated ~6% post-warmup loss must NOT evade the continuity gate.
    gate.reset();sample(gate,0,0);
    for(std::uint64_t t=100;t<=1600;t+=100)sample(gate,t,t*48);
    for(std::uint64_t t=1700;t<=4700;t+=100)
        sample(gate,t,t*48,(t-1600)/100,(t-1600)*48/16);
    check(gate.snapshot().fault==F::priming);

    // Alternating counter provenance is still one sustained source fault.
    gate.reset();sample(gate,0,0);
    for(std::uint64_t t=100;t<=1600;t+=100)sample(gate,t,t*48);
    // A complete window 1: underflow frames dominate.
    sample(gate,2700,2700*48,2700,0);
    check(gate.snapshot().consecutiveBadWindows==1);
    // Window 2: re-priming dominates (same absent PCM).
    sample(gate,3800,3800*48,2700,3000);
    check(gate.snapshot().consecutiveBadWindows==2);
    // Window 3: underflow again, threshold reached despite type switch.
    sample(gate,4900,4900*48,5400,3000);
    check(gate.snapshot().fault==F::underflow);

    // Callback alive but capture produces zero useful frames: prime forever.
    gate.reset();sample(gate,0,0);
    for(std::uint64_t t=100;t<=1600;t+=100)sample(gate,t,t*48);
    for(std::uint64_t t=1700;t<=4700;t+=100)
        sample(gate,t,t*48,0,(t-1600)*48);
    check(gate.snapshot().fault==F::priming);
    gate.reset();sample(gate,0,0);
    for(std::uint64_t t=100;t<=1600;t+=100)sample(gate,t,t*48);
    for(std::uint64_t t=1700;t<=4700;t+=100)
        sample(gate,t,t*48,0,0,(t-1600)*48/20);
    check(gate.snapshot().fault==F::overflow);

    // Cross-session counter regression, clock rollback, no-progress do not
    // invent a physical discontinuity or loop forever.
    gate.reset();sample(gate,10000,1000000,1,1,1);
    sample(gate,200,5);
    check(gate.snapshot().fault==F::none);
    gate.reset();sample(gate,0,0);
    for(std::uint64_t t=100;t<10000;t+=100)sample(gate,t,0);
    check(gate.snapshot().fault==F::none);
    return 0;
}
