// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/WasapiEndpointIdentityGuard.h"
#include <cstdlib>
int main()
{
#if defined(_WIN32)
    using G=armix::device::WasapiEndpointIdentityGuard;
    // CI Windows runner may have no audio hardware. Fail-closed before
    // touching stream APIs; actual endpoints are tested in hardware FAT.
    auto r=G::resolve({}, {});
    if(r.ok() || r.error.isEmpty()) std::abort();
    const auto hash=G::fingerprint(L"endpoint-example");
    if(hash.isEmpty()) std::abort();
    if(G::verify({}, "a", "b").isEmpty()) std::abort();
#endif
    return 0;
}
