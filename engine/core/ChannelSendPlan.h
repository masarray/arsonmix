// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelRoutingContract.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

// P1C: bounded, allocation-free compile-only bus/send scheduler.
// This is NOT installed on the live JUCE audio thread. Publishing a Plan
// later requires device/graph epoch validation and a quiescent handoff.
// In a future callback, process channels in topologicalOrder, take POST-FX
// buffers for preFader sends, and take POST-FADER/POST-MUTE buffers for
// postFader sends. Pre-fader send mute/solo policy belongs to caller.
namespace armix::mixer {

constexpr int maxChannelSends = 32;

enum class SendTap : std::uint8_t { preFader, postFader };
struct SendDefinition final {
    int source = -1;
    int destination = -1;
    SendTap tap = SendTap::postFader;
    float gain = 1.f;
    float pan = 0.f; // mono PRE equal-power pan; stereo/POST balance (see compile)
    bool enabled = true;
};
struct SendRequest final {
    std::array<bool,stripCount> toMaster {};
    std::array<SendDefinition,maxChannelSends> sends {};
    int count = 0;
    SendRequest() noexcept { toMaster.fill(true); }
};
enum class SendFault : std::uint8_t {
    none, tooManySends, invalidCount, invalidStrip, selfFeedback,
    invalidTap, invalidGain, invalidPan, invalidSourceFormat, duplicateSend, cycleDetected
};
struct SendError final {
    SendFault fault = SendFault::none;
    int sendIndex = -1;
    int strip = -1;
};

// Entire plan is immutable once published to a callback. No heap or
// dynamically sized vectors. All floating-point coefficient calculation,
// validation, and Kahn topological sorting occur on control thread ONLY.
struct ChannelSendPlan final {
    struct Edge {
        int destination = -1;
        SendTap tap = SendTap::postFader;
        float leftGain = 0.f, rightGain = 0.f;
    };
    std::array<Edge,maxChannelSends> orderedEdges {};
    std::array<int,stripCount+1> outgoingOffset {};
    std::array<int,stripCount> topologicalOrder {};
    std::array<int,stripCount> orderRank {};
    // True graph reachability, not mere topological rank. Bounded ten-bit
    // descendants per source, computed only during non-RT plan compilation.
    std::array<std::uint16_t,stripCount> downstreamMask {};
    std::array<bool,stripCount> toMaster {};
    // A strip can be a BUS RETURN even without any physical input; do not
    // gate these strips on ChannelRoutingPlan::Strip::active (physical only).
    std::array<bool,stripCount> receivesInternal {};
    int edgeCount = 0;

    static bool compile(const SendRequest& requested,
                        ChannelSendPlan& destination,
                        SendError& error,
                        std::span<const ChannelFormat> sourceFormats={}) noexcept {
        error = {};
        // Legacy isolated planners may omit formats; the live prepared graph
        // always supplies its validated effective per-channel formats.
        if (!sourceFormats.empty() && sourceFormats.size()!=stripCount) {
            error.fault=SendFault::invalidSourceFormat;return false;
        }
        if (requested.count < 0) {
            error.fault = SendFault::invalidCount; return false;
        }
        if (requested.count > maxChannelSends) {
            error.fault = SendFault::tooManySends; return false;
        }
        ChannelSendPlan candidate;
        candidate.toMaster = requested.toMaster;
        std::array<int,stripCount> outgoingCount {};
        std::array<int,stripCount> indegree {};
        for (int i=0;i<requested.count;++i) {
            const auto& s=requested.sends[i];
            if (s.source<0 || s.source>=stripCount ||
                s.destination<0 || s.destination>=stripCount) {
                error={SendFault::invalidStrip,i,s.source};return false;
            }
            if (s.source==s.destination) {
                error={SendFault::selfFeedback,i,s.source};return false;
            }
            if (s.tap!=SendTap::preFader && s.tap!=SendTap::postFader) {
                error={SendFault::invalidTap,i,s.source};return false;
            }
            if (!std::isfinite(s.gain)||s.gain<0.f||s.gain>4.f) {
                error={SendFault::invalidGain,i,s.source};return false;
            }
            if (!std::isfinite(s.pan)||s.pan< -1.f||s.pan>1.f) {
                error={SendFault::invalidPan,i,s.source};return false;
            }
            // One edge per source/destination/tap. Different tap types may
            // coexist, but identical duplicates would double the signal.
            for (int j=0;j<i;++j) {
                const auto& other=requested.sends[j];
                if (s.enabled && other.enabled && s.source==other.source &&
                    s.destination==other.destination && s.tap==other.tap) {
                    error={SendFault::duplicateSend,i,s.source};return false;
                }
            }
            if (!s.enabled) continue;
            if (!sourceFormats.empty() && sourceFormats[s.source]!=ChannelFormat::mono &&
                sourceFormats[s.source]!=ChannelFormat::stereo) {
                error={SendFault::invalidSourceFormat,i,s.source};return false;
            }
            ++outgoingCount[s.source];
            ++indegree[s.destination];
            candidate.receivesInternal[s.destination]=true;
            ++candidate.edgeCount;
        }
        candidate.outgoingOffset[0]=0;
        for (int i=0;i<stripCount;++i)
            candidate.outgoingOffset[i+1]=candidate.outgoingOffset[i]+outgoingCount[i];
        auto next = candidate.outgoingOffset;
        for (int i=0;i<requested.count;++i) {
            const auto& s=requested.sends[i];
            if (!s.enabled) continue;
            auto& e=candidate.orderedEdges[next[s.source]++];
            e.destination=s.destination;
            e.tap=s.tap;
            // PRE is taken after FX, before channel fader/PAN. The mono
            // scratch has identical L/R samples: project it once using
            // equal-power coefficients, without fabricating a second input.
            // POST already contains the channel's own PAN; preserve its
            // existing stereo balance pending an independent mono POST tap.
            const bool monoPre=s.tap==SendTap::preFader &&
                !sourceFormats.empty() && sourceFormats[s.source]==ChannelFormat::mono;
            e.leftGain=monoPre?s.gain*std::sqrt(0.5f*(1.f-s.pan)):
                s.gain*(s.pan>0.f?1.f-s.pan:1.f);
            e.rightGain=monoPre?s.gain*std::sqrt(0.5f*(1.f+s.pan)):
                s.gain*(s.pan<0.f?1.f+s.pan:1.f);
        }
        // Kahn's algorithm; deterministic ascending tie-break between strips.
        std::array<bool,stripCount> emitted {};
        for (int k=0;k<stripCount;++k) {
            int found=-1;
            for (int i=0;i<stripCount;++i)
                if (!emitted[i]&&indegree[i]==0) {found=i;break;}
            if (found<0) {
                error={SendFault::cycleDetected,-1,-1};return false;
            }
            emitted[found]=true;
            candidate.topologicalOrder[k]=found;
            candidate.orderRank[found]=k;
            for(int j=candidate.outgoingOffset[found];
                j<candidate.outgoingOffset[found+1];++j)
                --indegree[candidate.orderedEdges[j].destination];
        }
        // Reverse topological order ensures each child's transitive mask
        // is final before publishing its parent's mask. 10 strips < 16 bits.
        for(int k=stripCount-1;k>=0;--k) {
            const int source=candidate.topologicalOrder[k];
            std::uint16_t reachable=0;
            for(int e=candidate.outgoingOffset[source];
                e<candidate.outgoingOffset[source+1];++e) {
                const int child=candidate.orderedEdges[e].destination;
                reachable=static_cast<std::uint16_t>(
                    reachable | (std::uint16_t{1} << child) |
                    candidate.downstreamMask[child]);
            }
            candidate.downstreamMask[source]=reachable;
        }
        destination=candidate;
        return true;
    }

    // Called by a future callback with caller-owned, nonaliasing buffers.
    // Destination scratch MUST be allocated and cleared before processing.
    // RT complexity O(outgoingEdges[source] * frames), no dynamic work.
    void dispatch(int source,SendTap tap,
                  const float* sourceLeft,const float* sourceRight,
                  float* const* inputLeft,float* const* inputRight,
                  int frames) const noexcept {
        if(source<0||source>=stripCount||frames<=0||!sourceLeft||!sourceRight||
           !inputLeft||!inputRight)return;
        for(int e=outgoingOffset[source];e<outgoingOffset[source+1];++e) {
            const auto& edge=orderedEdges[e];
            if(edge.tap!=tap) continue;
            float* const dl=inputLeft[edge.destination];
            float* const dr=inputRight[edge.destination];
            if(!dl||!dr)continue;
            for(int i=0;i<frames;++i) {
                dl[i]+=sourceLeft[i]*edge.leftGain;
                dr[i]+=sourceRight[i]*edge.rightGain;
            }
        }
    }
    bool receivesInternalAudio(int strip) const noexcept {
        return strip>=0&&strip<stripCount&&receivesInternal[strip];
    }
    bool masterEnabled(int strip) const noexcept {
        return strip>=0&&strip<stripCount&&toMaster[strip];
    }
    bool upstreamOf(int source,int destination) const noexcept {
        return source>=0&&source<stripCount&&destination>=0&&
               destination<stripCount&&source!=destination&&
               (downstreamMask[source] & (std::uint16_t{1} << destination))!=0;
    }
};
} // namespace armix::mixer
