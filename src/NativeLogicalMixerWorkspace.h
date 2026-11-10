// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "../engine/core/DeviceIndependentMixerSession.h"
#include "NativeMixerSessionDocument.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>

// CONTROL-thread-only view and editor for the existing mixer session document.
// Its readback is explicitly LOGICAL: no PCM callback, output admission,
// fake meter, audio device open, or second DSP graph.
namespace armix::session {
class NativeLogicalMixerWorkspace final {
public:
    using Role=armix::mixer::ChannelRole;
    using ChannelId=armix::mixer::ChannelId;
    using ChannelEdit=armix::mixer::ChannelEdit;
    using WireChange=armix::mixer::WireChange;

    NativeLogicalMixerWorkspace() {
        document_.id=juce::Uuid().toDashedString().toLowerCase();
        document_.logicalOnly=true;
        document_.binding.inputMask="0";
        document_.binding.outputMask="0";
        document_.request.maxFrames=256; // offline preparation hint, NOT a device
        document_.request.physicalInputs=0;
        document_.savedMs=juce::Time::currentTimeMillis();
        for(std::size_t i=0;i<document_.legacyIds.inputs.size();++i)
            document_.legacyIds.inputs[i]={i+1};
        for(std::size_t i=0;i<document_.legacyIds.aux.size();++i)
            document_.legacyIds.aux[i]={i+11};
        document_.legacyIds.main={15};
        sync();
    }
    bool load(MixerSessionDocument document,
              std::vector<armix::shows::BinaryState> states={}) {
        if(!editor_.restore(document.roster))return false;
        // A physically bound historical .smix keeps its exact old binding
        // metadata and verified vendor binary states. Offline UI editing
        // never uses that metadata to route sound or open a device.
        workspaceToken_=juce::Uuid().toDashedString().toLowerCase();
        document_=std::move(document);
        states_=std::move(states);
        return true;
    }
    bool isLogicalOnly() const noexcept {return document_.logicalOnly;}
    const MixerSessionDocument& document() const noexcept {return document_;}
    const std::vector<armix::shows::BinaryState>& states() const noexcept {
        return states_;
    }
    std::uint64_t revision() const noexcept {return editor_.revision();}
    const juce::String& workspaceToken() const noexcept {return workspaceToken_;}

    struct Ack final {
        bool accepted=false;
        juce::String error;
        juce::String createdId;
        std::uint64_t revision=0;
    };
    Ack apply(const juce::var& request) {
        Ack ack;ack.revision=revision();
        const auto value=request.getProperty("baseRevision",{});
        std::uint64_t expected=0;
        if(request.getProperty("workspaceToken","").toString()!=workspaceToken_ ||
           !armix::session::parseDecimal(value,expected) ||
           expected!=revision()) {
            ack.error="STALE_LOGICAL_REVISION";return ack;
        }
        const auto operation=request.getProperty("operation","").toString();
        const auto rawId=request.getProperty("channelId",{});
        std::uint64_t idValue=0;
        if(operation!="CREATE_CHANNEL" && operation!="CREATE_GROUP" &&
           !armix::session::parseDecimal(rawId,idValue)) {
            ack.error="INVALID_LOGICAL_CHANNEL";return ack;
        }
        const ChannelId id{idValue};
        ChannelEdit edit;
        edit.id=id;
        const auto number=request.getProperty("value",{});
        const auto payload=request.getProperty("payload",{});
        const auto text=payload.getProperty("name","").toString();
        auto bad=[&](const juce::String& reason)->Ack {
            ack.error=reason;return ack;
        };
        armix::mixer::DeviceIndependentMixerSession::Result result;
        if(operation=="CREATE_CHANNEL") {
            edit.kind=ChannelEdit::Kind::add;
            edit.role=Role::input;
            edit.format=armix::mixer::ProcessingFormat::stereo;
            edit.connectMainOnCreate=true;
            const auto& rows=editor_.roster().channels();
            edit.position=rows.size()-1;
            edit.text=text.isNotEmpty()?text.toStdString():
                ("Track "+std::to_string(rows.size()+1));
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="CREATE_GROUP") {
            const auto name=text.isNotEmpty()?text.toStdString():
                ("Bus "+std::to_string(editor_.roster().size()));
            result=editor_.createBus(expected,name,Role::group);
        } else if(operation=="REMOVE_BUS") {
            result=editor_.removeBus(expected,id);
        } else if(operation=="DELETE_CHANNEL") {
            if(!document_.logicalOnly &&
               std::find(document_.legacyIds.inputs.begin(),document_.legacyIds.inputs.end(),id)
                   !=document_.legacyIds.inputs.end())
                return bad("LEGACY_INPUT_REQUIRED");
            result=editor_.removeInput(expected,id);
        } else if(operation=="RENAME_CHANNEL") {
            edit.kind=ChannelEdit::Kind::rename;
            edit.text=text.toStdString();
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="SET_PROCESSING_FORMAT") {
            const auto target=payload.getProperty("format","").toString();
            if(target!="MONO" && target!="STEREO")
                return bad("INVALID_FORMAT");
            edit.kind=ChannelEdit::Kind::setFormat;
            edit.format=target=="MONO"?
                armix::mixer::ProcessingFormat::mono:
                armix::mixer::ProcessingFormat::stereo;
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="SET_FADER" || operation=="SET_PAN") {
            if(!(number.isDouble() || number.isInt() || number.isInt64()))
                return bad("INVALID_CONTROL_VALUE");
            const auto n=static_cast<double>(number);
            if(!std::isfinite(n))return bad("INVALID_CONTROL_VALUE");
            if(operation=="SET_FADER") {
                if(n< -60. || n>10.)return bad("INVALID_FADER_DB");
                edit.kind=ChannelEdit::Kind::setFader;
                edit.value=static_cast<float>(std::pow(10.,n/20.));
            } else {
                if(n< -1. || n>1.)return bad("INVALID_PAN");
                edit.kind=ChannelEdit::Kind::setPan;
                edit.value=static_cast<float>(n);
            }
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="SET_MUTE" || operation=="SET_SOLO") {
            if(!number.isBool())return bad("INVALID_BOOLEAN_CONTROL");
            edit.kind=operation=="SET_MUTE"?
                ChannelEdit::Kind::setMute:ChannelEdit::Kind::setSolo;
            edit.enabled=static_cast<bool>(number);
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="REMOVE_FX_REFERENCE") {
            if(!document_.logicalOnly)return bad("FX_PLAN_REQUIRES_UNBOUND_SESSION");
            const auto* row=editor_.roster().find(id);
            const auto v=payload.getProperty("slot",{});
            if(!v.isInt() || !row || static_cast<int>(v)<0 ||
               static_cast<std::size_t>(static_cast<int>(v))>=row->insertCatalogIds.size())
                return bad("INVALID_LOGICAL_FX_SLOT");
            if(std::any_of(document_.inserts.begin(),document_.inserts.end(),
                   [&](const auto& fx){return fx.channel==id &&
                       fx.slot==static_cast<std::size_t>(static_cast<int>(v));}))
                return bad("FX_SLOT_OWNS_SAVED_STATE");
            edit.kind=ChannelEdit::Kind::removeInsertReference;
            edit.position=static_cast<std::size_t>(static_cast<int>(v));
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="SET_FX_SLOT_BYPASS") {
            if(!document_.logicalOnly)return bad("FX_PLAN_REQUIRES_UNBOUND_SESSION");
            const auto* row=editor_.roster().find(id);
            const auto v=payload.getProperty("slot",{}),enabled=payload.getProperty("enabled",{});
            if(!v.isInt() || !enabled.isBool() || !row || static_cast<int>(v)<0 ||
               static_cast<std::size_t>(static_cast<int>(v))>=row->insertCatalogIds.size())
                return bad("INVALID_LOGICAL_FX_SLOT");
            edit.kind=ChannelEdit::Kind::touchDocumentRevision;
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="SET_FX_RACK_BYPASS") {
            if(!number.isBool() || !editor_.roster().find(id))
                return bad("INVALID_LOGICAL_RACK");
            // Bypass lives in .smix metadata. Bump the authoritative
            // revision explicitly, never rewrite a live DSP parameter.
            edit.kind=ChannelEdit::Kind::touchDocumentRevision;
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="ADD_FX_REFERENCE") {
            // A physically saved Show may contain vendor states. Never
            // invent a binary processor lease or mismatch its state manifest.
            if(!document_.logicalOnly)
                return bad("FX_PLAN_REQUIRES_UNBOUND_SESSION");
            const auto catalog=payload.getProperty("catalogId","").toString();
            // The JUCE host verifies every external identity against its
            // actual installed catalog before calling this editor. Here we
            // store only an opaque reference, never load a vendor DLL.
            if(catalog.isEmpty() || catalog.getNumBytesAsUTF8()>256 ||
               catalog.containsChar('\n') || catalog.containsChar('\r'))
                return bad("INVALID_OFFLINE_FX_CATALOG");
            edit.kind=ChannelEdit::Kind::addInsertReference;
            edit.text=catalog.toStdString();
            result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
        } else if(operation=="WIRE_TRANSACTION") {
            const auto changes=payload.getProperty("changes",{});
            const auto* ops=changes.getArray();
            if(!ops || ops->size()!=1)return bad("INVALID_WIRE_BATCH");
            const auto& change=ops->getReference(0);
            const auto type=change.getProperty("op","").toString();
            std::uint64_t from=0,to=0,edgeId=0;
            if(!armix::session::parseDecimal(change.getProperty("from",{}),from))
                return bad("INVALID_WIRE_SOURCE");
            if(type=="DISCONNECT") {
                edit.kind=ChannelEdit::Kind::setPrimaryOutput;
                edit.id={from};edit.target={};
                result=editor_.edit(expected,std::span<const ChannelEdit>{&edit,1});
            } else {
                WireChange wire;
                wire.from={from};
                if(type=="REMOVE") {
                    wire.kind=WireChange::Kind::remove;
                    if(!armix::session::parseDecimal(change.getProperty("edgeId",{}),edgeId))
                        return bad("INVALID_WIRE_EDGE");
                    wire.edgeId=edgeId;
                } else {
                    if(!armix::session::parseDecimal(change.getProperty("to",{}),to))
                        return bad("INVALID_WIRE_DESTINATION");
                    wire.to={to};
                    wire.kind=type=="ROUTE_ONLY"?WireChange::Kind::routeOnly:
                        type=="CONNECT"?WireChange::Kind::connect:
                        WireChange::Kind::retarget;
                    if(type!="ROUTE_ONLY"&&type!="CONNECT"&&type!="RETARGET")
                        return bad("INVALID_WIRE_OPERATION");
                    if(type=="RETARGET") {
                        if(!armix::session::parseDecimal(change.getProperty("edgeId",{}),edgeId))
                            return bad("INVALID_WIRE_EDGE");
                        wire.edgeId=edgeId;
                    }
                    const auto gain=change.getProperty("gainLinear",juce::var(1.0));
                    if(!(gain.isDouble()||gain.isInt()))
                        return bad("INVALID_SEND_GAIN");
                    wire.gain=static_cast<float>(static_cast<double>(gain));
                }
                result=editor_.wire(expected,std::span<const WireChange>{&wire,1});
            }
        } else return bad("UNSUPPORTED_LOGICAL_OPERATION");
        if(!result.accepted()){
            ack.error="LOGICAL_EDIT_REJECTED";
            return ack;
        }
        sync();
        if(operation=="REMOVE_FX_REFERENCE") {
            const auto erased=static_cast<std::size_t>(static_cast<int>(
                payload.getProperty("slot",0)));
            auto& planned=document_.plannedSlotBypassed;
            planned.erase(std::remove_if(planned.begin(),planned.end(),
                [&](const auto& p){return p.channel==id && p.slot==erased;}),
                planned.end());
            for(auto& p:planned)
                if(p.channel==id && p.slot>erased)--p.slot;
        }
        if(operation=="SET_FX_SLOT_BYPASS") {
            auto& planned=document_.plannedSlotBypassed;
            const auto slot=static_cast<std::size_t>(static_cast<int>(
                payload.getProperty("slot",0)));
            planned.erase(std::remove_if(planned.begin(),planned.end(),
                [&](const auto& p){return p.channel==id && p.slot==slot;}),
                planned.end());
            const auto enabled=static_cast<bool>(payload.getProperty("enabled",false));
            auto stored=std::find_if(document_.inserts.begin(),document_.inserts.end(),
                [&](const auto& fx){return fx.channel==id && fx.slot==slot;});
            if(stored!=document_.inserts.end())stored->bypass=enabled;
            else if(enabled)planned.push_back({id,slot});
        }
        if(operation=="SET_FX_RACK_BYPASS") {
            const bool bypass=static_cast<bool>(number);
            auto& racks=document_.rackBypassed;
            racks.erase(std::remove(racks.begin(),racks.end(),id),racks.end());
            if(bypass)racks.push_back(id);
        }
        // Delete never keeps a dangling native insert/preset from the removed
        // ChannelId; no vendor destructors or PCM are touched here.
        if(operation=="DELETE_CHANNEL" || operation=="REMOVE_BUS") {
            std::erase_if(document_.request.sources,
                [&](const auto& source){return source.id==id;});
            std::erase_if(document_.waitingAsioInputs,
                [&](const auto& source){return source.id==id;});
            document_.inserts.erase(std::remove_if(
                document_.inserts.begin(),document_.inserts.end(),
                [&](const auto& fx){return !editor_.roster().find(fx.channel);}),
                document_.inserts.end());
            document_.plannedSlotBypassed.erase(std::remove_if(
                document_.plannedSlotBypassed.begin(),document_.plannedSlotBypassed.end(),
                [&](const auto& fx){return !editor_.roster().find(fx.channel);}),
                document_.plannedSlotBypassed.end());
            document_.rackBypassed.erase(std::remove_if(
                document_.rackBypassed.begin(),document_.rackBypassed.end(),
                [&](auto channel){return !editor_.roster().find(channel);}),
                document_.rackBypassed.end());
            std::vector<armix::shows::BinaryState> retained;
            for(const auto& state:states_) {
                if(std::any_of(document_.inserts.begin(),document_.inserts.end(),
                     [&](const auto& fx){return fx.sha256==state.sha256;}))
                    retained.push_back(state);
            }
            states_=std::move(retained);
        }
        ack.accepted=true;ack.revision=revision();
        if(!result.createdIds.empty())
            ack.createdId=juce::String(std::to_string(result.createdIds[0].value));
        return ack;
    }

    juce::var snapshot() const {
        auto graph=object(),cap=object();
        set(cap,"callbackOwnsChannelIds",false);
        juce::Array<juce::var> operations;
        for(const char* op:{"CREATE_CHANNEL","DELETE_CHANNEL","CREATE_GROUP",
            "REMOVE_BUS","RENAME_CHANNEL","SET_FADER","SET_PAN","SET_MUTE",
            "SET_SOLO","SET_PROCESSING_FORMAT","WIRE_TRANSACTION",
            "ADD_FX_REFERENCE","REMOVE_FX_REFERENCE",
            "SET_FX_SLOT_BYPASS","SET_FX_RACK_BYPASS"})operations.add(juce::String(op));
        set(cap,"supportedOps",juce::var(operations));
        set(cap,"dynamicChannels",true);set(cap,"groups",true);
        set(cap,"fxReturns",true);set(cap,"monitor",false);
        juce::Array<juce::var> rows,edges,destinations;
        for(const auto& row:editor_.roster().channels()) {
            auto channel=object(),controls=object();
            set(channel,"id",str(row.id.value));
            set(channel,"name",juce::String::fromUTF8(row.name.c_str()));
            const char* role=row.role==Role::main?"MAIN":
                row.role==Role::group?"GROUP":
                row.role==Role::fxReturn?"FX_RETURN":
                row.role==Role::monitor?"MONITOR":"INPUT";
            set(channel,"role",role);
            set(channel,"processingFormat",row.format==armix::mixer::ProcessingFormat::stereo?
                "STEREO":"MONO");
            set(controls,"faderDb",row.faderLinear>0.f?
                std::max(-60.,20.*std::log10(static_cast<double>(row.faderLinear))):-60.);
            set(controls,"pan",static_cast<double>(row.pan));
            set(controls,"mute",row.mute);set(controls,"solo",row.solo);
            set(channel,"controls",controls);
            auto source=object();
            set(source,"sourceFormat","NONE");
            set(channel,"source",source);
            juce::Array<juce::var> inserts,sends;
            for(std::size_t slot=0;slot<row.insertCatalogIds.size();++slot) {
                const auto& catalog=row.insertCatalogIds[slot];
                const auto id=juce::String::fromUTF8(catalog.c_str());
                auto fx=object();
                set(fx,"id","planned:"+str(row.id.value)+":"+juce::String(static_cast<int>(slot)));
                set(fx,"catalogId",id);
                const auto stored=std::find_if(document_.inserts.begin(),
                    document_.inserts.end(),[&](const auto& insert){
                        return insert.channel==row.id && insert.slot==slot;
                    });
                const auto label=id=="armix.builtin.arson-harmony.v1"?"Harmonice":
                    stored!=document_.inserts.end() && stored->name.isNotEmpty()?stored->name:
                    id=="armix.builtin.channel-compressor.v1"?"Compressor":
                    id=="armix.builtin.channel-eq.v1"?"Parametric EQ":id;
                set(fx,"name",label);
                set(fx,"bypassed",(stored!=document_.inserts.end() && stored->bypass) ||
                    std::any_of(document_.plannedSlotBypassed.begin(),
                    document_.plannedSlotBypassed.end(),
                    [&](const auto& p){return p.channel==row.id&&p.slot==slot;}));
                inserts.add(fx);
            }
            for(const auto& send:row.sends) {
                auto edge=object();
                set(edge,"edgeId",str(send.edgeId));
                set(edge,"from",str(row.id.value));
                set(edge,"to",str(send.destination.value));
                set(edge,"signalKind","AUDIO");set(edge,"enabled",true);
                set(edge,"gainLinear",static_cast<double>(send.gain));
                set(edge,"primaryOutput",send.primaryOutput);
                set(edge,"tap",send.preFader?"SPLIT_PRE":"POST_FADER");
                edges.add(edge);
                sends.add(edge);
                if(send.primaryOutput)set(channel,"primaryOutId",str(send.destination.value));
            }
            set(channel,"sends",juce::var(sends));
            set(channel,"inserts",juce::var(inserts));
            set(channel,"fxRackBypassed",std::find(document_.rackBypassed.begin(),
                document_.rackBypassed.end(),row.id)!=document_.rackBypassed.end());
            rows.add(channel);
        }
        set(graph,"channels",juce::var(rows));set(graph,"edges",juce::var(edges));
        set(graph,"destinations",juce::var(destinations));
        set(graph,"mainChannelId",str(editor_.mainId().value));
        set(graph,"revision",str(revision()));
        set(graph,"rosterRevision",str(revision()));
        set(graph,"capabilities",cap);
        auto result=object();
        set(result,"schema","arsonmix.native.logical-mixer.v1");
        set(result,"authority","LOGICAL_ONLY");
        set(result,"workspaceToken",workspaceToken_);
        set(result,"revision",str(revision()));
        set(result,"graph",graph);
        set(result,"physicalOutputAdmitted",false);
        set(result,"outputArmed",false);
        return result;
    }

private:
    armix::mixer::DeviceIndependentMixerSession editor_;
    MixerSessionDocument document_;
    juce::String workspaceToken_=juce::Uuid().toDashedString().toLowerCase();
    std::vector<armix::shows::BinaryState> states_;

    void sync() {
        document_.roster=editor_.roster();
        document_.acceptedRevision=editor_.revision();
        document_.savedMs=juce::Time::currentTimeMillis();
    }
    static juce::var object() {return juce::var(new juce::DynamicObject());}
    static juce::String str(std::uint64_t v) {
        return juce::String(std::to_string(v));
    }
    static void set(juce::var& dst,const char* key,const juce::var& value) {
        dst.getDynamicObject()->setProperty(key,value);
    }
};
} // namespace armix::session
