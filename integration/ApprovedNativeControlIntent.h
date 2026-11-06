// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ApprovedNativeV8Projection.h"
#include "../engine/core/DynamicChannelRoster.h"
#include <charconv>
#include <system_error>
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <span>
#include <vector>

// INTEGRATOR M3-B: bridge the approved /preview/ control ABI to the ONE
// native ChannelId PCM owner from ENGINE #266. Translation is CONTROL-thread
// only and has NO side effects. It does not install, drive or claim audio.
// Real JUCE nativeBus/callback admission and genuine mutating operation
// support must be independently established by the producer before use.
//
// The public UI submits SET_FADER/PAN/MUTE/SOLO; native PCM currently uses
// NATIVE_SET_CHANNEL_MIX / NATIVE_SET_CHANNEL_MUTE_SOLO / NATIVE_SET_BUS_MIX.
// This translator NEVER fabricates capability support or stable ChannelIds.
namespace armix::console {
struct ApprovedNativeControlIntent {
    juce::var translated;
    juce::String error;
    bool ok() const noexcept { return translated.isObject() && error.isEmpty(); }
};

inline bool approvedHasOperation(const juce::var& list,const char* name) {
    const auto* values=list.getArray();
    return values && values->contains(juce::var(juce::String(name)));
}

inline ApprovedNativeControlIntent translateApprovedNativeControl(
    const juce::var& publicIntent,
    const juce::var& acceptedSnapshot,
    const juce::var& installedBus) {
    const auto reject=[](const char* code)->ApprovedNativeControlIntent {
        return {{},juce::String(code)};
    };
    if(!publicIntent.isObject() || !acceptedSnapshot.isObject() ||
       !installedBus.isObject())return reject("NATIVE_READBACK_UNAVAILABLE");
    const auto device=acceptedSnapshot.getProperty("device",{});
    const auto graph=acceptedSnapshot.getProperty("graph",{});
    const auto caps=graph.getProperty("capabilities",{});
    const auto rows=graph.getProperty("channels",{});
    const auto nativeRows=installedBus.getProperty("channels",{});
    const auto& ids=publicIntent.getProperty("targetChannelId",{});
    const auto& request=publicIntent.getProperty("requestId",{});
    const auto op=publicIntent.getProperty("operation",{}).toString();
    const auto epoch=device.getProperty("epoch",{});
    const auto revision=graph.getProperty("revision",{});
    const auto roster=graph.getProperty("rosterRevision",{});
    if(!device.isObject() || !graph.isObject() || !caps.isObject() ||
       !rows.isArray() || !nativeRows.isArray() ||
       !static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",false)) ||
       !nativeV8Decimal(epoch) || epoch.toString()=="0" ||
       !nativeV8Decimal(revision) || revision.toString()=="0" ||
       !nativeV8Decimal(roster) || roster.toString()=="0" ||
       !nativeV8Decimal(ids) || ids.toString()=="0")
        return reject("CHANNELID_CALLBACK_NOT_ADMITTED");
    // Reject cross-device / cross-graph commands BEFORE any conversion.
    if(publicIntent.getProperty("deviceEpoch",{}).toString()!=epoch.toString() ||
       publicIntent.getProperty("baseGraphRevision",{}).toString()!=revision.toString() ||
       publicIntent.getProperty("baseRosterRevision",{}).toString()!=roster.toString() ||
       installedBus.getProperty("graphRevision",{}).toString()!=revision.toString() ||
       installedBus.getProperty("rosterRevision",{}).toString()!=roster.toString())
        return reject("STALE_NATIVE_CONTROL_REVISION");
    if(!request.isString() || request.toString().isEmpty() ||
       request.toString().length()>128)return reject("INVALID_REQUEST_ID");
    // P1 WIRE: the previous GUI emitted APPLY_WIRE_TRANSACTION, but this
    // translator accepted only fader/pan/mute/solo and silently left native
    // BUS routing unavailable. Validate the exact accepted ChannelId/EdgeId
    // inventory and public+native capabilities BEFORE compiling any changes.
    if(op=="APPLY_WIRE_TRANSACTION") {
        if(!approvedHasOperation(caps.getProperty("supportedOps",{}),
                                 "APPLY_WIRE_TRANSACTION") ||
           !approvedHasOperation(installedBus.getProperty("supportedOps",{}),
                                 "NATIVE_APPLY_WIRE_TRANSACTION"))
            return reject("NATIVE_WIRE_NOT_ADMITTED");
        juce::var owner,producer;
        for(const auto& row:*rows.getArray())
            if(row.getProperty("id","").toString()==ids.toString())owner=row;
        for(const auto& row:*nativeRows.getArray())
            if(row.getProperty("id","").toString()==ids.toString())producer=row;
        if(!owner.isObject()||!producer.isObject()||
           owner.getProperty("role","").toString()!=
           producer.getProperty("role","").toString())
            return reject("WIRE_SOURCE_NOT_ADMITTED");
        const auto sourceRole=owner.getProperty("role","").toString();
        if(sourceRole!="INPUT"&&sourceRole!="GROUP"&&sourceRole!="FX_RETURN")
            return reject("WIRE_INVALID_SOURCE_ROLE");
        const auto sourcePayload=publicIntent.getProperty("payload",{});
        const auto rawChanges=sourcePayload.getProperty("changes",{});
        const auto* changes=rawChanges.getArray();
        if(!changes||changes->isEmpty()||changes->size()>64)
            return reject("WIRE_INVALID_BATCH");
        const auto pubEdges=graph.getProperty("edges",{});
        const auto nativeEdges=installedBus.getProperty("edges",{});
        if(!pubEdges.isArray()||!nativeEdges.isArray())
            return reject("WIRE_EDGE_INVENTORY_UNAVAILABLE");
        juce::Array<juce::var> verified;
        for(const auto& edit:*changes) {
            if(!edit.isObject())return reject("WIRE_INVALID_EDIT");
            const auto kind=edit.getProperty("op","").toString();
            const auto from=edit.getProperty("from",{});
            if(from.toString()!=ids.toString()||
               (kind!="CONNECT" && kind!="REMOVE" &&
                kind!="RETARGET" && kind!="ROUTE_ONLY"))
                return reject("WIRE_SOURCE_OR_OPERATION_INVALID");
            auto item=std::make_unique<juce::DynamicObject>();
            item->setProperty("op",kind);
            item->setProperty("from",ids);
            if(kind=="REMOVE" || kind=="RETARGET") {
                const auto edgeId=edit.getProperty("edgeId",{});
                if(!nativeV8Decimal(edgeId))return reject("WIRE_EDGE_ID_INVALID");
                bool pubFound=false,nativeFound=false;
                for(const auto& e:*pubEdges.getArray())
                    if(e.getProperty("edgeId","").toString()==edgeId.toString() &&
                       e.getProperty("from","").toString()==ids.toString())
                        pubFound=true;
                for(const auto& e:*nativeEdges.getArray())
                    if(e.getProperty("edgeId","").toString()==edgeId.toString() &&
                       e.getProperty("from","").toString()==ids.toString())
                        nativeFound=true;
                if(!pubFound||!nativeFound)return reject("WIRE_STALE_EDGE");
                item->setProperty("edgeId",edgeId);
            }
            if(kind=="CONNECT" || kind=="RETARGET" || kind=="ROUTE_ONLY") {
                const auto to=edit.getProperty("to",{});
                if(!nativeV8Decimal(to) || to.toString()==ids.toString())
                    return reject("WIRE_INVALID_DESTINATION");
                juce::String role;
                bool nativeFound=false;
                for(const auto& row:*rows.getArray())
                    if(row.getProperty("id","").toString()==to.toString())
                        role=row.getProperty("role","").toString();
                for(const auto& row:*nativeRows.getArray())
                    if(row.getProperty("id","").toString()==to.toString())
                        nativeFound=true;
                if(!nativeFound ||
                   (role!="GROUP"&&role!="FX_RETURN"&&role!="MAIN"))
                    return reject("WIRE_UNSUPPORTED_DESTINATION");
                // MAIN is a supported ROUTE_ONLY target: the roster makes
                // exactly one serial unity-gain primary edge, no double dry.

                item->setProperty("to",to);
            }
            if(kind=="CONNECT" || kind=="ROUTE_ONLY") {
                double gain=0.;
                if(edit.getProperty("tap","").toString()!="POST_FADER" ||
                   !nativeV8Number(edit.getProperty("gainLinear",{}),gain) ||
                   gain<0.||gain>4.||
                   (kind=="ROUTE_ONLY"&&gain!=1.))
                    return reject("WIRE_UNSUPPORTED_TAP_OR_GAIN");
                item->setProperty("gainLinear",gain);
            }
            verified.add(juce::var(item.release()));
        }
        auto payload=std::make_unique<juce::DynamicObject>();
        payload->setProperty("changes",juce::var(verified));
        auto converted=std::make_unique<juce::DynamicObject>();
        converted->setProperty("requestId",request);
        converted->setProperty("deviceEpoch",epoch);
        converted->setProperty("baseGraphRevision",revision);
        converted->setProperty("baseRosterRevision",roster);
        converted->setProperty("targetChannelId",ids);
        converted->setProperty("operation","NATIVE_APPLY_WIRE_TRANSACTION");
        converted->setProperty("payload",juce::var(payload.release()));
        return {juce::var(converted.release()),{}};
    }
    if(op=="LOAD_INSERT") {
        // Native-accepted ChannelId + catalog identity + exact next slot.
        // Device/graph/roster were already independently compared above.
        if(!approvedHasOperation(caps.getProperty("supportedOps",{}),"LOAD_INSERT") ||
           !approvedHasOperation(installedBus.getProperty("supportedOps",{}),
                                 "NATIVE_LOAD_INSERT"))
            return reject("NATIVE_VST3_NOT_ADMITTED");
        juce::var ch,nativeCh;
        for(const auto& row:*rows.getArray())
            if(row.getProperty("id","").toString()==ids.toString())ch=row;
        for(const auto& row:*nativeRows.getArray())
            if(row.getProperty("id","").toString()==ids.toString())nativeCh=row;
        const auto role=ch.getProperty("role","").toString();
        if(!ch.isObject()||!nativeCh.isObject()||
           role!=nativeCh.getProperty("role","").toString()||
           (role!="INPUT"&&role!="GROUP"))
            return reject("NATIVE_VST3_TARGET_NOT_ADMITTED");
        const auto payload=publicIntent.getProperty("payload",{});
        const auto catalog=payload.getProperty("catalogId",{});
        const auto slot=payload.getProperty("slot",{});
        const auto publicFx=ch.getProperty("inserts",{});
        const auto nativeFx=nativeCh.getProperty("inserts",{});
        if(!payload.isObject()||!catalog.isString()||
           catalog.toString().isEmpty()||
           catalog.toString().getNumBytesAsUTF8()>256||
           !slot.isInt()||static_cast<int>(slot)<0||
           static_cast<int>(slot)>=8||
           !publicFx.isArray()||!nativeFx.isArray()||
           publicFx.getArray()->size()!=static_cast<int>(slot)||
           nativeFx.getArray()->size()!=static_cast<int>(slot))
            return reject("NATIVE_VST3_SLOT_OR_CATALOG_INVALID");
        auto args=std::make_unique<juce::DynamicObject>();
        args->setProperty("catalogId",catalog);
        args->setProperty("slot",slot);
        auto converted=std::make_unique<juce::DynamicObject>();
        converted->setProperty("requestId",request);
        converted->setProperty("deviceEpoch",epoch);
        converted->setProperty("baseGraphRevision",revision);
        converted->setProperty("baseRosterRevision",roster);
        converted->setProperty("targetChannelId",ids);
        converted->setProperty("operation","NATIVE_LOAD_INSERT");
        converted->setProperty("payload",juce::var(args.release()));
        return {juce::var(converted.release()),{}};
    }
    if(op!="SET_FADER" && op!="SET_PAN" && op!="SET_MUTE" &&
       op!="SET_SOLO")return reject("UNSUPPORTED_APPROVED_CONTROL");
    // Both native *and* GUI-side capabilities must be explicitly advertised
    // from real installed PCM. Native ops alone do not prove that legacy M4
    // ChannelIds (especially original CH1-10) support these controls.
    if(!approvedHasOperation(caps.getProperty("supportedOps",{}),
                             op.toRawUTF8()))
        return reject("PUBLIC_CONTROL_NOT_ADMITTED");
    juce::var channel,producer;
    for(const auto& item:*rows.getArray())
        if(item.getProperty("id","").toString()==ids.toString()) {
            if(channel.isObject())return reject("DUPLICATE_APPROVED_CHANNEL");
            channel=item;
        }
    for(const auto& item:*nativeRows.getArray())
        if(item.getProperty("id","").toString()==ids.toString()) {
            if(producer.isObject())return reject("DUPLICATE_NATIVE_CHANNEL");
            producer=item;
        }
    if(!channel.isObject() || !producer.isObject())
        return reject("UNKNOWN_OR_STALE_CHANNELID");
    const auto role=channel.getProperty("role","").toString();
    if(role!=producer.getProperty("role","").toString())
        return reject("NATIVE_ROLE_MISMATCH");
    const bool input=role=="INPUT";
    const bool bus=role=="GROUP" || role=="FX_RETURN";
    if(!input && !bus)return reject("CHANNEL_ROLE_NOT_MUTABLE");
    // A native GROUP has exactly the same ChannelId mute/solo atomics as
    // an INPUT. Keep the legacy AUX distinction in the engine role gate.
    const auto controls=channel.getProperty("controls",{});
    if(!controls.isObject())return reject("INVALID_NATIVE_CONTROLS");
    const auto fader=controls.getProperty("faderDb",{});
    const auto pan=controls.getProperty("pan",{});
    const auto mute=controls.getProperty("mute",{});
    const auto solo=controls.getProperty("solo",{});
    double acceptedDb=0.,acceptedPan=0.,nativeGain=0.,nativePan=0.;
    if(!nativeV8Number(fader,acceptedDb) ||
       !nativeV8Number(pan,acceptedPan) ||
       !nativeV8Number(producer.getProperty("fader",{}),nativeGain) ||
       !nativeV8Number(producer.getProperty("pan",{}),nativePan) ||
       !mute.isBool() || !solo.isBool() ||
       !producer.getProperty("mute",{}).isBool() ||
       !producer.getProperty("solo",{}).isBool() ||
       nativeGain<0. || nativeGain>4. ||
       std::abs(nativePan-acceptedPan)>0.015 ||
       static_cast<bool>(mute)!=static_cast<bool>(
           producer.getProperty("mute",{})) ||
       static_cast<bool>(solo)!=static_cast<bool>(
           producer.getProperty("solo",{})))
        return reject("NATIVE_CONTROL_READBACK_MISMATCH");
    const double nativeDb=nativeGain>0.?20.*std::log10(nativeGain):-60.;
    if(std::abs(nativeDb-acceptedDb)>0.11)
        return reject("STALE_NATIVE_CONTROL_READBACK");
    const auto payload=publicIntent.getProperty("payload",{});
    if(!payload.isObject())return reject("INVALID_CONTROL_PAYLOAD");
    const auto value=payload.getProperty(
        op=="SET_FADER"?"valueDb":"value",{});
    const char* nativeOperation=nullptr;
    auto translatedPayload=std::make_unique<juce::DynamicObject>();
    if(op=="SET_FADER" || op=="SET_PAN") {
        if(op=="SET_FADER") {
            double requested=0.;
            if(!nativeV8Number(value,requested) ||
               requested< -60. || requested>(bus?12.:10.))
                return reject("INVALID_FADER_VALUE");
            acceptedDb=requested;
        } else {
            double requested=0.;
            if(!nativeV8Number(value,requested) ||
               requested< -1. || requested>1.)
                return reject("INVALID_PAN_VALUE");
            acceptedPan=requested;
        }
        // PAN must not round-trip the companion fader through a clipped dB
        // projection: an accepted -60 dB can represent 0.001 linear, not
        // silence, and an upper-clamped +12 dB can represent gain 4.0.
        // Keep the verified native linear gain byte-for-byte on PAN edits.
        // Only an explicit SET_FADER request converts dB into new gain.
        const double gain=op=="SET_PAN"?nativeGain:
            (acceptedDb<= -60.?0.:std::pow(10.,acceptedDb/20.));
        if(!std::isfinite(gain) || gain>4.)
            return reject("UNREPRESENTABLE_GAIN");
        nativeOperation=bus?"NATIVE_SET_BUS_MIX":"NATIVE_SET_CHANNEL_MIX";
        translatedPayload->setProperty("gain",gain);
        translatedPayload->setProperty("pan",acceptedPan);
    } else {
        if(!value.isBool())return reject("INVALID_MUTE_SOLO_VALUE");
        nativeOperation=bus?"NATIVE_SET_BUS_MUTE_SOLO":
                            "NATIVE_SET_CHANNEL_MUTE_SOLO";
        translatedPayload->setProperty("mute",op=="SET_MUTE"?value:mute);
        translatedPayload->setProperty("solo",op=="SET_SOLO"?value:solo);
    }
    if(!approvedHasOperation(installedBus.getProperty("supportedOps",{}),
                             nativeOperation))
        return reject("NATIVE_PCM_OPERATION_NOT_ADMITTED");
    auto converted=std::make_unique<juce::DynamicObject>();
    converted->setProperty("requestId",request);
    converted->setProperty("deviceEpoch",epoch);
    converted->setProperty("baseGraphRevision",revision);
    converted->setProperty("baseRosterRevision",roster);
    converted->setProperty("targetChannelId",ids);
    converted->setProperty("operation",nativeOperation);
    converted->setProperty("payload",juce::var(translatedPayload.release()));
    return {juce::var(converted.release()),{}};
}

inline bool approvedNativeId(const juce::var& value,std::uint64_t& result) {
    if(!value.isString()||!nativeV8Decimal(value))return false;
    const auto bytes=value.toString().toStdString();
    std::uint64_t decoded=0;
    const auto parsed=std::from_chars(bytes.data(),bytes.data()+bytes.size(),decoded);
    if(parsed.ec!=std::errc{} ||
       parsed.ptr!=bytes.data()+bytes.size() || decoded==0)
        return false;
    result=decoded;
    return true;
}

// The only PCM mutation route for the approved Pages WebView controller.
// This does NOT own a callback, plugin instance, graph or UI state.
// The installed ENGINE source must accept the exact epoch/revision/ChannelId.
// No fallback to ordinal-only M4 or synthetic JS/DSP is permitted.
template<class Engine>
inline bool executeApprovedNativeControl(Engine& engine,
                                         const juce::var& translated) {
    if(!translated.isObject())return false;
    std::uint64_t epoch=0,revision=0,roster=0,id=0;
    if(!approvedNativeId(translated.getProperty("deviceEpoch",{}),epoch) ||
       !approvedNativeId(translated.getProperty("baseGraphRevision",{}),revision) ||
       !approvedNativeId(translated.getProperty("baseRosterRevision",{}),roster) ||
       !approvedNativeId(translated.getProperty("targetChannelId",{}),id))
        return false;
    const auto payload=translated.getProperty("payload",{});
    if(!payload.isObject())return false;
    const auto operation=translated.getProperty("operation","").toString();
    const auto channel=armix::mixer::ChannelId{id};
    if(operation=="NATIVE_APPLY_WIRE_TRANSACTION") {
        const auto raw=payload.getProperty("changes",{});
        const auto* edits=raw.getArray();
        if(!edits||edits->isEmpty()||edits->size()>64)return false;
        std::vector<armix::mixer::WireChange> changes;
        changes.reserve(static_cast<std::size_t>(edits->size()));
        for(const auto& edit:*edits) {
            if(!edit.isObject())return false;
            const auto op=edit.getProperty("op","").toString();
            std::uint64_t from=0,to=0,edgeId=0;
            if(!approvedNativeId(edit.getProperty("from",{}),from) || from!=id)
                return false;
            armix::mixer::WireChange change;
            change.from=armix::mixer::ChannelId{from};
            if(op=="CONNECT")change.kind=armix::mixer::WireChange::Kind::connect;
            else if(op=="REMOVE")change.kind=armix::mixer::WireChange::Kind::remove;
            else if(op=="RETARGET")change.kind=armix::mixer::WireChange::Kind::retarget;
            else if(op=="ROUTE_ONLY")change.kind=armix::mixer::WireChange::Kind::routeOnly;
            else return false;
            if(op=="CONNECT"||op=="RETARGET"||op=="ROUTE_ONLY") {
                if(!approvedNativeId(edit.getProperty("to",{}),to)||to==from)
                    return false;
                change.to=armix::mixer::ChannelId{to};
            }
            if(op=="REMOVE"||op=="RETARGET") {
                if(!approvedNativeId(edit.getProperty("edgeId",{}),edgeId))
                    return false;
                change.edgeId=edgeId;
            }
            if(op=="CONNECT"||op=="ROUTE_ONLY") {
                double gain=0.;
                if(!nativeV8Number(edit.getProperty("gainLinear",{}),gain) ||
                   gain<0.||gain>4.||
                   (op=="ROUTE_ONLY"&&gain!=1.))
                    return false;
                change.gain=static_cast<float>(gain);
            }
            changes.push_back(change);
        }
        // The real engine may still reject while monitoring/output is armed.
        // Never auto-mute, fabricate a JS graph, or bypass its safety fence.
        if constexpr(requires(Engine& e, std::span<const armix::mixer::WireChange> values) {
            e.applyNativeWireTransaction(epoch,revision,roster,values);
        }) {
            const auto applied=engine.applyNativeWireTransaction(
                epoch,revision,roster,
                std::span<const armix::mixer::WireChange>(changes));
            // NativeBusResult.error is const char*, not juce::String.
            // The compiler and regression probe must share the exact native
            // error ABI. Never claim ACK for a contradictory accepted/error pair.
            return applied.accepted && applied.error != nullptr &&
                   applied.error[0] == '\0';
        } else return false;
    }
    if(operation=="NATIVE_SET_CHANNEL_MIX" ||
       operation=="NATIVE_SET_BUS_MIX") {
        const auto rawGain=payload.getProperty("gain",{});
        const auto rawPan=payload.getProperty("pan",{});
        double gain=0.,pan=0.;
        if(!nativeV8Number(rawGain,gain) ||
           !nativeV8Number(rawPan,pan) ||
           gain<0. || gain>4. || pan< -1. || pan>1.)
            return false;
        if(operation=="NATIVE_SET_CHANNEL_MIX")
            return engine.setNativeInputMix(epoch,revision,channel,
                    static_cast<float>(gain),static_cast<float>(pan));
        return engine.setNativeBusMix(epoch,revision,channel,
                    static_cast<float>(gain),static_cast<float>(pan));
    }
    if(operation=="NATIVE_SET_CHANNEL_MUTE_SOLO" ||
       operation=="NATIVE_SET_BUS_MUTE_SOLO") {
        const auto mute=payload.getProperty("mute",{});
        const auto solo=payload.getProperty("solo",{});
        if(!mute.isBool()||!solo.isBool())return false;
        if(operation=="NATIVE_SET_CHANNEL_MUTE_SOLO")
            return engine.setNativeInputMuteSolo(epoch,revision,channel,
                                                static_cast<bool>(mute),
                                                static_cast<bool>(solo));
        if constexpr(requires(Engine& e){
           e.setNativeBusMuteSolo(epoch,revision,channel,true,false);
        })
            return engine.setNativeBusMuteSolo(epoch,revision,channel,
                                              static_cast<bool>(mute),
                                              static_cast<bool>(solo));
        return false;
    }
    return false;
}
} // namespace armix::console
