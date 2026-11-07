// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace armix::device {
// A pure CONTROL-thread decision: the selected ASIO driver may keep the same
// JUCE name while its ASIO4ALL/WDM pins were reconfigured externally.
// Never reuse the old live pin mask when the device is no longer open.
// A closed prior device is not a valid rollback target.
struct AudioSetupRecoveryPolicy final {
    struct Plan final {
        bool reuseActivePins;
        bool recreateDevice;
        bool priorKnownOpen;
        bool verifyAsioPinIdentity;
    };
    static constexpr Plan select(bool sameEndpoint, bool priorOpen,
                                 bool isAsio, bool completePinSelection) noexcept {
        return {
            sameEndpoint && priorOpen && completePinSelection,
            sameEndpoint && (!priorOpen || isAsio),
            priorOpen,
            sameEndpoint && priorOpen && isAsio && completePinSelection
        };
    }
};
} // namespace armix::device
