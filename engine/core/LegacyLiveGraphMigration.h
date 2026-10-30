// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicPreparedMixerBlock.h"
#include "PreparedMixerBlock.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

// DYN1B-2A: CONTROL-THREAD ONLY migration/admission bridge.
// Translate the proven 10 INPUT + 4 AUX + MAIN (+MONITOR) plan to ONE
// ChannelId-based PCM render schedule, with the SAME ordered device port map.
// This does not instantiate VST3, touch a JUCE callback, or publish sound.
// The next live handoff must retain current FX instance leases + Show state.
namespace armix::mixer {

struct LegacyChannelIds final {
    std::array<ChannelId,stripCount> inputs{};
    std::array<ChannelId,auxBusCount> aux{};
    ChannelId main{},monitor{};
    int legacyFxIndex(ChannelId id) const noexcept {
        for(int i=0;i<stripCount;++i)if(inputs[i]==id)return i;
        for(int b=0;b<auxBusCount;++b)if(aux[b]==id)return stripCount+b;
        return id==main?-1:-2; // -2: MONITOR has no legacy VST insert
    }
};
enum class LegacyMigrationFault : unsigned char {
    none,invalidLegacyPlan,invalidMonitorGains,rosterAdmission,invalidDynamicGraph
};
struct LegacyMigrationError final {
    LegacyMigrationFault fault=LegacyMigrationFault::none;
    RenderError legacy{};
    ChannelEditFault roster=ChannelEditFault::none;
    DynamicGraphError dynamic{};
};
struct LegacyMigratedGraph final {
    // Worst-case legacy: 32 internal + 32 AUX sends + MAIN + MONITOR
    // from one source are legal. Do not introduce an accidental 32-send
    // migration ceiling where the native legacy plan allows 66.
    DynamicChannelRoster roster{ChannelRosterBudget{
        32u*1024u*1024u,8u,static_cast<std::size_t>(maxChannelSends+maxAuxSends+2)}};
    DynamicPreparedMixerBlock pcm{};
    LegacyChannelIds ids{};
    std::uint64_t revision=0;
};

class LegacyGraphMigration final {
public:
    static bool compile(
        const ChannelRoutingConfig& channels,
        std::span<const int> callbackOrderedPhysicalPorts,
        const SendRequest& channelSends,const AuxBusRequest& aux,
        bool monitorEnabled,
        const std::array<float,stripCount+auxBusCount>& monitorGains,
        int maxFrames,std::size_t scratchBudget,
        LegacyMigratedGraph& destination,LegacyMigrationError& error)
    {
        error={};
        PreparedRenderGraph legacy;
        if(!PreparedRenderGraph::compile(channels,callbackOrderedPhysicalPorts,
                                         channelSends,legacy,error.legacy,aux)){
            error.fault=LegacyMigrationFault::invalidLegacyPlan;return false;
        }
        if(monitorEnabled){
            for(float gain:monitorGains)
                if(!std::isfinite(gain)||gain<0.f||gain>4.f){
                    error.fault=LegacyMigrationFault::invalidMonitorGains;return false;
                }
        }
        LegacyMigratedGraph next;
        std::vector<ChannelEdit> create;
        create.reserve(stripCount+auxBusCount+2);
        const auto add=[&](ChannelRole role,ProcessingFormat format,
                           const std::string& name){
            ChannelEdit op;op.kind=ChannelEdit::Kind::add;
            op.position=create.size();op.role=role;op.format=format;op.text=name;
            create.push_back(std::move(op));
        };
        for(int i=0;i<stripCount;++i){
            const auto fmt=legacy.channels.effectiveFormats[i];
            add(ChannelRole::input,fmt==ChannelFormat::stereo?
                ProcessingFormat::stereo:ProcessingFormat::mono,
                "CH "+std::to_string(i+1));
        }
        for(int b=0;b<auxBusCount;++b)
            add(ChannelRole::fxReturn,ProcessingFormat::stereo,
                "AUX "+std::to_string(b+1));
        add(ChannelRole::main,ProcessingFormat::stereo,"MAIN");
        if(monitorEnabled)add(ChannelRole::monitor,ProcessingFormat::stereo,"MONITOR");
        const auto created=next.roster.edit(0,create);
        if(!created.ok()){
            error.fault=LegacyMigrationFault::rosterAdmission;
            error.roster=created.fault;return false;
        }
        for(int i=0;i<stripCount;++i)next.ids.inputs[i]=created.createdIds[i];
        for(int b=0;b<auxBusCount;++b)
            next.ids.aux[b]=created.createdIds[stripCount+b];
        next.ids.main=created.createdIds[stripCount+auxBusCount];
        if(monitorEnabled)next.ids.monitor=created.createdIds.back();

        std::vector<ChannelEdit> ops;
        ops.reserve(128);
        const auto editValue=[&](ChannelId id,ChannelEdit::Kind kind,float value){
            ChannelEdit op;op.kind=kind;op.id=id;op.value=value;
            ops.push_back(std::move(op));
        };
        const auto mute=[&](ChannelId id,bool enabled){
            ChannelEdit op;op.kind=ChannelEdit::Kind::setMute;
            op.id=id;op.enabled=enabled;ops.push_back(std::move(op));
        };
        const auto solo=[&](ChannelId id,bool enabled){
            ChannelEdit op;op.kind=ChannelEdit::Kind::setSolo;
            op.id=id;op.enabled=enabled;ops.push_back(std::move(op));
        };
        const auto link=[&](ChannelId from,ChannelId to,float gain,
                            float pan=0.f,bool pre=false){
            ChannelEdit op;op.kind=ChannelEdit::Kind::addSendReference;
            op.id=from;op.target=to;op.value=gain;op.sendPan=pan;op.preFader=pre;
            ops.push_back(std::move(op));
        };
        for(int i=0;i<stripCount;++i){
            const auto id=next.ids.inputs[i];
            const auto& cfg=channels.stripsAndMaster.strips[i];
            editValue(id,ChannelEdit::Kind::setFader,cfg.faderLinear);
            editValue(id,ChannelEdit::Kind::setPan,cfg.pan);
            if(cfg.mute)mute(id,true);
            if(cfg.solo)solo(id,true);
            if(channelSends.toMaster[i])link(id,next.ids.main,1.f);
            if(monitorEnabled && monitorGains[i]>0.f)
                link(id,next.ids.monitor,monitorGains[i]);
        }
        for(int i=0;i<channelSends.count;++i){
            const auto& s=channelSends.sends[i];
            if(s.enabled)link(next.ids.inputs[s.source],next.ids.inputs[s.destination],
                              s.gain,s.pan,s.tap==SendTap::preFader);
        }
        for(int b=0;b<auxBusCount;++b){
            const auto& cfg=aux.buses[b];
            const auto id=next.ids.aux[b];
            if(!cfg.enabled){mute(id,true);continue;}
            editValue(id,ChannelEdit::Kind::setFader,cfg.gain);
            if(cfg.mute)mute(id,true);
            // Real existing AUX racks are allowed to produce signal even on
            // empty input. A metadata-only marker admits the FX seam; it
            // NEVER instantiates or copies a plugin.
            ChannelEdit marker;marker.kind=ChannelEdit::Kind::addInsertReference;
            marker.id=id;marker.text="legacy:aux-rack";
            ops.push_back(std::move(marker));
            if(cfg.toMaster)link(id,next.ids.main,1.f);
            if(monitorEnabled && monitorGains[stripCount+b]>0.f)
                link(id,next.ids.monitor,monitorGains[stripCount+b]);
        }
        for(int i=0;i<aux.count;++i){
            const auto& s=aux.sends[i];
            if(s.enabled)link(next.ids.inputs[s.source],next.ids.aux[s.destination],
                              s.gain,s.pan,s.tap==SendTap::preFader);
        }
        const auto& master=channels.stripsAndMaster.master;
        editValue(next.ids.main,ChannelEdit::Kind::setFader,master.gainLinear);
        editValue(next.ids.main,ChannelEdit::Kind::setPan,master.pan);
        if(master.mute)mute(next.ids.main,true);

        const auto modified=next.roster.edit(next.roster.revision(),ops);
        if(!modified.ok()){
            error.fault=LegacyMigrationFault::rosterAdmission;
            error.roster=modified.fault;return false;
        }
        DynamicGraphRequest request;
        request.physicalInputs=legacy.channels.inputPorts.enabledCount;
        request.maxFrames=maxFrames;
        request.maxScratchBytes=scratchBudget;
        request.mainMonoAfterFx=master.mono;
        for(int i=0;i<stripCount;++i){
            const auto& source=legacy.channels.base.strips[i];
            if(source.mode==SourceMode::disconnected)continue;
            request.sources.push_back({
                next.ids.inputs[i],source.inputA,
                source.mode==SourceMode::stereo?source.inputB:-1,
                source.mode==SourceMode::stereo
            });
        }
        if(!DynamicPreparedMixerBlock::compile(next.roster,request,
                                               next.pcm,error.dynamic)){
            error.fault=LegacyMigrationFault::invalidDynamicGraph;return false;
        }
        next.revision=next.roster.revision();
        destination=std::move(next); // publish only after caller's native MUTE/epoch ACK
        return true;
    }
};
} // namespace armix::mixer
