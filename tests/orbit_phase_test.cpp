// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/OrbitPhase.h"
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
int failures = 0;
void expectNear (const char* label, float got, float expected, float tolerance = 0.00001f)
{
    if (!std::isfinite (got) || std::abs (got - expected) > tolerance)
    {
        std::cerr << "FAIL " << label << ": " << got << " expected " << expected << '\n';
        ++failures;
    }
}
}

int main()
{
    using armix::core::orbitRamp;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    expectNear ("start position", orbitRamp (0.f, 0.0, 20.0, false), 0.f);
    expectNear ("quarter revolution", orbitRamp (0.f, 5.0, 20.0, false), .25f);
    expectNear ("half revolution", orbitRamp (0.f, 10.0, 20.0, false), .5f);
    expectNear ("full revolution wraps", orbitRamp (0.f, 20.0, 20.0, false), 0.f);
    expectNear ("just before seam", orbitRamp (0.f, 19.98, 20.0, false), .999f);
    expectNear ("after seam", orbitRamp (0.f, 20.02, 20.0, false), .001f);
    expectNear ("offset clockwise", orbitRamp (.6f, 5.0, 20.0, false), .85f);
    expectNear ("offset wrap clockwise", orbitRamp (.6f, 10.0, 20.0, false), .1f);
    expectNear ("offset anticlockwise", orbitRamp (.6f, 5.0, 20.0, true), .35f);
    expectNear ("anticlockwise wraps", orbitRamp (.1f, 5.0, 20.0, true), .85f);
    expectNear ("second revolution", orbitRamp (.1f, 40.0, 20.0, true), .1f);
    expectNear ("hours without drift", orbitRamp (.25f, 3600.0, 20.0, false), .25f);
    expectNear ("quickest period", orbitRamp (0.f, .5, 2.0, false), .25f);
    expectNear ("slowest period", orbitRamp (0.f, 45., 180., false), .25f);
    expectNear ("reject invalid period", orbitRamp (.1f, 3.0, 0.0, false), 0.f);
    expectNear ("reject negative period", orbitRamp (.1f, 3.0, -20., false), 0.f);
    expectNear ("reject invalid clock", orbitRamp (.1f, nan, 20.0, false), 0.f);
    expectNear ("reject invalid start", orbitRamp (std::numeric_limits<float>::quiet_NaN(), 0., 20., false), 0.f);
    if (failures > 0) return 1; // effective even for MSVC Release / NDEBUG
    std::cout << "Orbit ramp mathematical tests passed (Release-safe)\n";
    return 0;
}
