// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelSendPlan.h"
#include "AuxBusPlan.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

// P1D: one real, deterministic, *device-independent* block execution of
// physical inputs -> ten native strip lanes -> pre/post sends -> returns ->
// stereo master. Not connected to JUCE, ASIO, VST3 or live GUI yet.
// Construct/compile/reclaim on CONTROL thread; process() on ONE audio thread.
// A prepared instance must never be mutated or destroyed while processing.
namespace armix::mixer {

enum class RenderFault : unsigned char {
    none, invalidChannelRouting, invalidSendRouting, invalidAuxRouting
};
struct RenderError final {
    RenderFault fault = RenderFault::none;
    LayoutError channel {};
    SendError sends {};
    AuxError aux {};
};

struct PreparedRenderGraph final {
    ChannelRoutingPlan channels {};
    ChannelSendPlan sends {};
    AuxBusPlan aux {};
    std::array<bool, stripCount> audible {};
    std::array<bool, stripCount> scheduled {};
    std::array<float, stripCount> leftGain {};
    std::array<float, stripCount> rightGain {};

    // Pure control-thread compile: full rollback if any source or send is bad.
    // SOLO closure: include direct ancestors and direct descendants (transitive
    // within each direction) of soloed strips; do not inadvertently include
    // unrelated sources connected to a soloed strip's downstream FX return.
    static bool compile(const ChannelRoutingConfig& routing,
                        std::span<const int> callbackOrderedPhysicalPorts,
                        const SendRequest& sendsRequest,
                        PreparedRenderGraph& destination,
                        RenderError& error,
                        const AuxBusRequest& requestedAux={}) noexcept {
        error = {};
        PreparedRenderGraph candidate;
        if (!ChannelRoutingPlan::compile(routing, callbackOrderedPhysicalPorts,
                                         candidate.channels, error.channel)) {
            error.fault = RenderFault::invalidChannelRouting;
            return false;
        }
        if (!ChannelSendPlan::compile(sendsRequest, candidate.sends, error.sends,
                                      candidate.channels.effectiveFormats)) {
            error.fault = RenderFault::invalidSendRouting;
            return false;
        }
        if (!AuxBusPlan::compile(requestedAux,candidate.aux,error.aux,
                                candidate.channels.effectiveFormats)){
            error.fault=RenderFault::invalidAuxRouting;
            return false;
        }

        const auto& configs = routing.stripsAndMaster.strips;
        std::array<bool,stripCount> ancestors {};
        std::array<bool,stripCount> descendants {};
        bool anySolo = false;
        for(int i=0;i<stripCount;++i) {
            if (configs[i].solo && !configs[i].mute) {
                anySolo = true;
                ancestors[i] = descendants[i] = true;
            }
        }
        if (anySolo) {
            // DAG rank means each enabled edge only needs one pass in each
            // direction, no runtime recursion, allocations or cycle walk.
            for (int k=stripCount-1;k>=0;--k) {
                const int source=candidate.sends.topologicalOrder[k];
                for(int e=candidate.sends.outgoingOffset[source];
                    e<candidate.sends.outgoingOffset[source+1];++e)
                    if(ancestors[candidate.sends.orderedEdges[e].destination])
                        ancestors[source]=true;
            }
            for (int k=0;k<stripCount;++k) {
                const int source=candidate.sends.topologicalOrder[k];
                if(!descendants[source])continue;
                for(int e=candidate.sends.outgoingOffset[source];
                    e<candidate.sends.outgoingOffset[source+1];++e)
                    descendants[candidate.sends.orderedEdges[e].destination]=true;
            }
        }
        for(int i=0;i<stripCount;++i) {
            const auto& c=configs[i];
            const bool audible = !c.mute && (!anySolo || ancestors[i] || descendants[i]);
            candidate.audible[i]=audible;
            const bool hasPhysical=c.source!=SourceMode::disconnected;
            candidate.scheduled[i]=audible &&
                (hasPhysical || candidate.sends.receivesInternalAudio(i));
            if (!candidate.scheduled[i]) continue;
            if(candidate.channels.effectiveFormats[i]==ChannelFormat::mono ||
               c.source==SourceMode::mono) {
                // Mono equal-power PAN; normalized -3dB center, unity edges.
                candidate.leftGain[i]=c.faderLinear * std::sqrt(0.5f*(1.f-c.pan));
                candidate.rightGain[i]=c.faderLinear * std::sqrt(0.5f*(1.f+c.pan));
            } else {
                // Stereo BALANCE: no unrequested cross-feed.
                candidate.leftGain[i]=c.faderLinear*(c.pan>0.f?1.f-c.pan:1.f);
                candidate.rightGain[i]=c.faderLinear*(c.pan<0.f?1.f+c.pan:1.f);
            }
        }
        destination=candidate;
        return true;
    }
};

// P1I: A single native parameter plane, updated only by the control thread.
// All coefficients are calculated on control thread. Callback reads atomics
// once per block and interpolates into fixed strip scratch buffers. The
// immutable topology (DAG/physical ports/FX ownership) is never rebuilt for
// ordinary fader, pan, MUTE, or SOLO changes.
class RealtimeStripControls final {
public:
    struct Target {
        std::atomic<float> left{1.f},right{1.f};
        std::atomic<bool> mute{false},solo{false};
    };
    bool set(int strip,float gain,float pan,bool mono,bool mute,bool solo) noexcept {
        if(strip<0||strip>=stripCount||!std::isfinite(gain)||gain<0.f||gain>4.f||
           !std::isfinite(pan)||pan< -1.f||pan>1.f)return false;
        const float left=mono?gain*std::sqrt(0.5f*(1.f-pan)):
                               gain*(pan>0.f?1.f-pan:1.f);
        const float right=mono?gain*std::sqrt(0.5f*(1.f+pan)):
                                gain*(pan<0.f?1.f+pan:1.f);
        auto& c=targets_[strip];
        c.left.store(left,std::memory_order_relaxed);
        c.right.store(right,std::memory_order_relaxed);
        c.mute.store(mute,std::memory_order_relaxed);
        c.solo.store(solo,std::memory_order_relaxed);
        return true;
    }
    void setPreparedCoefficients(int strip,float left,float right) noexcept {
        targets_[strip].left.store(left,std::memory_order_relaxed);
        targets_[strip].right.store(right,std::memory_order_relaxed);
    }
    const Target& at(int strip) const noexcept{return targets_[strip];}
private:
    std::array<Target,stripCount> targets_{};
};

struct RenderLevels final {
    std::array<float,stripCount> channelPeak {};
    // Actual independent post-fader PCM peaks. Keep the aggregate for older telemetry.
    std::array<float,stripCount> channelPeakL {}, channelPeakR {};
    std::array<float,auxBusCount> auxPeak {};
    float masterPeakL = 0.f, masterPeakR = 0.f;
};

// Isolates buffer lifetime and prepared plan from device callback, FX
// instance lifetime and GUI. Build a replacement off RT; swap a POINTER
// only through the host's proven callback-boundary epoch/ack ownership.
class PreparedMixerBlock final {
public:
    // The maximum is an implementation bound, NOT an OS device promise.
    // Allocations are constructor-only; 20 strip lanes + 2 master + 2
    // monitor scratch + optional 8 AUX. No RT allocation or secondary DSP pass.
    static constexpr int hardFrameLimit = 8192;
    PreparedMixerBlock(const PreparedRenderGraph& plan, int maxFrames)
        : plan_(plan), capacity_(maxFrames),
          backing_(validCapacity(maxFrames)
                ? static_cast<std::size_t>(stripCount*2+4+
                    (plan.aux.active?auxBusCount*2:0))*maxFrames
                : 0u, 0.f) {
        if (!validCapacity(maxFrames))
            capacity_=0;
        for(int i=0;i<stripCount;++i){
            const auto& s=plan_.channels.base.strips[i];
            const auto pair=s.mode==SourceMode::disconnected
                ?invalidSourcePair:packSourcePair(s.inputA,s.inputB);
            physicalTarget_[i].store(pair,std::memory_order_relaxed);
            physicalBlend_[i]={pair,pair,0};
        }
        // All gain targets exist before ANY audio thread sees the candidate.
        for(int e=0;e<plan_.aux.edgeCount;++e){
            const float v=plan_.aux.edges[e].initialGain;
            auxTarget_[e].store(v,std::memory_order_relaxed);
            auxApplied_[e]=v;
            auxGoal_[e]=v;
            auxRampRemaining_[e]=0;
        }
        for(int b=0;b<auxBusCount;++b){
            const float gain=plan_.aux.buses[b].gain;
            auxBusTarget_[b].store(gain,std::memory_order_relaxed);
            auxBusApplied_[b]=auxBusGoal_[b]=gain;
            auxBusRemaining_[b]=0;
        }
        if(!capacity_)return;
        for(int i=0;i<stripCount;++i) {
            left_[i]=backing_.data()+static_cast<std::size_t>(2*i)*capacity_;
            right_[i]=left_[i]+capacity_;
        }
        masterL_=backing_.data()+static_cast<std::size_t>(stripCount*2)*capacity_;
        masterR_=masterL_+capacity_;
        monitorScratchL_=masterR_+capacity_;
        monitorScratchR_=monitorScratchL_+capacity_;
        if(plan.aux.active){
            for(int b=0;b<auxBusCount;++b){
                auxLeft_[b]=monitorScratchR_+capacity_+
                    static_cast<std::size_t>(b*2)*capacity_;
                auxRight_[b]=auxLeft_[b]+capacity_;
            }
        }
    }
    PreparedMixerBlock(const PreparedMixerBlock&)=delete;
    PreparedMixerBlock& operator=(const PreparedMixerBlock&)=delete;
    PreparedMixerBlock(PreparedMixerBlock&&)=delete;
    PreparedMixerBlock& operator=(PreparedMixerBlock&&)=delete;

    int capacity() const noexcept { return capacity_; }
    const PreparedRenderGraph& plan() const noexcept {return plan_;}

    // M1: CONTROL writes one atomic verified callback source pair; audio thread
    // performs a 64-sample input crossfade BEFORE processing the same FX rack.
    // Only an existing same-format physical channel may switch without mute.
    bool setPhysicalSource(int strip,SourceMode mode,int left,int right) noexcept {
        if(strip<0||strip>=stripCount||mode==SourceMode::disconnected||
           plan_.channels.base.strips[strip].mode!=mode)return false;
        const int n=plan_.channels.inputPorts.enabledCount;
        if(left<0||left>=n||right<0||right>=n||
           (mode==SourceMode::mono&&left!=right)||
           (mode==SourceMode::stereo&&left==right))return false;
        physicalTarget_[strip].store(packSourcePair(left,right),
                                     std::memory_order_release);
        return true;
    }

    // Control thread updates an EXISTING PRE or POST edge only. Creating a
    // new edge or changing its tap remains a structural graph transaction.
    // Every valid send has its own independently smoothed DSP target.
    bool setAuxSendGain(int source,int bus,SendTap tap,float gain) noexcept {
        if(source<0||source>=stripCount||bus<0||bus>=auxBusCount||
           (tap!=SendTap::preFader&&tap!=SendTap::postFader)||
           !std::isfinite(gain)||gain<0.f||gain>1.f)
            return false;
        for(int e=plan_.aux.offset[source];e<plan_.aux.offset[source+1];++e)
            if(plan_.aux.edges[e].bus==bus && plan_.aux.edges[e].tap==tap){
                auxTarget_[e].store(gain,std::memory_order_release);
                // Publish target before making the native PCM renderer
                // switch from its original static-zero-cost path.
                auxAutomationActive_.store(true,std::memory_order_release);
                return true;
            }
        return false;
    }
    // AUX master is a live fader, never a topology change. It scales the
    // already-rendered AUX FX return for both MAIN and MONITOR after inserts.
    bool setAuxBusGain(int bus,float gain) noexcept {
        if(bus<0||bus>=auxBusCount||!plan_.aux.active||
           !plan_.aux.buses[bus].enabled||!std::isfinite(gain)||
           gain<0.f||gain>4.f)return false;
        auxBusTarget_[bus].store(gain,std::memory_order_release);
        auxBusAutomationActive_.store(true,std::memory_order_release);
        return true;
    }
    float auxBusGain(int bus) const noexcept {
        return bus>=0&&bus<auxBusCount&&plan_.aux.buses[bus].enabled
          ?auxBusTarget_[bus].load(std::memory_order_acquire):-1.f;
    }
    float auxSendGain(int source,int bus,SendTap tap) const noexcept {
        if(source<0||source>=stripCount||bus<0||bus>=auxBusCount)return -1.f;
        for(int e=plan_.aux.offset[source];e<plan_.aux.offset[source+1];++e)
            if(plan_.aux.edges[e].bus==bus && plan_.aux.edges[e].tap==tap)
                return auxTarget_[e].load(std::memory_order_acquire);
        return -1.f;
    }

    // CONTROL only, before publishing a MONITOR-enabled prepared graph.
    // Initial send levels start at their approved values, not an audible
    // ramp from zero when a device first arms its independent MONITOR bus.
    void prepareMonitorGains(
        const std::array<float,stripCount+auxBusCount>& gains) noexcept {
        for(int i=0;i<stripCount+auxBusCount;++i) {
            monitorApplied_[i]=monitorGoal_[i]=gains[i];
            monitorRemaining_[i]=0;
        }
        monitorPrimed_=true;
    }

    // FX callback signature: void(int stripId, float* L, float* R, int frames)
    // noexcept, with stripId=-1 for the master FX chain. This interface is
    // explicitly stereo scratch, NOT a claim that mono VST bus negotiation is
    // solved. The native FX adapter must negotiate sidechain/mono/stereo off RT.
    template<class Fx>
    bool process(const float* const* physical, int callbackInputCount,
                 float* outputL, float* outputR, int frames, Fx&& processFx,
                 RenderLevels* observed=nullptr,
                 const RealtimeStripControls* live=nullptr,
                 const std::array<float,stripCount+auxBusCount>* monitorGains=nullptr,
                 float* monitorL=nullptr,float* monitorR=nullptr,
                 const std::array<std::atomic<float>,stripCount+auxBusCount>*
                     liveMonitorGains=nullptr) noexcept {
        static_assert(std::is_nothrow_invocable_v<Fx&,int,float*,float*,int>,
                      "DSP hook must be noexcept and realtime bounded");
        if (observed) *observed={};
        if (frames<=0 || !outputL || !outputR)return false;
        // A physical callback may expose aliased input/output PCM. Rejected
        // buffer contracts still clear physical outputs; successful mixing
        // must consume ALL capture sources before writing to output storage.
        const bool portsValid=outputL!=outputR &&
            ((monitorL==nullptr)==(monitorR==nullptr)) &&
            (!monitorL || (monitorGains && monitorL!=monitorR &&
               monitorL!=outputL && monitorL!=outputR &&
               monitorR!=outputL && monitorR!=outputR));
        if (!portsValid || capacity_==0 || frames>capacity_ ||
            callbackInputCount<0) {
            std::fill_n(outputL,frames,0.f);
            if(outputR!=outputL)std::fill_n(outputR,frames,0.f);
            if(monitorL)std::fill_n(monitorL,frames,0.f);
            if(monitorR && monitorR!=monitorL)std::fill_n(monitorR,frames,0.f);
            return false;
        }
        // Also support direct PreparedMixerBlock users (no JUCE FxGraph
        // wrapper): the first real MONITOR render starts at its approved
        // levels, never with a fake 64-frame fade from an unpatched zero.
        if(monitorL && !monitorPrimed_) {
            for(int i=0;i<stripCount+auxBusCount;++i) {
                const float gain=liveMonitorGains
                    ?(*liveMonitorGains)[i].load(std::memory_order_acquire)
                    :(*monitorGains)[i];
                monitorApplied_[i]=monitorGoal_[i]=gain;
                monitorRemaining_[i]=0;
            }
            monitorPrimed_=true;
        }
        float* const monitorBusL=monitorL?monitorScratchL_:nullptr;
        float* const monitorBusR=monitorR?monitorScratchR_:nullptr;
        if(monitorBusL) {
            std::fill_n(monitorBusL,frames,0.f);
            std::fill_n(monitorBusR,frames,0.f);
        }
        for(int i=0;i<stripCount;++i) {
            std::fill_n(left_[i],frames,0.f);
            std::fill_n(right_[i],frames,0.f);
        }
        PreparedRenderGraph const& graph=plan_;
        std::fill_n(masterL_,frames,0.f);
        std::fill_n(masterR_,frames,0.f);
        if(graph.aux.active)
            for(int b=0;b<auxBusCount;++b){
                std::fill_n(auxLeft_[b],frames,0.f);
                std::fill_n(auxRight_[b],frames,0.f);
            }

        bool anySolo=false;
        if(live)for(int strip=0;strip<stripCount;++strip)
            anySolo |= live->at(strip).solo.load(std::memory_order_relaxed) &&
                      !live->at(strip).mute.load(std::memory_order_relaxed);
        for(int k=0;k<stripCount;++k) {
            const int strip=graph.sends.topologicalOrder[k];
            if(!graph.scheduled[strip]) continue;
            auto* const l=left_[strip];
            auto* const r=right_[strip];
            const auto& source=graph.channels.base.strips[strip];
            // Incoming sends are already in l/r. Add actual hardware only
            // when that port is valid; never overwrite internal bus returns.
            if(source.mode!=SourceMode::disconnected&&physical){
                auto& fade=physicalBlend_[strip]; // audio-producer owned
                const auto request=physicalTarget_[strip].load(std::memory_order_acquire);
                // Mid-fade requests coalesce to the latest target, but never
                // interrupt a currently audible ramp with a discontinuity.
                if(fade.remaining==0&&request!=fade.target){
                    fade.previous=fade.target;
                    fade.target=request;
                    fade.remaining=64;
                }
                const auto valid=[&](std::uint32_t pair) noexcept {
                    const int a=static_cast<int>(pair&0xffffu);
                    const int b=static_cast<int>((pair>>16)&0xffffu);
                    return pair!=invalidSourcePair&&a<callbackInputCount&&
                        b<callbackInputCount&&physical[a]&&physical[b];
                };
                const int a=static_cast<int>(fade.target&0xffffu);
                const int b=static_cast<int>((fade.target>>16)&0xffffu);
                if(fade.remaining==0){
                    if(valid(fade.target))for(int f=0;f<frames;++f){
                        l[f]+=physical[a][f];r[f]+=physical[b][f];
                    }
                }else{
                    const int oldA=static_cast<int>(fade.previous&0xffffu);
                    const int oldB=static_cast<int>((fade.previous>>16)&0xffffu);
                    const bool oldGood=valid(fade.previous),newGood=valid(fade.target);
                    for(int f=0;f<frames;++f){
                        const float t=1.f-static_cast<float>(--fade.remaining)/64.f;
                        if(oldGood){l[f]+=physical[oldA][f]*(1.f-t);
                                    r[f]+=physical[oldB][f]*(1.f-t);}
                        if(newGood){l[f]+=physical[a][f]*t;
                                    r[f]+=physical[b][f]*t;}
                        if(fade.remaining==0){
                            if(newGood)for(int j=f+1;j<frames;++j){
                                l[j]+=physical[a][j];r[j]+=physical[b][j];
                            }
                            break;
                        }
                    }
                }
            }
            // Channel MONO includes internally received stereo sends.
            // Default unassigned FX returns must explicitly select STEREO.
            if(graph.channels.effectiveFormats[strip]==ChannelFormat::mono) {
                for(int f=0;f<frames;++f) {
                    const float m=0.5f*(l[f]+r[f]);
                    l[f]=m;
                    r[f]=m;
                }
            }
            processFx(strip,l,r,frames); // genuine pre-fader channel-FX seam
            // Solo closure includes a soloed strip and its source/return
            // ancestors/descendants, matching the compiler's original policy.
            bool routeAudible=true;
            float gl=graph.leftGain[strip],gr=graph.rightGain[strip];
            if(live) {
                const auto& target=live->at(strip);
                routeAudible=!target.mute.load(std::memory_order_relaxed);
                if(routeAudible&&anySolo) {
                    routeAudible=false;
                    for(int j=0;j<stripCount;++j) {
                        const auto& s=live->at(j);
                        if(s.solo.load(std::memory_order_relaxed) &&
                           !s.mute.load(std::memory_order_relaxed) &&
                           (j==strip||graph.sends.upstreamOf(strip,j)||
                            graph.sends.upstreamOf(j,strip))) {
                            routeAudible=true;break;
                        }
                    }
                }
                gl=target.left.load(std::memory_order_relaxed);
                gr=target.right.load(std::memory_order_relaxed);
                // A MUTE or SOLO transition must also silence PRE sends.
                // Smooth the pre-send source gate over <=64 samples; it does
                // not create/restart FX, drivers, nodes or send topology.
                const float targetGate=routeAudible?1.f:0.f;
                if(!rampPrimed_[strip]) {
                    preGate_[strip]=targetGate;
                    preGateTarget_[strip]=targetGate;
                    preRampRemaining_[strip]=0;
                    lastGainL_[strip]=gl;
                    lastGainR_[strip]=gr;
                    rampPrimed_[strip]=true;
                }
                // The MUTE/PRE-SEND de-click is 64 AUDIO SAMPLES, not one
                // callback: variable 4/16/32-frame JUCE quanta must not turn
                // a musical 64-frame transition into a hard-edge pop.
                // The target can reverse mid-ramp; continue from the current
                // coefficient and restart a full 64-sample envelope.
                if(targetGate!=preGateTarget_[strip]) {
                    preGateTarget_[strip]=targetGate;
                    preRampRemaining_[strip]=64;
                }
                float gate=preGate_[strip];
                int remaining=preRampRemaining_[strip];
                for(int f=0;f<frames;++f) {
                    if(remaining>0) {
                        gate+=(targetGate-gate)/static_cast<float>(remaining);
                        --remaining;
                    } else gate=targetGate;
                    l[f]*=gate;r[f]*=gate;
                }
                preGate_[strip]=gate;
                preRampRemaining_[strip]=remaining;
            }
            graph.sends.dispatch(strip,SendTap::preFader,l,r,
                                 left_.data(),right_.data(),frames);
            // Before operator automation, preserve the original
            // precompiled immutable AUX dispatch (fast path, no atomics).
            // Once the message thread publishes a changed send target,
            // the SAME callback switches to one live PRE/POST processor
            // which follows 64-sample gains across varying block sizes.
            if(auxAutomationActive_.load(std::memory_order_acquire))
                dispatchLiveAux(strip,SendTap::preFader,l,r,frames);
            else graph.aux.dispatch(strip,SendTap::preFader,l,r,
                                    auxLeft_.data(),auxRight_.data(),frames);
            float peak=0.f, peakL=0.f, peakR=0.f;
            if(monitorBusL) {
                const float next=liveMonitorGains
                    ?(*liveMonitorGains)[strip].load(std::memory_order_acquire)
                    :(*monitorGains)[strip];
                if(next!=monitorGoal_[strip]) {
                    monitorGoal_[strip]=next;
                    monitorRemaining_[strip]=64;
                }
            }
            if(live) {
                // A block-linear fader/PAN ramp avoids zipper clicks at the
                // native callback rate. UI sends at most one target per rAF.
                const int ramp=std::min(frames,64);
                const float dL=(gl-lastGainL_[strip])/ramp;
                const float dR=(gr-lastGainR_[strip])/ramp;
                float lv=lastGainL_[strip],rv=lastGainR_[strip];
                for(int f=0;f<frames;++f) {
                    if(f<ramp){lv+=dL;rv+=dR;}
                    l[f]*=lv;r[f]*=rv;
                    if(monitorBusL) {
                        const float gain=nextMonitorGain(strip);
                        monitorBusL[f]+=l[f]*gain;
                        monitorBusR[f]+=r[f]*gain;
                    }
                    const float sampleL=std::abs(l[f]), sampleR=std::abs(r[f]);
                    peakL=std::max(peakL,sampleL);
                    peakR=std::max(peakR,sampleR);
                    peak=std::max(peak,std::max(sampleL,sampleR));
                    if(graph.sends.toMaster[strip]) {
                        masterL_[f]+=l[f];masterR_[f]+=r[f];
                    }
                }
                lastGainL_[strip]=gl;lastGainR_[strip]=gr;
            }else for(int f=0;f<frames;++f) {
                l[f]*=gl;
                r[f]*=gr;
                if(monitorBusL) {
                    const float gain=nextMonitorGain(strip);
                    monitorBusL[f]+=l[f]*gain;
                    monitorBusR[f]+=r[f]*gain;
                }
                const float sampleL=std::abs(l[f]), sampleR=std::abs(r[f]);
                peakL=std::max(peakL,sampleL);
                peakR=std::max(peakR,sampleR);
                peak=std::max(peak,std::max(sampleL,sampleR));
                if(graph.sends.toMaster[strip]) {
                    masterL_[f]+=l[f];
                    masterR_[f]+=r[f];
                }
            }
            if(observed) {
                observed->channelPeak[strip]=peak;
                observed->channelPeakL[strip]=peakL;
                observed->channelPeakR[strip]=peakR;
            }
            graph.sends.dispatch(strip,SendTap::postFader,l,r,
                                 left_.data(),right_.data(),frames);
            // Before operator automation, preserve the original
            // precompiled immutable AUX dispatch (fast path, no atomics).
            // Once the message thread publishes a changed send target,
            // the SAME callback switches to one live PRE/POST processor
            // which follows 64-sample gains across varying block sizes.
            if(auxAutomationActive_.load(std::memory_order_acquire))
                dispatchLiveAux(strip,SendTap::postFader,l,r,frames);
            else graph.aux.dispatch(strip,SendTap::postFader,l,r,
                                    auxLeft_.data(),auxRight_.data(),frames);
        }
        // Dedicated AUX returns execute AFTER all ten source strips.
        // Input channel count NEVER decreases when an AUX is enabled.
        // AUX FX IDs are stripCount+b; -1 remains MASTER FX callback.
        if(graph.aux.active)for(int b=0;b<auxBusCount;++b){
            const auto& bus=graph.aux.buses[b];
            if(!bus.enabled)continue;
            float* const l=auxLeft_[b],* const r=auxRight_[b];
            processFx(stripCount+b,l,r,frames);
            float peak=0.f;
            const int monitorLane=stripCount+b;
            if(monitorBusL) {
                const float next=liveMonitorGains
                    ?(*liveMonitorGains)[monitorLane].load(std::memory_order_acquire)
                    :(*monitorGains)[monitorLane];
                if(next!=monitorGoal_[monitorLane]) {
                    monitorGoal_[monitorLane]=next;
                    monitorRemaining_[monitorLane]=64;
                }
            }
            // Original zero-automation fast path remains one scalar
            // multiplication; only a real operator AUX fader edit enables
            // the sample-continuous 64-frame de-zipper across 4/8/16-frame
            // callbacks. The fader scales the return AFTER hosted AUX VST3
            // for both MAIN and independent MONITOR, not source dry PCM.
            const bool automated=auxBusAutomationActive_.load(
                std::memory_order_acquire);
            const float target=automated
                ?auxBusTarget_[b].load(std::memory_order_acquire):bus.gain;
            if(automated && target!=auxBusGoal_[b]) {
                auxBusGoal_[b]=target;
                auxBusRemaining_[b]=64;
            }
            float current=automated?auxBusApplied_[b]:bus.gain;
            int remaining=automated?auxBusRemaining_[b]:0;
            for(int f=0;f<frames;++f) {
                if(remaining>0) {
                    current+=(target-current)/static_cast<float>(remaining);
                    --remaining;
                } else current=target;
                const float vl=l[f]*current,vr=r[f]*current;
                if(monitorBusL) {
                    const float gain=nextMonitorGain(monitorLane);
                    if(!bus.mute) {
                        monitorBusL[f]+=vl*gain;
                        monitorBusR[f]+=vr*gain;
                    }
                }
                peak=std::max(peak,std::max(std::abs(vl),std::abs(vr)));
                if(bus.toMaster && !bus.mute){
                    masterL_[f]+=vl;
                    masterR_[f]+=vr;
                }
            }
            if(automated) {
                auxBusApplied_[b]=remaining==0?target:current;
                auxBusRemaining_[b]=remaining;
            }
            if(observed)observed->auxPeak[b]=peak;
        }
        processFx(-1,masterL_,masterR_,frames);
        graph.channels.base.finishMaster(masterL_,masterR_,frames);
        float peakL=0.f,peakR=0.f;
        for(int f=0;f<frames;++f) {
            outputL[f]=masterL_[f];
            outputR[f]=masterR_[f];
            peakL=std::max(peakL,std::abs(outputL[f]));
            peakR=std::max(peakR,std::abs(outputR[f]));
        }
        if(monitorBusL) {
            // Physical input PCM is fully consumed and the independent MONITOR
            // bus is already summed: publish once, with no extra FX traversal.
            std::copy_n(monitorBusL,frames,monitorL);
            std::copy_n(monitorBusR,frames,monitorR);
        }
        if(observed){observed->masterPeakL=peakL; observed->masterPeakR=peakR;}
        return true;
    }
private:
    // Callback exclusive: monitor gain follows 64 sample steps regardless of
    // 4/16/32/512-frame hardware buffers; no second effect render.
    float nextMonitorGain(int lane) noexcept {
        auto& applied=monitorApplied_[lane];
        auto& remaining=monitorRemaining_[lane];
        const float target=monitorGoal_[lane];
        if(remaining>0) {
            applied+=(target-applied)/static_cast<float>(remaining);
            --remaining;
        } else applied=target;
        return applied;
    }
    static constexpr std::uint32_t invalidSourcePair=0xffffffffu;
    static constexpr std::uint32_t packSourcePair(int left,int right) noexcept {
        return static_cast<std::uint32_t>(left)|
               (static_cast<std::uint32_t>(right)<<16);
    }
    struct PhysicalSourceFade final {
        std::uint32_t previous=invalidSourcePair,target=invalidSourcePair;
        int remaining=0; // callback-thread-only, never shared with GUI
    };
    // Audio-thread exclusive smoothing state plus atomically published gain
    // targets. No lock, heap, plugin recreation or per-block graph compile.
    // POST/PRE edge identity and normalized PAN are immutable per revision.
    void dispatchLiveAux(int source,SendTap tap,const float* l,const float* r,
                         int frames) noexcept {
        if(!plan_.aux.active)return;
        for(int e=plan_.aux.offset[source];e<plan_.aux.offset[source+1];++e){
            const auto& edge=plan_.aux.edges[e];
            if(edge.tap!=tap)continue;
            float* dl=auxLeft_[edge.bus],* dr=auxRight_[edge.bus];
            if(!dl||!dr)continue;
            // Preserve a 64-SAMPLE de-zipper across arbitrarily small /
            // variable callbacks. PRE and POST use the same ramp; no
            // frame-size-dependent jump, heap, lock, or graph recompilation.
            const float target=auxTarget_[e].load(std::memory_order_acquire);
            if(target!=auxGoal_[e]) {
                auxGoal_[e]=target;
                auxRampRemaining_[e]=64;
            }
            float current=auxApplied_[e];
            int remaining=auxRampRemaining_[e];
            if(remaining==0) {
                const float gl=target*edge.leftPan,gr=target*edge.rightPan;
                for(int f=0;f<frames;++f){dl[f]+=l[f]*gl;dr[f]+=r[f]*gr;}
            } else {
                for(int f=0;f<frames;++f) {
                    if(remaining>0) {
                        current+=(target-current)/static_cast<float>(remaining);
                        --remaining;
                    }else current=target;
                    dl[f]+=l[f]*current*edge.leftPan;
                    dr[f]+=r[f]*current*edge.rightPan;
                }
            }
            auxApplied_[e]=remaining==0?target:current;
            auxRampRemaining_[e]=remaining;
        }
    }
    static bool validCapacity(int f) noexcept {return f>0 && f<=hardFrameLimit;}
    const PreparedRenderGraph plan_;
    int capacity_=0;
    std::vector<float> backing_; // ALLOCATED BEFORE callback, never resized
    std::array<float*,stripCount> left_{};
    std::array<float*,stripCount> right_{};
    std::array<float*,auxBusCount> auxLeft_{},auxRight_{};
    std::array<float,stripCount> preGate_{},preGateTarget_{},
        lastGainL_{},lastGainR_{};
    std::array<int,stripCount> preRampRemaining_{};
    std::array<std::atomic<std::uint32_t>,stripCount> physicalTarget_{};
    std::array<PhysicalSourceFade,stripCount> physicalBlend_{};
    // OFF until any native SET_SEND_LEVEL. Audio callback reads only;
    // control-thread publication is monotonic for this prepared graph.
    std::atomic<bool> auxAutomationActive_{false};
    std::array<std::atomic<float>,maxAuxSends> auxTarget_{};
    std::array<float,maxAuxSends> auxApplied_{},auxGoal_{}; // callback exclusive
    std::array<int,maxAuxSends> auxRampRemaining_{};
    std::array<float,stripCount+auxBusCount> monitorApplied_{},monitorGoal_{};
    std::array<int,stripCount+auxBusCount> monitorRemaining_{};
    bool monitorPrimed_=false; // sole audio producer except prepublish seeding
    std::array<std::atomic<float>,auxBusCount> auxBusTarget_{};
    std::array<float,auxBusCount> auxBusApplied_{},auxBusGoal_{};
    std::array<int,auxBusCount> auxBusRemaining_{};
    std::atomic<bool> auxBusAutomationActive_{false};
    // State exclusively advanced in the single JUCE audio callback.
    std::array<bool,stripCount> rampPrimed_{};
    float* masterL_=nullptr;
    float* masterR_=nullptr;
    float* monitorScratchL_=nullptr;
    float* monitorScratchR_=nullptr;
};
} // namespace armix::mixer
