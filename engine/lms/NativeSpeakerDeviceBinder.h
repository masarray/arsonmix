// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// M6 CONTROL-side boundary: only an attested JUCE output inventory can
// replace an LMS browser's untrusted DEMO/ordinal pins. Not an audio writer.
#include "NativeSpeakerOutputReservation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace armix::lms {
struct TrustedSpeakerDeviceView final {
    // ONLY constructed from a captured, active JUCE device mask and the
    // accepted native ChannelId routing snapshot, never from WebView JSON.
    std::uint64_t deviceEpoch=0,graphRevision=0,mainChannelId=0;
    std::string backend,deviceIdentity;
    double sampleRate=0.;
    int callbackOutputs=0;
    int mainL=-1,mainR=-1,monitorL=-1,monitorR=-1;
    bool acceptedPcm=false,exactDeviceMask=false,allOutputsDisarmed=false;
    std::vector<VerifiedOutputPort> actualEnabledPorts;
    std::vector<int> otherOccupiedOrdinals;
};
enum class SpeakerBindFault : std::uint8_t {
    none,unverifiedDevice,staleDevice,staleGraph,invalidSource,
    changedFormat,invalidPhysicalPin,invalidLoudspeakerDraft
};
struct SpeakerBoundDraft final {
    SpeakerSystemRequest verifiedRequest;
    NativePcmOutputInventory verifiedInventory;
    std::uint64_t sourceChannelId=0;
};
struct SpeakerBindResult final {
    SpeakerBindFault fault=SpeakerBindFault::none;
    [[nodiscard]] bool ok() const noexcept {return fault==SpeakerBindFault::none;}
};
class NativeSpeakerDeviceBinder final {
public:
    // Validate first, then atomically stage a complete CONTROL-only result.
    // A rejected candidate never changes prior verified input or caller state.
    static SpeakerBindResult bind(const SpeakerSystemRequest& proposed,
            const TrustedSpeakerDeviceView& device,
            std::uint64_t sourceChannelId,SpeakerBoundDraft& output) {
        const auto fail=[](SpeakerBindFault f){return SpeakerBindResult{f};};
        if(!device.exactDeviceMask||!device.acceptedPcm||
           !device.allOutputsDisarmed||device.backend!="ASIO"||
           device.deviceIdentity.empty()||device.actualEnabledPorts.empty())
            return fail(SpeakerBindFault::unverifiedDevice);
        if(!device.deviceEpoch||proposed.deviceEpoch!=device.deviceEpoch)
            return fail(SpeakerBindFault::staleDevice);
        if(!device.graphRevision||proposed.graphRevision!=device.graphRevision)
            return fail(SpeakerBindFault::staleGraph);
        if(!sourceChannelId||sourceChannelId!=device.mainChannelId)
            return fail(SpeakerBindFault::invalidSource);
        if(!std::isfinite(device.sampleRate)||device.sampleRate<8000.||
           proposed.sampleRate!=device.sampleRate||
           proposed.activeOutputChannels!=device.callbackOutputs||
           device.callbackOutputs<2||device.callbackOutputs>256)
            return fail(SpeakerBindFault::changedFormat);
        std::unordered_set<std::string> actualIds;
        std::unordered_set<int> callbackOrdinals;
        for(const auto& p:device.actualEnabledPorts){
            if(p.identity.empty()||p.identity.starts_with("DEMO-")||
               p.callbackOrdinal<0||p.callbackOrdinal>=device.callbackOutputs||
               !actualIds.insert(p.identity).second||
               !callbackOrdinals.insert(p.callbackOrdinal).second)
                return fail(SpeakerBindFault::invalidPhysicalPin);
        }
        if(static_cast<int>(callbackOrdinals.size())!=device.callbackOutputs)
            return fail(SpeakerBindFault::invalidPhysicalPin);
        for(const auto& p:proposed.outputs){
            if(!actualIds.contains(p.portIdentity)||p.portIdentity.starts_with("DEMO-"))
                return fail(SpeakerBindFault::invalidPhysicalPin);
            // The user can inspect an output draft but cannot pre-authorize
            // a physical loudspeaker by checking limiter/protection in UI.
            if(!p.mute)return fail(SpeakerBindFault::invalidLoudspeakerDraft);
        }
        SpeakerBoundDraft staged;
        staged.verifiedRequest=proposed;
        // UI pin ordinals are completely discarded, including forged
        // "verifiedPorts" that happen to use an actual display label.
        staged.verifiedRequest.verifiedPorts=device.actualEnabledPorts;
        staged.verifiedInventory.deviceEpoch=device.deviceEpoch;
        staged.verifiedInventory.acceptedGraphRevision=device.graphRevision;
        staged.verifiedInventory.mainChannelId=device.mainChannelId;
        staged.verifiedInventory.deviceIdentity=device.deviceIdentity;
        staged.verifiedInventory.sampleRate=device.sampleRate;
        staged.verifiedInventory.callbackOutputs=device.callbackOutputs;
        staged.verifiedInventory.mainL=device.mainL;
        staged.verifiedInventory.mainR=device.mainR;
        staged.verifiedInventory.monitorL=device.monitorL;
        staged.verifiedInventory.monitorR=device.monitorR;
        staged.verifiedInventory.acceptedChannelIdPcm=device.acceptedPcm;
        staged.verifiedInventory.allOutputsDisarmed=device.allOutputsDisarmed;
        staged.verifiedInventory.enabledOutputPorts=device.actualEnabledPorts;
        staged.verifiedInventory.otherOccupiedOutputOrdinals=device.otherOccupiedOrdinals;
        staged.sourceChannelId=sourceChannelId;
        // The existing M3 output-owner guard is the single authority for
        // MAIN/MONITOR/EXTERNAL and duplicate-output collision checks.
        if(!NativeSpeakerOutputReservation::preflight(
            staged.verifiedRequest,staged.verifiedInventory,
            staged.sourceChannelId).ok())
            return fail(SpeakerBindFault::invalidPhysicalPin);
        output=std::move(staged);
        return {};
    }
};
} // namespace armix::lms
