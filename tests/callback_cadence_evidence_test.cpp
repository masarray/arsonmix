// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/CallbackCadenceEvidence.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
using C = armix::rt::CallbackCadenceEvidence;
static void check(bool ok) { if (!ok) std::abort(); }
static bool close(double a, double b, double epsilon=0.001)
{
    return std::abs(a-b) < epsilon;
}
int main()
{
    C cadence;
    check(cadence.observe(1000000,480).kind == C::Kind::invalid);
    cadence.prepare(48000.0);
    check(cadence.observe(1000000,480).kind == C::Kind::first);

    // 480 samples -> 10ms, even though NEXT block requests 64 samples.
    auto measure = cadence.observe(1010000,64);
    check(measure.kind == C::Kind::onSchedule);
    check(measure.actualIntervalUs == 10000);
    check(close(measure.expectedIntervalUs,10000.0));
    check(close(measure.ratio,1.0));

    // 64 samples -> 1.3333ms, even though NEXT is 512 frames.
    measure = cadence.observe(1011333,512);
    check(measure.kind == C::Kind::onSchedule);
    check(close(measure.expectedIntervalUs,1333.333333,0.001));
    // An older approach compared this interval to the CURRENT 512-frame
    // buffer (10.67ms), hiding a real late callback.
    measure = cadence.observe(1022000,480);
    check(measure.kind == C::Kind::onSchedule);
    check(close(measure.expectedIntervalUs,10666.66667,0.001));

    // Previous block 480 @ 48k = 10ms. Late classifications are strictly
    // greater than the 1.5 and 2.3 nominal-period heuristic thresholds.
    measure = cadence.observe(1037000,480);
    check(measure.kind == C::Kind::onSchedule);
    measure = cadence.observe(1052001,480);
    check(measure.kind == C::Kind::late);
    measure = cadence.observe(1075002,480);
    check(measure.kind == C::Kind::veryLate);
    check(measure.comparable());
    check(measure.actualIntervalUs == 23001);

    // Critical opposite direction: 64-frame previous period gives 1.333ms,
    // and the new 480-frame block must NOT suppress a 4ms late interval.
    cadence.prepare(48000.0);
    cadence.observe(2000000,64);
    measure = cadence.observe(2004000,480);
    check(measure.kind == C::Kind::veryLate);
    check(close(measure.expectedIntervalUs,1333.333333,0.001));

    // EXACT threshold and fixed-size fast case.
    cadence.prepare(48000.0);
    cadence.observe(3000000,480);
    measure = cadence.observe(3015000,480);
    check(measure.kind == C::Kind::onSchedule);
    measure = cadence.observe(3030001,480);
    check(measure.kind == C::Kind::late);
    cadence.prepare(48000.0);
    cadence.observe(3000000,480);
    measure = cadence.observe(3023000,480);
    check(measure.kind == C::Kind::late);

    // Monotonic regression/wrap must never underflow unsigned arithmetic.
    cadence.prepare(44100.0);
    cadence.observe(6000000,441);
    measure = cadence.observe(5999900,128);
    check(measure.kind == C::Kind::clockRegression);
    measure = cadence.observe(6002802,441);
    check(measure.kind == C::Kind::veryLate);
    check(close(measure.expectedIntervalUs,1000000.0*128/44100.0,0.001));

    // A new JUCE device generation resets the cadence seed.
    cadence.prepare(96000.0);
    check(cadence.observe(7000000,96).kind == C::Kind::first);
    measure = cadence.observe(7001000,192);
    check(measure.kind == C::Kind::onSchedule);
    check(close(measure.expectedIntervalUs,1000.0));
    cadence.prepare(44100.0);
    check(cadence.observe(7001100,441).kind == C::Kind::first);
    measure = cadence.observe(7011100,64);
    check(measure.kind == C::Kind::onSchedule);
    check(close(measure.expectedIntervalUs,10000.0));

    // Invalid samples/rates kill the previous baseline (never compare across
    // broken route transitions). No allocations, mutex, OS calls or threads.
    cadence.prepare(NAN);
    check(cadence.observe(1000,480).kind == C::Kind::invalid);
    cadence.prepare(48000.0);
    check(cadence.observe(0,480).kind == C::Kind::invalid);
    check(cadence.observe(1000,0).kind == C::Kind::invalid);
    check(cadence.observe(1000,65537).kind == C::Kind::invalid);
    check(cadence.observe(1000,128).kind == C::Kind::first);
    check(cadence.observe(1100,480).kind == C::Kind::onSchedule);
    check(!C{}.observe(1,1).comparable());
    check(std::strstr(C::provenance(),"NOT_RENDER_XRUN") != nullptr);

    // 100k steadily varying callback quanta: generate exact rational timing
    // under a 48kHz virtual clock, verifying zero false late counts.
    cadence.prepare(48000.0);
    constexpr int sequence[5] {64,128,256,480,512};
    std::uint64_t frames = 0;
    const std::uint64_t epoch = 1000000;
    for (int n = 0; n < 100000; ++n)
    {
        const auto atUs = epoch + (1000000ULL*frames + 24000ULL)/48000ULL;
        auto seen = cadence.observe(atUs,sequence[n%5]);
        check(seen.kind == (n==0 ? C::Kind::first : C::Kind::onSchedule));
        frames += static_cast<std::uint64_t>(sequence[n%5]);
    }
}
