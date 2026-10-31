// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicNativeSessionPublisher.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// M3D: one CONTROL-thread native transaction owner for the future Studio
// Console. A channel/BUS edit is never visible as ACCEPTED merely because
// the roster editor succeeded. Only a published PCM + routing snapshot can
// advance the authoritative roster. No GUI/JS PCM, device, callback or FX
// instances are created here.
namespace armix::mixer {
class DynamicAcceptedSession final {
public:
    enum class Fault : std::uint8_t {
        none, staleRoster, staleGraph, editRejected, graphRejected
    };
    struct Result final {
        Fault fault=Fault::none;
        ChannelEditResult edit{};
        DynamicNativeSessionPublisher::CommitResult native{};
        std::uint64_t rosterRevision=0,graphRevision=0;
        bool ok() const noexcept {return fault==Fault::none;}
    };

    explicit DynamicAcceptedSession(ChannelRosterBudget budget={})
        : accepted_(budget) {}

    DynamicAcceptedSession(const DynamicAcceptedSession&)=delete;
    DynamicAcceptedSession& operator=(const DynamicAcceptedSession&)=delete;

    // CONTROL only: never expose draft, stale or rejected topology to GUI.
    const DynamicChannelRoster& acceptedRoster() const noexcept {return accepted_;}
    std::uint64_t acceptedRosterRevision() const noexcept {return accepted_.revision();}
    std::uint64_t acceptedGraphRevision() const noexcept {
        return publisher_.acceptedRevision();
    }
    const DynamicConsoleRoutingView* acceptedRouting(
        std::uint64_t deviceEpoch,std::uint64_t expectedGraphRevision) const noexcept {
        return publisher_.acceptedRouting(deviceEpoch,expectedGraphRevision);
    }

    Result edit(std::uint64_t expectedRosterRevision,
                std::uint64_t expectedGraphRevision,
                std::span<const ChannelEdit> changes,
                const DynamicGraphRequest& route,
                std::uint64_t expectedDeviceEpoch,
                std::uint64_t activeDeviceEpoch,
                bool allOutputsDisarmed) {
        Result result=preflight(expectedRosterRevision,expectedGraphRevision);
        if(!result.ok())return result;
        auto candidate=accepted_; // once per batch; ONLY CONTROL
        result.edit=candidate.edit(expectedRosterRevision,changes);
        if(!result.edit.ok()) {
            result.fault=Fault::editRejected;return result;
        }
        return publish(std::move(candidate),route,expectedGraphRevision,
                       expectedDeviceEpoch,activeDeviceEpoch,allOutputsDisarmed,
                       std::move(result));
    }

    // Studio Pro: one independent Bus ChannelId and automatic assignment of
    // selected primary OUTs; FX Return leaves those OUTs intact and adds sends.
    Result createBusForSelected(std::uint64_t expectedRosterRevision,
                               std::uint64_t expectedGraphRevision,
                               std::span<const ChannelId> selection,
                               std::string name,ChannelRole busRole,
                               const DynamicGraphRequest& route,
                               std::uint64_t expectedDeviceEpoch,
                               std::uint64_t activeDeviceEpoch,
                               bool allOutputsDisarmed,
                               float parallelGain=.25f) {
        Result result=preflight(expectedRosterRevision,expectedGraphRevision);
        if(!result.ok())return result;
        auto candidate=accepted_;
        result.edit=candidate.createBusForSelected(
            expectedRosterRevision,selection,std::move(name),busRole,parallelGain);
        if(!result.edit.ok()) {
            result.fault=Fault::editRejected;return result;
        }
        return publish(std::move(candidate),route,expectedGraphRevision,
                       expectedDeviceEpoch,activeDeviceEpoch,allOutputsDisarmed,
                       std::move(result));
    }

    // Live routine controls change ONLY accepted edges/nodes. Persist values
    // in the accepted roster after native atomics were ACKed so subsequent
    // compiled snapshots preserve the exact last known mixer levels.
    // This never advances structural revision or restarts the audio callback.
    bool setLiveMix(std::uint64_t epoch,std::uint64_t graphRevision,
                    ChannelId id,float linear,float pan) noexcept {
        if(!accepted_.find(id) ||
           !publisher_.setChannelMix(epoch,graphRevision,id,linear,pan))
            return false;
        return accepted_.rememberLiveMix(id,linear,pan);
    }
    bool setLiveParallelSend(std::uint64_t epoch,std::uint64_t graphRevision,
                             ChannelId source,ChannelId destination,
                             bool preFader,float linear) noexcept {
        const auto* row=accepted_.find(source);
        if(!row)return false;
        const bool legal=std::any_of(row->sends.begin(),row->sends.end(),
            [=](const ChannelSendRef& send) {
                return send.destination==destination && !send.primaryOutput &&
                       send.preFader==preFader;
            });
        if(!legal ||
           !publisher_.setParallelSendGain(epoch,graphRevision,
                                            source,destination,preFader,linear))
            return false;
        return accepted_.rememberLiveParallelSend(
            source,destination,preFader,linear);
    }

    // This is a callback-ready DELEGATE, not an additional audio renderer.
    // M3D does not attach it to the production JUCE device until VST/Show
    // ownership and native output-port parity have been migrated.
    template<class Fx>
    bool render(std::uint64_t epoch,const float* const* inputs,int inputCount,
                float* mainL,float* mainR,int frames,bool muted,Fx&& fx,
                float* monitorL=nullptr,float* monitorR=nullptr) noexcept {
        return publisher_.render(epoch,inputs,inputCount,mainL,mainR,frames,
                                 muted,std::forward<Fx>(fx),monitorL,monitorR);
    }
    void unpublish() noexcept {publisher_.unpublish();}
    void pollRetirement() noexcept {publisher_.pollRetirement();}

private:
    Result preflight(std::uint64_t rosterRevision,
                     std::uint64_t graphRevision) const noexcept {
        Result result;
        result.rosterRevision=accepted_.revision();
        result.graphRevision=publisher_.acceptedRevision();
        if(rosterRevision!=result.rosterRevision)
            result.fault=Fault::staleRoster;
        else if(graphRevision!=result.graphRevision)
            result.fault=Fault::staleGraph;
        return result;
    }
    Result publish(DynamicChannelRoster&& candidate,
                   const DynamicGraphRequest& route,
                   std::uint64_t previousGraphRevision,
                   std::uint64_t requestedEpoch,std::uint64_t currentEpoch,
                   bool outputsDisarmed,Result&& result) {
        result.native=publisher_.commit(candidate,route,previousGraphRevision,
                                       requestedEpoch,currentEpoch,outputsDisarmed);
        if(!result.native.ok()) {
            result.fault=Fault::graphRejected;
            result.edit.createdIds.clear(); // NEVER advertise phantom Bus
            return result; // candidate destroyed; accepted state intact
        }
        static_assert(std::is_nothrow_move_assignable_v<DynamicChannelRoster>);
        accepted_=std::move(candidate);
        result.rosterRevision=accepted_.revision();
        result.graphRevision=result.native.revision;
        return result;
    }

    DynamicChannelRoster accepted_;
    DynamicNativeSessionPublisher publisher_;
};
} // namespace armix::mixer
