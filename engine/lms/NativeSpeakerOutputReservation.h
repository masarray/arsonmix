// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// LMS M3 CONTROL-side output ownership fence. The JUCE callback remains
// the ONLY hardware writer. This policy is an admission prerequisite,
// NOT a second routing engine and NOT proof of loudspeaker protection.
#include "PreparedSpeakerSystem.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace armix::lms {
struct NativePcmOutputInventory final {
    // Obtained from the accepted ChannelId PCM graph + current device snapshot,
    // NEVER from GUI labels or ordinal assumptions.
    std::uint64_t deviceEpoch=0,acceptedGraphRevision=0,mainChannelId=0;
    std::string deviceIdentity;
    double sampleRate=0.;
    int callbackOutputs=0;
    int mainL=-1,mainR=-1,monitorL=-1,monitorR=-1;
    bool acceptedChannelIdPcm=false,allOutputsDisarmed=false;
    std::vector<int> otherOccupiedOutputOrdinals; // e.g. EXTERNAL or BUS direct
    std::vector<VerifiedOutputPort> enabledOutputPorts;
};
enum class SpeakerReservationFault : std::uint8_t {
    none,notAcceptedPcm,staleDevice,staleGraph,invalidSource,
    deviceLayoutMismatch,occupiedOutput,unknownPort,unprotectedOutput
};
struct SpeakerReservationResult final {
    SpeakerReservationFault fault=SpeakerReservationFault::none;
    std::vector<int> reservedCallbackOrdinals; // CONTROL: explicit exclusive pins
    std::uint64_t acceptedDeviceEpoch=0,acceptedGraphRevision=0;
    bool mayWritePhysicalAudio=false; // M3 always FALSE: no calibrated limiter.
    [[nodiscard]] bool ok() const noexcept{return fault==SpeakerReservationFault::none;}
};
class NativeSpeakerOutputReservation final {
public:
    // NO allocation in the realtime callback; only invoke from CONTROL with
    // current device readback and accepted native ChannelId graph.
    // Even an "unmuted" browser design cannot arm real loudspeakers using a
    // Boolean acknowledgement or a digital hard clip masquerading as a limiter.
    static SpeakerReservationResult preflight(
        const SpeakerSystemRequest& request,
        const NativePcmOutputInventory& accepted,
        std::uint64_t requestedSourceChannelId) {
        SpeakerReservationResult report{};
        auto reject=[&](SpeakerReservationFault reason) {
            report.fault=reason;
            report.reservedCallbackOrdinals.clear();
            return report;
        };
        if(!accepted.acceptedChannelIdPcm||!accepted.allOutputsDisarmed||
           accepted.deviceIdentity.empty()||accepted.deviceEpoch==0||
           accepted.acceptedGraphRevision==0)
            return reject(SpeakerReservationFault::notAcceptedPcm);
        if(request.deviceEpoch!=accepted.deviceEpoch)
            return reject(SpeakerReservationFault::staleDevice);
        if(request.graphRevision!=accepted.acceptedGraphRevision)
            return reject(SpeakerReservationFault::staleGraph);
        if(!requestedSourceChannelId||requestedSourceChannelId!=accepted.mainChannelId)
            return reject(SpeakerReservationFault::invalidSource);
        if(request.activeOutputChannels!=accepted.callbackOutputs||
           accepted.callbackOutputs<2||accepted.callbackOutputs>256||
           !std::isfinite(accepted.sampleRate)||
           accepted.sampleRate!=request.sampleRate||
           accepted.mainL<0||accepted.mainR<0||
           accepted.mainL==accepted.mainR||
           accepted.mainL>=accepted.callbackOutputs||
           accepted.mainR>=accepted.callbackOutputs)
            return reject(SpeakerReservationFault::deviceLayoutMismatch);
        auto owned=accepted.otherOccupiedOutputOrdinals;
        owned.push_back(accepted.mainL);owned.push_back(accepted.mainR);
        if(accepted.monitorL>=0||accepted.monitorR>=0) {
            if(accepted.monitorL<0||accepted.monitorR<0||
               accepted.monitorL==accepted.monitorR||
               accepted.monitorL>=accepted.callbackOutputs||
               accepted.monitorR>=accepted.callbackOutputs)
                return reject(SpeakerReservationFault::deviceLayoutMismatch);
            owned.push_back(accepted.monitorL);owned.push_back(accepted.monitorR);
        }
        std::unordered_set<int> used;
        for(const auto pin:owned)
            if(pin<0||pin>=accepted.callbackOutputs||!used.insert(pin).second)
                return reject(SpeakerReservationFault::deviceLayoutMismatch);
        for(const auto& selected:request.outputs) {
            const auto verified=std::find_if(accepted.enabledOutputPorts.begin(),
                accepted.enabledOutputPorts.end(),[&](const VerifiedOutputPort& p){
                    return p.identity==selected.portIdentity;
                });
            const auto draft=std::find_if(request.verifiedPorts.begin(),
                request.verifiedPorts.end(),[&](const VerifiedOutputPort& p){
                    return p.identity==selected.portIdentity;
                });
            if(verified==accepted.enabledOutputPorts.end()||
               draft==request.verifiedPorts.end()||
               verified->callbackOrdinal!=draft->callbackOrdinal)
                return reject(SpeakerReservationFault::unknownPort);
            const int ordinal=verified->callbackOrdinal;
            if(ordinal<0||ordinal>=accepted.callbackOutputs)
                return reject(SpeakerReservationFault::unknownPort);
            if(!used.insert(ordinal).second)
                return reject(SpeakerReservationFault::occupiedOutput);
            report.reservedCallbackOrdinals.push_back(ordinal);
            // The M2 "limiter" is currently a HARD DIGITAL CEILING, not a
            // calibrated voltage/RMS/peak speaker safety DSP. No live output
            // authorization is legal merely because a user enabled it.
            if(!selected.mute)
                return reject(SpeakerReservationFault::unprotectedOutput);
        }
        report.acceptedDeviceEpoch=accepted.deviceEpoch;
        report.acceptedGraphRevision=accepted.acceptedGraphRevision;
        // Result describes only a safely disconnected mapping draft.
        // A later true native limiter + verified profile must introduce a
        // distinct typed hardware publication transaction.
        return report;
    }
};
} // namespace armix::lms
