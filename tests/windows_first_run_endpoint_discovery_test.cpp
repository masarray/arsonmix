// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/WindowsFirstRunEndpointDiscovery.h"
#include <iostream>
int main()
{
    using Probe = armix::device::WindowsFirstRunEndpointDiscovery;
    const auto result = Probe::inspect ({}, {});
    // Machine-independent live test: CI need not have VB-CABLE, speakers, or
    // an activated audio endpoint. The only permitted output of missing JUCE
    // names is a safe denial; the read-only probe must never open a stream.
    if (result.decision.mayAttemptApply()) return 1;
    std::cout << "FIRST_RUN_READ_ONLY_NO_GUESSED_DEVICE_PASS\n";
    return 0;
}
