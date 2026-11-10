// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "DynamicChannelRoster.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// P0: CONTROL-thread logical editor. No device manager, PCM callback, plugin
// instances, hardware pin ordinals or output admission live in this type.
// The *same* DynamicChannelRoster descriptors and stable ChannelId/EdgeId
// are consumed by the existing native graph compiler after verified binding.
// This is a mutable session DOCUMENT, not a second audio engine.
namespace armix::mixer {

class DeviceIndependentMixerSession final {
public:
    struct Result final {
        ChannelEditFault fault=ChannelEditFault::none;
        std::uint64_t revision=0;
        std::vector<ChannelId> createdIds;
        bool accepted() const noexcept {return fault==ChannelEditFault::none;}
    };

    DeviceIndependentMixerSession() {
        // Keep native default strip layout and stable identities even with
        // no audio interface. An input's default OUT is logical, never a
        // physical playback-pair guess. Source patches start as NONE.
        std::vector<ChannelStripDefinition> rows;
        rows.reserve(15);
        for(std::uint64_t i=1;i<=10;++i) {
            ChannelStripDefinition channel;
            channel.id={i};
            channel.role=ChannelRole::input;
            channel.format=ProcessingFormat::stereo;
            channel.name=std::string("CH ")+(i<10?"0":"")+std::to_string(i);
            channel.sends.push_back({ChannelId{15},1.f,false,0.f,true,i});
            rows.push_back(std::move(channel));
        }
        for(std::uint64_t i=11;i<=14;++i) {
            ChannelStripDefinition bus;
            bus.id={i};
            bus.role=ChannelRole::fxReturn;
            bus.format=ProcessingFormat::stereo;
            bus.name="AUX "+std::string(1,static_cast<char>('A'+i-11));
            bus.sends.push_back({ChannelId{15},1.f,false,0.f,true,i});
            rows.push_back(std::move(bus));
        }
        ChannelStripDefinition master;
        master.id={15};
        master.role=ChannelRole::main;
        master.format=ProcessingFormat::stereo;
        master.name="MAIN";
        rows.push_back(std::move(master));
        initialized_=roster_.restoreExact(1,rows);
    }

    bool initialized() const noexcept {return initialized_;}
    std::uint64_t revision() const noexcept {return roster_.revision();}
    const DynamicChannelRoster& roster() const noexcept {return roster_;}
    ChannelId mainId() const noexcept {
        for(const auto& channel:roster_.channels())
            if(channel.role==ChannelRole::main)return channel.id;
        return {};
    }

    // Import the *same* roster from a validated native .smix document. This
    // operation does not reinterpret retained hardware ordinals or cause a
    // device restart. Rejects cyclic routes before making it authoritative.
    bool restore(const DynamicChannelRoster& retained) {
        if(retained.revision()==0 || !routeDag(retained))return false;
        roster_=retained;
        initialized_=true;
        return true;
    }

    Result edit(std::uint64_t expected,std::span<const ChannelEdit> ops) {
        if(!initialized_)return {ChannelEditFault::invalidAction,revision(),{}};
        auto candidate=roster_;
        auto result=candidate.edit(expected,ops);
        return commit(std::move(candidate),std::move(result));
    }

    Result wire(std::uint64_t expected,std::span<const WireChange> ops) {
        if(!initialized_)return {ChannelEditFault::invalidAction,revision(),{}};
        auto candidate=roster_;
        auto result=candidate.wireTransaction(expected,ops);
        return commit(std::move(candidate),std::move(result));
    }

    Result createBus(std::uint64_t expected,std::string name,ChannelRole role) {
        if(!initialized_)return {ChannelEditFault::invalidAction,revision(),{}};
        auto candidate=roster_;
        auto result=candidate.createStandaloneBus(expected,std::move(name),role);
        return commit(std::move(candidate),std::move(result));
    }

    Result removeBus(std::uint64_t expected,ChannelId id) {
        if(!initialized_)return {ChannelEditFault::invalidAction,revision(),{}};
        auto candidate=roster_;
        auto result=candidate.removeBusAndRestoreSources(expected,id);
        return commit(std::move(candidate),std::move(result));
    }

    Result removeInput(std::uint64_t expected,ChannelId id) {
        if(!initialized_)return {ChannelEditFault::invalidAction,revision(),{}};
        auto candidate=roster_;
        auto result=candidate.removeInputAndDisconnect(expected,id);
        return commit(std::move(candidate),std::move(result));
    }

private:
    DynamicChannelRoster roster_{ChannelRosterBudget{
        32u*1024u*1024u,8u,32u}};
    bool initialized_=false;

    Result commit(DynamicChannelRoster candidate,ChannelEditResult change) {
        if(!change.ok())return {change.fault,revision(),{}};
        // The existing roster editor checks local endpoints/duplicates.
        // Validate the WHOLE DAG here, even without hardware: cycles must
        // never be accepted as an offline edit that later breaks PCM binding.
        if(!routeDag(candidate))
            return {ChannelEditFault::invalidValue,revision(),{}};
        auto created=std::move(change.createdIds);
        roster_=std::move(candidate);
        return {ChannelEditFault::none,revision(),std::move(created)};
    }

    static bool routeDag(const DynamicChannelRoster& roster) {
        const auto& rows=roster.channels();
        if(rows.empty() || rows.size()>4096)return false;
        std::unordered_set<std::uint64_t> all;
        std::unordered_set<std::uint64_t> edgeIds;
        int mains=0;
        for(const auto& row:rows) {
            if(!row.id.value || !all.insert(row.id.value).second)return false;
            mains+=row.role==ChannelRole::main;
        }
        if(mains!=1)return false;
        // DFS using stable IDs; bound the recursion by session limits.
        std::unordered_set<std::uint64_t> visited,active;
        const auto visit=[&](auto&& self,ChannelId id)->bool {
            if(active.count(id.value))return false;
            if(visited.count(id.value))return true;
            const auto* channel=roster.find(id);
            if(!channel)return false;
            active.insert(id.value);
            for(const auto& edge:channel->sends) {
                if(!edge.edgeId || !edgeIds.insert(edge.edgeId).second ||
                   !all.count(edge.destination.value) ||
                   !self(self,edge.destination))return false;
            }
            active.erase(id.value);
            visited.insert(id.value);
            return true;
        };
        // Edge identity is checked once globally above. For previously
        // visited subgraphs no outgoing edge is traversed twice.
        for(const auto& row:rows)
            if(!visit(visit,row.id))return false;
        return true;
    }
};

} // namespace armix::mixer
