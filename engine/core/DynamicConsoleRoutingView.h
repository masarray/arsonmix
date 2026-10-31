// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelRoster.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// M3C: CONTROL-thread projection of an ACCEPTED native audio session only.
// Enables Studio Console IN/OUT/SEND badges, distinct direct-vs-parallel paths,
// and clickable BUS source counts without inventing a browser-side audio graph.
// The owner publishes this together with the same revision's prepared PCM.
// Never traverse or allocate this model in the real-time audio callback.
namespace armix::mixer {
struct ConsoleSendView final {
    ChannelId destination{};
    float gain=1.f,pan=0.f;
    bool preFader=false;
};
struct ConsoleChannelView final {
    ChannelId id{};
    ChannelRole role=ChannelRole::input;
    ProcessingFormat format=ProcessingFormat::mono;
    std::string name;
    ChannelId primaryOutput{}; // zero is genuine OUT NONE, NOT muted parallel sends
    std::vector<ConsoleSendView> parallelSends;
    std::vector<ChannelId> primarySources;
    std::vector<ChannelId> parallelSendSources;
    std::size_t sourceCount() const noexcept {
        std::size_t count=primarySources.size();
        for(const auto id:parallelSendSources)
            if(std::find(primarySources.begin(),primarySources.end(),id)
                ==primarySources.end())++count;
        return count;
    }
};
class DynamicConsoleRoutingView final {
public:
    std::uint64_t rosterRevision=0;
    std::vector<ConsoleChannelView> channels;
    ChannelId main{},monitor{};
    const ConsoleChannelView* find(ChannelId id) const noexcept {
        const auto it=std::find_if(channels.begin(),channels.end(),
            [id](const ConsoleChannelView& ch){return ch.id==id;});
        return it==channels.end()?nullptr:&*it;
    }
    // A separate projection is constructed before native graph ACK.
    // False means no data from a malformed roster can be advertised as LIVE.
    static bool build(const DynamicChannelRoster& roster,
                      DynamicConsoleRoutingView& accepted) {
        DynamicConsoleRoutingView next;
        next.rosterRevision=roster.revision();
        const auto& rows=roster.channels();
        next.channels.reserve(rows.size());
        std::unordered_map<std::uint64_t,std::size_t> index;
        index.reserve(rows.size());
        for(const auto& row:rows) {
            if(!row.id.value || !index.emplace(row.id.value,next.channels.size()).second)
                return false;
            ConsoleChannelView item;
            item.id=row.id;item.role=row.role;item.format=row.format;
            item.name=row.name;
            if(row.role==ChannelRole::main) {
                if(next.main.value)return false;
                next.main=row.id;
            }
            if(row.role==ChannelRole::monitor) {
                if(next.monitor.value)return false;
                next.monitor=row.id;
            }
            next.channels.push_back(std::move(item));
        }
        if(!next.main.value)return false;
        for(std::size_t i=0;i<rows.size();++i) {
            auto& sender=next.channels[i];
            for(const auto& link:rows[i].sends) {
                const auto it=index.find(link.destination.value);
                if(it==index.end())return false;
                auto& destination=next.channels[it->second];
                if(link.primaryOutput) {
                    if(sender.primaryOutput.value || link.preFader ||
                       sender.id==next.main || sender.id==next.monitor)
                        return false;
                    sender.primaryOutput=link.destination;
                    destination.primarySources.push_back(sender.id);
                } else {
                    sender.parallelSends.push_back(
                        {link.destination,link.gain,link.pan,link.preFader});
                    // One source shown once in the Bus fan-in even if it has
                    // multiple legal taps to that destination.
                    if(std::find(destination.parallelSendSources.begin(),
                                 destination.parallelSendSources.end(),sender.id)
                       ==destination.parallelSendSources.end())
                        destination.parallelSendSources.push_back(sender.id);
                }
            }
        }
        accepted=std::move(next);
        return true;
    }
};
} // namespace armix::mixer
