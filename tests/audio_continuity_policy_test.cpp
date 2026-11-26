// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/AudioContinuityPolicy.h"
#include <cassert>
#include <cstdint>

int main() {
    using namespace armix::device;
    static_assert(!AudioPowerPolicy::inspectDevice(NativePowerEvent::displayOff));
    static_assert(!AudioPowerPolicy::inspectDevice(NativePowerEvent::displayOn));
    static_assert(!AudioPowerPolicy::inspectDevice(NativePowerEvent::systemSuspend));
    static_assert(AudioPowerPolicy::inspectDevice(NativePowerEvent::systemResume));
    static_assert(!AudioPowerPolicy::changeMixerIntent(NativePowerEvent::systemResume));

    OutputContinuityIntent output;
    output.operatorRequested(true);
    output.deviceInterrupted();
    assert(output.desired() && output.interrupted());
    output.deviceInterrupted(); // repeated callback STOP is idempotent
    assert(output.desired());
    output.deviceRecovered();
    assert(output.desired() && !output.interrupted());
    output.deviceInterrupted();
    output.operatorRequested(false); // explicit operator MUTE wins
    assert(!output.desired() && !output.interrupted());
    output.deviceRecovered();
    assert(!output.desired()); // cannot silently undo explicit MUTE

    AudioContinuityRetry retry;
    retry.fault(100);
    const auto generation=retry.generation();
    assert(!retry.due(449) && retry.due(450));
    retry.failed(450);
    assert(!retry.due(1449) && retry.due(1450));
    retry.failed(1450);
    assert(retry.due(4450));
    retry.failed(4450);
    assert(retry.due(12450));
    retry.failed(12450);
    assert(retry.due(42450)); // bounded cadence but never permanently exhausted
    for(unsigned i=0;i<100;++i)retry.failed(42450);
    assert(retry.attempts()==4 && !retry.due(72449) && retry.due(72450));
    retry.endpointReturned(42500);
    assert(!retry.due(42849) && retry.due(42850));
    retry.recovered();
    assert(!retry.pending() && retry.generation()>generation);
    retry.fault(50000);
    assert(retry.pending());
    retry.cancel();
    assert(!retry.pending());
}
