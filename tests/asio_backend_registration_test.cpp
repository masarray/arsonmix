// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AsioPriorityDeviceManager.h"
#include "../engine/device/AudioSetupRecoveryPolicy.h"
#include <cstdlib>
int main()
{
    using P=armix::device::AudioSetupRecoveryPolicy;
    constexpr auto wasapi=P::select(true,true,false,true);
    static_assert(wasapi.reuseActivePins && !wasapi.recreateDevice &&
                  wasapi.priorKnownOpen && !wasapi.verifyAsioPinIdentity);
    constexpr auto asio=P::select(true,true,true,true);
    static_assert(asio.reuseActivePins && asio.recreateDevice &&
                  asio.priorKnownOpen && asio.verifyAsioPinIdentity);
    constexpr auto closedAsio=P::select(true,false,true,true);
    static_assert(!closedAsio.reuseActivePins && closedAsio.recreateDevice &&
                  !closedAsio.priorKnownOpen && !closedAsio.verifyAsioPinIdentity);
    constexpr auto closedWasapi=P::select(true,false,false,true);
    static_assert(!closedWasapi.reuseActivePins && closedWasapi.recreateDevice);
    constexpr auto freshBackend=P::select(false,true,true,true);
    static_assert(!freshBackend.reuseActivePins && !freshBackend.recreateDevice &&
                  freshBackend.priorKnownOpen && !freshBackend.verifyAsioPinIdentity);
    constexpr auto noPinDraft=P::select(true,true,true,false);
    static_assert(!noPinDraft.reuseActivePins && noPinDraft.recreateDevice &&
                  !noPinDraft.verifyAsioPinIdentity);
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
