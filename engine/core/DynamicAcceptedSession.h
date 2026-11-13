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
                bool allOutputsDisarmed,
                DynamicNativeSessionPublisher::FxPlan fx={}) {
        Result result=preflight(expectedRosterRevision,expectedGraphRevision);
        if(!result.ok())return result;
        auto candidate=accepted_; // once per batch; ONLY CONTROL
        result.edit=candidate.edit(expectedRosterRevision,changes);
        if(!result.edit.ok()) {
            result.fault=Fault::editRejected;return result;
        }
        return publish(std::move(candidate),route,expectedGraphRevision,
                       expectedDeviceEpoch,activeDeviceEpoch,allOutputsDisarmed,
                       std::move(result),std::move(fx));
    }


    // CONTROL ONLY: first-sound product intent. The accepted session, not JS,
    // creates its own stereo MUSIC channel and default MAIN edge atomically
    // with the virtual source patch. Caller verifies the SAME physical MAIN
    // identity before setting trustedMain. Never silently select hardware.
    // A remembered player-owned ChannelId is reused without touching a
    // user-authored SOURCE ONLY / BUS / MUTE / FX graph.
    Result ensurePlayerFirstSound(std::uint64_t expectedRosterRevision,
                                  std::uint64_t expectedGraphRevision,
                                  const DynamicGraphRequest& route,
                                  std::uint64_t requestedEpoch,
                                  std::uint64_t activeEpoch,
                                  bool outputsDisarmed,bool trustedMain,
                                  ChannelId rememberedOwned={},
                                  std::uint64_t deckId=1,
                                  DynamicNativeSessionPublisher::FxPlan fx={}) {
        auto result=preflight(expectedRosterRevision,expectedGraphRevision);
        if(!result.ok())return result;
        if(!trustedMain || !deckId || !requestedEpoch ||
           requestedEpoch!=activeEpoch) {
            result.fault=Fault::graphRejected;
            result.native.fault=DynamicNativeSessionPublisher::Fault::invalidCandidate;
            return result;
        }
        const ChannelId owned=rememberedOwned.value?rememberedOwned:playerOwned_;
        if(owned.value) {
            const auto* channel=accepted_.find(owned);
            const auto patch=std::find_if(acceptedRequest_.sources.begin(),
                acceptedRequest_.sources.end(),[&](const DynamicSourcePatch& s) {
                    return s.id==owned;
                });
            if(!channel || channel->role!=ChannelRole::input ||
               patch==acceptedRequest_.sources.end() ||
               patch->kind!=DynamicSourceKind::playerDeck ||
               patch->deckId!=deckId) {
                result.fault=Fault::graphRejected;
                result.native.fault=DynamicNativeSessionPublisher::Fault::invalidCandidate;
                return result; // never seize an edited or reused channel
            }
            playerOwned_=owned;
            result.edit.revision=expectedRosterRevision;
            result.native.revision=expectedGraphRevision;
            return result; // idempotent: never duplicate channel/MAIN edge
        }
        // No stale browser physical draft may sneak into an armed player
        // channel creation. Only one new INTERNAL deck source is admissible.
        const auto physicalUnchanged=
            expectedGraphRevision!=0 &&
            route.physicalInputs==acceptedRequest_.physicalInputs &&
            route.maxFrames==acceptedRequest_.maxFrames &&
            route.maxScratchBytes==acceptedRequest_.maxScratchBytes &&
            route.mainMonoAfterFx==acceptedRequest_.mainMonoAfterFx &&
            route.sources.size()==acceptedRequest_.sources.size() &&
            std::equal(route.sources.begin(),route.sources.end(),
                acceptedRequest_.sources.begin(),
                [](const DynamicSourcePatch& a,const DynamicSourcePatch& b) {
                    return a.id==b.id && a.kind==b.kind &&
                           a.deckId==b.deckId && a.inputL==b.inputL &&
                           a.inputR==b.inputR && a.stereo==b.stereo;
                });
        if(!outputsDisarmed && !physicalUnchanged) {
            result.fault=Fault::graphRejected;
            result.native.fault=DynamicNativeSessionPublisher::Fault::outputArmed;
            return result;
        }
        const auto& channels=accepted_.channels();
        const auto main=std::find_if(channels.begin(),channels.end(),
            [](const ChannelStripDefinition& row) {
                return row.role==ChannelRole::main;
            });
        if(main==channels.end()) {
            result.fault=Fault::editRejected;
            result.edit.fault=ChannelEditFault::invalidAction;
            return result;
        }
        auto candidate=accepted_;
        ChannelEdit create;
        create.kind=ChannelEdit::Kind::add;
        create.position=static_cast<std::size_t>(main-channels.begin());
        create.role=ChannelRole::input;
        create.format=ProcessingFormat::stereo;
        create.text="MUSIC";
        create.connectMainOnCreate=true;
        result.edit=candidate.edit(expectedRosterRevision,
                                    std::span<const ChannelEdit>(&create,1));
        if(!result.edit.ok() || result.edit.createdIds.size()!=1) {
            result.fault=Fault::editRejected;return result;
        }
        auto effective=route; // CONTROL-owned source array, no callback heap
        const ChannelId id=result.edit.createdIds.front();
        effective.sources.push_back({id,-1,-1,true,
                                      DynamicSourceKind::playerDeck,deckId});
        // This flag relaxes the general structural-output disarm only after
        // the caller attests unchanged physical endpoints/pins and MAIN.
        // It does not alter hardware identity or unmute any physical output.
        auto committed=publish(std::move(candidate),effective,
            expectedGraphRevision,requestedEpoch,activeEpoch,
            outputsDisarmed,std::move(result),std::move(fx),
            physicalUnchanged);
        if(committed.ok())playerOwned_=id;
        return committed; // rollback leaves last accepted PCM intact
    }

    // A single native structural operation over accepted ChannelSendRef.
    // No JS DSP or independent router: same compiled PCM, routing readback,
    // device epoch, revision and FX leases as all other native edit paths.
    Result wireTransaction(std::uint64_t expectedRosterRevision,
                           std::uint64_t expectedGraphRevision,
                           std::span<const WireChange> changes,
                           const DynamicGraphRequest& route,
                           std::uint64_t expectedDeviceEpoch,
                           std::uint64_t activeDeviceEpoch,
                           bool allOutputsDisarmed,
                           DynamicNativeSessionPublisher::FxPlan fx={}) {
        Result result=preflight(expectedRosterRevision,expectedGraphRevision);
        if(!result.ok())return result;
        // Validate the hardware generation BEFORE inspecting draft edge IDs.
        // A stale request may reference an edge removed by a newer device
        // epoch; return the stronger device fault without touching the graph.
        if(!expectedDeviceEpoch || expectedDeviceEpoch!=activeDeviceEpoch) {
            result.fault=Fault::graphRejected;
            result.native.fault=DynamicNativeSessionPublisher::Fault::staleDevice;
            result.native.revision=publisher_.acceptedRevision();
            return result;
        }
        auto candidate=accepted_;
        result.edit=candidate.wireTransaction(expectedRosterRevision,changes);
        if(!result.edit.ok()) {
            result.fault=Fault::editRejected;return result;
        }
        // Never trust an armed GUI candidate that quietly changes capture
        // pins, DSP block capacity or device admission. Only pure internal
        // edges can be published while hardware output is armed.
        const bool samePhysical=
            route.physicalInputs==acceptedRequest_.physicalInputs &&
            route.maxFrames==acceptedRequest_.maxFrames &&
            route.maxScratchBytes==acceptedRequest_.maxScratchBytes &&
            route.mainMonoAfterFx==acceptedRequest_.mainMonoAfterFx &&
            route.sources.size()==acceptedRequest_.sources.size() &&
            std::equal(route.sources.begin(),route.sources.end(),
                acceptedRequest_.sources.begin(),
                [](const DynamicSourcePatch& lhs,const DynamicSourcePatch& rhs) {
                    return lhs.id==rhs.id && lhs.inputL==rhs.inputL &&
                           lhs.inputR==rhs.inputR && lhs.stereo==rhs.stereo &&
                           lhs.kind==rhs.kind && lhs.deckId==rhs.deckId;
                });
        if(!allOutputsDisarmed && !samePhysical) {
            result.fault=Fault::graphRejected;
            result.native.fault=DynamicNativeSessionPublisher::Fault::outputArmed;
            result.native.revision=publisher_.acceptedRevision();
            result.edit.createdEdgeIds.clear();
            return result;
        }
        return publish(std::move(candidate),route,expectedGraphRevision,
                       expectedDeviceEpoch,activeDeviceEpoch,allOutputsDisarmed,
                       std::move(result),std::move(fx),samePhysical);
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
                               float parallelGain=.25f,
                               DynamicNativeSessionPublisher::FxPlan fx={}) {
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
                       std::move(result),std::move(fx));
    }

    // Add an empty GROUP or FX Channel, directly OUT -> MAIN, exactly as a
    // Console create command. No hardware input channel is consumed.
    Result createBus(std::uint64_t expectedRosterRevision,
                     std::uint64_t expectedGraphRevision,
                     std::string name,ChannelRole role,
                     const DynamicGraphRequest& route,
                     std::uint64_t expectedDeviceEpoch,
                     std::uint64_t activeDeviceEpoch,
                     bool allOutputsDisarmed,
                     DynamicNativeSessionPublisher::FxPlan fx={}) {
        Result result=preflight(expectedRosterRevision,expectedGraphRevision);
        if(!result.ok())return result;
        auto candidate=accepted_;
        result.edit=candidate.createStandaloneBus(
            expectedRosterRevision,std::move(name),role);
        if(!result.edit.ok()) {
            result.fault=Fault::editRejected;return result;
        }
        return publish(std::move(candidate),route,expectedGraphRevision,
                       expectedDeviceEpoch,activeDeviceEpoch,allOutputsDisarmed,
                       std::move(result),std::move(fx));
    }

    // Delete an existing GROUP or FX return, atomically rerouting primary
    // senders to MAIN and removing addressed parallel Sends. Rejected PCM
    // compile leaves the old Bus and its audio completely intact.
    Result removeBus(std::uint64_t expectedRosterRevision,
                     std::uint64_t expectedGraphRevision,
                     ChannelId busId,
                     const DynamicGraphRequest& route,
                     std::uint64_t expectedDeviceEpoch,
                     std::uint64_t activeDeviceEpoch,
                     bool allOutputsDisarmed,
                     DynamicNativeSessionPublisher::FxPlan fx={}) {
        Result result=preflight(expectedRosterRevision,expectedGraphRevision);
        if(!result.ok())return result;
        auto candidate=accepted_;
        result.edit=candidate.removeBusAndRestoreSources(
            expectedRosterRevision,busId);
        if(!result.edit.ok()) {
            result.fault=Fault::editRejected;return result;
        }
        return publish(std::move(candidate),route,expectedGraphRevision,
                       expectedDeviceEpoch,activeDeviceEpoch,allOutputsDisarmed,
                       std::move(result),std::move(fx));
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
    // Pre-insert, pre-fader digital input gain; a real ChannelId PCM target.
    bool setLiveInputGain(std::uint64_t epoch,std::uint64_t graphRevision,
                          ChannelId id,float linear) noexcept {
        const auto* row=accepted_.find(id);
        if(!row || row->role!=ChannelRole::input ||
           !publisher_.setChannelInputGain(epoch,graphRevision,id,linear))return false;
        return accepted_.rememberLiveInputGain(id,linear);
    }
    // The accepted physical patch is canonical too: subsequent Group/FX
    // graph recompiles must preserve the most recently ACKed live SOURCE.
    bool setLivePhysicalSource(std::uint64_t epoch,std::uint64_t graphRevision,
                               ChannelId id,int inputL,int inputR,
                               bool stereo) noexcept {
        if(!accepted_.find(id))return false;
        const auto it=std::find_if(acceptedRequest_.sources.begin(),
                                  acceptedRequest_.sources.end(),
            [id](const DynamicSourcePatch& p){return p.id==id;});
        if(it==acceptedRequest_.sources.end() ||
           it->kind!=DynamicSourceKind::physical || it->stereo!=stereo ||
           !publisher_.setPhysicalSource(epoch,graphRevision,id,inputL,inputR,stereo))
            return false;
        it->inputL=inputL;it->inputR=inputR;
        return true;
    }
    const DynamicGraphRequest& acceptedPhysicalRequest() const noexcept {
        return acceptedRequest_; // CONTROL readback, not browser draft
    }
    bool setLiveMuteSolo(std::uint64_t epoch,std::uint64_t graphRevision,
                         ChannelId id,bool mute,bool solo) noexcept {
        if(!accepted_.find(id)||
           !publisher_.setChannelMuteSolo(epoch,graphRevision,id,mute,solo))
            return false;
        return accepted_.rememberLiveMuteSolo(id,mute,solo);
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

    // The exact existing parallel EdgeId is toggled without deleting its
    // connection, changing the source's primary OUT, or recompiling the graph.
    bool setLiveSendEnabled(std::uint64_t epoch,std::uint64_t graphRevision,
                            ChannelId source,std::uint64_t edgeId,bool enabled) noexcept {
        const auto* row=accepted_.find(source);
        if(!row || !edgeId)return false;
        for(const auto& edge:row->sends)
            if(edge.edgeId==edgeId && !edge.primaryOutput){
                if(!publisher_.setWireEnabled(epoch,graphRevision,source,edgeId,enabled))
                    return false;
                return accepted_.rememberLiveSendEnabled(source,edgeId,enabled);
            }
        return false;
    }
    bool setLiveWireGain(std::uint64_t epoch,std::uint64_t revision,
                         ChannelId source,std::uint64_t edgeId,float gain) noexcept {
        const auto* row=accepted_.find(source);
        if(!row||!edgeId)return false;
        for(const auto& edge:row->sends)
            if(edge.edgeId==edgeId&&!edge.primaryOutput)
                return setLiveParallelSend(epoch,revision,source,edge.destination,edge.preFader,gain);
        return false;
    }
    // This is a callback-ready DELEGATE, not an additional audio renderer.
    // M3D does not attach it to the production JUCE device until VST/Show
    // ownership and native output-port parity have been migrated.
    template<class Fx>
    bool render(std::uint64_t epoch,const float* const* inputs,int inputCount,
                float* mainL,float* mainR,int frames,bool muted,Fx&& fx,
                float* monitorL=nullptr,float* monitorR=nullptr,
                std::span<float> channelPeaksL={},
                std::span<float> channelPeaksR={},
                std::span<const DynamicPlayerPcmBlock> playerPcm={}) noexcept {
        return publisher_.render(epoch,inputs,inputCount,mainL,mainR,frames,
                                 muted,std::forward<Fx>(fx),monitorL,monitorR,
                                 channelPeaksL,channelPeaksR,playerPcm);
    }
    void unpublish() noexcept {publisher_.unpublish();hasHostedFx_=false;}
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
                   bool outputsDisarmed,Result&& result,
                   DynamicNativeSessionPublisher::FxPlan fx,
                   bool verifiedInternalWireOnly=false) {
        // Existing SOURCE targets belong to the accepted native session.
        // For already-patched ChannelIds with the same input format, ignore
        // stale physical pin ordinals supplied by an old GUI draft during
        // +GROUP/+FX Return. An explicit format change is structural.
        // E2: once an accepted graph has hosted native VST, structural
        // +GROUP/+FX Return/reorder must publish the same real instance
        // leases at the next revision. Without this check, an omitted FxPlan
        // silently drops processing on every already-audible channel.
        // Reject while preserving old graph, vendor state and PCM.
        if(hasHostedFx_ && !fx.owner) {
            result.fault=Fault::graphRejected;
            result.native.fault=DynamicNativeSessionPublisher::Fault::invalidCandidate;
            result.native.revision=publisher_.acceptedRevision();
            result.edit.createdIds.clear();
            result.edit.createdEdgeIds.clear();
            return result;
        }
        // A structural OUT/GROUP/Return edit does NOT implicitly change IN.
        // The operator's same-format IN gesture is accepted first through
        // setLivePhysicalSource(epoch,revision,...), which also updates the
        // canonical acceptedRequest_. A stale GUI draft must rebase from that
        // accepted SOURCE before asking for a structural ACK. Never silently
        // restore old pins and claim the requested new route was accepted.
        // Genuine format changes remain structural, with normal graph checks.
        for(const auto& source:route.sources) {
            const bool valid=DynamicPreparedMixerBlock::validSourcePatch(
                route,source);
            const auto prior=std::find_if(acceptedRequest_.sources.begin(),
                acceptedRequest_.sources.end(),
                [&](const DynamicSourcePatch& p){return p.id==source.id;});
            const bool conflicting=prior!=acceptedRequest_.sources.end() &&
                (source.kind!=prior->kind || source.deckId!=prior->deckId ||
                 (source.stereo==prior->stereo &&
                  (source.inputL!=prior->inputL || source.inputR!=prior->inputR)));
            if(!valid || conflicting) {
                result.fault=Fault::graphRejected;
                result.native.fault=DynamicNativeSessionPublisher::Fault::invalidGraph;
                result.native.graphError={DynamicGraphFault::invalidSource,source.id};
                result.native.revision=publisher_.acceptedRevision();
                result.edit.createdIds.clear();
            result.edit.createdEdgeIds.clear();
                return result; // old audible SOURCE and VST3 leases unchanged
            }
        }
        auto effective=route; // CONTROL-only; preallocated before commit
        const bool nextHasFx=static_cast<bool>(fx.owner);
        result.native=publisher_.commit(candidate,effective,previousGraphRevision,
                                       requestedEpoch,currentEpoch,outputsDisarmed,
                                       std::move(fx),verifiedInternalWireOnly);
        if(!result.native.ok()) {
            result.fault=Fault::graphRejected;
            result.edit.createdIds.clear();
            result.edit.createdEdgeIds.clear(); // NEVER advertise phantom Bus
            return result; // candidate destroyed; accepted state intact
        }
        static_assert(std::is_nothrow_move_assignable_v<DynamicChannelRoster>);
        accepted_=std::move(candidate);
        acceptedRequest_=std::move(effective);
        hasHostedFx_=nextHasFx;
        result.rosterRevision=accepted_.revision();
        result.graphRevision=result.native.revision;
        return result;
    }

    DynamicChannelRoster accepted_;
    DynamicGraphRequest acceptedRequest_{}; // CONTROL-only typed source SSOT
    ChannelId playerOwned_{}; // CONTROL-owned, never a name-based guess
    DynamicNativeSessionPublisher publisher_;
    bool hasHostedFx_=false; // CONTROL only; never an audio callback flag
};
} // namespace armix::mixer
