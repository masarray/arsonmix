// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelRoster.h"
#include <algorithm>
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
};
enum class DynamicGraphFault : std::uint8_t {
    none, noMaster, multipleMasters, invalidFrameCapacity,
    invalidPhysicalCount, missingSource, invalidSource,
    invalidSendDestination, mainFeedback, cycle, invalidChannel,
    scratchBudget, overflow
};
struct DynamicGraphError final {
    DynamicGraphFault fault=DynamicGraphFault::none;
    ChannelId id{};
};

class DynamicPreparedMixerBlock final {
    struct Edge final {
        std::size_t destination=0;
        float gainL=0.f,gainR=0.f;
        bool pre=false;
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
        std::size_t main=notScheduled;
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
                if(to==i) {error={DynamicGraphFault::cycle,rows[i].id};return false;}
                const float factor=rows[i].format==ProcessingFormat::mono &&
                                   send.preFader?0.7071067811865475f:1.f;
                outgoing[i].push_back({to,send.gain*factor,send.gain*factor,send.preFader});
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

        // Live lanes are only those that can actually reach MAIN.
        // Adding 1,024 disconnected descriptors does NOT reserve 2,048
        // huge per-channel scratch buffers or invoke 1,024 VST callbacks.
        std::vector<unsigned char> reachesMain(rows.size(),0);
        if(next.lanes_[main].enabled)reachesMain[main]=1;
        for(auto k=order.rbegin();k!=order.rend();++k)
            if(next.lanes_[*k].enabled)
                for(const auto& edge:outgoing[*k])
                    if(next.lanes_[edge.destination].enabled &&
                       reachesMain[edge.destination])reachesMain[*k]=1;
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
            if(reachesMain[i] && (hasSignal[i] || i==main))
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
        destination=std::move(next);
        return true;
    }

    template<class Fx>
    bool process(const float* const* physical,int physicalCount,
                 float* outputL,float* outputR,int frames,Fx&& processFx) noexcept {
        if(frames<=0 || !outputL || !outputR)return false;
        std::fill_n(outputL,frames,0.f);
        std::fill_n(outputR,frames,0.f);
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
                for(int f=0;f<frames;++f){
                    dl[f]+=l[f]*edge.gainL;dr[f]+=r[f]*edge.gainR;
                }
            }
            for(int f=0;f<frames;++f){
                l[f]*=lane.gainL;r[f]*=lane.gainR;
            }
            for(std::size_t e=lane.first;e<lane.last;++e) {
                const auto& edge=edges_[e];
                if(edge.pre)continue;
                const auto& receiver=lanes_[edge.destination];
                float* const dl=scratch_.data()+receiver.slot*2*block;
                float* const dr=dl+block;
                for(int f=0;f<frames;++f){
                    dl[f]+=l[f]*edge.gainL;dr[f]+=r[f]*edge.gainR;
                }
            }
        }
        if(main_!=notScheduled && lanes_[main_].enabled) {
            const auto& master=lanes_[main_];
            if(master.slot<renderOrder_.size()){
                const float* const l=scratch_.data()+master.slot*2*block;
                const float* const r=l+block;
                std::copy_n(l,frames,outputL);std::copy_n(r,frames,outputR);
            }
        }
        return true;
    }
private:
    std::vector<Lane> lanes_;
    std::vector<Edge> edges_;
    std::vector<std::size_t> renderOrder_;
    std::vector<float> scratch_;
    std::size_t main_=notScheduled;
    int capacity_=0;
};
} // namespace armix::mixer
