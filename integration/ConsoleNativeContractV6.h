// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/core/DynamicAcceptedSession.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
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
    std::string requestId; // Opaque per-WebView-session token, echoed exactly in ACK.
    std::uint64_t deviceEpoch=0,baseGraphRevision=0,baseRosterRevision=0;
    Operation operation=Operation::patchInput;
    ChannelId target{};
};

enum class IntentFaultV6 : std::uint8_t {
    none, invalidRequest, duplicateOrOldRequest, staleDevice,
    staleGraph, staleRoster
};

// The GUI sends an opaque string ('gui...:sequence'). Numeric <= comparison
// between different GUI incarnations is INVALID; an old editor reconnect can
// legally send a lower sequence. The existing native control writer owns a
// bounded recently-completed request-ID cache and supplies a VIEW of it here.
// This function does not retain IDs, emit ACK, create DSP or change the graph.
// Reject malformed IDs before they enter native diagnostic logs.
inline bool validConsoleRequestId(std::string_view id) noexcept {
    if(id.empty() || id.size()>128)return false;
    return std::all_of(id.begin(),id.end(),[](unsigned char ch) noexcept {
        return ch>=0x21 && ch<=0x7e;
    });
}
inline IntentFaultV6 validateConsoleIntent(
    const ConsoleIntentV6& intent,std::uint64_t actualDeviceEpoch,
    std::span<const std::string_view> completedRequestIds,
    const DynamicAcceptedSession& accepted) noexcept {
    if(!validConsoleRequestId(intent.requestId))
        return IntentFaultV6::invalidRequest;
    if(std::find(completedRequestIds.begin(),completedRequestIds.end(),
                 std::string_view(intent.requestId))!=completedRequestIds.end())
        return IntentFaultV6::duplicateOrOldRequest;
    if(!actualDeviceEpoch || intent.deviceEpoch!=actualDeviceEpoch)
        return IntentFaultV6::staleDevice;
    if(intent.baseGraphRevision!=accepted.acceptedGraphRevision())
        return IntentFaultV6::staleGraph;
    if(intent.baseRosterRevision!=accepted.acceptedRosterRevision())
        return IntentFaultV6::staleRoster;
    return IntentFaultV6::none;
}

// ChannelId is a native uint64 but a JSON string, *never* JavaScript Number.
// Reject malformed/overflowed/noncanonical identities instead of sending an
// intent to a guessed strip ordinal. Caller verifies this ID exists in the
// exact accepted native graph before authorizing a command.
inline bool parseNativeChannelId(std::string_view token,ChannelId& out) noexcept {
    if(token.empty() || (token.size()>1 && token.front()=='0'))return false;
    std::uint64_t value=0;
    for(const char ch:token) {
        if(ch<'0'||ch>'9')return false;
        const auto digit=static_cast<std::uint64_t>(ch-'0');
        if(value>(std::numeric_limits<std::uint64_t>::max()-digit)/10u)
            return false;
        value=value*10u+digit;
    }
    if(value==0)return false; // Zero is the explicit unconnected OUT sentinel.
    out=ChannelId{value};return true;
}


struct ConsoleAckV6 final {
    enum class Status : std::uint8_t { acceptedGraph, rejected };
    std::string requestId; // Must match user's opaque intent byte-for-byte.
    std::uint64_t deviceEpoch=0,graphRevision=0,rosterRevision=0;
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
    const DynamicAcceptedSession::Result* nativeResult=nullptr) {
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
inline std::string_view consoleWireAckStatus(const ConsoleAckV6& ack) noexcept {
    return ack.status==ConsoleAckV6::Status::acceptedGraph
        ?std::string_view{"ACCEPTED"}:std::string_view{"REJECTED"};
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
    // GUI v6 uses canonical decimal-string uint64 revisions and BigInt.
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

inline std::string_view consoleWireRole(ChannelRole role) noexcept {
    switch(role) {
        case ChannelRole::input: return "INPUT";
        case ChannelRole::group: return "GROUP";
        case ChannelRole::fxReturn: return "FX_RETURN";
        case ChannelRole::monitor: return "MONITOR";
        case ChannelRole::main: return "MAIN";
    }
    return "UNKNOWN";
}
struct ConsoleWireChannelV6 final {
    std::string id,role,processingFormat,name,primaryOutId;
    std::vector<ConsoleSendV6> sends;
    std::size_t primaryInputCount=0,parallelSendInputCount=0;
    bool sourcePinVerified=false,actualPcmPresent=false;
};
struct ConsoleWireReadbackV6 final {
    std::uint32_t schemaVersion=6;
    std::string deviceEpoch,graphRevision,rosterRevision,mainId,monitorId;
    std::vector<ConsoleWireChannelV6> channels;
    ProductionCapabilitiesV6 capabilities{};
    bool productionCallbackLive=false;
};
// Control-only serialization model, NOT a device, JSON emitter or audio renderer.
inline ConsoleWireReadbackV6 toConsoleWireReadbackV6(
    const ConsoleAcceptedReadbackV6& a) {
    ConsoleWireReadbackV6 out;
    out.deviceEpoch=std::to_string(a.deviceEpoch);
    out.graphRevision=std::to_string(a.graphRevision);
    out.rosterRevision=std::to_string(a.rosterRevision);
    out.mainId=a.mainId;out.monitorId=a.monitorId;
    out.capabilities=a.capabilities;out.productionCallbackLive=a.productionCallbackLive;
    out.channels.reserve(a.channels.size());
    for(const auto& c:a.channels) {
        ConsoleWireChannelV6 ch;
        ch.id=c.id;ch.role=std::string(consoleWireRole(c.role));
        ch.processingFormat=c.format==ProcessingFormat::mono?"MONO":"STEREO";
        ch.name=c.name;ch.primaryOutId=c.primaryOutId;ch.sends=c.sends;
        ch.primaryInputCount=c.primaryInputCount;
        ch.parallelSendInputCount=c.parallelSendInputCount;
        ch.sourcePinVerified=c.sourcePinVerified;
        ch.actualPcmPresent=c.actualPcmPresent;
        out.channels.push_back(std::move(ch));
    }
    return out;
}
} // namespace armix::integration
