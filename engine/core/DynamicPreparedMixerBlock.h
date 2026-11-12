// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelRoster.h"
#include "RealtimeSlew64.h"
#include "StereoLatencyDelay.h"
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

// Legacy physical aggregate initializers stay valid. Virtual music never
// borrows an unused physical callback ordinal.
enum class DynamicSourceKind : std::uint8_t { physical, playerDeck };
struct DynamicSourcePatch final {
    ChannelId id{};
    int inputL=-1, inputR=-1; // physical callback ordinals only
    bool stereo=false;
    DynamicSourceKind kind=DynamicSourceKind::physical;
    std::uint64_t deckId=0; // nonzero iff kind == playerDeck
};
// A borrowed, callback-lifetime view of device-rate, worker-prepared PCM.
// The caller advances each deck's FIFO exactly once before graph fan-out.
struct DynamicPlayerPcmBlock final {
    std::uint64_t deckId=0;
    const float* left=nullptr;
    const float* right=nullptr;
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
        std::uint64_t playerDeckId=0; // disjoint from physical input
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
        armix::dsp::RealtimeSlew64 appliedL{},appliedR{},gate{},inputApplied{}; // AUDIO only
        std::atomic<float> inputGain{1.f}; // CONTROL writes, AUDIO reads
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
    // O1: immutable lane roster + one lock-free control target per lane.
    // The callback owns the transition state; a physical tap is NOT a WIRE.
    struct ExternalFade final {
        std::uint32_t current=0xffffffffu,next=0xffffffffu;
        int remaining=0;
    };
    static constexpr std::uint32_t noExternal=0xffffffffu;
    static constexpr std::uint32_t encodeExternal(int l,int r) noexcept {
        return l<0 ? noExternal :
            static_cast<std::uint32_t>(l) |
            (static_cast<std::uint32_t>(r)<<16);
    }
    static constexpr int externalLeft(std::uint32_t pair) noexcept {
        return pair==noExternal?-1:static_cast<int>(pair&0xffffu);
    }
    static constexpr int externalRight(std::uint32_t pair) noexcept {
        return pair==noExternal?-1:static_cast<int>(pair>>16);
    }
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
    // CONTROL only: input DIGITAL trim, before FX, separate from FADER/PAN.
    // 64-sample dezipper is owned by the existing native callback.
    bool setChannelInputGain(ChannelId id,float gain) noexcept {
        if(!id.value || !std::isfinite(gain) || gain<0.f || gain>16.f)return false;
        const auto found=controlIndex_.find(id.value);
        if(found==controlIndex_.end() || lanes_[found->second].role!=ChannelRole::input)
            return false;
        channelTargets_[found->second].inputGain.store(gain,std::memory_order_release);
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
    // CONTROL-only validation for source-kind-tagged graph edits.
    // A deck has two PCM planes even if the mixer channel processing is mono.
    static bool validSourcePatch(const DynamicGraphRequest& request,
                                 const DynamicSourcePatch& patch) noexcept {
        if(patch.kind==DynamicSourceKind::physical)
            return patch.deckId==0 && validPhysicalPatch(
                request.physicalInputs,patch.inputL,patch.inputR,patch.stereo);
        return patch.kind==DynamicSourceKind::playerDeck &&
               patch.deckId!=0 && patch.stereo &&
               patch.inputL==-1 && patch.inputR==-1;
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
           lane.playerDeckId!=0 || lane.inputStereo!=stereo)return false; // physical-only
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
    // CONTROL only. Empty unpatched lanes deliberately have no PCM scratch.
    bool channelScheduled(ChannelId id) const noexcept {
        const auto found=controlIndex_.find(id.value);
        return found!=controlIndex_.end()&&
            std::find(renderOrder_.begin(),renderOrder_.end(),found->second)!=renderOrder_.end();
    }

    // CONTROL only: the caller has already verified the ASIO device epoch,
    // sparse physical pins, exclusive MAIN/MONITOR ownership and graph revision.
    // Target is a callback ordinal, NEVER a physical port number.
    bool setExternalOutput(ChannelId id,int left,int right,int outputCount) noexcept {
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end() || outputCount<2 || outputCount>256 ||
           (left<0 ? right!=-1 :
             (right<0 || left>=outputCount || right>=outputCount ||
              left==right)) || !channelScheduled(id))return false;
        const auto role=lanes_[it->second].role;
        if(role!=ChannelRole::input && role!=ChannelRole::group &&
           role!=ChannelRole::fxReturn)return false;
        externalTargets_[it->second].store(
            encodeExternal(left,right),std::memory_order_release);
        return true;
    }
    // Independent installed CONTROL readback. No UI draft can claim LIVE.
    std::pair<int,int> externalOutput(ChannelId id) const noexcept {
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end())return {-1,-1};
        const auto pair=externalTargets_[it->second].load(std::memory_order_acquire);
        return {externalLeft(pair),externalRight(pair)};
    }

    // Independent audio-callback proof: APPLIED differs from merely
    // requested until the existing hardware render finishes its transition.
    std::pair<int,int> appliedExternalOutput(ChannelId id) const noexcept {
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end())return {-1,-1};
        const auto pair=externalAppliedTargets_[it->second].load(
            std::memory_order_acquire);
        return {externalLeft(pair),externalRight(pair)};
    }
    // AUDIO only: an attempted native PCM render that fails must revoke
    // callback-APPLIED evidence. Keep the requested CONTROL patch intact:
    // the next valid block can recover it without operator re-ARM/re-WIRE.
    // This is bounded to prepared atomic slots and never builds a graph,
    // reallocates buffers, or touches the control-only ChannelId hash map.
    void invalidateAppliedExternalOutputs() noexcept {
        for(const auto i:renderOrder_)
            externalAppliedTargets_[i].store(noExternal,std::memory_order_release);
    }

    // CONTROL-only DAG PDC. Every fan-in receives sample-aligned PCM,
    // including a destination's own hardware capture and PRE-fader sends.
    // A failure discards the unpublished candidate; the old graph stays live.
    template<class Latency>
    bool prepareLatency(Latency&& latency,std::size_t budget,DynamicGraphError& error) {
        std::vector<int> arrival(lanes_.size(),0),output(lanes_.size(),0);
        std::size_t bytes=scratch_.size()*sizeof(float);
        edgeDelays_.resize(edges_.size());physicalDelays_.resize(lanes_.size());
        sinkDelays_.resize(lanes_.size());externalDelays_.resize(lanes_.size());
        const auto allocate=[&](armix::dsp::StereoLatencyDelay& delay,int samples) {
            if(samples<0||samples>armix::dsp::StereoLatencyDelay::maximumSamples)return false;
            const auto need=static_cast<std::size_t>(samples)*2*sizeof(float);
            if(bytes>budget||need>budget-bytes)return false;
            bytes+=need;return delay.prepare(samples);
        };
        for(const auto i:renderOrder_) {
            const int own=latency(lanes_[i].id);
            if(own<0||own>armix::dsp::StereoLatencyDelay::maximumSamples||
               arrival[i]>armix::dsp::StereoLatencyDelay::maximumSamples-own) {
                error={DynamicGraphFault::overflow,lanes_[i].id};return false;
            }
            const auto dryBytes=static_cast<std::size_t>(own)*2*sizeof(float);
            if(bytes>budget||dryBytes>budget-bytes) {
                error={DynamicGraphFault::scratchBudget,lanes_[i].id};return false;
            }
            bytes+=dryBytes; // prepared VST bypass delay is part of PDC budget
            output[i]=arrival[i]+own;
            for(std::size_t e=lanes_[i].first;e<lanes_[i].last;++e)
                arrival[edges_[e].destination]=std::max(arrival[edges_[e].destination],output[i]);
        }
        int terminal=0,externalTerminal=0;
        for(const auto i:renderOrder_){
            if(i==main_||i==monitor_)terminal=std::max(terminal,output[i]);
            if(lanes_[i].role==ChannelRole::input ||
               lanes_[i].role==ChannelRole::group ||
               lanes_[i].role==ChannelRole::fxReturn)
                externalTerminal=std::max(externalTerminal,output[i]);
        }
        // Across independent physical buses, PDC shares a single target.
        terminal=std::max(terminal,externalTerminal);
        externalTerminal=terminal;
        for(const auto i:renderOrder_) {
            const auto& lane=lanes_[i];
            if((lane.inputL>=0 || lane.playerDeckId!=0) &&
               !allocate(physicalDelays_[i],arrival[i])) {
                error={DynamicGraphFault::scratchBudget,lane.id};return false;
            }
            for(std::size_t e=lane.first;e<lane.last;++e)
                if(!allocate(edgeDelays_[e],arrival[edges_[e].destination]-output[i])) {
                    error={DynamicGraphFault::scratchBudget,lane.id};return false;
                }
            if((i==main_||i==monitor_)&&!allocate(sinkDelays_[i],terminal-output[i])) {
                error={DynamicGraphFault::scratchBudget,lane.id};return false;
            }
            // All potential direct taps are prepared on CONTROL so changing
            // an output never allocates or rebuilds a VST rack on AUDIO.
            if((lane.role==ChannelRole::input ||
                lane.role==ChannelRole::group ||
                lane.role==ChannelRole::fxReturn) &&
               !allocate(externalDelays_[i],externalTerminal-output[i])) {
                error={DynamicGraphFault::scratchBudget,lane.id};return false;
            }
        }
        latencySamples_=terminal;return true;
    }
    int latencySamples() const noexcept {return latencySamples_;}

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
               !std::isfinite(row.inputGainLinear) || row.inputGainLinear<0.f ||
               row.inputGainLinear>16.f ||
               (row.role!=ChannelRole::input && row.inputGainLinear!=1.f) ||
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
               lane.playerDeckId!=0 || !validSourcePatch(req,patch)) {
                error={DynamicGraphFault::invalidSource,patch.id};return false;
            }
            if(patch.kind==DynamicSourceKind::physical) {
                lane.inputL=patch.inputL;lane.inputR=patch.inputR;
            } else {
                lane.playerDeckId=patch.deckId;
            }
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

        // Capture/FX lifetime is independent of output routing. SOURCE ONLY
        // must still run inserts and publish its local post-fader meters.
        // Schedule signal roots and descendants even with NO path to a sink;
        // unpatched empty descriptors remain scratch-free. Keep empty sinks
        // so MAIN/MONITOR silence is always rendered deterministically.
        std::vector<unsigned char> hasSignal(rows.size(),0);
        for(std::size_t i=0;i<rows.size();++i)
            hasSignal[i]=static_cast<unsigned char>(
                next.lanes_[i].inputL>=0 ||
                next.lanes_[i].playerDeckId!=0 ||
                !rows[i].insertCatalogIds.empty());
        for(const auto i:order) {
            if(!next.lanes_[i].enabled || !hasSignal[i])continue;
            for(const auto& edge:outgoing[i])
                if(next.lanes_[edge.destination].enabled)
                    hasSignal[edge.destination]=1;
        }
        for(const auto i:order)
            if(hasSignal[i] || i==main || i==monitor)
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
        // One immutable CSR adjacency over the retained processing lanes.
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
            target.inputGain.store(rows[i].inputGainLinear,std::memory_order_relaxed);
            target.inputApplied.reset(rows[i].inputGainLinear);
        }
        next.externalTargets_=std::make_unique<std::atomic<std::uint32_t>[]>(rows.size());
        next.externalAppliedTargets_=std::make_unique<std::atomic<std::uint32_t>[]>(rows.size());
        next.externalFades_=std::make_unique<ExternalFade[]>(rows.size());
        next.externalBlockTargets_.assign(rows.size(),noExternal);
        for(std::size_t i=0;i<rows.size();++i) {
            next.externalTargets_[i].store(noExternal,std::memory_order_relaxed);
            next.externalAppliedTargets_[i].store(noExternal,std::memory_order_relaxed);
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
                 std::span<float> channelPeaksR={},
                 std::span<const DynamicPlayerPcmBlock> playerPcm={},
                 float* const* hardwareOutputs=nullptr,
                 int hardwareOutputCount=0) noexcept {
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
        if(outputL==outputR || hardwareOutputCount<0 ||
           hardwareOutputCount>256 ||
           (hardwareOutputCount!=0 && !hardwareOutputs) ||
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
                for(int f=0;f<frames;++f) {
                    float vl=oldL?oldL[f]:0.f,vr=oldR?oldR[f]:0.f;
                    if(fade.remaining>0) {
                        const float alpha=1.f-static_cast<float>(fade.remaining-1)/
                            static_cast<float>(armix::dsp::RealtimeSlew64::samples);
                        vl+=((newL?newL[f]:0.f)-vl)*alpha;
                        vr+=((newR?newR[f]:0.f)-vr)*alpha;
                        if(--fade.remaining==0){fade.current=fade.next;oldL=newL;oldR=newR;}
                    }
                    if(!physicalDelays_.empty())physicalDelays_[i].process(vl,vr);
                    l[f]+=vl;r[f]+=vr;
                }
            }
            // A silent/starved deck never blocks the mixer or microphone.
            // Resolve the bounded source view once per channel/block, not
            // once per sample. Shared decks do NOT advance any producer FIFO.
            if(lane.playerDeckId!=0) {
                const DynamicPlayerPcmBlock* found=nullptr;
                for(const auto& deck:playerPcm)
                    if(deck.deckId==lane.playerDeckId) {found=&deck;break;}
                // Even when the worker runs dry, advance the prepared PDC
                // delay with silence: otherwise returning music would release
                // stale buffered frames at the wrong callback time.
                for(int f=0;f<frames;++f) {
                    float vl=(found&&found->left)?found->left[f]:0.f;
                    float vr=(found&&found->right)?found->right[f]:0.f;
                    if(!physicalDelays_.empty())
                        physicalDelays_[i].process(vl,vr);
                    l[f]+=vl;r[f]+=vr;
                }
            }
            if(lane.format==ProcessingFormat::mono)
                for(int f=0;f<frames;++f){
                    const float v=.5f*(l[f]+r[f]);l[f]=v;r[f]=v;
                }
            // Digital input GAIN belongs *before* all channel FX and does
            // not modify either PRE or POST send topology. One bounded pass
            // over existing scratch; no heap, lock, graph rebuild or extra FX.
            if(lane.role==ChannelRole::input){
                auto& trim=channelTargets_[i];
                trim.inputApplied.retarget(trim.inputGain.load(std::memory_order_acquire));
                for(int f=0;f<frames;++f){
                    const float g=trim.inputApplied.next();l[f]*=g;r[f]*=g;
                }
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
                    float vl=l[f]*applied*edge.panL,vr=r[f]*applied*edge.panR;
                    if(!edgeDelays_.empty())edgeDelays_[e].process(vl,vr);
                    dl[f]+=vl;dr[f]+=vr;
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
                    float vl=l[f]*applied*edge.panL,vr=r[f]*applied*edge.panR;
                    if(!edgeDelays_.empty())edgeDelays_[e].process(vl,vr);
                    dl[f]+=vl;dr[f]+=vr;
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
            if(!sinkDelays_.empty())for(int f=0;f<frames;++f)sinkDelays_[sink].process(l[f],r[f]);
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
        // External hardware taps consume the SAME post-FX/post-fader scratch.
        // All input reads and native plugin calls are already complete. The
        // host must not clear these dedicated output buffers a second time.
        if(hardwareOutputs && hardwareOutputCount) {
            // A JUCE/ASIO callback ordinal must own a unique physical PCM
            // buffer. Two DIFFERENT EXTERNAL pairs can otherwise alias the
            // same amplifier memory and silently mix unrelated ChannelIds.
            // Previous validation only compared each pair against MAIN,
            // MONITOR, or its own opposite side; it missed cross-pair alias.
            // Compare borrowed pointers, not port labels: non-contiguous
            // enabled ASIO masks are explicitly supported. Bounded O(N²)
            // for the already-verified active callback pins, no allocation,
            // sorting, lock, driver lookup or additional DSP pass on AUDIO.
            for(int c=0;c<hardwareOutputCount;++c) {
                if(!hardwareOutputs[c])continue; // inactive pins cannot receive PCM
                for(int earlier=0;earlier<c;++earlier)
                    if(hardwareOutputs[c]==hardwareOutputs[earlier])
                        return false; // publisher clears ALL physical outputs
            }
            for(int c=0;c<hardwareOutputCount;++c) {
                float* out=hardwareOutputs[c];
                if(out && out!=outputL && out!=outputR &&
                   out!=monitorL && out!=monitorR)
                    std::fill_n(out,frames,0.f);
            }
            // Validate ALL targets before any sum. A misbehaving callback
            // exposing aliased hardware pointers fails closed as a whole.
            for(const auto i:renderOrder_) {
                const auto pair=externalTargets_[i].load(std::memory_order_acquire);
                externalBlockTargets_[i]=pair; // validate exactly this block's immutable choice
                const auto& fading=externalFades_[i];
                const std::uint32_t targets[3]={pair,fading.current,fading.next};
                for(const auto target:targets) {
                    const int l=externalLeft(target),r=externalRight(target);
                    if(l<0)continue;
                    if(r<0 || l>=hardwareOutputCount || r>=hardwareOutputCount ||
                       l==r || !hardwareOutputs[l] || !hardwareOutputs[r] ||
                       hardwareOutputs[l]==hardwareOutputs[r] ||
                       hardwareOutputs[l]==outputL || hardwareOutputs[l]==outputR ||
                       hardwareOutputs[r]==outputL || hardwareOutputs[r]==outputR ||
                       hardwareOutputs[l]==monitorL || hardwareOutputs[l]==monitorR ||
                       hardwareOutputs[r]==monitorL || hardwareOutputs[r]==monitorR)
                        return false;
                }
            }
            // External tap delay advances even while the tap is disconnected,
            // so reconnect cannot spill a stale FX tail into a new amplifier.
            for(const auto i:renderOrder_) {
                const auto& lane=lanes_[i];
                if(lane.role!=ChannelRole::input &&
                   lane.role!=ChannelRole::group &&
                   lane.role!=ChannelRole::fxReturn)continue;
                auto& fade=externalFades_[i];
                const auto wanted=externalBlockTargets_[i]; // same preflighted target
                if(fade.remaining==0 && fade.current!=wanted) {
                    fade.next=wanted;
                    fade.remaining=armix::dsp::RealtimeSlew64::samples;
                }
                const auto slot=lane.slot;
                const float* src=scratch_.data()+slot*2*block;
                const float* srcR=src+block;
                for(int f=0;f<frames;++f) {
                    float l=src[f],r=srcR[f];
                    if(!externalDelays_.empty())externalDelays_[i].process(l,r);
                    float nextWeight=1.f,oldWeight=0.f;
                    if(fade.remaining>0) {
                        nextWeight=1.f-static_cast<float>(fade.remaining-1)/
                           static_cast<float>(armix::dsp::RealtimeSlew64::samples);
                        oldWeight=1.f-nextWeight;
                        if(--fade.remaining==0)fade.current=fade.next;
                    }
                    if(fade.remaining>0 && fade.current!=noExternal) {
                        hardwareOutputs[externalLeft(fade.current)][f]+=l*oldWeight;
                        hardwareOutputs[externalRight(fade.current)][f]+=r*oldWeight;
                    }
                    const auto active=fade.remaining>0?fade.next:fade.current;
                    if(active!=noExternal) {
                        hardwareOutputs[externalLeft(active)][f]+=l*nextWeight;
                        hardwareOutputs[externalRight(active)][f]+=r*nextWeight;
                    }
                }
                externalAppliedTargets_[i].store(
                    fade.remaining==0?fade.current:noExternal,
                    std::memory_order_release);
            }
        }
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
    std::vector<armix::dsp::StereoLatencyDelay> edgeDelays_,physicalDelays_,sinkDelays_,externalDelays_;
    std::vector<std::uint32_t> externalBlockTargets_; // preallocated, AUDIO-only writes
    int latencySamples_=0;
    std::unique_ptr<ChannelTarget[]> channelTargets_;
    std::unique_ptr<std::atomic<std::uint32_t>[]> sourceTargets_;
    std::unique_ptr<SourceFade[]> sourceFades_;
    std::unique_ptr<SendTarget[]> sendTargets_;
    std::unique_ptr<std::atomic<std::uint32_t>[]> externalTargets_;
    std::unique_ptr<std::atomic<std::uint32_t>[]> externalAppliedTargets_;
    std::unique_ptr<ExternalFade[]> externalFades_;
    std::size_t main_=notScheduled,monitor_=notScheduled;
    bool mainMonoAfterFx_=false;
    int capacity_=0,physicalInputs_=0;
};
} // namespace armix::mixer
