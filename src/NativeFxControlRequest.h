// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../integration/ApprovedNativeControlIntent.h"
#include <cstdint>

// Message/CONTROL-thread-only identity policy for built-in FX commands.
// EQ, Compressor, Harmonice and Vocal Throw must all validate the SAME exact
// ChannelId, device epoch, graph revision, insert lease and request envelope.
// No plugin construction, editor, callback, graph or PCM authority lives here.
namespace armix::builtin_fx {
struct NativeFxControlRequest final {
    juce::var rawChannel,rawEpoch,rawRevision,rawInstance;
    juce::String request;
    int slot=-1;
    std::uint64_t channel=0,epoch=0,revision=0;
    bool valid=false,unbound=false;

    static NativeFxControlRequest parse(const juce::var& args,int maximumSlots) {
        NativeFxControlRequest result;
        result.rawChannel=args.getProperty("channelId",{});
        result.rawEpoch=args.getProperty("deviceEpoch",{});
        result.rawRevision=args.getProperty("graphRevision",{});
        result.rawInstance=args.getProperty("instanceId",{});
        const auto rawRequest=args.getProperty("requestId",{});
        const auto rawSlot=args.getProperty("slot",{});
        const auto rawWorkspace=args.getProperty("workspaceToken",{});
        // Every built-in editor receives the same typed CONTROL envelope.
        // Never turn JSON 3.9, "3" or true into slot 3/1; nor coerce an
        // integer requestId into a string or a numeric workspace token into
        // LOGICAL_ONLY authority. Existing native callers send integer slots
        // and string request IDs, including Harmonice user preset actions.
        result.request=rawRequest.isString()?rawRequest.toString():juce::String{};
        result.slot=rawSlot.isInt()?static_cast<int>(rawSlot):-1;
        result.unbound=rawWorkspace.isString()&&rawWorkspace.toString().isNotEmpty();
        const bool workspaceTyped=rawWorkspace.isVoid()||rawWorkspace.isString();
        result.valid=args.isObject() && rawRequest.isString() && rawSlot.isInt() &&
            workspaceTyped && maximumSlots>0 &&
            armix::console::approvedNativeId(result.rawChannel,result.channel) &&
            armix::console::approvedNativeId(result.rawEpoch,result.epoch) &&
            armix::console::approvedNativeId(result.rawRevision,result.revision) &&
            result.rawInstance.isString() &&
            result.rawInstance.toString().isNotEmpty() &&
            result.slot>=0 && result.slot<maximumSlots &&
            result.request.isNotEmpty() && result.request.length()<=72;
        return result;
    }

    // Parse the host FX mailbox sequence, not a ChannelId. The existing
    // protocol admits 1..19 ASCII digits after its exact effect prefix.
    // This is exception-free and cannot overflow uint64 at <=19 digits.
    std::uint64_t sequence(const char* prefix) const noexcept {
        if(!prefix)return 0;
        const juce::String marker(prefix);
        if(!request.startsWith(marker))return 0;
        const auto digits=request.substring(marker.length());
        if(digits.isEmpty()||digits.length()>19)return 0;
        std::uint64_t value=0;
        for(auto c:digits) {
            if(c<'0'||c>'9')return 0;
            value=value*10u+static_cast<std::uint64_t>(c-'0');
        }
        return value;
    }
};
} // namespace armix::builtin_fx
