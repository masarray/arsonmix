// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/ClockBridgeSizing.h"
#include "../engine/device/AdaptiveClockBridge.h"
#include <array>
#include <cmath>
#include <cstdlib>
static void check (bool ok) { if (!ok) std::abort(); }

int main()
{
    using armix::device::ClockBridgeSizing;
    const auto normal = ClockBridgeSizing::choose (48000,48000,480,480,48,32768,24.);
    check (normal.valid && normal.targetMs>=24. && normal.targetMs<30.);
    const auto longRender = ClockBridgeSizing::choose (48000,48000,480,1440,48,32768,24.);
    check (longRender.valid && longRender.targetMs>=60. && longRender.targetMs<70.);
    const auto differing = ClockBridgeSizing::choose (44100,48000,441,1440,48,32768,24.);
    check (differing.valid && differing.targetMs>=60.);
    check (!ClockBridgeSizing::choose (48000,48000,480,48000,48,32768,24.).valid);
    check (!ClockBridgeSizing::choose (0,48000,480,480,48,32768,24.).valid);

    // Regression for 30-ms render block / 10-ms capture: a fixed 24-ms FIFO
    // target causes repeated starvation in the old implementation. The
    // derived headroom must run 60 seconds with ZERO post-prime losses.
    armix::device::AdaptiveClockBridge bridge;
    check (bridge.prepare (48000,48000,2,32768,longRender.targetMs));
    std::array<float,480> inL {}, inR {};
    std::array<float,1440> outL {}, outR {};
    inL.fill(0.25f); inR.fill(0.25f);
    const float* inputs[]={inL.data(),inR.data()};
    float* outputs[]={outL.data(),outR.data()};
    for (int tick=0; tick<6000; ++tick)
    {
        // Tick duration 10 ms; producer and consumer have distinct schedules.
        check (bridge.push(inputs,2,480)==480);
        if ((tick+1)%3 == 0)
        {
            bridge.pull(outputs,2,1440);
            if (tick>100)
            {
                for (int i=0;i<1440;++i)
                {
                    check (std::isfinite(outL[static_cast<std::size_t>(i)]));
                    check (std::abs(outL[static_cast<std::size_t>(i)]-0.25f)<0.02f);
                    check (std::abs(outR[static_cast<std::size_t>(i)]-0.25f)<0.02f);
                }
            }
        }
    }
    auto stats = bridge.status();
    check (stats.primingFrames>0);
    check (stats.underflowFrames==0 && stats.overflowFrames==0);
    check (stats.fillFrames<32768);
}
