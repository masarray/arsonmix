// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AsioRegistrationPolicy.h"
#include "../engine/device/AsioRegistryIdentityProbe.h"
#include <array>
#include <cstdlib>
#include <string>
#include <vector>
using P=armix::device::AsioRegistrationPolicy;
static void check(bool yes) { if(!yes)std::abort(); }
static P::Entry a(std::wstring name=L"Studio ASIO",
                  std::wstring key=L"vendor.asio",
                  std::wstring guid=L"{12345678-1234-ABCD-7890-1234567890ab}")
{
    return {std::move(name),std::move(key),std::move(guid),true};
}
int main()
{
    std::vector<P::Entry> drivers {a()};
    auto r=P::resolve(drivers,L"Studio ASIO");
    check(r.ok());
    check(r.identity.registryKey==L"vendor.asio");
    check(r.identity.canonicalClsid==L"{12345678-1234-abcd-7890-1234567890ab}");
    check(armix::device::AsioRegistryIdentityProbe::diagnosticFingerprint(r.identity)!=0);
    check(!P::resolve(drivers,L"Unknown ASIO").ok());
    check(P::resolve(drivers,L"").error==P::Error::invalidName);
    auto updated=r.identity;
    check(r.identity.sameDriverClass(updated));
    updated.canonicalClsid=L"{22222222-1234-abcd-7890-1234567890ab}";
    check(!r.identity.sameDriverClass(updated));
    updated=r.identity;
    updated.registryKey=L"other.vendor";
    check(!r.identity.sameDriverClass(updated));

    // JUCE's name-to-index selection cannot identify two entries sharing a
    // friendly name. Never silently select first/last on registry reordering.
    drivers.push_back(a(L"Studio ASIO",L"other.asio",
        L"{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}"));
    check(P::resolve(drivers,L"Studio ASIO").error==P::Error::ambiguousName);
    drivers.pop_back();

    drivers[0].inprocServerRegistered=false;
    check(P::resolve(drivers,L"Studio ASIO").error==P::Error::missingComServer);
    drivers[0].inprocServerRegistered=true;
    const std::array<std::wstring,7> invalid {
      L"", L"not-a-guid",L"{00000000-0000-0000-0000-000000000000}",
      L"{12345678-1234-ABCD-7890-1234567890az}",
      L"{123456781234-ABCD-7890-1234567890ab}",
      L"{12345678-1234-ABCD-7890-1234567890ab}noise",
      L"12345678-1234-ABCD-7890-1234567890ab"
    };
    for(const auto& value:invalid)
    {
        drivers[0].clsid=value;
        check(P::resolve(drivers,L"Studio ASIO").error==P::Error::invalidClsid);
    }
    drivers[0]=a();
    drivers[0].registryKey.clear();
    check(P::resolve(drivers,L"Studio ASIO").error==P::Error::invalidClsid);
    drivers[0]=a();
    check(r.identity.sameDriverClass(P::resolve(drivers,L"Studio ASIO").identity));
    check(std::string(P::reason(P::Error::ambiguousName))
        =="ASIO_AMBIGUOUS_REGISTERED_DRIVER_NAME");

    // Native read-only smoke: CI machines usually have no installed ASIO.
    // Missing/denied registry is a truthful observation, never fake evidence.
    auto native=armix::device::AsioRegistryIdentityProbe::resolve(
        L"__ArSonMix_not_installed_ASIO_driver_20261026__");
    check(!native.registration.ok());
    check(native.registration.error==P::Error::missing
          || native.registration.error==P::Error::registryScanFailed);
    return 0;
}
