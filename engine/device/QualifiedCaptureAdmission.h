// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "MultiDeviceInputPlan.h"
#include "SecondaryCaptureClockStream.h"
#include <cstdint>

// CONTROL-side exact A/B admission; no driver query or realtime work.
// Caller MUST derive both device identities and masks from independently
// attested native hardware readback, never from display labels or .smix.
namespace armix::device {
class QualifiedCaptureAdmission final {
public:
    enum class Fault {
        none, stalePrimary, wrongTopology, wrongPrimaryPins,
        secondaryNotReady, wrongCaptureLease, unsafeFrames
    };
    static Fault verify(const MultiDeviceInputPlan& plan,
                        const SecondaryCaptureClockStream& clock,
                        std::uint64_t primaryEpoch,
                        int verifiedPrimaryInputCount,
                        int maximumRenderFrames) noexcept {
        if(!primaryEpoch||plan.primaryEpoch()!=primaryEpoch||
           plan.deviceCount()!=2||
           plan.device(0).generation!=primaryEpoch)
            return Fault::stalePrimary;
        if(!plan.device(0).primary||plan.device(1).primary||
           !plan.device(0).deviceLease||!plan.device(1).deviceLease||
           plan.device(0).deviceLease==plan.device(1).deviceLease||
           plan.device(1).activePins<1||
           plan.device(1).activePins>16)
            return Fault::wrongTopology;
        if(verifiedPrimaryInputCount<1||
           plan.device(0).activePins!=verifiedPrimaryInputCount)
            return Fault::wrongPrimaryPins;
        if(maximumRenderFrames<1||
           maximumRenderFrames>MultiDeviceInputPlan::maxCallbackFrames)
            return Fault::unsafeFrames;
        if(!clock.ready())return Fault::secondaryNotReady;
        const auto& format=clock.format(); // CONTROL only; not in RT
        if(format.captureGeneration!=plan.device(1).generation||
           format.deviceLease!=plan.device(1).deviceLease||
           format.primaryEpoch!=primaryEpoch||
           format.channels!=plan.device(1).activePins||
           format.primaryPeriod<1||format.primaryPeriod>maximumRenderFrames)
            return Fault::wrongCaptureLease;
        return Fault::none;
    }
};
} // namespace armix::device
