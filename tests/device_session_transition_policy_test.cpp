// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/DeviceSessionTransitionPolicy.h"
#include <cassert>
#include <initializer_list>
using armix::device::DeviceSessionDecision;
using armix::device::decideDeviceSession;

int main()
{
    // Opening and closing the dialog without a real hardware generation
    // change never discards an accepted mixer, regardless of its contents.
    static_assert(decideDeviceSession(true, true, true) ==
                  DeviceSessionDecision::retainBoundMixer);
    static_assert(decideDeviceSession(true, true, false) ==
                  DeviceSessionDecision::retainBoundMixer);
    // A failed open cannot create a default mixer or grant output permission.
    static_assert(decideDeviceSession(false, false, true) ==
                  DeviceSessionDecision::rejectedDevice);
    static_assert(decideDeviceSession(false, false, false) ==
                  DeviceSessionDecision::rejectedDevice);
    // A new driver epoch with user-authored routing/FX is a NEEDS-REBIND
    // state, never a permission to overwrite the document with 10ch defaults.
    static_assert(decideDeviceSession(true, false, true) ==
                  DeviceSessionDecision::retainUnboundMixer);
    // Only a genuinely blank first-run mixer can start from the silent graph.
    static_assert(decideDeviceSession(true, false, false) ==
                  DeviceSessionDecision::createFirstSilentMixer);
    for (bool verified : {false, true}) {
        for (bool bound : {false, true}) {
            for (bool edited : {false, true}) {
                const auto decision = decideDeviceSession(verified,bound,edited);
                assert(decision != DeviceSessionDecision::createFirstSilentMixer ||
                       (verified && !bound && !edited));
            }
        }
    }
}
