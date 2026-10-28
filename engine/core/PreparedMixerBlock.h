// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelSendPlan.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
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
    none, invalidChannelRouting, invalidSendRouting
};
struct RenderError final {
    RenderFault fault = RenderFault::none;
    LayoutError channel {};
    SendError sends {};
};

struct PreparedRenderGraph final {
    ChannelRoutingPlan channels {};
    ChannelSendPlan sends {};
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
                        RenderError& error) noexcept {
        error = {};
        PreparedRenderGraph candidate;
        if (!ChannelRoutingPlan::compile(routing, callbackOrderedPhysicalPorts,
                                         candidate.channels, error.channel)) {
            error.fault = RenderFault::invalidChannelRouting;
            return false;
        }
        if (!ChannelSendPlan::compile(sendsRequest, candidate.sends, error.sends)) {
            error.fault = RenderFault::invalidSendRouting;
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
    float masterPeakL = 0.f, masterPeakR = 0.f;
};

// Isolates buffer lifetime and prepared plan from device callback, FX
// instance lifetime and GUI. Build a replacement off RT; swap a POINTER
// only through the host's proven callback-boundary epoch/ack ownership.
class PreparedMixerBlock final {
public:
    // The maximum is an implementation bound, NOT an OS device promise.
    // Allocations are constructor-only; 20 strip lanes + 2 master lanes.
    static constexpr int hardFrameLimit = 8192;
    PreparedMixerBlock(const PreparedRenderGraph& plan, int maxFrames)
        : plan_(plan), capacity_(maxFrames),
          backing_(validCapacity(maxFrames)
                ? static_cast<std::size_t>(stripCount*2+2)*maxFrames
                : 0u, 0.f) {
        if (!validCapacity(maxFrames))
            capacity_=0;
        if(!capacity_)return;
        for(int i=0;i<stripCount;++i) {
            left_[i]=backing_.data()+static_cast<std::size_t>(2*i)*capacity_;
            right_[i]=left_[i]+capacity_;
        }
        masterL_=backing_.data()+static_cast<std::size_t>(stripCount*2)*capacity_;
        masterR_=masterL_+capacity_;
    }
    PreparedMixerBlock(const PreparedMixerBlock&)=delete;
    PreparedMixerBlock& operator=(const PreparedMixerBlock&)=delete;
    PreparedMixerBlock(PreparedMixerBlock&&)=delete;
    PreparedMixerBlock& operator=(PreparedMixerBlock&&)=delete;

    int capacity() const noexcept { return capacity_; }
    const PreparedRenderGraph& plan() const noexcept {return plan_;}

    // FX callback signature: void(int stripId, float* L, float* R, int frames)
    // noexcept, with stripId=-1 for the master FX chain. This interface is
    // explicitly stereo scratch, NOT a claim that mono VST bus negotiation is
    // solved. The native FX adapter must negotiate sidechain/mono/stereo off RT.
    template<class Fx>
    bool process(const float* const* physical, int callbackInputCount,
                 float* outputL, float* outputR, int frames, Fx&& processFx,
                 RenderLevels* observed=nullptr,
                 const RealtimeStripControls* live=nullptr) noexcept {
        static_assert(std::is_nothrow_invocable_v<Fx&,int,float*,float*,int>,
                      "DSP hook must be noexcept and realtime bounded");
        if (observed) *observed={};
        if (frames<=0 || !outputL || !outputR)return false;
        std::fill_n(outputL,frames,0.f);
        std::fill_n(outputR,frames,0.f);
        if (capacity_==0 || frames>capacity_ || callbackInputCount<0)
            return false;
        for(int i=0;i<stripCount;++i) {
            std::fill_n(left_[i],frames,0.f);
            std::fill_n(right_[i],frames,0.f);
        }
        PreparedRenderGraph const& graph=plan_;
        std::fill_n(masterL_,frames,0.f);
        std::fill_n(masterR_,frames,0.f);

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
            if (source.mode!=SourceMode::disconnected &&
                physical && source.inputA>=0 &&
                source.inputA<callbackInputCount &&
                source.inputB>=0 && source.inputB<callbackInputCount &&
                physical[source.inputA] && physical[source.inputB]) {
                const auto* const a=physical[source.inputA];
                const auto* const b=physical[source.inputB];
                for(int f=0;f<frames;++f) {
                    l[f]+=a[f];
                    r[f]+=b[f];
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
                    lastGainL_[strip]=gl;
                    lastGainR_[strip]=gr;
                    rampPrimed_[strip]=true;
                }
                const int ramp=std::min(frames,64);
                const float dGate=(targetGate-preGate_[strip])/ramp;
                float gate=preGate_[strip];
                for(int f=0;f<frames;++f) {
                    if(f<ramp)gate+=dGate;
                    l[f]*=gate;r[f]*=gate;
                }
                preGate_[strip]=targetGate;
            }
            graph.sends.dispatch(strip,SendTap::preFader,l,r,
                                 left_.data(),right_.data(),frames);
            float peak=0.f;
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
                    peak=std::max(peak,std::max(std::abs(l[f]),std::abs(r[f])));
                    if(graph.sends.toMaster[strip]) {
                        masterL_[f]+=l[f];masterR_[f]+=r[f];
                    }
                }
                lastGainL_[strip]=gl;lastGainR_[strip]=gr;
            }else for(int f=0;f<frames;++f) {
                l[f]*=gl;
                r[f]*=gr;
                peak=std::max(peak,std::max(std::abs(l[f]),std::abs(r[f])));
                if(graph.sends.toMaster[strip]) {
                    masterL_[f]+=l[f];
                    masterR_[f]+=r[f];
                }
            }
            if(observed) observed->channelPeak[strip]=peak;
            graph.sends.dispatch(strip,SendTap::postFader,l,r,
                                 left_.data(),right_.data(),frames);
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
        if(observed){observed->masterPeakL=peakL; observed->masterPeakR=peakR;}
        return true;
    }
private:
    static bool validCapacity(int f) noexcept {return f>0 && f<=hardFrameLimit;}
    const PreparedRenderGraph plan_;
    int capacity_=0;
    std::vector<float> backing_; // ALLOCATED BEFORE callback, never resized
    std::array<float*,stripCount> left_{};
    std::array<float*,stripCount> right_{};
    std::array<float,stripCount> preGate_{},lastGainL_{},lastGainR_{};
    std::array<bool,stripCount> rampPrimed_{};
    float* masterL_=nullptr;
    float* masterR_=nullptr;
};
} // namespace armix::mixer
