// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AsioPriorityDeviceManager.h"
#include <cstdlib>
int main()
{
#if defined(_WIN32)
    // A headless CI runner need NOT own an ASIO interface. But the Windows
    // app MUST ship a functioning ASIO backend type, not just a GUI label.
    armix::device::AsioPriorityDeviceManager manager;
    bool foundAsio = false;
    bool foundWasapi = false;
    for (auto* type : manager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() == "ASIO") foundAsio = true;
        if (type->getTypeName() == "Windows Audio") foundWasapi = true;
    }
    if (!foundAsio || !foundWasapi) std::abort();
#endif
    return 0;
}
