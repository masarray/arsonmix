// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AsioRestartRecoveryGate.h"
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    using armix::device::AsioRestartRecoveryGate;
    AsioRestartRecoveryGate gate;
    assert(!gate.due(999999));
    gate.observeEpoch(21,1000);
    assert(!gate.due(1349) && gate.due(1350));
    gate.waitForReadback(1350); // no debit when ASIO panel is still settling
    assert(gate.driverTransactions()==0 && !gate.due(2099) && gate.due(2100));
    gate.driverTransaction(2100);
    gate.carryInternalEpoch(22);
    gate.observeEpoch(22,2150);
    assert(gate.trackedEpoch()==22 && gate.driverTransactions()==1);
    assert(!gate.due(3299) && gate.due(3300));
    gate.driverTransaction(3300); // pin correction or reopened device
    assert(gate.driverTransactions()==2 && !gate.due(7299));
    gate.bindFailed(3350); // never shorten a scheduled driver cool-down
    assert(!gate.due(7299) && gate.due(7300));
    gate.driverTransaction(7300);
    assert(!gate.mayChangeDriver());
    assert(!gate.due(15299) && gate.due(15300));
    gate.bindFailed(15300); // read-only rebind is still allowed after budget
    assert(!gate.due(17699) && gate.due(17700));
    gate.observeEpoch(23,18000); // real external device change reopens budget
    assert(gate.driverTransactions()==0 && gate.mayChangeDriver());
    assert(!gate.due(18349) && gate.due(18350));
    gate.recovered();
    assert(gate.driverTransactions()==0);
    // avoid overflow across uptime wrap or synthetic test timestamps
    gate.observeEpoch(24,std::numeric_limits<std::uint64_t>::max()-10);
    assert(!gate.due(std::numeric_limits<std::uint64_t>::max()-1));
    assert(gate.due(std::numeric_limits<std::uint64_t>::max()));
}
