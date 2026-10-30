// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelSendPlan.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// P1K: additional, dedicated stereo AUX/FX returns. These are NOT strips.
// The original 10 INPUT strips and their P1G internal channel-channel sends
// remain unchanged. AUX receives only from input strips (never an AUX), then
// returns only to MASTER. This one-way topology makes feedback impossible by
// construction. Everything validates/compiles on CONTROL, zero RT allocations.
namespace armix::mixer {
inline constexpr int auxBusCount=4;
inline constexpr int maxAuxSends=32;
struct AuxBusDefinition final {
    bool enabled=false, toMaster=true, mute=false;
    float gain=1.f; // linear 0..4, post AUX insert
};
struct AuxSendDefinition final {
    int source=-1;         // INPUT 0..9, including existing internal returns
    int destination=-1;    // AUX index 0..3, never an INPUT index
    SendTap tap=SendTap::postFader;
    float gain=.25f, pan=0.f;
    bool enabled=true;
};
struct AuxBusRequest final {
    std::array<AuxBusDefinition,auxBusCount> buses{};
    std::array<AuxSendDefinition,maxAuxSends> sends{};
    int count=0;
};
enum class AuxFault : std::uint8_t {
    none, invalidCount, invalidBus, invalidStrip, disabledTarget,
    invalidTap, invalidGain, invalidPan, invalidSourceFormat, duplicateSend
};
struct AuxError final {
    AuxFault fault=AuxFault::none;
    int index=-1;
};
struct AuxBusPlan final {
    struct Edge {
        int bus=-1;
        SendTap tap=SendTap::postFader;
        float leftGain=0.f,rightGain=0.f;
    };
    std::array<AuxBusDefinition,auxBusCount> buses{};
    std::array<Edge,maxAuxSends> edges{};
    std::array<int,stripCount+1> offset{};
    int edgeCount=0;
    bool active=false;

    static bool compile(const AuxBusRequest& requested,AuxBusPlan& out,AuxError& error,
                        std::span<const ChannelFormat> sourceFormats={}) noexcept {
        error={};
        if(!sourceFormats.empty() && sourceFormats.size()!=stripCount){
            error={AuxFault::invalidSourceFormat,-1};return false;
        }
        if(requested.count<0||requested.count>maxAuxSends){
            error={AuxFault::invalidCount,-1};return false;
        }
        AuxBusPlan candidate;
        candidate.buses=requested.buses;
        for(int b=0;b<auxBusCount;++b) {
            const auto& bus=requested.buses[b];
            if(!std::isfinite(bus.gain)||bus.gain<0.f||bus.gain>4.f){
                error={AuxFault::invalidGain,b};return false;
            }
            candidate.active|=bus.enabled;
        }
        std::array<int,stripCount> outgoing{};
        for(int i=0;i<requested.count;++i) {
            const auto& s=requested.sends[i];
            if(s.source<0||s.source>=stripCount){
                error={AuxFault::invalidStrip,i};return false;
            }
            if(s.destination<0||s.destination>=auxBusCount){
                error={AuxFault::invalidBus,i};return false;
            }
            if(s.tap!=SendTap::preFader && s.tap!=SendTap::postFader){
                error={AuxFault::invalidTap,i};return false;
            }
            if(!std::isfinite(s.gain)||s.gain<0.f||s.gain>1.f){
                error={AuxFault::invalidGain,i};return false;
            }
            if(!std::isfinite(s.pan)||s.pan< -1.f||s.pan>1.f){
                error={AuxFault::invalidPan,i};return false;
            }
            if(!s.enabled)continue;
            if(!sourceFormats.empty() && sourceFormats[s.source]!=ChannelFormat::mono &&
               sourceFormats[s.source]!=ChannelFormat::stereo){
                error={AuxFault::invalidSourceFormat,i};return false;
            }
            if(!requested.buses[s.destination].enabled){
                error={AuxFault::disabledTarget,i};return false;
            }
            for(int j=0;j<i;++j) {
                const auto& prev=requested.sends[j];
                if(prev.enabled && prev.source==s.source &&
                   prev.destination==s.destination && prev.tap==s.tap){
                    error={AuxFault::duplicateSend,i};return false;
                }
            }
            ++outgoing[s.source];
            ++candidate.edgeCount;
        }
        for(int s=0;s<stripCount;++s)
            candidate.offset[s+1]=candidate.offset[s]+outgoing[s];
        auto next=candidate.offset;
        for(int i=0;i<requested.count;++i){
            const auto& s=requested.sends[i];
            if(!s.enabled)continue;
            auto& e=candidate.edges[next[s.source]++];
            const bool monoPre=s.tap==SendTap::preFader &&
                !sourceFormats.empty() && sourceFormats[s.source]==ChannelFormat::mono;
            e={s.destination,s.tap,
               monoPre?s.gain*std::sqrt(0.5f*(1.f-s.pan)):
                   s.gain*(s.pan>0.f?1.f-s.pan:1.f),
               monoPre?s.gain*std::sqrt(0.5f*(1.f+s.pan)):
                   s.gain*(s.pan<0.f?1.f+s.pan:1.f)};
        }
        out=candidate;
        return true;
    }

    // Unconditioned block-level send accumulation: caller owns mute/solo
    // source policy and supplies channel scratch AFTER PRE gate / fader.
    // Every destination is a dedicated, preallocated AUX stereo buffer.
    void dispatch(int source,SendTap tap,const float* left,const float* right,
                  float* const* auxL,float* const* auxR,int frames) const noexcept {
        if(!active||source<0||source>=stripCount||frames<=0||!left||!right||
           !auxL||!auxR)return;
        for(int i=offset[source];i<offset[source+1];++i){
            const auto& e=edges[i];
            if(e.tap!=tap)continue;
            auto* dl=auxL[e.bus];auto* dr=auxR[e.bus];
            if(!dl||!dr)continue;
            for(int f=0;f<frames;++f){
                dl[f]+=left[f]*e.leftGain;
                dr[f]+=right[f]*e.rightGain;
            }
        }
    }
};
} // namespace armix::mixer
