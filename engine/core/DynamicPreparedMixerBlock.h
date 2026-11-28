// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicChannelRoster.h"
#include "AutoDuckerKeyContext.h"
#include "RealtimeSlew64.h"
#include "StereoLatencyDelay.h"
#include "RealtimeStereoControlWord.h"
#include <algorithm>
#include <array>
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
// Callback-local private PFL capture. Storage belongs to the host, never
// aliases programme/physical output. A successful graph render publishes
// only the selected strip's post-insert, pre-fader/pre-mute PCM; no WIRE,
// SOLO, gain, monitor mix or DSP state is changed. A missing lane/invalid
// quantum yields an empty receipt, never the previous block's samples.
class DynamicPflCapture final {
public:
    static constexpr int capacity=4096;
    ChannelId requested{};
    void reset() noexcept {captured_={};frames_=0;}
    void capture(ChannelId id,const float* l,const float* r,int frames) noexcept {
        if(!requested.value||id!=requested||frames<1||frames>capacity)return;
        std::copy_n(l,frames,left_.data());
        std::copy_n(r,frames,right_.data());
        captured_=id;frames_=frames;
    }
    ChannelId captured() const noexcept {return captured_;}
    std::span<const float> left() const noexcept {return {left_.data(),static_cast<std::size_t>(frames_)};}
    std::span<const float> right() const noexcept {return {right_.data(),static_cast<std::size_t>(frames_)};}
private:
    ChannelId captured_{};
    int frames_=0;
    std::array<float,capacity> left_{},right_{};
};
// Optional host-owned programme tap for an independent sink. This is NOT
// PFL: it observes the causal post-insert, post-mute/solo/fader/pan lane,
// before terminal/physical output alignment and host output envelopes.
// No borrowed scratch pointer can escape the render lease. A known dormant
// channel produces explicit silence; a missing/invalid request has no receipt.
class DynamicProgrammeTap final {
public:
    static constexpr int capacity=8192; // same maximum as the native host quantum
    enum class State : std::uint8_t { unavailable, dormant, rendered };
    ChannelId requested{};
    void reset() noexcept {captured_={};frames_=0;state_=State::unavailable;}
    static bool eligible(ChannelRole role) noexcept {
        return role==ChannelRole::input||role==ChannelRole::group||
               role==ChannelRole::fxReturn;
    }
    void capture(ChannelId id,const float* l,const float* r,int frames) noexcept {
        if(!requested.value||id!=requested||frames<1||frames>capacity||!l||!r)return;
        std::copy_n(l,frames,left_.data());std::copy_n(r,frames,right_.data());
        captured_=id;frames_=frames;state_=State::rendered;
    }
    void silence(ChannelId id,int frames) noexcept {
        if(!requested.value||id!=requested||frames<1||frames>capacity)return;
        std::fill_n(left_.data(),frames,0.f);std::fill_n(right_.data(),frames,0.f);
        captured_=id;frames_=frames;state_=State::dormant;
    }
    ChannelId captured() const noexcept {return captured_;}
    State state() const noexcept {return state_;}
    std::span<const float> left() const noexcept {return {left_.data(),static_cast<std::size_t>(frames_)};}
    std::span<const float> right() const noexcept {return {right_.data(),static_cast<std::size_t>(frames_)};}
private:
    ChannelId captured_{};
    State state_=State::unavailable;
    int frames_=0;
    std::array<float,capacity> left_{},right_{};
};
// Host-owned whole-callback receipt. A driver interval may be rendered in
// several native graph quanta; no partial or mixed-generation PCM escapes.
// Allocate once outside the callback; append only into fixed stereo storage.
// A failed optional tap does not invalidate otherwise valid hardware PCM.
class DynamicProgrammeBlock final {
public:
    static constexpr int capacity=DynamicProgrammeTap::capacity;
    ChannelId requested{};
    void reset() noexcept {
        source_={};epoch_=revision_=0;frames_=written_=0;valid_=complete_=false;
        slice_.reset();slice_.requested={};
    }
    bool begin(std::uint64_t epoch,int frames) noexcept {
        reset();
        if(!requested.value||!epoch||frames<1||frames>capacity)return false;
        source_=requested;epoch_=epoch;frames_=frames;valid_=true;
        slice_.requested=source_;return true;
    }
    DynamicProgrammeTap* slice() noexcept {
        if(!valid_||complete_)return nullptr;
        slice_.reset();return &slice_;
    }
    bool append(int offset,int frames,std::uint64_t epoch,std::uint64_t revision) noexcept {
        if(!valid_||complete_||epoch!=epoch_||!revision||
           (revision_&&revision_!=revision)||offset!=written_||
           frames<=0||frames>frames_-written_||slice_.captured()!=source_||
           slice_.state()==DynamicProgrammeTap::State::unavailable||
           slice_.left().size()!=static_cast<std::size_t>(frames)||
           slice_.right().size()!=static_cast<std::size_t>(frames)){
            reset();return false;
        }
        revision_=revision;
        std::copy_n(slice_.left().data(),frames,left_.data()+offset);
        std::copy_n(slice_.right().data(),frames,right_.data()+offset);
        written_+=frames;slice_.reset();return true;
    }
    // Apply the host's existing startup/output ramp to the exact sample
    // already assembled. Invalid/absent optional tap remains a no-op.
    void scale(int index,float gain) noexcept {
        if(valid_&&!complete_&&index>=0&&index<written_){
            left_[index]*=gain;right_[index]*=gain;
        }
    }
    bool finish(std::uint64_t epoch) noexcept {
        if(!valid_||complete_||epoch!=epoch_||written_!=frames_){
            reset();return false;
        }
        complete_=true;return true;
    }
    ChannelId captured() const noexcept {return complete_?source_:ChannelId{};}
    std::uint64_t revision() const noexcept {return complete_?revision_:0;}
    std::array<float*,2> pcm() noexcept {
        return complete_?std::array<float*,2>{left_.data(),right_.data()}:
                         std::array<float*,2>{nullptr,nullptr};
    }
    int frames() const noexcept {return complete_?frames_:0;}
private:
    DynamicProgrammeTap slice_;
    ChannelId source_{};
    std::uint64_t epoch_=0,revision_=0;
    int frames_=0,written_=0;
    bool valid_=false,complete_=false;
    std::array<float,capacity> left_{},right_{};
};
// Default keeps all physical buses time-aligned. Independent monitoring
// removes only the terminal MONITOR pad, never its own DSP or fan-in PDC.
// This is an explicit prepared-graph policy, not a realtime control.
enum class MonitorLatencyPolicy : std::uint8_t { aligned, independent };
// Post-fader dynamics have two explicit timing contracts. Strict is useful
// for callers requiring sample coincidence; live ducking consumes the actual
// causal key, including source FX latency, without retiming the audible graph.
enum class SidechainTimingPolicy : std::uint8_t { requireCoincidence, causalLive };
// Control-only dependency; never an audible send.
struct DynamicSidechainDependency final {ChannelId source{},target{};};
struct DynamicGraphRequest final {
    std::vector<DynamicSourcePatch> sources;
    int physicalInputs=0;
    int maxFrames=512;
    std::size_t maxScratchBytes=64U*1024U*1024U; // admission, not channel-count cap
    bool mainMonoAfterFx=false; // legacy MAIN mono occurs after MAIN VST+balance
    MonitorLatencyPolicy monitorLatencyPolicy=MonitorLatencyPolicy::aligned;
    std::vector<DynamicSidechainDependency> postFaderSidechains;
};
enum class DynamicGraphFault : std::uint8_t {
    none, noMaster, multipleMasters, invalidFrameCapacity,
    invalidPhysicalCount, missingSource, invalidSource,
    invalidSendDestination, duplicateSend, mainFeedback, cycle, invalidChannel,
    scratchBudget, overflow, multipleMonitors, monitorFeedback, invalidMonitorLatencyPolicy,
    invalidSidechain, sidechainLatency
};
struct DynamicGraphError final {
    DynamicGraphFault fault=DynamicGraphFault::none;
    ChannelId id{};
};

class DynamicPreparedMixerBlock final {
    struct Edge final {
        std::size_t destination=0;
        std::uint64_t id=0;
        float panL=0.f,panR=0.f,initialGain=1.f;
        bool pre=false,primary=false,enabled=true;
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
        std::size_t first=0,last=0,slot=0,keyFirst=0,keyLast=0;
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
        armix::dsp::RealtimeSlew64 appliedL{},appliedR{},gate{},inputApplied{},wireSumApplied{}; // AUDIO only
        std::atomic<float> inputGain{1.f}; // CONTROL writes, AUDIO reads
        std::atomic<float> wireSumGain{1.f};
        // A topology retirement is NOT a persisted MUTE/fader edit.
        static_assert(std::atomic<int>::is_always_lock_free&&std::atomic<bool>::is_always_lock_free);
        std::atomic<int> retireFrames{0};
        std::atomic<bool> retireDone{false};
        float retireGain=1.f,retireGoal=1.f,retireStep=0.f;
        int retireRemaining=0,retireDuration=64,retireDrain=0; // AUDIO only
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
        std::atomic<bool> enabled{true};
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
    bool setChannelWireSumGain(ChannelId id,float gain) noexcept {
        if(!id.value || !std::isfinite(gain) || gain<0.f || gain>2.f)return false;
        const auto found=controlIndex_.find(id.value);
        if(found==controlIndex_.end() || (lanes_[found->second].role!=ChannelRole::input && lanes_[found->second].role!=ChannelRole::group && lanes_[found->second].role!=ChannelRole::fxReturn))
            return false;
        channelTargets_[found->second].wireSumGain.store(gain,std::memory_order_release);
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
    bool canSwitchPhysicalSource(ChannelId id,int inputL,int inputR,
                                 bool stereo) const noexcept {
        if(!id.value ||
           !validPhysicalPatch(physicalInputs_,inputL,inputR,stereo))
            return false;
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end())return false;
        const auto& lane=lanes_[it->second];
        return lane.role==ChannelRole::input && lane.inputL>=0 &&
           !lane.playerDeckId && lane.inputStereo==stereo;
    }
    // One lock-free word binds the pin pair to its request generation.
    // A -> B -> A cannot settle on a receipt from the original A.
    bool setPhysicalSource(ChannelId id,int inputL,int inputR,bool stereo,
                           std::uint64_t* ticket=nullptr) noexcept {
        if(!canSwitchPhysicalSource(id,inputL,inputR,stereo))return false;
        const auto i=controlIndex_.find(id.value)->second;
        const auto previous=sourceTargets_[i].load(std::memory_order_relaxed);
        // Retrying the exact same target after a lost ACK/timeout must
        // retain its receipt, not restart or supersede an in-flight ramp.
        if((previous>>32)!=0&&static_cast<std::uint32_t>(previous)==packPhysical(inputL,inputR)){
            if(ticket)*ticket=previous>>32;
            return true;
        }
        const auto sequence=(previous>>32)+1;
        if(sequence>0xffffffffull)return false;
        const auto target=(sequence<<32)|packPhysical(inputL,inputR);
        sourceTargets_[i].store(target,std::memory_order_release);
        if(ticket)*ticket=sequence;
        return true;
    }
    std::uint64_t requestedPhysicalSourceTicket(ChannelId id) const noexcept {
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end()||lanes_[it->second].inputL<0)return 0;
        return sourceTargets_[it->second].load(std::memory_order_acquire)>>32;
    }
    std::uint64_t appliedPhysicalSourceTicket(ChannelId id) const noexcept {
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end()||lanes_[it->second].inputL<0)return 0;
        const auto i=it->second;
        const auto target=sourceTargets_[i].load(std::memory_order_acquire);
        return sourceAppliedTargets_[i].load(std::memory_order_acquire)==target?
            target>>32:0;
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
    bool setParallelSendEnabled(ChannelId source,ChannelId destination,
                                bool preFader,bool enabled) noexcept {
        if(!source.value||!destination.value)return false;
        const auto found=controlIndex_.find(source.value);
        if(found==controlIndex_.end())return false;
        const auto& lane=lanes_[found->second];
        for(std::size_t e=lane.first;e<lane.last;++e){
            const auto& edge=edges_[e];
            if(lanes_[edge.destination].id==destination&&
               edge.pre==preFader&&!edge.primary){
                sendTargets_[e].enabled.store(enabled,std::memory_order_release);
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
              left==right)))return false;
        // Dormant strips already own a preallocated target slot, but no PCM
        // scratch. Saving a destination must not wake DSP or require a source.
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

    // Immutable, CONTROL-only evidence from the exact accepted PDC plan.
    // A potential direct tap is not proof that a hardware patch is armed.
    struct LatencyPlanSummary final {
        bool valid=false,hasMonitor=false;
        MonitorLatencyPolicy monitorPolicy=MonitorLatencyPolicy::aligned;
        int alignedSamples=0,mainBeforeAlignment=0,monitorBeforeAlignment=0;
        int potentialDirectMaxSamples=0,monitorOutputSamples=0;
        int monitorAlignmentPad() const noexcept {return monitorOutputSamples-monitorBeforeAlignment;}
        int monitorIndependentSaving() const noexcept {return hasMonitor?alignedSamples-monitorBeforeAlignment:0;}
        ChannelId potentialDirectMaxChannel{};
        unsigned potentialDirectMaxTies=0;
        bool directTapRaisesTarget() const noexcept {
            return valid && potentialDirectMaxSamples >
                std::max(mainBeforeAlignment,hasMonitor?monitorBeforeAlignment:0);
        }
    };
    LatencyPlanSummary latencyPlanSummary() const noexcept {return latencyPlan_;}

    // CONTROL-only DAG PDC. Every fan-in receives sample-aligned PCM,
    // including a destination's own hardware capture and PRE-fader sends.
    // A failure discards the unpublished candidate; the old graph stays live.
    template<class Latency>
    bool prepareLatency(Latency&& latency,std::size_t budget,DynamicGraphError& error,
                        SidechainTimingPolicy sidechainTiming=SidechainTimingPolicy::requireCoincidence) {
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
        // Strict clients retain the original coincidence fence. Native live
        // ducking instead uses actual post-fader PCM from this callback in DAG
        // order; it adds neither program delay nor a previous-block key.
        // A latent source can trigger later (or earlier than a latent target).
        // Audio fan-in/output PDC below is identical in both modes.
        if(sidechainTiming==SidechainTimingPolicy::requireCoincidence)
        for(const auto i:renderOrder_){
            const auto& lane=lanes_[i];
            for(auto k=lane.keyFirst;k<lane.keyLast;++k){
                const auto source=postKeys_[k].laneIndex;
                if(output[source]!=arrival[i]||output[i]!=arrival[i]){
                    error={DynamicGraphFault::sidechainLatency,lane.id};return false;
                }
            }
        }
        int terminal=0,externalTerminal=0;
        LatencyPlanSummary summary;
        summary.hasMonitor=monitor_!=notScheduled;
        summary.monitorPolicy=monitorLatencyPolicy_;
        summary.mainBeforeAlignment=output[main_];
        if(summary.hasMonitor)summary.monitorBeforeAlignment=output[monitor_];
        for(const auto i:renderOrder_){
            if(i==main_||i==monitor_)terminal=std::max(terminal,output[i]);
            if(lanes_[i].role==ChannelRole::input ||
               lanes_[i].role==ChannelRole::group ||
               lanes_[i].role==ChannelRole::fxReturn) {
                if(output[i]>externalTerminal || summary.potentialDirectMaxTies==0) {
                    externalTerminal=output[i];
                    summary.potentialDirectMaxChannel=lanes_[i].id;
                    summary.potentialDirectMaxTies=1;
                } else if(output[i]==externalTerminal) {
                    ++summary.potentialDirectMaxTies;
                }
            }
        }
        // Across independent physical buses, PDC shares a single target.
        summary.potentialDirectMaxSamples=externalTerminal;
        terminal=std::max(terminal,externalTerminal);
        externalTerminal=terminal;
        // MAIN and every potential direct output remain on the existing
        // global reference, even when MONITOR is explicitly independent.
        summary.monitorOutputSamples=!summary.hasMonitor?0:
            monitorLatencyPolicy_==MonitorLatencyPolicy::independent?
                summary.monitorBeforeAlignment:terminal;
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
            if((i==main_||i==monitor_)&&!allocate(sinkDelays_[i],
                   (i==monitor_?summary.monitorOutputSamples:terminal)-output[i])) {
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
        latencySamples_=terminal;
        summary.alignedSamples=terminal;summary.valid=true;
        latencyPlan_=summary;return true;
    }
    int latencySamples() const noexcept {return latencySamples_;}

    struct ContinuityPlan final {
        std::vector<std::pair<std::size_t,std::size_t>> lanes,edges; // new, old
    };
    // CONTROL: only identical surviving topology and PDC are admissible.
    // Inbound wires may be removed with their already faded destination;
    // live SOLO topology and changed PDC still require an explicit stopped edit.
    bool prepareContinuity(const DynamicPreparedMixerBlock& old,ChannelId removed,
                           ContinuityPlan& plan,bool dormantRosterEdit=false,bool wireAddition=false) {
        if(wireAddition&&(removed.value||dormantRosterEdit||renderOrder_.size()!=old.renderOrder_.size()))return false;
        if(wireAddition)for(std::size_t i=0;i<old.lanes_.size();++i)
            if(Word::decode(old.channelTargets_[i].state.load(std::memory_order_acquire)).solo)return false;
        if(capacity_!=old.capacity_||physicalInputs_!=old.physicalInputs_||
           mainMonoAfterFx_!=old.mainMonoAfterFx_||
           monitorLatencyPolicy_!=old.monitorLatencyPolicy_||
           latencySamples_!=old.latencySamples_||
           lanes_.size()+(removed.value?1u:0u)!=old.lanes_.size()+(dormantRosterEdit&&!removed.value?1u:0u))return false;
        // A silent descriptor can change Kahn's tie-breaking through its
        // default MAIN edge even though it has no scheduled PCM. Prove the
        // same scheduled lane set, edges, keys and delays below, then reuse
        // the previous audible order (including floating-point summation).
        if(dormantRosterEdit&&renderOrder_.size()!=old.renderOrder_.size())return false;
        // A live track retirement may keep independent MONITOR and secondary
        // clocks armed ONLY when every surviving scheduled dependency, delay
        // and processing order is unchanged. A downstream dormancy change
        // or a changed post-fader key requires the ordinary gated edit.
        const bool activeRemoval=removed.value&&!dormantRosterEdit;
        if(activeRemoval&&(!old.channelScheduled(removed)||
           renderOrder_.size()+1!=old.renderOrder_.size()))return false;
        if(removed.value){
            const auto found=old.controlIndex_.find(removed.value);
            if(found==old.controlIndex_.end()||controlIndex_.contains(removed.value))return false;
            if(dormantRosterEdit){
                const auto& retired=old.lanes_[found->second];
                if(old.channelScheduled(removed)||retired.inputL>=0||retired.playerDeckId||
                   (retired.role!=ChannelRole::input&&retired.role!=ChannelRole::group)||
                   Word::decode(old.channelTargets_[found->second].state.load()).solo)return false;
            }
            for(std::size_t i=0;i<old.lanes_.size();++i)
                if(Word::decode(old.channelTargets_[i].state.load(std::memory_order_acquire)).solo)
                    return false;
        }
        const auto delayEqual=[](const auto& a,std::size_t i,const auto& b,std::size_t j){
            return i<a.size()&&j<b.size()&&a[i].samples()==b[j].samples();
        };
        for(std::size_t i=0;i<lanes_.size();++i){
            const auto& n=lanes_[i];const auto found=old.controlIndex_.find(n.id.value);
            if(found==old.controlIndex_.end()){
                if(!dormantRosterEdit||removed.value||channelScheduled(n.id)||n.inputL>=0||n.playerDeckId||
                   (n.role!=ChannelRole::input&&n.role!=ChannelRole::group)||
                   n.first!=n.last||n.keyFirst!=n.keyLast||
                   Word::decode(channelTargets_[i].state.load()).solo)return false;
                continue;
            }
            const auto j=found->second;const auto& o=old.lanes_[j];
            const bool wasScheduled=old.channelScheduled(n.id),nowScheduled=channelScheduled(n.id);
            if(n.role!=o.role||n.format!=o.format||n.monoPan!=o.monoPan||
               n.inputStereo!=o.inputStereo||n.playerDeckId!=o.playerDeckId||
               (n.inputL>=0)!=(o.inputL>=0)||(wireAddition&&n.enabled!=o.enabled))return false;
            // A fresh zero-latency Compressor may activate an unpatched
            // dormant lane. It has no physical/player input or prior history.
            // A downstream lane which becomes dormant has no physical/player
            // source or FX by compiler construction. Its removed contribution
            // has drained before publication; no live processor tail is lost.
            if(dormantRosterEdit||activeRemoval||wireAddition){
                if(wasScheduled!=nowScheduled||n.keyLast-n.keyFirst!=o.keyLast-o.keyFirst)return false;
                for(std::size_t k=0;k<n.keyLast-n.keyFirst;++k)
                    if(postKeys_[n.keyFirst+k].id!=old.postKeys_[o.keyFirst+k].id)return false;
            }
            if(!wasScheduled&&nowScheduled&&!removed.value&&n.inputL<0&&!n.playerDeckId)continue;
            if(wasScheduled&&!nowScheduled&&removed.value&&n.inputL<0&&!n.playerDeckId)continue;
            if(wasScheduled!=nowScheduled||
               !delayEqual(physicalDelays_,i,old.physicalDelays_,j)||
               !delayEqual(sinkDelays_,i,old.sinkDelays_,j)||
               !delayEqual(externalDelays_,i,old.externalDelays_,j))return false;
            if(n.inputL>=0) {
                const auto previous=old.sourceTargets_[j].load();
                if(static_cast<std::uint32_t>(sourceTargets_[i].load())!=
                   static_cast<std::uint32_t>(previous))return false;
                sourceTargets_[i].store(previous);
            }
            if(channelTargets_[i].state.load()!=old.channelTargets_[j].state.load()||
               channelTargets_[i].inputGain.load()!=old.channelTargets_[j].inputGain.load()||
               channelTargets_[i].wireSumGain.load()!=old.channelTargets_[j].wireSumGain.load())return false;
            std::size_t survivingEdges=0;
            for(std::size_t k=o.first;k<o.last;++k)
                if(old.lanes_[old.edges_[k].destination].id!=removed)++survivingEdges;
            if(wireAddition?n.last-n.first<survivingEdges:n.last-n.first!=survivingEdges)return false;
            std::size_t matchedEdges=0;
            plan.lanes.emplace_back(i,j);
            externalTargets_[i].store(old.externalTargets_[j].load(std::memory_order_acquire));
            for(std::size_t e=n.first;e<n.last;++e){
                const auto& edge=edges_[e];
                const auto prior=std::find_if(old.edges_.begin()+o.first,old.edges_.begin()+o.last,
                    [&](const Edge& x){return x.id==edge.id&&
                        old.lanes_[x.destination].id==lanes_[edge.destination].id;});
                if(prior==old.edges_.begin()+o.last){
                    if(!wireAddition||edge.primary)return false;
                    // New SEND starts silent; its own 64-sample slew and
                    // zero-initialized delay run in the existing renderer.
                    sendTargets_[e].applied.reset(0.f);
                    continue;
                }
                ++matchedEdges;
                const auto k=static_cast<std::size_t>(prior-old.edges_.begin());
                if(edge.pre!=prior->pre||edge.primary!=prior->primary||
                   edge.panL!=prior->panL||edge.panR!=prior->panR||
                   sendTargets_[e].gain.load()!=old.sendTargets_[k].gain.load()||
                   sendTargets_[e].enabled.load()!=old.sendTargets_[k].enabled.load()||
                   !delayEqual(edgeDelays_,e,old.edgeDelays_,k))return false;
                plan.edges.emplace_back(e,k);
            }
            if(wireAddition&&matchedEdges!=survivingEdges)return false;
        }
        if(wireAddition){
            // Preserve audible accumulation order only if every new edge
            // respects it. Post-fader key identities were verified above.
            std::vector<std::size_t> position(lanes_.size(),lanes_.size());
            for(std::size_t k=0;k<old.renderOrder_.size();++k){
                const auto i=controlIndex_.at(old.lanes_[old.renderOrder_[k]].id.value);
                position[i]=k;renderOrder_[k]=i;
            }
            for(auto i:renderOrder_)for(auto e=lanes_[i].first;e<lanes_[i].last;++e)
                if(position[i]>=position[edges_[e].destination])return false;
        }else if(dormantRosterEdit){
            // All scheduled dependencies are already proven equivalent.
            // Scratch slots and delay state are lane-indexed, not tied to
            // this ordering; only the callback execution order is restored.
            for(std::size_t k=0;k<renderOrder_.size();++k){
                const auto id=old.lanes_[old.renderOrder_[k]].id;
                renderOrder_[k]=controlIndex_.at(id.value);
            }
        }else if(activeRemoval){
            // The old execution order minus the fully faded track is still
            // a valid topological order: surviving edges and keys matched.
            // Avoid reordering floating-point accumulation of unaffected PCM.
            std::size_t k=0;
            for(const auto index:old.renderOrder_){
                const auto id=old.lanes_[index].id;
                if(id==removed)continue;
                if(k>=renderOrder_.size())return false;
                renderOrder_[k++]=controlIndex_.at(id.value);
            }
            if(k!=renderOrder_.size())return false;
        }
        return true;
    }
    // AUDIO only, once, under the existing render permit. Transfer ring
    // ownership by swap (no sample copies, allocations or destruction).
    void adoptContinuity(DynamicPreparedMixerBlock& old,const ContinuityPlan& plan) noexcept {
        for(const auto& [i,j]:plan.lanes){
            auto& n=channelTargets_[i];auto& o=old.channelTargets_[j];
            n.appliedL=o.appliedL;n.appliedR=o.appliedR;n.gate=o.gate;
            n.inputApplied=o.inputApplied;n.wireSumApplied=o.wireSumApplied;
            n.retireGain=o.retireGain;n.retireGoal=o.retireGoal;n.retireStep=o.retireStep;
            n.retireRemaining=o.retireRemaining;n.retireDuration=o.retireDuration;
            sourceFades_[i]=old.sourceFades_[j];externalFades_[i]=old.externalFades_[j];
            sourceAppliedTargets_[i].store(old.sourceAppliedTargets_[j].load(std::memory_order_acquire),
                                           std::memory_order_release);
            externalAppliedTargets_[i].store(old.externalAppliedTargets_[j].load(std::memory_order_acquire),
                                             std::memory_order_release);
            physicalDelays_[i].swapHistory(old.physicalDelays_[j]);
            sinkDelays_[i].swapHistory(old.sinkDelays_[j]);
            externalDelays_[i].swapHistory(old.externalDelays_[j]);
        }
        for(const auto& [i,j]:plan.edges){
            sendTargets_[i].applied=old.sendTargets_[j].applied;
            edgeDelays_[i].swapHistory(old.edgeDelays_[j]);
        }
    }
    bool beginRetirement(ChannelId id,int frames) noexcept {
        const auto it=controlIndex_.find(id.value);
        if(it==controlIndex_.end()||frames<1||frames>4096)return false;
        auto& t=channelTargets_[it->second];
        t.retireDone.store(false,std::memory_order_relaxed);
        t.retireFrames.store(frames,std::memory_order_release);return true;
    }
    bool retirementComplete(ChannelId id) const noexcept {
        const auto it=controlIndex_.find(id.value);
        return it!=controlIndex_.end()&&
            channelTargets_[it->second].retireDone.load(std::memory_order_acquire);
    }
    void cancelRetirement(ChannelId id) noexcept {
        const auto it=controlIndex_.find(id.value);
        if(it!=controlIndex_.end())channelTargets_[it->second].retireFrames.store(0,std::memory_order_release);
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
        if(req.monitorLatencyPolicy!=MonitorLatencyPolicy::aligned &&
           req.monitorLatencyPolicy!=MonitorLatencyPolicy::independent){
            error.fault=DynamicGraphFault::invalidMonitorLatencyPolicy;return false;
        }
        DynamicPreparedMixerBlock next;
        next.monitorLatencyPolicy_=req.monitorLatencyPolicy;
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
               !std::isfinite(row.wireSumGainLinear) || row.wireSumGainLinear<0.f || row.wireSumGainLinear>2.f ||
               ((row.role!=ChannelRole::input && row.role!=ChannelRole::group && row.role!=ChannelRole::fxReturn) && row.wireSumGainLinear!=1.f) ||
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
        if(monitor==notScheduled && req.monitorLatencyPolicy==MonitorLatencyPolicy::independent){
            error.fault=DynamicGraphFault::invalidMonitorLatencyPolicy;return false;
        }
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
                if(send.primaryOutput&&!send.enabled){
                    error={DynamicGraphFault::invalidSendDestination,rows[i].id};return false;
                }
                outgoing[i].push_back({to,send.edgeId,gl,gr,send.gain,send.preFader,
                                       send.primaryOutput,send.enabled});
                parents[to].push_back(i);
                ++indegree[to];
            }
        }
        // Sidechain order constraints share the audio DAG's cycle check but
        // never enter the audible CSR, solo closure, gain or send fan-out.
        std::vector<std::vector<std::size_t>> sideTo(rows.size()),sideFrom(rows.size());
        for(const auto& dep:req.postFaderSidechains){
            const auto s=byId.find(dep.source.value),t=byId.find(dep.target.value);
            if(s==byId.end()||t==byId.end()||s->second==t->second){
                error={DynamicGraphFault::invalidSidechain,dep.target};return false;
            }
            auto& sources=sideFrom[t->second];
            if(std::find(sources.begin(),sources.end(),s->second)!=sources.end()||
               sources.size()>=8*armix::builtin_fx::AutoDuckerCore::maxSources){
                error={DynamicGraphFault::invalidSidechain,dep.target};return false;
            }
            sources.push_back(s->second);sideTo[s->second].push_back(t->second);
            ++indegree[t->second];
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
            for(const auto target:sideTo[i])if(--indegree[target]==0)queue.push_back(target);
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

        // One target-local sorted key view; source publication indexes are
        // prepared on CONTROL. No scratch copies or scans of unrelated routes.
        next.postKeyPublish_.resize(rows.size());
        for(std::size_t i=0;i<rows.size();++i){
            auto& lane=next.lanes_[i];
            auto& from=sideFrom[i];
            std::sort(from.begin(),from.end(),[&](auto a,auto b){return rows[a].id.value<rows[b].id.value;});
            lane.keyFirst=next.postKeys_.size();
            for(auto source:from){
                next.postKeyPublish_[source].push_back(next.postKeys_.size());
                next.postKeys_.push_back({rows[source].id.value,source,{}});
            }
            lane.keyLast=next.postKeys_.size();
        }
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
        next.sourceTargets_=std::make_unique<std::atomic<std::uint64_t>[]>(rows.size());
        next.sourceAppliedTargets_=std::make_unique<std::atomic<std::uint64_t>[]>(rows.size());
        next.sourceFades_=std::make_unique<SourceFade[]>(rows.size());
        next.sourceBlockTargets_.resize(rows.size());
        for(std::size_t i=0;i<rows.size();++i){
            const auto& lane=next.lanes_[i];
            if(lane.inputL<0)continue;
            const auto initial=packPhysical(lane.inputL,lane.inputR);
            next.sourceTargets_[i].store(initial,std::memory_order_relaxed);
            next.sourceFades_[i].current=next.sourceFades_[i].next=initial;
            if(scheduled[i])next.duckerKeys_.push_back({lane.id.value,i,{}});
        }
        std::sort(next.duckerKeys_.begin(),next.duckerKeys_.end(),
            [](const auto& a,const auto& b){return a.id<b.id;});
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
            target.wireSumGain.store(rows[i].wireSumGainLinear,std::memory_order_relaxed);
            target.wireSumApplied.reset(rows[i].wireSumGainLinear);
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
            next.sendTargets_[e].enabled.store(next.edges_[e].enabled,std::memory_order_relaxed);
            next.sendTargets_[e].applied.reset(next.edges_[e].enabled?initial:0.f);
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
                 int hardwareOutputCount=0,bool allowUnmappedMonitor=false,
                 DynamicPflCapture* pfl=nullptr,
                 DynamicProgrammeTap* programme=nullptr) noexcept {
        if(pfl)pfl->reset();
        if(programme)programme->reset();
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
           (monitor_!=notScheduled && !monitorL && !allowUnmappedMonitor) ||
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
        // A raw, pre-trim capture key is independent of strip/render order.
        // Read each physical patch once at the block boundary; use the SAME
        // snapshot for the program source render. A re-patch temporarily
        // invalidates the key instead of triggering from the previous pin.
        for(auto& entry:duckerKeys_){
            const auto i=entry.laneIndex;
            const auto& lane=lanes_[i];
            const auto target=sourceTargets_[i].load(std::memory_order_acquire);
            const auto pair=static_cast<std::uint32_t>(target);
            sourceBlockTargets_[i]=target;
            entry.key={};
            const auto& fade=sourceFades_[i];
            const bool audible=!blockTargets_[i].mute &&
                (!anySolo||soloAncestors_[i]||soloDescendants_[i]);
            if(!audible||!physical||fade.remaining||pair!=fade.current)continue;
            const int a=physicalLeft(pair),b=physicalRight(pair);
            if(a<0||a>=physicalCount||!physical[a])continue;
            if(lane.inputStereo&&(b<0||b>=physicalCount||!physical[b]))continue;
            entry.key={physical[a],lane.inputStereo?physical[b]:nullptr};
        }
        for(auto& entry:postKeys_)entry.key={}; // never reuse a previous callback view
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
        // Stage a view until the render succeeds. Copy BEFORE channel fader
        // and mute because these scratch lanes are subsequently changed.
        for(const auto i:renderOrder_) {
            const auto& lane=lanes_[i];
            float* const l=scratch_.data()+lane.slot*2*block;
            float* const r=l+block;
            // The scratch buffers contain only accumulated incoming WIREs here.
            // Scale their sum before adding any local physical/player input.
            auto& sum=channelTargets_[i];
            sum.wireSumApplied.retarget(sum.wireSumGain.load(std::memory_order_acquire));
            sum.wireSumApplied.applyStereo(l,r,frames);
            if(lane.inputL>=0 && physical) {
                auto& fade=sourceFades_[i];
                const auto desired=static_cast<std::uint32_t>(sourceBlockTargets_[i]);
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
                auto* const physicalDelay=!physicalDelays_.empty() && physicalDelays_[i].active()?
                    &physicalDelays_[i]:nullptr;
                for(int f=0;f<frames;++f) {
                    float vl=oldL?oldL[f]:0.f,vr=oldR?oldR[f]:0.f;
                    if(fade.remaining>0) {
                        const float alpha=1.f-static_cast<float>(fade.remaining-1)/
                            static_cast<float>(armix::dsp::RealtimeSlew64::samples);
                        vl+=((newL?newL[f]:0.f)-vl)*alpha;
                        vr+=((newR?newR[f]:0.f)-vr)*alpha;
                        if(--fade.remaining==0){fade.current=fade.next;oldL=newL;oldR=newR;}
                    }
                    if(physicalDelay)physicalDelay->process(vl,vr);
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
                auto* const physicalDelay=!physicalDelays_.empty() && physicalDelays_[i].active()?
                    &physicalDelays_[i]:nullptr;
                for(int f=0;f<frames;++f) {
                    float vl=(found&&found->left)?found->left[f]:0.f;
                    float vr=(found&&found->right)?found->right[f]:0.f;
                    if(physicalDelay)physicalDelay->process(vl,vr);
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
                trim.inputApplied.applyStereo(l,r,frames);
            }
            {
                const auto post=std::span<const AutoDuckerKeyEntry>(postKeys_).subspan(lane.keyFirst,lane.keyLast-lane.keyFirst);
                const AutoDuckerKeyScope keyScope(duckerKeys_,lane.id.value,frames,post);
                processFx(lane.id,l,r,frames); // same adapter signature for CH1..N
            }
            // PFL listens to the selected strip, including when its own
            // fader or programme gate is closed. For a bus, incoming PCM
            // remains exactly the authored mix; do not bypass upstream gates.
            // Exclude terminal sinks to prevent recursive MONITOR audition.
            if(pfl&&pfl->requested==lane.id&&
               (lane.role==ChannelRole::input||lane.role==ChannelRole::group||
                lane.role==ChannelRole::fxReturn))
                pfl->capture(lane.id,l,r,frames);
            // A MUTE/SOLO transition must silence PRE and POST sends, but
            // keep the original processor alive. Ramp spans 64 real samples.
            auto& gate=channelTargets_[i].gate;
            gate.applyStereo(l,r,frames);
            // Fade after FX and before every PRE/POST/physical tap.
            auto& retirement=channelTargets_[i];
            const int retire=retirement.retireFrames.load(std::memory_order_acquire);
            const float retireGoal=retire>0?0.f:1.f;
            if(retireGoal!=retirement.retireGoal){
                retirement.retireGoal=retireGoal;
                if(retire>0)retirement.retireDuration=retire;
                retirement.retireRemaining=retirement.retireDuration;
                retirement.retireStep=(retireGoal-retirement.retireGain)/
                    static_cast<float>(retirement.retireRemaining);
                retirement.retireDrain=latencySamples_;
            }
            const bool alreadySilent=retirement.retireRemaining==0&&retirement.retireGain==0.f;
            if(retirement.retireRemaining||retirement.retireGain!=1.f){
                for(int f=0;f<frames;++f){
                    if(retirement.retireRemaining>0){
                        if(--retirement.retireRemaining==0)retirement.retireGain=retireGoal;
                        else retirement.retireGain+=retirement.retireStep;
                    }
                    l[f]*=retirement.retireGain;r[f]*=retirement.retireGain;
                }
            }
            if(retire>0&&alreadySilent)retirement.retireDrain=std::max(0,retirement.retireDrain-frames);
            for(std::size_t e=lane.first;e<lane.last;++e) {
                const auto& edge=edges_[e];
                if(!edge.pre)continue;
                const auto& receiver=lanes_[edge.destination];
                float* const dl=scratch_.data()+receiver.slot*2*block;
                float* const dr=dl+block;
                auto& target=sendTargets_[e];
                const float stored=target.gain.load(std::memory_order_acquire);
                const float goal=target.enabled.load(std::memory_order_acquire)?stored:0.f;
                // Coalesced, callback-continuous 64 SAMPLE slew. A 4-frame
                // ASIO callback advances only 4/64 of the requested change.
                target.applied.retarget(goal);
                auto* const edgeDelay=!edgeDelays_.empty() && edgeDelays_[e].active()?
                    &edgeDelays_[e]:nullptr;
                if(!edgeDelay)
                    target.applied.accumulateStereo(l,r,dl,dr,frames,edge.panL,edge.panR);
                else for(int f=0;f<frames;++f){
                    const float applied=target.applied.next();
                    float vl=l[f]*applied*edge.panL,vr=r[f]*applied*edge.panR;
                    edgeDelay->process(vl,vr);
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
            if(controls.appliedL.remaining==0 && controls.appliedR.remaining==0){
                const float gainL=controls.appliedL.current,gainR=controls.appliedR.current;
                if(gainL==1.f && gainR==1.f){
                    for(int f=0;f<frames;++f){
                        observedL=std::max(observedL,std::abs(l[f]));
                        observedR=std::max(observedR,std::abs(r[f]));
                    }
                }else{
                    for(int f=0;f<frames;++f){
                        l[f]*=gainL;r[f]*=gainR;
                        observedL=std::max(observedL,std::abs(l[f]));
                        observedR=std::max(observedR,std::abs(r[f]));
                    }
                }
            }else for(int f=0;f<frames;++f){
                l[f]*=controls.appliedL.next();
                r[f]*=controls.appliedR.next();
                observedL=std::max(observedL,std::abs(l[f]));
                observedR=std::max(observedR,std::abs(r[f]));
            }
            // Source is fully processed for THIS callback. Its scratch is
            // retained until all consumers finish; never inject key PCM.
            const bool keyAudible=!goal.mute&&(!anySolo||soloAncestors_[i]||soloDescendants_[i]);
            if(keyAudible){
                for(auto k:postKeyPublish_[i])postKeys_[k].key={l,r};
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
                const float stored=target.gain.load(std::memory_order_acquire);
                const float goal=target.enabled.load(std::memory_order_acquire)?stored:0.f;
                // Coalesced, callback-continuous 64 SAMPLE slew. A 4-frame
                // ASIO callback advances only 4/64 of the requested change.
                target.applied.retarget(goal);
                auto* const edgeDelay=!edgeDelays_.empty() && edgeDelays_[e].active()?
                    &edgeDelays_[e]:nullptr;
                if(!edgeDelay)
                    target.applied.accumulateStereo(l,r,dl,dr,frames,edge.panL,edge.panR);
                else for(int f=0;f<frames;++f){
                    const float applied=target.applied.next();
                    float vl=l[f]*applied*edge.panL,vr=r[f]*applied*edge.panR;
                    edgeDelay->process(vl,vr);
                    dl[f]+=vl;dr[f]+=vr;
                }
            }
        }
        // Physical input PCM has been fully staged and processed into
        // scratch. Only NOW may output buffers be cleared: on supported
        // in-place/aliased hardware buffers an earlier clear would erase the
        // input before the source strips consumed it. This adds no allocation,
        // copy, lock, callback, DSP pass or latency.
        // Each terminal sink is written once, after all aliased input
        // reads: prepared PCM copy OR silence, not redundant clear+copy.
        const auto copySink=[&](std::size_t sink,float* l,float* r) noexcept {
            if(sink==notScheduled || !lanes_[sink].enabled ||
               lanes_[sink].slot>=renderOrder_.size()){
                std::fill_n(l,frames,0.f);std::fill_n(r,frames,0.f);return;
            }
            const auto slot=lanes_[sink].slot;
            const float* const source=scratch_.data()+slot*2*block;
            std::copy_n(source,frames,l);
            std::copy_n(source+block,frames,r);
            if(!sinkDelays_.empty() && sinkDelays_[sink].active())
                for(int f=0;f<frames;++f)sinkDelays_[sink].process(l[f],r[f]);
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
                        {if(pfl)pfl->reset();return false;} // publisher clears ALL physical outputs
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
                        {if(pfl)pfl->reset();return false;}
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
                auto* const externalDelay=!externalDelays_.empty() && externalDelays_[i].active()?
                    &externalDelays_[i]:nullptr;
                const auto slot=lane.slot;
                const float* src=scratch_.data()+slot*2*block;
                const float* srcR=src+block;
                // Disconnected taps still retain exact latency history for
                // immediate reconnect, but never read discarded delayed PCM.
                if(fade.remaining==0 && fade.current==noExternal){
                    if(externalDelay)externalDelay->advanceHistory(src,srcR,frames);
                    externalAppliedTargets_[i].store(noExternal,std::memory_order_release);
                    continue;
                }
                for(int f=0;f<frames;++f) {
                    float l=src[f],r=srcR[f];
                    if(externalDelay)externalDelay->process(l,r);
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
        // A completion receipt follows successful delivery, including PDC
        // drain and physical taps. A cancelled request never authorizes removal.
        for(const auto i:renderOrder_){
            // Receipt for a completed physical crossfade, never a null
            // capture pair or an unrendered request. Same loop as retirement.
            const auto& lane=lanes_[i];
            if(programme&&programme->requested==lane.id&&
               DynamicProgrammeTap::eligible(lane.role)){
                const float* const l=scratch_.data()+lane.slot*2*block;
                programme->capture(lane.id,l,l+block,frames);
            }
            if(lane.inputL>=0){
                const auto target=sourceBlockTargets_[i];
                const auto pair=static_cast<std::uint32_t>(target);
                const auto& fade=sourceFades_[i];
                const int a=physicalLeft(pair),b=physicalRight(pair);
                if(physical&&a>=0&&a<physicalCount&&physical[a]&&
                   (!lane.inputStereo||(b>=0&&b<physicalCount&&physical[b]))&&
                   !fade.remaining&&fade.current==pair)
                    sourceAppliedTargets_[i].store(target,std::memory_order_release);
            }
            auto& t=channelTargets_[i];
            if(t.retireFrames.load(std::memory_order_acquire)>0&&t.retireGain==0.f&&
               t.retireRemaining==0&&t.retireDrain==0&&
               (hardwareOutputs||externalTargets_[i].load(std::memory_order_acquire)==noExternal))
                t.retireDone.store(true,std::memory_order_release);
        }
        // A dormant descriptor has no scratch slot. Resolve only when
        // explicitly requested and not rendered; no DSP wake or fake PCM.
        if(programme&&programme->requested.value&&
           programme->state()==DynamicProgrammeTap::State::unavailable)
            for(const auto& lane:lanes_)
                if(lane.id==programme->requested&&DynamicProgrammeTap::eligible(lane.role)){
                    programme->silence(lane.id,frames);break;
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
    std::vector<AutoDuckerKeyEntry> postKeys_;
    std::vector<std::vector<std::size_t>> postKeyPublish_;
    std::vector<AutoDuckerKeyEntry> duckerKeys_; // preallocated, callback-local views
    std::vector<std::uint64_t> sourceBlockTargets_;
    std::vector<armix::dsp::StereoLatencyDelay> edgeDelays_,physicalDelays_,sinkDelays_,externalDelays_;
    std::vector<std::uint32_t> externalBlockTargets_; // preallocated, AUDIO-only writes
    int latencySamples_=0;
    LatencyPlanSummary latencyPlan_{};
    MonitorLatencyPolicy monitorLatencyPolicy_=MonitorLatencyPolicy::aligned;
    std::unique_ptr<ChannelTarget[]> channelTargets_;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::unique_ptr<std::atomic<std::uint64_t>[]> sourceTargets_,sourceAppliedTargets_;
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
