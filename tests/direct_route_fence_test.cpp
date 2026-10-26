// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/DirectRouteFence.h"
#include <cstdlib>
using armix::device::DirectRouteFence;
static void check(bool ok) { if (!ok) std::abort(); }

int main()
{
    using F = DirectRouteFence::Fault;
    DirectRouteFence f;
    check(!f.permitted());
    check(f.fault() == F::awaitingApproval);
    check(!f.observeStart("ASIO", "Live USB", "3", "3"));
    check(!f.approve("ASIO", "", "3", "3"));
    check(!f.approve("ASIO", "Live USB", "0", "3"));
    check(!f.approve("ASIO", "Live USB", "3", ""));
    check(!f.approve("ASIO", "Live USB", "03", "3"));
    check(!f.approve("ASIO", "Live USB", "G", "3"));
    check(!f.permitted());

    check(f.approve("ASIO", "Live USB", "3", "3"));
    check(f.permitted() && f.fault() == F::none);
    check(f.observeStart("ASIO", "Live USB", "3", "3"));

    // ASIO multichannel output may shift from pair 1/2 to pair 3/4
    // without a driver-name change. Fail BEFORE microphone reaches DSP.
    check(!f.observeStart("ASIO", "Live USB", "3", "c"));
    check(!f.permitted() && f.fault() == F::unexpectedRoute);
    check(!f.observeStart("ASIO", "Live USB", "3", "3")); // sticky
    check(!f.resumeExpected("ASIO", "Live USB", "3", "c"));
    check(!f.resumeExpected("ASIO", "Live USB", "c", "3"));
    check(!f.resumeExpected("ASIO", "Backup USB", "3", "3"));
    check(f.resumeExpected("ASIO", "Live USB", "3", "3"));
    check(f.permitted() && f.fault() == F::none);

    check(!f.observeStart("Windows Audio", "Live USB", "3", "3"));
    check(!f.permitted() && f.fault() == F::unexpectedRoute);
    f.suspend();
    check(f.approve("ASIO", "Backup USB", "1", "c"));
    check(f.observeStart("ASIO", "Backup USB", "1", "c"));

    // Canonical mask hash retains channels above 64-bit without truncation.
    check(f.approve("ASIO", "128ch", "10000000000000000000000000000003", "3"));
    check(f.observeStart("ASIO", "128ch", "10000000000000000000000000000003", "3"));
    check(!f.observeStart("ASIO", "128ch", "3", "3"));
    f.suspend();
    check(f.approve("Windows Audio", "Headphones", "1", "3"));

    check(DirectRouteFence::fingerprint("ASIO", "ABC", "3", "3")
        != DirectRouteFence::fingerprint("ASIOA", "BC", "3", "3"));
    check(DirectRouteFence::fingerprint("ASIO", "ABC", "1", "3")
        != DirectRouteFence::fingerprint("ASIO", "ABC", "3", "1"));
    check(DirectRouteFence::fingerprint("", "USB", "3", "3") == 0);
    return 0;
}
