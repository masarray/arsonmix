// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/DirectRouteFence.h"
#include <cstdlib>
using armix::device::DirectRouteFence;
static void check(bool ok) { if(!ok)std::abort(); }
int main()
{
    using F=DirectRouteFence::Fault;
    DirectRouteFence f;
    check(!f.permitted());
    check(f.fault()==F::awaitingApproval);
    check(!f.observeStart("ASIO","Live USB"));
    check(!f.approve("ASIO",""));
    check(!f.permitted());
    check(f.approve("ASIO","Live USB"));
    check(f.permitted() && f.fault()==F::none);
    check(f.observeStart("ASIO","Live USB"));
    // Hotplug swaps driver API or another identically configured device.
    check(!f.observeStart("Windows Audio","Live USB"));
    check(!f.permitted() && f.fault()==F::unexpectedRoute);
    check(!f.observeStart("ASIO","Live USB")); // NO silent re-arm
    check(!f.permitted());
    check(!f.resumeExpected("ASIO","Another Device"));
    check(f.resumeExpected("ASIO","Live USB"));
    check(f.permitted());
    f.suspend();
    // Explicit user ratification can safely resume normal direct route.
    f.suspend();
    check(f.fault()==F::awaitingApproval);
    check(f.approve("ASIO","Backup USB"));
    check(f.observeStart("ASIO","Backup USB"));
    check(!f.observeStart("ASIO","Live USB"));
    f.suspend();
    check(f.approve("Windows Audio","Headphones"));
    check(f.observeStart("Windows Audio","Headphones"));
    check(DirectRouteFence::fingerprint("ASIO","ABC")
         !=DirectRouteFence::fingerprint("ASIOA","BC"));
    check(DirectRouteFence::fingerprint("ASIO","ABC")
         !=DirectRouteFence::fingerprint("WASAPI","ABC"));
    check(DirectRouteFence::fingerprint("","USB")==0);
    check(DirectRouteFence::fingerprint("ASIO","")==0);
    return 0;
}
