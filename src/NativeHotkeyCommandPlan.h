// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeHotkeyRegistry.h"
#include <cstdint>

// MV-13: CONTROL-thread-only decision boundary for native hotkey commands.
// The JUCE ConsoleWindow owns one dispatcher and one authoritative snapshot;
// this pure planner never registers OS chords, mutates a graph or touches PCM.
namespace armix::hotkey {
class NativeHotkeyCommandPlan final {
public:
    enum class Kind { ignore, parameter, action, intent };
    struct Dispatch final {Kind kind=Kind::ignore; juce::var request;};

    // Shared registration/firing role and ChannelId check. All IDs remain
    // opaque uint64 decimal strings (never JS doubles or UI strip indices).
    static juce::var admittedChannel(const juce::var& graph,
                                     const juce::String& channelId) {
        if (!static_cast<bool>(graph.getProperty("capabilities",{})
                                   .getProperty("callbackOwnsChannelIds",false)))
            return {};
        const auto rows=graph.getProperty("channels",{});
        if (!rows.isArray())return {};
        for (const auto& row:*rows.getArray()) {
            const auto role=row.getProperty("role","").toString();
            if (row.getProperty("id",{}).isString() &&
                row.getProperty("id","").toString()==channelId &&
                (role=="INPUT"||role=="GROUP"||role=="FX_RETURN"))
                return row;
        }
        return {};
    }

    static bool exactFxSlot(const juce::var& row,int slot,const juce::String& insertId) {
        const auto inserts=row.getProperty("inserts",{});
        return inserts.isArray() && slot>=0 && slot<inserts.getArray()->size() &&
            insertId.isNotEmpty() &&
            inserts.getArray()->getReference(slot).getProperty("id",{}).isString() &&
            inserts.getArray()->getReference(slot).getProperty("id","").toString()==insertId;
    }

    // Fail closed before any registry persistence or native OS registration.
    // ParamID admission remains owned by the existing VST3 parameter service.
    static juce::String registrationError(const juce::var& supplied,
                                           const juce::var& accepted) {
        const auto graph=accepted.getProperty("graph",{});
        const auto row=admittedChannel(graph,
                           supplied.getProperty("channelId","").toString());
        if (!row.isObject())return "HOTKEY_CHANNEL_NOT_ADMITTED";
        const auto operation=supplied.getProperty("operation","").toString();
        const bool rack=operation=="SET_FX_RACK_BYPASS";
        const bool slot=operation=="SET_FX_SLOT_BYPASS";
        const bool param=operation.startsWith("SET_FX_PARAM_");
        if (rack && !row.getProperty("fxRackBypassed",{}).isBool())
            return "HOTKEY_FX_INSTANCE_NOT_ADMITTED";
        if (slot) {
            const auto index=supplied.getProperty("slot",{});
            const auto token=supplied.getProperty("insertId",{});
            if (!index.isInt() || !token.isString() ||
                !exactFxSlot(row,static_cast<int>(index),token.toString()))
                return "HOTKEY_FX_INSTANCE_NOT_ADMITTED";
        }
        if (!rack && !slot && !param) {
            if (operation!="SET_MUTE" && operation!="SET_SOLO")
                return "HOTKEY_ACTION_UNSUPPORTED";
            const auto ops=graph.getProperty("capabilities",{})
                                .getProperty("supportedOps",{});
            if (!ops.isArray() || !ops.getArray()->contains(juce::var(operation)))
                return "HOTKEY_ACTION_NOT_ADMITTED";
        }
        return {};
    }

    // Plan only. ConsoleWindow's current onAction / handleApprovedNativeControl
    // remain the SINGLE transport and enforcement points, including ACK and
    // accepted post-ACK readback. No hotkey-specific graph mutation path.
    static Dispatch dispatch(const Mapping& mapping,const juce::var& accepted,
                             const juce::String& sessionId,bool hostReady,
                             std::uint64_t requestSerial) {
        if (!hostReady || mapping.scope!="SESSION" ||
            mapping.sessionId!=sessionId || requestSerial==0 ||
            !Registry::valid(mapping))
            return {};
        const auto graph=accepted.getProperty("graph",{});
        const auto row=admittedChannel(graph,mapping.channelId);
        if (!row.isObject())return {};
        if (mapping.operation.startsWith("SET_FX_PARAM_"))
            return {Kind::parameter,{}};
        const bool rack=mapping.operation=="SET_FX_RACK_BYPASS";
        const bool slot=mapping.operation=="SET_FX_SLOT_BYPASS";
        if (rack||slot) {
            if (slot&&!exactFxSlot(row,mapping.slot,mapping.insertId))return {};
            const auto bypass=rack?row.getProperty("fxRackBypassed",{}):
                row.getProperty("inserts",{}).getArray()->getReference(mapping.slot)
                   .getProperty("bypassed",{});
            if (!bypass.isBool())return {};
            auto out=juce::var(new juce::DynamicObject());
            auto* target=out.getDynamicObject();
            target->setProperty("action",rack?"routingNativeFxRackBypass":
                                                   "routingNativeFxInsertBypass");
            target->setProperty("requestId","hotkey-fx-"+
                juce::String(static_cast<juce::int64>(requestSerial)));
            target->setProperty("channelId",mapping.channelId);
            target->setProperty("deviceEpoch",accepted.getProperty("device",{})
                                                 .getProperty("epoch","0"));
            target->setProperty("revision",graph.getProperty("revision","0"));
            target->setProperty("value",!static_cast<bool>(bypass));
            if (slot) {
                target->setProperty("slot",mapping.slot);
                target->setProperty("insertId",mapping.insertId);
            }
            return {Kind::action,out};
        }
        if (mapping.operation!="SET_MUTE" && mapping.operation!="SET_SOLO")return {};
        const auto current=row.getProperty("controls",{}).getProperty(
            mapping.operation=="SET_MUTE"?"mute":"solo",{});
        if (!current.isBool())return {};
        auto value=juce::var(new juce::DynamicObject());
        value.getDynamicObject()->setProperty("value",!static_cast<bool>(current));
        auto out=juce::var(new juce::DynamicObject());
        auto* target=out.getDynamicObject();
        target->setProperty("requestId","hotkey-"+
            juce::String(static_cast<juce::int64>(requestSerial)));
        target->setProperty("operation",mapping.operation);
        target->setProperty("targetChannelId",mapping.channelId);
        target->setProperty("deviceEpoch",accepted.getProperty("device",{})
                                             .getProperty("epoch","0"));
        target->setProperty("baseGraphRevision",graph.getProperty("revision","0"));
        target->setProperty("baseRosterRevision",graph.getProperty("rosterRevision","0"));
        target->setProperty("payload",value);
        return {Kind::intent,out};
    }
};
} // namespace armix::hotkey
