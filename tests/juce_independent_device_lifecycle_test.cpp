// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/JuceIndependentClockDevice.h"
#include <cstdlib>
static void check (bool ok) { if (!ok) std::abort(); }
int main()
{
    // Hardware-independent lifecycle proof: invalid requests are rejected
    // before opening any WASAPI endpoint, and close is idempotent.
    armix::device::JuceIndependentClockDevice d;
    check (!d.active());
    check (!d.faulted());
    check (d.captureCallbacks() == 0);
    check (d.renderCallbacks() == 0);
    check (d.open ({}, nullptr, {}).isNotEmpty());
    check (!d.active());
    d.close();
    d.close();
    check (!d.active());
    return 0;
}
