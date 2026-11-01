// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/core/DynamicAcceptedSession.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// CONTROL-THREAD ONLY: typed integration seam between the already-accepted
// native ChannelId graph and the future Console V6 UI. Never a second DSP,
// a second channel roster, a producer of physical pin truth, or a callback.
// The caller still needs the actual JUCE callback admission/capabilities.
namespace armix::integration {
using armix::mixer::ChannelId;
using armix::mixer::ChannelRole;
using armix::mixer::ProcessingFormat;
using armix::mixer::DynamicAcceptedSession;

struct ConsoleIntentV6 final {
    enum class Operation : std::uint8_t {
        patchInput, patchOutput, addChannel, groupSelected, addFxReturn,
        setInsert, setSend, setMix, recallShow
    };
    std::uint64_t requestId=0; // monotonic per live connection; never a strip index
    std::uint64_t deviceEpoch=0,baseGraphRevision=0,baseRosterRevision=0;
    Operation operation=Operation::patchInput;
    ChannelId target{};
};

enum class IntentFaultV6 : std::uint8_t {
    none, invalidRequest, duplicateOrOldRequest, staleDevice,
    staleGraph, staleRoster
};

// This is a preflight ONLY. The real JUCE control writer applies the native
// intent and returns ACK. lastCompletedId comes from that writer: do not
// mutate it here, or attempt to ACK a draft UI edit.
inline IntentFaultV6 validateConsoleIntent(
    const ConsoleIntentV6& intent,std::uint64_t actualDeviceEpoch,
    std::uint64_t lastCompletedId,const DynamicAcceptedSession& accepted) noexcept {
    if(!intent.requestId)return IntentFaultV6::invalidRequest;
    if(intent.requestId<=lastCompletedId)return IntentFaultV6::duplicateOrOldRequest;
    if(!actualDeviceEpoch || intent.deviceEpoch!=actualDeviceEpoch)
        return IntentFaultV6::staleDevice;
    if(intent.baseGraphRevision!=accepted.acceptedGraphRevision())
        return IntentFaultV6::staleGraph;
    if(intent.baseRosterRevision!=accepted.acceptedRosterRevision())
        return IntentFaultV6::staleRoster;
    return IntentFaultV6::none;
}

struct ConsoleAckV6 final {
    enum class Status : std::uint8_t { acceptedGraph, rejected };
    std::uint64_t requestId=0,deviceEpoch=0,graphRevision=0,rosterRevision=0;
    Status status=Status::rejected;
    IntentFaultV6 preflight=IntentFaultV6::none;
    DynamicAcceptedSession::Fault nativeFault=DynamicAcceptedSession::Fault::none;
    // ACCEPTED GRAPH != audible LIVE. This bit must only be set by the
    // actual installed JUCE production callback authority, never by a
    // test fixture or a browser-approved route.
    bool productionCallbackLive=false;
};

inline ConsoleAckV6 consoleAckFromNative(
    const ConsoleIntentV6& intent,IntentFaultV6 preflight,
    const DynamicAcceptedSession& accepted,
    const DynamicAcceptedSession::Result* nativeResult=nullptr) noexcept {
    ConsoleAckV6 ack;
    ack.requestId=intent.requestId;ack.deviceEpoch=intent.deviceEpoch;
    ack.graphRevision=accepted.acceptedGraphRevision();
    ack.rosterRevision=accepted.acceptedRosterRevision();
    ack.preflight=preflight;
    if(nativeResult)ack.nativeFault=nativeResult->fault;
    if(preflight==IntentFaultV6::none && nativeResult && nativeResult->ok() &&
       nativeResult->graphRevision==ack.graphRevision &&
       nativeResult->rosterRevision==ack.rosterRevision)
        ack.status=ConsoleAckV6::Status::acceptedGraph;
    return ack; // Never claim productionCallbackLive here.
}

struct ConsoleSendV6 final {
    std::string destinationId; // Decimal string avoids JS Number precision loss.
    float gain=0.f,pan=0.f;
    bool preFader=false;
};

struct ConsoleChannelV6 final {
    std::string id;
    ChannelRole role=ChannelRole::input;
    ProcessingFormat format=ProcessingFormat::mono;
    std::string name;
    std::string primaryOutId;
    std::vector<ConsoleSendV6> sends;
    std::size_t primaryInputCount=0,parallelSendInputCount=0;
    // A GUI must NOT synthesize a physical IN1 from a logical strip number.
    // The actual accepted callback pin map is a separate native field that
    // only the device/graph owner can bind after an epoch-checked ACK.
    bool sourcePinVerified=false,actualPcmPresent=false;
};

// Runtime permissions, not "the offline graph can render" feature claims.
struct ProductionCapabilitiesV6 final {
    bool callbackOwnsChannelIds=false,fullVendorFxMigration=false;
    bool showAndOutputParity=false,physicalCaptureMapped=false;
    bool addChannel=false,group=false,fxReturn=false,monitor=false;
    bool universalLive() const noexcept {
        return callbackOwnsChannelIds && fullVendorFxMigration &&
               showAndOutputParity && physicalCaptureMapped;
    }
};

struct ConsoleAcceptedReadbackV6 final {
    static constexpr std::uint32_t schemaVersion=6;
    std::uint64_t deviceEpoch=0,graphRevision=0,rosterRevision=0;
    std::vector<ConsoleChannelV6> channels;
    std::string mainId,monitorId;
    ProductionCapabilitiesV6 capabilities{};
    bool productionCallbackLive=false;
};

// ONLY consume the accepted native graph from the exact graphRevision and
// epoch. Do not accept a separate draft route, a UI array of 10 channel IDs,
// or a browser-created Group/FX Return. On rejection, preserve caller's prior
// readback unmodified. This readback deliberately never certifies hardware.
inline bool projectAcceptedConsoleV6(
    const DynamicAcceptedSession& native,std::uint64_t deviceEpoch,
    std::uint64_t exactGraphRevision,
    ConsoleAcceptedReadbackV6& destination) {
    if(!deviceEpoch || !exactGraphRevision ||
       native.acceptedGraphRevision()!=exactGraphRevision)return false;
    const auto* view=native.acceptedRouting(deviceEpoch,exactGraphRevision);
    if(!view || view->rosterRevision!=native.acceptedRosterRevision() ||
       view->channels.size()!=native.acceptedRoster().size())return false;
    ConsoleAcceptedReadbackV6 next;
    next.deviceEpoch=deviceEpoch;
    next.graphRevision=exactGraphRevision;
    next.rosterRevision=view->rosterRevision;
    next.mainId=std::to_string(view->main.value);
    next.monitorId=view->monitor.value?std::to_string(view->monitor.value):std::string{};
    next.channels.reserve(view->channels.size());
    for(const auto& row:view->channels) {
        if(!row.id.value || !native.acceptedRoster().find(row.id))return false;
        ConsoleChannelV6 ch;
        ch.id=std::to_string(row.id.value);
        ch.role=row.role;ch.format=row.format;ch.name=row.name;
        if(row.primaryOutput.value)
            ch.primaryOutId=std::to_string(row.primaryOutput.value);
        ch.primaryInputCount=row.primarySources.size();
        ch.parallelSendInputCount=row.parallelSendSources.size();
        ch.sends.reserve(row.parallelSends.size());
        for(const auto& send:row.parallelSends)
            ch.sends.push_back({std::to_string(send.destination.value),
                                send.gain,send.pan,send.preFader});
        next.channels.push_back(std::move(ch));
    }
    destination=std::move(next);
    return true;
}
} // namespace armix::integration
