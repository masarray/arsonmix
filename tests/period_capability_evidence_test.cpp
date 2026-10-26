// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/PeriodCapabilityEvidence.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
static void check(bool pass) { if (!pass) std::abort(); }

int main()
{
    using armix::device::PeriodCapabilityEvidence;
    constexpr std::array<int,6> periods{960,64,128,480,480,-1};
    const auto d=PeriodCapabilityEvidence::inspect(48000,128,periods);
    check(d.validFormat && d.activePeriodAdvertised);
    check(d.smallestReportedFrames==64 && d.conservativeReportedFrames==480);
    check(std::abs(d.nominalCallbackMs - 2.66666667)<1.e-5);
    check(std::abs(d.conservativeCallbackMs - 10.)<1.e-9);
    constexpr std::array<int,3> small{64,128,256};
    const auto noTen=PeriodCapabilityEvidence::inspect(48000,256,small);
    check(noTen.validFormat && noTen.conservativeReportedFrames==256);
    check(noTen.conservativeCallbackMs < 10.);
    const auto absent=PeriodCapabilityEvidence::inspect(44100,441,small);
    check(absent.validFormat && !absent.activePeriodAdvertised);
    const auto unknown=PeriodCapabilityEvidence::inspect(44100,441,{});
    check(unknown.validFormat && unknown.smallestReportedFrames==0 &&
          unknown.conservativeReportedFrames==0 && !unknown.activePeriodAdvertised);
    check(!PeriodCapabilityEvidence::inspect(0,480,periods).validFormat);
    check(!PeriodCapabilityEvidence::inspect(
        std::numeric_limits<double>::quiet_NaN(),480,periods).validFormat);
    check(!PeriodCapabilityEvidence::inspect(
        std::numeric_limits<double>::infinity(),480,periods).validFormat);
    check(!PeriodCapabilityEvidence::inspect(48000,-1,periods).validFormat);
    check(!PeriodCapabilityEvidence::inspect(48000,65537,periods).validFormat);
    return 0;
}
