// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/DeviceSessionTransitionPolicy.h"
#include <cassert>
#include <initializer_list>
using armix::device::DeviceSessionDecision;
using armix::device::decideDeviceSession;

int main()
{
    using armix::device::retainedMixerQuantum;
    static_assert(retainedMixerQuantum(256,1024,8192,false,true)==256);
    static_assert(retainedMixerQuantum(256,64,8192,false,true)==64);
    static_assert(retainedMixerQuantum(64,512,8192,false,true)==64);
    static_assert(retainedMixerQuantum(256,512,8192,true,false)==256);
    static_assert(retainedMixerQuantum(256,512,8192,true,true)==0);
    static_assert(retainedMixerQuantum(256,8193,8192,false,false)==0);
    static_assert(retainedMixerQuantum(0,512,8192,false,false)==0);
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
