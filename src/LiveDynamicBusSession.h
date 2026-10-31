// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../engine/core/DynamicNativeSessionPublisher.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// M3E live, strictly dry and 0-FX pilot. ONE JUCE callback renders either
// legacy 10CH or this dynamic graph, NEVER both. A Group consumes a genuinely
// new ChannelId, not an unused input strip. The control plane may accept a
// candidate only while all physical and secondary outputs are disarmed.
// Explicitly refuse complex legacy AUX/FX/monitor/Show routes rather than
// converting them incompletely and losing audio behind the operator's back.
namespace armix::mixer {
class LiveDynamicBusSession final {
public:
    static constexpr int maxPhysicalPins=256;
    LiveDynamicBusSession()=default;
    LiveDynamicBusSession(const LiveDynamicBusSession&)=delete;
    LiveDynamicBusSession& operator=(const LiveDynamicBusSession&)=delete;

    struct Selection final {
        std::array<int,10> channels{};
        int count=0;
    };

    juce::String createForSelected(const juce::var& accepted,
                                  const juce::var& status,
                                  const Selection& selection,
                                  std::uint64_t deviceEpoch,
                                  int actualInputs,int actualOutputs,
                                  int frames,bool disarmed) {
        if(!disarmed)return "MUTE MAIN and DISARM secondary output before creating a Bus";
        if(active_.load(std::memory_order_acquire))
            return "Stop the current live Group before modifying the pilot Bus route";
        if(deviceEpoch==0||actualInputs<1||actualInputs>maxPhysicalPins ||
           actualOutputs<2||actualOutputs>maxPhysicalPins ||
           frames<32||frames>8192)
            return "Native device mask/quantum is unavailable for Dynamic Bus";
        if(selection.count<1||selection.count>10)
            return "Select one or more active INPUT channels for GROUP Bus";
        if(!accepted.isObject()||!status.isObject())
            return "First APPLY an actual native 10CH physical INPUT route";
        const auto routeRows=accepted.getProperty("channels",{});
        const auto* rows=routeRows.getArray();
        if(!rows||rows->size()!=10)
            return "Selected Show is not an accepted 10CH physical source route";
        // This deliberately isolates zero-FX GROUP latency/PCM proof; FX and
        // Aux migration must reuse M4 owner rather than duplicate VST objects.
        if(static_cast<int>(status.getProperty("fxCount",0))!=0 ||
           static_cast<int>(status.getProperty("auxCount",0))!=0 ||
           static_cast<int>(status.getProperty("sendCount",0))!=0 ||
           static_cast<int>(status.getProperty("auxSendCount",0))!=0)
            return "M3E dry Bus pilot requires 0 VST3, AUX and prior Sends; preserve existing route";
        const auto mons=status.getProperty("monitorOutputs",{}).getArray();
        if(mons&&!mons->isEmpty())
            return "M3E dry Bus pilot needs MONITOR disabled; preserve existing monitor patch";
        const auto* ports=status.getProperty("ports",{}).getArray();
        const auto* outs=status.getProperty("outputPorts",{}).getArray();
        const auto* main=status.getProperty("mainOutputs",{}).getArray();
        if(!ports||!outs||!main||main->size()!=2||
           ports->size()!=actualInputs||outs->size()!=actualOutputs)
            return "Native INPUT/MAIN readback changed; retry after DEVICE/APPLY ACK";
        auto findInput=[&](int port)->int {
            for(const auto& p:*ports)
                if(static_cast<int>(p.getProperty("port",-1))==port)
                    return static_cast<int>(p.getProperty("callback",-1));
            return -1;
        };
        auto findOutput=[&](int physical)->int {
            for(int k=0;k<outs->size();++k)
                if(static_cast<int>((*outs)[k])==physical)return k;
            return -1;
        };
        const int left=findOutput(static_cast<int>((*main)[0]));
        const int right=findOutput(static_cast<int>((*main)[1]));
        if(left<0||right<0||left==right)return "MAIN output pins are not active in this driver";
        std::array<bool,10> isSelected{};
        for(int k=0;k<selection.count;++k){
            const int idx=selection.channels[k];
            if(idx<0||idx>=10||isSelected[static_cast<std::size_t>(idx)])
                return "Invalid or duplicated channel selection";
            isSelected[static_cast<std::size_t>(idx)]=true;
        }
        DynamicChannelRoster roster;
        std::vector<ChannelEdit> add;
        add.reserve(11);
        for(int i=0;i<11;++i){
            ChannelEdit row;row.kind=ChannelEdit::Kind::add;row.position=i;
            row.role=i==10?ChannelRole::main:ChannelRole::input;
            const auto mode=i==10?juce::String("STEREO"):
                (*rows)[i].getProperty("mode",{}).toString();
            row.format=mode=="STEREO"?ProcessingFormat::stereo:ProcessingFormat::mono;
            row.text=i==10?"MAIN":"CH "+std::to_string(i+1);
            if(i!=10 && mode!="NONE"&&mode!="MONO"&&mode!="STEREO")
                return "Legacy BUS/unsupported source needs full Show migration; route unchanged";
            if(i<10 && isSelected[static_cast<std::size_t>(i)]&&mode=="NONE")
                return "Selected channel has no native physical source; route unchanged";
            add.push_back(std::move(row));
        }
        const auto first=roster.edit(0,add);
        if(!first.ok()||first.createdIds.size()!=11)
            return "Native dynamic ChannelId allocation rejected";
        std::vector<ChannelEdit> links;
        DynamicGraphRequest req;
        req.physicalInputs=actualInputs;
        req.maxFrames=frames;
        req.maxScratchBytes=64u*1024u*1024u;
        for(int i=0;i<10;++i){
            const auto& row=(*rows)[i];
            const auto mode=row.getProperty("mode",{}).toString();
            if(mode=="NONE")continue;
            const int inL=findInput(static_cast<int>(row.getProperty("left",-1)));
            const int inR=mode=="STEREO"?
                findInput(static_cast<int>(row.getProperty("right",-1))):-1;
            if(inL<0||inL>=actualInputs ||
               (mode=="STEREO"&&(inR<0||inR>=actualInputs||inL==inR)))
                return "Physical capture port absent from current driver readback";
            req.sources.push_back({first.createdIds[i],inL,inR,mode=="STEREO"});
            ChannelEdit link;
            link.kind=ChannelEdit::Kind::setPrimaryOutput;
            link.id=first.createdIds[i];link.target=first.createdIds[10];
            links.push_back(std::move(link));
        }
        const auto wired=roster.edit(roster.revision(),links);
        if(!wired.ok())return "Unable to create source to MAIN routing";
        std::vector<ChannelId> selected;
        selected.reserve(selection.count);
        for(int k=0;k<selection.count;++k)
            selected.push_back(first.createdIds[selection.channels[k]]);
        const auto bus=roster.createBusForSelected(
            roster.revision(),selected,"VOCAL GROUP",ChannelRole::group);
        if(!bus.ok()||bus.createdIds.size()!=1)
            return "Native independent GROUP allocation/routing was rejected";
        // Commit exactly once. No GUI state is changed before native graph
        // compilation and immutable epoch/revision publication succeed.
        const auto result=publisher_.commit(roster,req,
            publisher_.acceptedRevision(),deviceEpoch,deviceEpoch,disarmed);
        if(!result.ok())
            return "Native dynamic PCM GROUP compile/publish rejected (fault "+
                juce::String(static_cast<int>(result.fault))+")";
        roster_=std::move(roster);
        group_=bus.createdIds.front();
        main_=first.createdIds[10];
        actualMainL_.store(left,std::memory_order_relaxed);
        actualMainR_.store(right,std::memory_order_relaxed);
        inputCount_.store(actualInputs,std::memory_order_relaxed);
        epoch_.store(deviceEpoch,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_release);
        active_.store(true,std::memory_order_release);
        return {};
    }

    void stop() noexcept {
        active_.store(false,std::memory_order_release);
        generation_.fetch_add(1,std::memory_order_release);
        // Keep publisher alive across device stop. Old prepared snapshots are
        // retired only after native callback lease release on control poll.
        publisher_.unpublish();
    }
    void poll() noexcept {publisher_.pollRetirement();}
    bool active() const noexcept {return active_.load(std::memory_order_acquire);}
    bool activeFor(std::uint64_t epoch,int inputs,int outputs) const noexcept {
        return active()&&epoch==epoch_.load(std::memory_order_acquire)&&
            inputs==inputCount_.load(std::memory_order_relaxed)&&
            actualMainL_.load(std::memory_order_relaxed)>=0&&
            actualMainR_.load(std::memory_order_relaxed)<outputs;
    }
    std::array<int,2> mainPair() const noexcept {
        return {actualMainL_.load(std::memory_order_relaxed),
                actualMainR_.load(std::memory_order_relaxed)};
    }
    std::uint64_t generation() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }
    std::uint64_t acceptedRevision() const noexcept {return publisher_.acceptedRevision();}
    juce::var status() const {
        auto out=std::make_unique<juce::DynamicObject>();
        out->setProperty("active",active());
        out->setProperty("epoch",static_cast<juce::int64>(epoch_.load()));
        out->setProperty("revision",static_cast<juce::int64>(publisher_.acceptedRevision()));
        out->setProperty("groupChannelId",static_cast<juce::int64>(group_.value));
        out->setProperty("mainChannelId",static_cast<juce::int64>(main_.value));
        if(active()) {
            const auto* accepted=publisher_.acceptedRouting(epoch_.load(),
                publisher_.acceptedRevision());
            if(accepted) {
                if(const auto* bus=accepted->find(group_)) {
                    out->setProperty("name",juce::String(bus->name));
                    out->setProperty("sourceCount",static_cast<int>(bus->sourceCount()));
                    juce::Array<juce::var> names;
                    for(const auto id:bus->primarySources)
                        if(const auto* row=roster_.find(id))
                            names.add(juce::String(row->name));
                    out->setProperty("sources",juce::var(names));
                }
            }
        }
        out->setProperty("mode","NATIVE_SINGLE_CALLBACK_DRY_GROUP_PILOT");
        return juce::var(out.release());
    }
    bool render(std::uint64_t epoch,const float* const* inputs,int inputCount,
                float* mainL,float* mainR,int frames,bool muted) noexcept {
        const auto noFx=[](ChannelId,float*,float*,int) noexcept {};
        return publisher_.render(epoch,inputs,inputCount,mainL,mainR,frames,
                                 muted,noFx);
    }
private:
    DynamicChannelRoster roster_{};
    DynamicNativeSessionPublisher publisher_{};
    ChannelId group_{},main_{};
    std::atomic<bool> active_{false};
    std::atomic<std::uint64_t> epoch_{0},generation_{0};
    std::atomic<int> inputCount_{-1},actualMainL_{-1},actualMainR_{-1};
};
} // namespace armix::mixer
