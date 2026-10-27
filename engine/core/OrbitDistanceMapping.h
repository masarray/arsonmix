// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

namespace armix::core
{
// Invert a monotonic vendor DISPLAY mapping, not an assumed linear physical scale.
// Find the interval for the *exact* displayed value and aim at its centre.
// This prevents a midpoint search from yielding 1.99 m when 2.00 m was requested.
// A setpoint the vendor cannot display exactly is rejected, never misrepresented.
template <typename ReadDisplayedValue>
bool normalizedForDisplayedSetpoint(double requested,double minimum,double maximum,
                                   ReadDisplayedValue&& read,float& result) noexcept
{
    if (!std::isfinite(requested) || !std::isfinite(minimum) ||
        !std::isfinite(maximum) || minimum >= maximum ||
        requested < minimum || requested > maximum) return false;
    constexpr int iterations=30;
    auto boundary=[&](bool greater,double& out)->bool {
        double lo=0.,hi=1.;
        const auto atLo=read(0.f),atHi=read(1.f);
        if (!std::isfinite(atLo)||!std::isfinite(atHi)||atLo>atHi) return false;
        for(int i=0;i<iterations;++i) {
            const double mid=(lo+hi)*0.5;
            const double displayed=read(static_cast<float>(mid));
            if(!std::isfinite(displayed))return false;
            if(greater?displayed>requested:displayed>=requested)hi=mid;
            else lo=mid;
        }
        out=hi;return true;
    };
    double first=0.,end=1.;
    if(!boundary(false,first)||!boundary(true,end))return false;
    const float candidate=static_cast<float>((first+end)*0.5);
    const double displayed=read(candidate);
    // Decimal text parsed into doubles may contain a few ULPs of representation.
    if(!std::isfinite(displayed) || std::abs(displayed-requested)>
        1.e-7*std::max(1.,std::abs(requested)))return false;
    result=candidate;
    return true;
}
}
