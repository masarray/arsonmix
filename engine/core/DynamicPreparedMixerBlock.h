// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelRoster.h"
#include "RealtimeSlew64.h"
#include "RealtimeStereoControlWord.h"
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
        bool monoPan=false; // mono source/processing, independent of VST rack width
        float gainL=1.f,gainR=1.f;
        std::size_t first=0,last=0,slot=0;
    };
    // Immutable topology + atomics for live control targets. Callback-only
    // smoothed state is preallocated per accepted graph revision.
    // One atomic pair prevents a PAN/fader request crossing the callback
    // between separate L and R loads. A torn stereo target can cause a
    // transient image/level jump even with sample ramps. 64-bit atomics are
    // required lock-free: callback must never invoke a hidden mutex.
    using Word=armix::dsp::RealtimeStereoControlWord;
    struct StereoGainPair final {float left=1.f,right=1.f;};
    // True microphone mono uses equal-power PAN even when a stereo VST rack
    // processes its duplicated L/R PCM. A stereo physical source uses
    // balance PAN unless explicitly folded to mono processing. Computing
    // the coefficient is CONTROL-only; the callback reads one atomic pair.
    static StereoGainPair coefficients(bool monoPan,float gain,float pan) noexcept {
        if(monoPan)
            return {gain*std::sqrt(.5f*(1.f-pan)),
                    gain*std::sqrt(.5f*(1.f+pan))};
        return {gain*(pan>0.f?1.f-pan:1.f),
                gain*(pan<0.f?1.f+pan:1.f)};
    }
    struct ChannelTarget final {
        std::atomic<std::uint64_t> state{Word::encode(1.f,1.f,false,false)};
        armix::dsp::RealtimeSlew64 appliedL{},appliedR{},gate{}; // AUDIO only
    };
    // CONTROL only packs already verified callback-ordinal physical pin pairs.
    // 0xFFFF means missing right input for mono, never an active physical pin.
    static constexpr std::uint32_t packPhysical(int left,int right) noexcept {
        return static_cast<std::uint32_t>(left) |
               (static_cast<std::uint32_t>(right<0?0xffff:right)<<16);
    }
    static constexpr int physicalLeft(std::uint32_t word) noexcept {
        return static_cast<int>(word&0xffffu);
    }
    static constexpr int physicalRight(std::uint32_t word) noexcept {
        const auto right=static_cast<int>((word>>16)&0xffffu);
        return right==0xffff?-1:right;
    }
    struct SourceFade final {
        std::uint32_t current=0,next=0;
        int remaining=0; // AUDIO only; pending CONTROL target is one atomic
    };
    struct SendTarget final {
        static_assert(std::atomic<float>::is_always_lock_free,
            "Realtime SEND must never enter a hidden blocking atomic path");
        std::atomic<float> gain{1.f};
        armix::dsp::RealtimeSlew64 applied{};
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
        // Reuse the already-built ChannelId lookup; no O(N) scan on every
        // rapid GUI fader tick, no callback hash lookup and no new allocation.
        const auto found=controlIndex_.find(id.value);
        if(found==controlIndex_.end())return false;
        const auto i=found->second;
        const auto gains=coefficients(lanes_[i].monoPan,gain,pan);
        auto& target=channelTargets_[i];
        const auto old=Word::decode(target.state.load(std::memory_order_relaxed));
        // Serialized native CONTROL writer: keep mute/solo atomically paired
        // with this fader/PAN target, never four separately loaded controls.
        target.state.store(Word::encode(gains.left,gains.right,old.mute,old.solo),
                           std::memory_order_release);
        return true;
    }
    // Canonical physical-pin validation shared by structural preflight and
    // accepted realtime SOURCE changes. Never normalize an invalid draft
    // into an apparently successful ACK by replacing it with old pins.
    static bool validPhysicalPatch(int physicalInputs,int inputL,int inputR,
                                   bool stereo) noexcept {
        return physicalInputs>=0 && physicalInputs<=256 &&
               inputL>=0 && inputL<physicalInputs && inputL<0xffff &&
               (stereo ? (inputR>=0 && inputR<physicalInputs &&
                          inputR<0xffff && inputR!=inputL)
                       : inputR==-1);
    }
    // Same-format live physical INPUT switch. Stable ChannelId and VST
    // instance never change; retain one latest target, no command FIFO.
    // A new source request during the 64-sample crossfade is coalesced and
    // starts only after the current ramp completes, avoiding a click.
    bool setPhysicalSource(ChannelId id,int inputL,int inputR,
                           bool stereo) noexcept {
        if(!id.value ||
           !validPhysicalPatch(physicalInputs_,inputL,inputR,stereo))
            return false;
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end())return false;
        const auto& lane=lanes_[it->second];
        if(lane.role!=ChannelRole::input || lane.inputL<0 ||
           lane.inputStereo!=stereo)return false; // format requires APPLY
        sourceTargets_[it->second].store(packPhysical(inputL,inputR),
                                          std::memory_order_release);
        return true;
    }
    bool setChannelMuteSolo(ChannelId id,bool mute,bool solo) noexcept {
        if(!id.value)return false;
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end())return false;
        auto& target=channelTargets_[it->second];
        const auto old=Word::decode(target.state.load(std::memory_order_relaxed));
        target.state.store(Word::encode(old.left,old.right,mute,solo),
                           std::memory_order_release);
        return true;
    }
    // Change gain ONLY on an existing parallel send (PRE or POST).
    // Exclusive primary OUT and source/destination edits stay structural.
    bool setParallelSendGain(ChannelId source,ChannelId destination,
                             bool preFader,float gain) noexcept {
        if(!source.value || !destination.value || !std::isfinite(gain) ||
           gain<0.f || gain>4.f)return false;
        const auto found=controlIndex_.find(source.value);
        if(found==controlIndex_.end())return false;
        const auto& lane=lanes_[found->second];
        // Only that source's prepared CSR outgoing edges are examined.
        // The public GUI can address arbitrary stable ChannelIds regardless
        // of visual bank, strip position or hidden channels.
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
    bool contains(ChannelId id) const noexcept {
        return controlIndex_.find(id.value)!=controlIndex_.end();
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
        next.physicalInputs_=req.physicalInputs;
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
            lane.enabled=true; // keep prepared FX/tail alive across MUTE/SOLO
            lane.monoPan=row.format==ProcessingFormat::mono;
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
               !validPhysicalPatch(req.physicalInputs,patch.inputL,
                                   patch.inputR,patch.stereo)) {
                error={DynamicGraphFault::invalidSource,patch.id};return false;
            }
            lane.inputL=patch.inputL;lane.inputR=patch.inputR;
            lane.inputStereo=patch.stereo;
            // The channel SOURCE and the INSERT PROCESSING FORMAT are
            // independent. Mono mic -> stereo VST still retains mono PAN.
            if(!patch.stereo)lane.monoPan=true;
        }
        for(std::size_t i=0;i<rows.size();++i) {
            const auto gains=coefficients(next.lanes_[i].monoPan,
                                          rows[i].faderLinear,rows[i].pan);
            next.lanes_[i].gainL=gains.left;
            next.lanes_[i].gainR=gains.right;
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
                // WIRE-first MVP admits ONE audible edge per ordered pair.
                // A legacy PRE + POST pair is still duplicate dry PCM,
                // even when the tap flag differs. Reject before publication.
                const bool duplicate=std::any_of(
                    outgoing[i].begin(),outgoing[i].end(),
                    [&](const Edge& existing) {
                        return existing.destination==to;
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
        next.initialAudible_.resize(rows.size());
        for(std::size_t i=0;i<rows.size();++i)
            next.initialAudible_[i]=static_cast<unsigned char>(
                !rows[i].mute && (!anySolo||solos[i]!=0));
        next.topologicalOrder_=order; // control-prepared, never RT sorted

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
        next.sourceTargets_=std::make_unique<std::atomic<std::uint32_t>[]>(rows.size());
        next.sourceFades_=std::make_unique<SourceFade[]>(rows.size());
        for(std::size_t i=0;i<rows.size();++i){
            const auto& lane=next.lanes_[i];
            if(lane.inputL<0)continue;
            const auto initial=packPhysical(lane.inputL,lane.inputR);
            next.sourceTargets_[i].store(initial,std::memory_order_relaxed);
            next.sourceFades_[i].current=next.sourceFades_[i].next=initial;
        }
        next.channelTargets_=std::make_unique<ChannelTarget[]>(rows.size());
        next.blockTargets_.resize(rows.size());
        next.soloAncestors_.assign(rows.size(),0);
        next.soloDescendants_.assign(rows.size(),0);
        for(std::size_t i=0;i<rows.size();++i) {
            auto& target=next.channelTargets_[i];
            target.state.store(Word::encode(next.lanes_[i].gainL,
                next.lanes_[i].gainR,rows[i].mute,rows[i].solo),
                std::memory_order_relaxed);
            target.appliedL.reset(next.lanes_[i].gainL);
            target.appliedR.reset(next.lanes_[i].gainR);
            target.gate.reset(next.initialAudible_[i]?1.f:0.f);
        }
        next.sendTargets_=std::make_unique<SendTarget[]>(next.edges_.size());
        for(std::size_t e=0;e<next.edges_.size();++e){
            const float initial=next.edges_[e].initialGain;
            next.sendTargets_[e].gain.store(initial,std::memory_order_relaxed);
            next.sendTargets_[e].applied.reset(initial);
        }
        // Recycle the CONTROL compiler's verified stable-ID index instead
        // of allocating a second runtime lookup or scanning all channels.
        // This table is NEVER touched from the audio callback.
        next.controlIndex_=std::move(byId);
        destination=std::move(next);
        return true;
    }

    template<class Fx>
    bool process(const float* const* physical,int physicalCount,
                 float* outputL,float* outputR,int frames,Fx&& processFx,
                 float* monitorL=nullptr,float* monitorR=nullptr,
                 std::span<float> channelPeaksL={},
                 std::span<float> channelPeaksR={}) noexcept {
        if(frames<=0 || !outputL || !outputR)return false;
        // The physical 10CH/4AUX callback requests only the stable
        // legacy prefix of an expanded ChannelId roster. Dynamic extra
        // GROUP/FX RETURN lanes still process PCM, but need no JS meter
        // until a native meter UI consumer is explicitly subscribed.
        // Never allocate proportional-to-roster arrays on AUDIO.
        if(channelPeaksL.size()!=channelPeaksR.size())
            return false;
        if(!channelPeaksL.empty()) {
            std::fill_n(channelPeaksL.data(),channelPeaksL.size(),0.f);
            std::fill_n(channelPeaksR.data(),channelPeaksR.size(),0.f);
        }
        if(outputL==outputR ||
           (monitorL==nullptr)!=(monitorR==nullptr) ||
           (monitor_!=notScheduled && !monitorL) ||
           (monitorL && (monitorL==monitorR || monitorL==outputL ||
                         monitorL==outputR || monitorR==outputL ||
                         monitorR==outputR)))return false;
        // A JUCE driver can expose input and output backed by the same PCM
        // storage. Failure still clears every output; successful processing
        // must defer that clear until ALL input sources have been read into
        // their own prepared scratch lanes (including independent MONITOR).
        if(frames>capacity_ || physicalCount<0) {
            std::fill_n(outputL,frames,0.f);
            std::fill_n(outputR,frames,0.f);
            if(monitorL){
                std::fill_n(monitorL,frames,0.f);
                std::fill_n(monitorR,frames,0.f);
            }
            return false;
        }
        const auto block=static_cast<std::size_t>(capacity_);
        // One canonical 64-bit target per logical channel, read once/block.
        // Realtime SOLO closure reuses CONTROL-prepared order and CSR edges.
        // No allocation, mutex, VST3 lease/reference, sorting or worker on RT.
        bool anySolo=false;
        for(std::size_t i=0;i<lanes_.size();++i) {
            const auto state=Word::decode(
                channelTargets_[i].state.load(std::memory_order_acquire));
            blockTargets_[i]=state;
            const auto root=static_cast<unsigned char>(state.solo&&!state.mute);
            soloAncestors_[i]=root;soloDescendants_[i]=root;
            anySolo|=root!=0;
        }
        if(anySolo) {
            for(auto it=topologicalOrder_.rbegin();it!=topologicalOrder_.rend();++it){
                const auto i=*it;
                for(std::size_t e=lanes_[i].first;e<lanes_[i].last;++e)
                    if(soloAncestors_[edges_[e].destination])soloAncestors_[i]=1;
            }
            // Forward from original SOLO roots, not unrelated upstream sends.
            for(const auto i:topologicalOrder_)
                if(soloDescendants_[i])
                    for(std::size_t e=lanes_[i].first;e<lanes_[i].last;++e)
                        soloDescendants_[edges_[e].destination]=1;
        }
        for(std::size_t i=0;i<lanes_.size();++i) {
            const bool audible=!blockTargets_[i].mute &&
                (!anySolo||soloAncestors_[i]||soloDescendants_[i]);
            channelTargets_[i].gate.retarget(audible?1.f:0.f);
        }
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
            if(lane.inputL>=0 && physical) {
                auto& fade=sourceFades_[i];
                const auto desired=sourceTargets_[i].load(std::memory_order_acquire);
                if(fade.remaining==0 && desired!=fade.current) {
                    fade.next=desired;fade.remaining=armix::dsp::RealtimeSlew64::samples;
                }
                // Resolve each verified physical pair ONCE per block; the
                // inner sample loop only blends already-addressed PCM.
                const auto resolve=[&](std::uint32_t pair,int side) noexcept
                    ->const float* {
                    const int a=physicalLeft(pair),b=physicalRight(pair);
                    if(a<0||a>=physicalCount||!physical[a])return nullptr;
                    if(lane.inputStereo){
                        if(b<0||b>=physicalCount||!physical[b])return nullptr;
                        return side==0?physical[a]:physical[b];
                    }
                    return physical[a];
                };
                const float* oldL=resolve(fade.current,0);
                const float* oldR=resolve(fade.current,1);
                const float* newL=fade.remaining?resolve(fade.next,0):oldL;
                const float* newR=fade.remaining?resolve(fade.next,1):oldR;
                if(fade.remaining==0) {
                    if(oldL&&oldR)
                        for(int f=0;f<frames;++f){l[f]+=oldL[f];r[f]+=oldR[f];}
                } else {
                    for(int f=0;f<frames;++f) {
                        const float alpha=1.f-
                            static_cast<float>(fade.remaining-1)*
                            (1.f/static_cast<float>(armix::dsp::RealtimeSlew64::samples));
                        const float beforeL=oldL?oldL[f]:0.f;
                        const float beforeR=oldR?oldR[f]:0.f;
                        const float afterL=newL?newL[f]:0.f;
                        const float afterR=newR?newR[f]:0.f;
                        l[f]+=beforeL+(afterL-beforeL)*alpha;
                        r[f]+=beforeR+(afterR-beforeR)*alpha;
                        --fade.remaining;
                        if(fade.remaining==0){
                            fade.current=fade.next;
                            // New CONTROL requests are consumed next block;
                            // never change the source mid-sample.
                            oldL=newL;oldR=newR;
                            for(int k=f+1;k<frames;++k)
                                if(oldL&&oldR){l[k]+=oldL[k];r[k]+=oldR[k];}
                            break;
                        }
                    }
                }
            }
            if(lane.format==ProcessingFormat::mono)
                for(int f=0;f<frames;++f){
                    const float v=.5f*(l[f]+r[f]);l[f]=v;r[f]=v;
                }
            processFx(lane.id,l,r,frames); // same adapter signature for CH1..N
            // A MUTE/SOLO transition must silence PRE and POST sends, but
            // keep the original processor alive. Ramp spans 64 real samples.
            auto& gate=channelTargets_[i].gate;
            for(int f=0;f<frames;++f){
                const float g=gate.next();l[f]*=g;r[f]*=g;
            }
            for(std::size_t e=lane.first;e<lane.last;++e) {
                const auto& edge=edges_[e];
                if(!edge.pre)continue;
                const auto& receiver=lanes_[edge.destination];
                float* const dl=scratch_.data()+receiver.slot*2*block;
                float* const dr=dl+block;
                auto& target=sendTargets_[e];
                const float goal=target.gain.load(std::memory_order_acquire);
                // Coalesced, callback-continuous 64 SAMPLE slew. A 4-frame
                // ASIO callback advances only 4/64 of the requested change.
                target.applied.retarget(goal);
                for(int f=0;f<frames;++f){
                    const float applied=target.applied.next();
                    dl[f]+=l[f]*applied*edge.panL;
                    dr[f]+=r[f]*applied*edge.panR;
                }
            }
            auto& controls=channelTargets_[i];
            const auto& goal=blockTargets_[i];
            const float goalL=goal.left,goalR=goal.right;
            // Left and right use ONE atomically read stereo goal. Ramp state
            // survives variable hardware callback sizes and gesture reversals.
            controls.appliedL.retarget(goalL);
            controls.appliedR.retarget(goalR);
            float observedL=0.f,observedR=0.f;
            for(int f=0;f<frames;++f){
                l[f]*=controls.appliedL.next();
                r[f]*=controls.appliedR.next();
                observedL=std::max(observedL,std::abs(l[f]));
                observedR=std::max(observedR,std::abs(r[f]));
            }
            if(i<channelPeaksL.size()) {
                channelPeaksL[i]=observedL;
                channelPeaksR[i]=observedR;
            }
            for(std::size_t e=lane.first;e<lane.last;++e) {
                const auto& edge=edges_[e];
                if(edge.pre)continue;
                const auto& receiver=lanes_[edge.destination];
                float* const dl=scratch_.data()+receiver.slot*2*block;
                float* const dr=dl+block;
                auto& target=sendTargets_[e];
                const float goal=target.gain.load(std::memory_order_acquire);
                // Coalesced, callback-continuous 64 SAMPLE slew. A 4-frame
                // ASIO callback advances only 4/64 of the requested change.
                target.applied.retarget(goal);
                for(int f=0;f<frames;++f){
                    const float applied=target.applied.next();
                    dl[f]+=l[f]*applied*edge.panL;
                    dr[f]+=r[f]*applied*edge.panR;
                }
            }
        }
        // Physical input PCM has been fully staged and processed into
        // scratch. Only NOW may output buffers be cleared: on supported
        // in-place/aliased hardware buffers an earlier clear would erase the
        // input before the source strips consumed it. This adds no allocation,
        // copy, lock, callback, DSP pass or latency.
        std::fill_n(outputL,frames,0.f);
        std::fill_n(outputR,frames,0.f);
        if(monitorL){
            std::fill_n(monitorL,frames,0.f);
            std::fill_n(monitorR,frames,0.f);
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
    std::unordered_map<std::uint64_t,std::size_t> controlIndex_; // CONTROL only
    std::vector<Edge> edges_;
    std::vector<std::size_t> renderOrder_,topologicalOrder_;
    std::vector<unsigned char> initialAudible_,soloAncestors_,soloDescendants_;
    std::vector<Word::Snapshot> blockTargets_; // all allocated before RT
    std::vector<float> scratch_;
    std::unique_ptr<ChannelTarget[]> channelTargets_;
    std::unique_ptr<std::atomic<std::uint32_t>[]> sourceTargets_;
    std::unique_ptr<SourceFade[]> sourceFades_;
    std::unique_ptr<SendTarget[]> sendTargets_;
    std::size_t main_=notScheduled,monitor_=notScheduled;
    bool mainMonoAfterFx_=false;
    int capacity_=0,physicalInputs_=0;
};
} // namespace armix::mixer
