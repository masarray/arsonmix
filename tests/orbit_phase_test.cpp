// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/core/OrbitPhase.h"
#include <cassert>
#include <cmath>
int main()
{
    using armix::core::orbitRamp;
    auto near = [] (float a, float b) { return std::abs (a - b) < 0.00001f; };
    assert (near (orbitRamp (0.f, 0.0, 20.0, false), 0.f));
    assert (near (orbitRamp (0.f, 5.0, 20.0, false), .25f));
    assert (near (orbitRamp (0.f, 10.0, 20.0, false), .5f));
    assert (near (orbitRamp (0.f, 20.0, 20.0, false), 0.f));
    assert (near (orbitRamp (.6f, 5.0, 20.0, false), .85f));
    assert (near (orbitRamp (.6f, 10.0, 20.0, false), .1f));
    assert (near (orbitRamp (.6f, 5.0, 20.0, true), .35f));
    assert (near (orbitRamp (.1f, 5.0, 20.0, true), .85f));
    assert (near (orbitRamp (.1f, 40.0, 20.0, true), .1f));
    assert (near (orbitRamp (.1f, 3.0, 0.0, false), 0.f));
    assert (near (orbitRamp (.1f, NAN, 20.0, false), 0.f));
}
