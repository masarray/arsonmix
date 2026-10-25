// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/WasapiCapabilityProbe.h"
#include <cstdlib>
int main()
{
#if defined(_WIN32)
    // GitHub-hosted Windows runners may have zero audio endpoints.
    // The native capability reader must still return a safe bounded response,
    // without starting an audio stream or requiring working hardware.
    const auto report = armix::device::WasapiCapabilityProbe::report();
    if (report.isEmpty() || report.length() > 200000) std::abort();
    if (!report.contains ("WASAPI") &&
        !report.contains ("COM initialization failed")) std::abort();
#endif
    return 0;
}
