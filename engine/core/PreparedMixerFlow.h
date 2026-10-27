// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelSendPlan.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <utility>

// P1D offline integration: combines P1A physical input, P1B channel format,
// and P1C DAG sends into one deterministic dry-processing contract.
// NOT INSTALLED IN JUCE. Caller owns a preallocated workspace and an immutable
// validated plan for the entire block. Graph publication/device lifetime is
// deliberately outside this component.
namespace armix::mixer {

enum class FlowFault { none, inputLayout, sendTopology };
struct FlowError final {
    FlowFault fault = FlowFault::none;
    LayoutError layout {};
    SendError send {};
};

struct PreparedMixerFlow final {
    struct StripMix {
        bool audible = false;          // post-mute/solo with positive fader
        bool canSendPre = false;        // pre-fader is still subject to mute/solo
        bool mayReceivePhysical = false;
        float gainL = 0.f, gainR = 0.f;
    };
    ChannelRoutingPlan routing {};
    ChannelSendPlan sends {};
    std::array<StripMix, stripCount> mix {};

    static bool compile(const ChannelRoutingConfig& config,
                        std::span<const int> callbackOrderedPhysicalPorts,
                        const SendRequest& sendsRequest,
                        PreparedMixerFlow& destination,
                        FlowError& error) noexcept {
        error = {};
        PreparedMixerFlow next;
        if (!ChannelRoutingPlan::compile(config,callbackOrderedPhysicalPorts,
                                         next.routing,error.layout)) {
            error.fault=FlowFault::inputLayout; return false;
        }
        if (!ChannelSendPlan::compile(sendsRequest,next.sends,error.send)) {
            error.fault=FlowFault::sendTopology; return false;
        }
        bool anySolo=false;
        for (int i=0;i<stripCount;++i) {
            const auto& c=config.stripsAndMaster.strips[i];
            if(c.solo && !c.mute &&
               (c.source!=SourceMode::disconnected ||
                next.sends.receivesInternalAudio(i))) anySolo=true;
        }
        for (int i=0;i<stripCount;++i) {
            const auto& c=config.stripsAndMaster.strips[i];
            auto& m=next.mix[i];
            m.mayReceivePhysical=c.source!=SourceMode::disconnected;
            const bool sourceAvailable=m.mayReceivePhysical ||
                                       next.sends.receivesInternalAudio(i);
            m.canSendPre=sourceAvailable && !c.mute && (!anySolo || c.solo);
            m.audible=m.canSendPre && c.faderLinear>0.f;
            if(!m.audible)continue;
            // Channel FORMAT, not physical pin width, chooses the pan law.
            // MONO-to-STEREO dual-mono uses stereo balance (unity center).
            // Forced stereo-to-MONO uses equal-power pan after the raw fold.
            // This also applies to internal returns with no hardware input.
            if(next.routing.effectiveFormats[i]==ChannelFormat::mono) {
                m.gainL=c.faderLinear*std::sqrt(.5f*(1.f-c.pan));
                m.gainR=c.faderLinear*std::sqrt(.5f*(1.f+c.pan));
            } else {
                m.gainL=c.faderLinear*(c.pan>0.f?1.f-c.pan:1.f);
                m.gainR=c.faderLinear*(c.pan<0.f?1.f+c.pan:1.f);
            }
        }
        destination=next;
        return true;
    }

    template <std::size_t Capacity>
    struct Workspace final {
        static_assert(Capacity>0,"Fixed DSP workspace requires at least 1 frame");
        std::array<std::array<float,Capacity>,stripCount> incomingL {},incomingR {};
        std::array<float,Capacity> sourceL {},sourceR {};
        std::array<float,Capacity> physicalL {},physicalR {};
        std::array<float,Capacity> masterL {},masterR {};
        std::array<float*,stripCount> sendL {},sendR {};

        Workspace() noexcept {
            for(int i=0;i<stripCount;++i) {
                sendL[i]=incomingL[i].data();sendR[i]=incomingR[i].data();
            }
        }
        Workspace(const Workspace&)=delete;
        Workspace& operator=(const Workspace&)=delete;
    };

    // FxProcessor signature: void(int strip, ChannelFormat format,
    //                            float* left, float* right, int frames) noexcept.
    // Dry unit tests pass an empty noexcept processor. The future host may call
    // each channel's independent VST chain here, after raw physical+bus sum.
    // The callback MUST supply a prepared workspace with no concurrent access.
    // On invalid frame count no buffers are touched: host must output silence.
    template <std::size_t Capacity, class FxProcessor>
    bool render(const float* const* inputs,int callbackInputs,int frames,
                Workspace<Capacity>& work,FxProcessor&& processFx) const noexcept {
        if(frames<=0||static_cast<std::size_t>(frames)>Capacity||
           callbackInputs<0||callbackInputs>maxPhysicalPorts)return false;
        for(int strip=0;strip<stripCount;++strip) {
            std::fill_n(work.incomingL[strip].data(),frames,0.f);
            std::fill_n(work.incomingR[strip].data(),frames,0.f);
        }
        Plan::clearMaster(work.masterL.data(),work.masterR.data(),frames);
        for(int order=0;order<stripCount;++order) {
            const int strip=sends.topologicalOrder[order];
            const auto& control=mix[strip];
            if(!control.mayReceivePhysical && !sends.receivesInternalAudio(strip))
                continue;
            std::copy_n(work.incomingL[strip].data(),frames,work.sourceL.data());
            std::copy_n(work.incomingR[strip].data(),frames,work.sourceR.data());
            bool physicallyAvailable=false;
            if(control.mayReceivePhysical) {
                physicallyAvailable=routing.extractSource(
                    strip,inputs,callbackInputs,work.physicalL.data(),
                    work.physicalR.data(),frames);
                if(physicallyAvailable) {
                    for(int i=0;i<frames;++i) {
                        work.sourceL[i]+=work.physicalL[i];
                        work.sourceR[i]+=work.physicalR[i];
                    }
                }
            }
            // Missing/disconnected driver input must remain silent even when
            // a VST generates output from zeros. However, an internal return
            // is allowed to process a reverb tail with no fresh input.
            if(!physicallyAvailable && !sends.receivesInternalAudio(strip))
                continue;
            // POST-FX, PRE-FADER tap: fader zero does not suppress the send.
            processFx(strip,routing.effectiveFormats[strip],
                      work.sourceL.data(),work.sourceR.data(),frames);
            if(control.canSendPre)
                sends.dispatch(strip,SendTap::preFader,
                               work.sourceL.data(),work.sourceR.data(),
                               work.sendL.data(),work.sendR.data(),frames);
            // POST-FADER tap, including MUTE/SOLO arbitration.
            if(!control.audible)continue;
            for(int i=0;i<frames;++i) {
                work.sourceL[i]*=control.gainL;
                work.sourceR[i]*=control.gainR;
            }
            sends.dispatch(strip,SendTap::postFader,
                           work.sourceL.data(),work.sourceR.data(),
                           work.sendL.data(),work.sendR.data(),frames);
            if(sends.masterEnabled(strip)) {
                for(int i=0;i<frames;++i) {
                    work.masterL[i]+=work.sourceL[i];
                    work.masterR[i]+=work.sourceR[i];
                }
            }
        }
        routing.base.finishMaster(work.masterL.data(),work.masterR.data(),frames);
        return true;
    }
};
} // namespace armix::mixer
