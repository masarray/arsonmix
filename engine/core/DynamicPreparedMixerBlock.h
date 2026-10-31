// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelRoster.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <numeric>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

// DYN1A: replacement-ready runtime-sized, device-INDEPENDENT prepared PCM
// kernel. NOT connected to a second host audio callback or a GUI +CHANNEL.
// Control builds/validates/budgets; audio thread processes immutable schedule,
// preallocated scratch and caller-owned FX references. No heap/locks in process.
// Migrate the current ten-strip LiveDryRoutingLab to this ONE kernel at DYN1B.
namespace armix::mixer {

struct DynamicSourcePatch final {
    ChannelId id{};
    int inputL=-1, inputR=-1; // callback pointer ordinals, NOT sparse device labels
    bool stereo=false;
};
struct DynamicGraphRequest final {
    std::vector<DynamicSourcePatch> sources;
    int physicalInputs=0;
    int maxFrames=512;
    std::size_t maxScratchBytes=64U*1024U*1024U; // admission, not channel-count cap
    bool mainMonoAfterFx=false; // legacy MAIN mono occurs after MAIN VST+balance
};
enum class DynamicGraphFault : std::uint8_t {
    none, noMaster, multipleMasters, invalidFrameCapacity,
    invalidPhysicalCount, missingSource, invalidSource,
    invalidSendDestination, duplicateSend, mainFeedback, cycle, invalidChannel,
    scratchBudget, overflow, multipleMonitors, monitorFeedback
};
struct DynamicGraphError final {
    DynamicGraphFault fault=DynamicGraphFault::none;
    ChannelId id{};
};

class DynamicPreparedMixerBlock final {
    struct Edge final {
        std::size_t destination=0;
        float panL=0.f,panR=0.f,initialGain=1.f;
        bool pre=false,primary=false;
    };
    struct Lane final {
        ChannelId id{};
        ProcessingFormat format=ProcessingFormat::mono;
        ChannelRole role=ChannelRole::input;
        int inputL=-1,inputR=-1;
        bool inputStereo=false,enabled=false;
        float gainL=1.f,gainR=1.f;
        std::size_t first=0,last=0,slot=0;
    };
    // Immutable topology + atomics for live control targets. Callback-only
    // smoothed state is preallocated per accepted graph revision.
    struct ChannelTarget final {
        std::atomic<float> left{1.f},right{1.f};
        float appliedL=1.f,appliedR=1.f;
    };
    struct SendTarget final {
        std::atomic<float> gain{1.f};
        float applied=1.f;
    };
    static constexpr std::size_t notScheduled=std::numeric_limits<std::size_t>::max();
public:
    DynamicPreparedMixerBlock()=default;
    DynamicPreparedMixerBlock(const DynamicPreparedMixerBlock&)=delete;
    DynamicPreparedMixerBlock& operator=(const DynamicPreparedMixerBlock&)=delete;
    DynamicPreparedMixerBlock(DynamicPreparedMixerBlock&&)=default;
    DynamicPreparedMixerBlock& operator=(DynamicPreparedMixerBlock&&)=default;
    std::size_t channelCount() const noexcept {return lanes_.size();}
    std::size_t activeChannels() const noexcept {return renderOrder_.size();}
    int capacity() const noexcept {return capacity_;}
    std::size_t scratchBytes() const noexcept {return scratch_.size()*sizeof(float);}
    // CONTROL ONLY. Stable ChannelId lookup is outside the audio callback.
    // Does not replace the prepared audio graph, modify DSP topology, or
    // instantiate/copy a VST3 plugin. Audible gain is dezippered in process().
    bool setChannelMix(ChannelId id,float gain,float pan) noexcept {
        if(!id.value || !std::isfinite(gain) || gain<0.f || gain>4.f ||
           !std::isfinite(pan) || pan< -1.f || pan>1.f)return false;
        for(std::size_t i=0;i<lanes_.size();++i)if(lanes_[i].id==id) {
            const bool mono=lanes_[i].format==ProcessingFormat::mono;
            const float left=mono?gain*std::sqrt(.5f*(1.f-pan)):
                gain*(pan>0.f?1.f-pan:1.f);
            const float right=mono?gain*std::sqrt(.5f*(1.f+pan)):
                gain*(pan<0.f?1.f+pan:1.f);
            channelTargets_[i].left.store(left,std::memory_order_release);
            channelTargets_[i].right.store(right,std::memory_order_release);
            return true;
        }
        return false;
    }
    // Change gain ONLY on an existing parallel send (PRE or POST).
    // Exclusive primary OUT and source/destination edits stay structural.
    bool setParallelSendGain(ChannelId source,ChannelId destination,
                             bool preFader,float gain) noexcept {
        if(!source.value || !destination.value || !std::isfinite(gain) ||
           gain<0.f || gain>4.f)return false;
        for(const auto& lane:lanes_)if(lane.id==source) {
            for(std::size_t e=lane.first;e<lane.last;++e){
                const auto& edge=edges_[e];
                if(lanes_[edge.destination].id==destination &&
                   edge.pre==preFader && !edge.primary) {
                    sendTargets_[e].gain.store(gain,std::memory_order_release);
                    return true;
                }
            }
            return false;
        }
        return false;
    }
    bool contains(ChannelId id) const noexcept {
        return std::any_of(lanes_.begin(),lanes_.end(),
            [id](const Lane& x){return x.id==id;});
    }

    // Control thread only. Rejection leaves the previous prepared object
    // byte-for-byte usable; caller must publish with epoch/revision MUTE/ACK.
    static bool compile(const DynamicChannelRoster& roster,
                        const DynamicGraphRequest& req,
                        DynamicPreparedMixerBlock& destination,
                        DynamicGraphError& error) {
        error={};
        if(req.maxFrames<=0 || req.maxFrames>8192) {
            error.fault=DynamicGraphFault::invalidFrameCapacity;return false;
        }
        if(req.physicalInputs<0 || req.physicalInputs>256) {
            error.fault=DynamicGraphFault::invalidPhysicalCount;return false;
        }
        DynamicPreparedMixerBlock next;
        next.capacity_=req.maxFrames;
        const auto& rows=roster.channels();
        next.lanes_.reserve(rows.size());
        std::unordered_map<std::uint64_t,std::size_t> byId;
        byId.reserve(rows.size());
        std::size_t main=notScheduled,monitor=notScheduled;
        for(std::size_t i=0;i<rows.size();++i) {
            const auto& row=rows[i];
            if(row.id.value==0 || !std::isfinite(row.faderLinear) ||
               row.faderLinear<0.f || row.faderLinear>4.f ||
               !std::isfinite(row.pan) || row.pan< -1.f || row.pan>1.f ||
               (row.format!=ProcessingFormat::mono &&
                row.format!=ProcessingFormat::stereo) ||
               !byId.emplace(row.id.value,i).second) {
                error={DynamicGraphFault::invalidChannel,row.id};return false;
            }
            if(row.role==ChannelRole::main) {
                if(main!=notScheduled) {
                    error={DynamicGraphFault::multipleMasters,row.id};return false;
                }
                main=i;
            } else if(row.role==ChannelRole::monitor) {
                if(monitor!=notScheduled) {
                    error={DynamicGraphFault::multipleMonitors,row.id};return false;
                }
                monitor=i;
            } else if(row.role!=ChannelRole::input &&
                      row.role!=ChannelRole::fxReturn &&
                      row.role!=ChannelRole::group) {
                error={DynamicGraphFault::invalidChannel,row.id};return false;
            }
            Lane lane;
            lane.id=row.id;lane.format=row.format;lane.role=row.role;
            lane.enabled=!row.mute;
            if(row.format==ProcessingFormat::mono) {
                lane.gainL=row.faderLinear*std::sqrt(0.5f*(1.f-row.pan));
                lane.gainR=row.faderLinear*std::sqrt(0.5f*(1.f+row.pan));
            }else{
                lane.gainL=row.faderLinear*(row.pan>0.f?1.f-row.pan:1.f);
                lane.gainR=row.faderLinear*(row.pan<0.f?1.f+row.pan:1.f);
            }
            next.lanes_.push_back(lane);
        }
        if(main==notScheduled){error.fault=DynamicGraphFault::noMaster;return false;}
        next.main_=main;
        next.monitor_=monitor;
        next.mainMonoAfterFx_=req.mainMonoAfterFx;
        for(const auto& patch:req.sources) {
            const auto it=byId.find(patch.id.value);
            if(it==byId.end()){error={DynamicGraphFault::missingSource,patch.id};return false;}
            auto& lane=next.lanes_[it->second];
            if(lane.role!=ChannelRole::input || lane.inputL!=-1 ||
               patch.inputL<0 || patch.inputL>=req.physicalInputs ||
               (patch.stereo && (patch.inputR<0 ||
                  patch.inputR>=req.physicalInputs || patch.inputL==patch.inputR)) ||
               (!patch.stereo && patch.inputR!=-1)) {
                error={DynamicGraphFault::invalidSource,patch.id};return false;
            }
            lane.inputL=patch.inputL;lane.inputR=patch.inputR;
            lane.inputStereo=patch.stereo;
        }

        // Stable-ID edges are resolved exactly ONCE; all runtime references
        // are dense ordinals in a CSR adjacency schedule (no RT hash lookups).
        std::vector<std::vector<Edge>> outgoing(rows.size());
        std::vector<std::vector<std::size_t>> parents(rows.size());
        std::vector<std::size_t> indegree(rows.size(),0);
        for(std::size_t i=0;i<rows.size();++i) {
            for(const auto& send:rows[i].sends) {
                const auto it=byId.find(send.destination.value);
                if(it==byId.end() || !std::isfinite(send.gain) ||
                   send.gain<0.f || send.gain>4.f) {
                    error={DynamicGraphFault::invalidSendDestination,rows[i].id};return false;
                }
                const auto to=it->second;
                if(i==main) {error={DynamicGraphFault::mainFeedback,rows[i].id};return false;}
                if(i==monitor) {error={DynamicGraphFault::monitorFeedback,rows[i].id};return false;}
                if(to==i) {error={DynamicGraphFault::cycle,rows[i].id};return false;}
                // A duplicate edge would sum the same send twice and can
                // cause unexpectedly loud signal. Different taps are legal.
                const bool duplicate=std::any_of(
                    outgoing[i].begin(),outgoing[i].end(),
                    [&](const Edge& existing) {
                        return existing.destination==to &&
                               existing.pre==send.preFader;
                    });
                if(duplicate) {
                    error={DynamicGraphFault::duplicateSend,rows[i].id};
                    return false;
                }
                if(!std::isfinite(send.pan)||send.pan< -1.f||send.pan>1.f){
                    error={DynamicGraphFault::invalidSendDestination,rows[i].id};
                    return false;
                }
                const bool monoPre=rows[i].format==ProcessingFormat::mono && send.preFader;
                const float gl=monoPre?std::sqrt(.5f*(1.f-send.pan)):
                    (send.pan>0.f?1.f-send.pan:1.f);
                const float gr=monoPre?std::sqrt(.5f*(1.f+send.pan)):
                    (send.pan<0.f?1.f+send.pan:1.f);
                outgoing[i].push_back({to,gl,gr,send.gain,send.preFader,
                                       send.primaryOutput});
                parents[to].push_back(i);
                ++indegree[to];
            }
        }
        // Kahn compile O(V+E), no per-callback DAG sort or send scan.
        std::deque<std::size_t> queue;
        for(std::size_t i=0;i<rows.size();++i)
            if(indegree[i]==0)queue.push_back(i);
        std::vector<std::size_t> order;
        order.reserve(rows.size());
        while(!queue.empty()) {
            const auto i=queue.front();queue.pop_front();
            order.push_back(i);
            for(const auto& edge:outgoing[i])
                if(--indegree[edge.destination]==0)queue.push_back(edge.destination);
        }
        if(order.size()!=rows.size()){
            error.fault=DynamicGraphFault::cycle;return false;
        }
        // Solo closure: include source ancestors and downstream descendants;
        // preserve MUTE supremacy. Everything else must remain silent.
        std::vector<unsigned char> solos(rows.size(),0);
        bool anySolo=false;
        for(std::size_t i=0;i<rows.size();++i)
            if(rows[i].solo && !rows[i].mute){solos[i]=1;anySolo=true;}
        if(anySolo) {
            for(auto k=order.rbegin();k!=order.rend();++k)
                for(const auto& edge:outgoing[*k])
                    if(solos[edge.destination])solos[*k]=1;
            // Forward closure MUST start from original solos, not upstream
            // ancestors (those may feed other unrelated downstream paths).
            std::vector<unsigned char> forward(rows.size(),0);
            for(std::size_t i=0;i<rows.size();++i)
                if(rows[i].solo && !rows[i].mute)forward[i]=1;
            for(const auto i:order)
                if(forward[i])
                    for(const auto& edge:outgoing[i])forward[edge.destination]=1;
            for(std::size_t i=0;i<rows.size();++i)
                solos[i]=static_cast<unsigned char>(solos[i]||forward[i]);
        }
        for(std::size_t i=0;i<rows.size();++i)
            next.lanes_[i].enabled=next.lanes_[i].enabled &&
                                    (!anySolo || solos[i]!=0);

        // One immutable render schedule serves BOTH logical output sinks.
        // Active monitor-only channels must not vanish merely because they
        // do not route to MAIN. Disconnected descriptors remain scratch-free.
        std::vector<unsigned char> reachesOutput(rows.size(),0);
        if(next.lanes_[main].enabled)reachesOutput[main]=1;
        if(monitor!=notScheduled && next.lanes_[monitor].enabled)
            reachesOutput[monitor]=1;
        for(auto k=order.rbegin();k!=order.rend();++k)
            if(next.lanes_[*k].enabled)
                for(const auto& edge:outgoing[*k])
                    if(next.lanes_[edge.destination].enabled &&
                       reachesOutput[edge.destination])
                        reachesOutput[*k]=1;
        // Reverse reachability alone would schedule 128 silent lanes with
        // sends to MAIN. Include only actual capture sources, explicit FX
        // generator lanes, and descendants; preserve empty MAIN for mute.
        std::vector<unsigned char> hasSignal(rows.size(),0);
        for(std::size_t i=0;i<rows.size();++i)
            hasSignal[i]=static_cast<unsigned char>(
                next.lanes_[i].inputL>=0 || !rows[i].insertCatalogIds.empty());
        for(const auto i:order) {
            if(!next.lanes_[i].enabled || !hasSignal[i])continue;
            for(const auto& edge:outgoing[i])
                if(next.lanes_[edge.destination].enabled)
                    hasSignal[edge.destination]=1;
        }
        for(const auto i:order)
            if(reachesOutput[i] && (hasSignal[i] || i==main || i==monitor))
                next.renderOrder_.push_back(i);
        // Edges must only target retained scratch lanes. This is distinct
        // from "reachable from MAIN": a silent incoming lane has no slot.
        std::vector<unsigned char> scheduled(rows.size(),0);
        for(const auto i:next.renderOrder_)scheduled[i]=1;

        const auto active=next.renderOrder_.size();
        const std::size_t frame=static_cast<std::size_t>(req.maxFrames);
        if(frame>0 && active>req.maxScratchBytes/(2*sizeof(float)*frame)) {
            error.fault=DynamicGraphFault::scratchBudget;return false;
        }
        // One immutable CSR adjacency; only main-reachable edges retained.
        for(std::size_t i=0;i<rows.size();++i) {
            auto& lane=next.lanes_[i];
            lane.first=next.edges_.size();
            if(scheduled[i]) {
                for(const auto& edge:outgoing[i])
                    if(scheduled[edge.destination])next.edges_.push_back(edge);
            }
            lane.last=next.edges_.size();
        }
        std::size_t slot=0;
        for(auto i:next.renderOrder_)next.lanes_[i].slot=slot++;
        next.scratch_.assign(active*2*frame,0.f);
        next.channelTargets_=std::make_unique<ChannelTarget[]>(rows.size());
        for(std::size_t i=0;i<rows.size();++i) {
            auto& target=next.channelTargets_[i];
            target.left.store(next.lanes_[i].gainL,std::memory_order_relaxed);
            target.right.store(next.lanes_[i].gainR,std::memory_order_relaxed);
            target.appliedL=next.lanes_[i].gainL;
            target.appliedR=next.lanes_[i].gainR;
        }
        next.sendTargets_=std::make_unique<SendTarget[]>(next.edges_.size());
        for(std::size_t e=0;e<next.edges_.size();++e){
            const float initial=next.edges_[e].initialGain;
            next.sendTargets_[e].gain.store(initial,std::memory_order_relaxed);
            next.sendTargets_[e].applied=initial;
        }
        destination=std::move(next);
        return true;
    }

    template<class Fx>
    bool process(const float* const* physical,int physicalCount,
                 float* outputL,float* outputR,int frames,Fx&& processFx,
                 float* monitorL=nullptr,float* monitorR=nullptr) noexcept {
        if(frames<=0 || !outputL || !outputR)return false;
        if(outputL==outputR ||
           (monitorL==nullptr)!=(monitorR==nullptr) ||
           (monitor_!=notScheduled && !monitorL) ||
           (monitorL && (monitorL==monitorR || monitorL==outputL ||
                         monitorL==outputR || monitorR==outputL ||
                         monitorR==outputR)))return false;
        std::fill_n(outputL,frames,0.f);
        std::fill_n(outputR,frames,0.f);
        if(monitorL){
            std::fill_n(monitorL,frames,0.f);
            std::fill_n(monitorR,frames,0.f);
        }
        if(frames>capacity_ || physicalCount<0)return false;
        const auto block=static_cast<std::size_t>(capacity_);
        // Prepared active scratch only; zero-patched lanes cost nothing.
        for(const auto i:renderOrder_){
            auto& lane=lanes_[i];
            float* const l=scratch_.data()+lane.slot*2*block;
            float* const r=l+block;
            std::fill_n(l,frames,0.f);
            std::fill_n(r,frames,0.f);
        }
        // Incoming edges must accumulate AFTER sources have been mixed.
        // This separate clear pass is needed so later nodes keep their sends.
        for(const auto i:renderOrder_) {
            const auto& lane=lanes_[i];
            float* const l=scratch_.data()+lane.slot*2*block;
            float* const r=l+block;
            if(lane.inputL>=0 && physical && lane.inputL<physicalCount &&
               physical[lane.inputL]) {
                const float* const a=physical[lane.inputL];
                const float* const b=lane.inputStereo && lane.inputR<physicalCount &&
                                     physical[lane.inputR]?physical[lane.inputR]:nullptr;
                if(lane.inputStereo && !b) {
                    // Missing right callback pin fails silent, no invented dual-mono.
                }else for(int f=0;f<frames;++f) {
                    l[f]+=a[f];
                    r[f]+=b?b[f]:a[f];
                }
            }
            if(lane.format==ProcessingFormat::mono)
                for(int f=0;f<frames;++f){
                    const float v=.5f*(l[f]+r[f]);l[f]=v;r[f]=v;
                }
            processFx(lane.id,l,r,frames); // same adapter signature for CH1..N
            for(std::size_t e=lane.first;e<lane.last;++e) {
                const auto& edge=edges_[e];
                if(!edge.pre)continue;
                const auto& receiver=lanes_[edge.destination];
                float* const dl=scratch_.data()+receiver.slot*2*block;
                float* const dr=dl+block;
                auto& target=sendTargets_[e];
                const float goal=target.gain.load(std::memory_order_acquire);
                const int ramp=std::min(frames,64);
                const float step=(goal-target.applied)/static_cast<float>(ramp);
                float applied=target.applied;
                for(int f=0;f<frames;++f){
                    if(f<ramp)applied+=step;
                    dl[f]+=l[f]*applied*edge.panL;
                    dr[f]+=r[f]*applied*edge.panR;
                }
                target.applied=goal;
            }
            auto& controls=channelTargets_[i];
            const float goalL=controls.left.load(std::memory_order_acquire);
            const float goalR=controls.right.load(std::memory_order_acquire);
            const int ramp=std::min(frames,64);
            const float stepL=(goalL-controls.appliedL)/static_cast<float>(ramp);
            const float stepR=(goalR-controls.appliedR)/static_cast<float>(ramp);
            float currentL=controls.appliedL,currentR=controls.appliedR;
            for(int f=0;f<frames;++f){
                if(f<ramp){currentL+=stepL;currentR+=stepR;}
                l[f]*=currentL;r[f]*=currentR;
            }
            controls.appliedL=goalL;controls.appliedR=goalR;
            for(std::size_t e=lane.first;e<lane.last;++e) {
                const auto& edge=edges_[e];
                if(edge.pre)continue;
                const auto& receiver=lanes_[edge.destination];
                float* const dl=scratch_.data()+receiver.slot*2*block;
                float* const dr=dl+block;
                auto& target=sendTargets_[e];
                const float goal=target.gain.load(std::memory_order_acquire);
                const int ramp=std::min(frames,64);
                const float step=(goal-target.applied)/static_cast<float>(ramp);
                float applied=target.applied;
                for(int f=0;f<frames;++f){
                    if(f<ramp)applied+=step;
                    dl[f]+=l[f]*applied*edge.panL;
                    dr[f]+=r[f]*applied*edge.panR;
                }
                target.applied=goal;
            }
        }
        // Output roles are terminal sinks; copy each independently from
        // the SAME preallocated render graph. No second FX traversal.
        const auto copySink=[&](std::size_t sink,float* l,float* r) noexcept {
            if(sink==notScheduled || !lanes_[sink].enabled)return;
            const auto slot=lanes_[sink].slot;
            if(slot>=renderOrder_.size())return;
            const float* const source=scratch_.data()+slot*2*block;
            std::copy_n(source,frames,l);
            std::copy_n(source+block,frames,r);
        };
        copySink(main_,outputL,outputR);
        if(mainMonoAfterFx_) {
            // Mirror legacy master: balance+gain happen within MAIN, then
            // true mono folds after the master insert, never before it.
            for(int f=0;f<frames;++f) {
                const float mono=.5f*(outputL[f]+outputR[f]);
                outputL[f]=mono;outputR[f]=mono;
            }
        }
        if(monitorL)copySink(monitor_,monitorL,monitorR);
        return true;
    }
private:
    std::vector<Lane> lanes_;
    std::vector<Edge> edges_;
    std::vector<std::size_t> renderOrder_;
    std::vector<float> scratch_;
    std::unique_ptr<ChannelTarget[]> channelTargets_;
    std::unique_ptr<SendTarget[]> sendTargets_;
    std::size_t main_=notScheduled,monitor_=notScheduled;
    bool mainMonoAfterFx_=false;
    int capacity_=0;
};
} // namespace armix::mixer
