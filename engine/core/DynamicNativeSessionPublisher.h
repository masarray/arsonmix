// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicPreparedMixerBlock.h"
#include "DynamicConsoleRoutingView.h"
#include "NativeMusicTempoMonitor.h"
#include "NativeBeatFrameContext.h"
#include "RealtimeRoutePublisher.h"
#include "RealtimeRenderPermit.h"
#include "../lms/NativeSpeakerOutputReservation.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

// M3B: one callback-ready ChannelId session publisher, NOT another audio
// device/callback. Control thread prepares immutable graph + publisher lease;
// the existing JUCE callback may eventually use render() INSTEAD OF the old
// PreparedMixerBlock after CH1/Show/native VST owner migration succeeds.
// Never run both renderers or sum their outputs. No browser-only LIVE status.
namespace armix::mixer {
class DynamicNativeSessionPublisher final {
public:
    // Control-owned native FX lifetime travels IN the same accepted PCM
    // snapshot. JUCE types stay outside this portable DSP publisher.
    // Optional one-pass observer of the already-accepted MAIN PCM after
    // ChannelId routing and rack FX. No new renderer, separate mixing pass,
    // hardware pin ownership, or data lifetime beyond this callback.
    // Observers MUST be no-throw, allocation-free and MUST NOT mutate MAIN.
    // This is pre-master-output gain; the physical post-gain path remains the
    // authority for future speaker activation and must be gated separately.
    struct MainPcmObserver final {
        void* context=nullptr;
        void (*observe)(void*,const float*,const float*,int) noexcept=nullptr;
    };
    // M5: CONTROL-only, immutable readback of a VERIFIED but DISCONNECTED
    // speaker design. Never exposed to the realtime callback. In particular,
    // `validatedDraft` must NOT be presented as physically APPLIED/LIVE.
    enum class SpeakerDraftFault : std::uint8_t {
        none,staleDeviceOrGraph,staleDraft,invalidSource,
        invalidReservation,invalidDsp,preparationFailed
    };
    struct SpeakerDraftAck final {
        SpeakerDraftFault fault=SpeakerDraftFault::none;
        armix::lms::SpeakerReservationFault reservationFault=
            armix::lms::SpeakerReservationFault::none;
        armix::lms::SpeakerPlanFault planFault=armix::lms::SpeakerPlanFault::none;
        std::uint64_t deviceEpoch=0,graphRevision=0,designRevision=0;
        std::size_t preparedScratchBytes=0;
        bool validatedDraft=false,hardwareApplied=false;
        [[nodiscard]] bool ok() const noexcept {
            return fault==SpeakerDraftFault::none && validatedDraft &&
                   !hardwareApplied;
        }
    };
    struct SpeakerDraftReadback final {
        std::uint64_t deviceEpoch=0,graphRevision=0,designRevision=0;
        ChannelId mainSource{};
        std::string deviceIdentity;
        armix::lms::SpeakerSystemRequest design;
        std::vector<int> reservedOutputOrdinals;
        std::size_t preparedScratchBytes=0;
        bool validatedDraft=true,hardwareApplied=false;
    };
    struct FxPlan final {
        std::shared_ptr<void> owner; // CONTROL-only copy/destruction
        void* context=nullptr;       // unchanged owner.get(), RT raw pointer
        void (*render)(void*,ChannelId,float*,float*,int) noexcept=nullptr;
        std::uint64_t deviceEpoch=0,graphRevision=0;
        int (*latency)(void*,ChannelId) noexcept=nullptr;
        bool valid() const noexcept {
            return (!owner&&!context&&!render&&!deviceEpoch&&!graphRevision&&!latency) ||
                   (owner&&context==owner.get()&&render&&
                    deviceEpoch>0&&graphRevision>0);
        }
    };
private:
    struct Prepared final {
        DynamicPreparedMixerBlock graph;
        DynamicConsoleRoutingView routing;
        FxPlan fx; // SAME lifetime and revision as native PCM graph
        int physicalInputs=0;
        bool monitor=false;
    };
    // On CONTROL only: one speaker design belongs to the same accepted
    // ChannelId/epoch. The prepared DSP is intentionally NOT placed in the
    // hardware callback's published snapshot until physical protection exists.
    std::unique_ptr<SpeakerDraftReadback> speakerDraft_;
    std::unique_ptr<armix::lms::PreparedSpeakerSystem> speakerPrepared_;
    using Publisher=armix::rt::RealtimeRoutePublisher<Prepared>;
public:
    enum class Fault : std::uint8_t {
        none, staleDevice, staleRevision, outputArmed, invalidGraph,
        retirementFull, invalidCandidate
    };
    struct CommitResult final {
        Fault fault=Fault::none;
        DynamicGraphError graphError{};
        std::uint64_t revision=0;
        bool ok() const noexcept {return fault==Fault::none;}
    };

    DynamicNativeSessionPublisher()=default;
    DynamicNativeSessionPublisher(const DynamicNativeSessionPublisher&)=delete;
    DynamicNativeSessionPublisher& operator=(const DynamicNativeSessionPublisher&)=delete;

    // CONTROL ONLY: caller already verified the real hardware capture mask
    // and independently disarmed MAIN, MONITOR and secondary outputs.
    // A rejected compile/publish never changes the previous accepted PCM.
    CommitResult commit(const DynamicChannelRoster& roster,
                        const DynamicGraphRequest& request,
                        std::uint64_t expectedRevision,
                        std::uint64_t proposedDeviceEpoch,
                        std::uint64_t activeDeviceEpoch,
                        bool allOutputsDisarmed,
                        FxPlan fx={nullptr,nullptr,nullptr,0,0,nullptr},bool verifiedInternalWireOnly=false,
                        bool verifiedInternalFxOnly=false) {
        CommitResult report;report.revision=publisher_.lastRevision();
        if(!fx.valid() ||
           (fx.owner && (fx.deviceEpoch!=proposedDeviceEpoch ||
                         fx.graphRevision!=expectedRevision+1))) {
            report.fault=Fault::invalidCandidate;return report;
        }
        if(proposedDeviceEpoch==0 ||
           proposedDeviceEpoch!=activeDeviceEpoch) {
            report.fault=Fault::staleDevice;return report;
        }
        if(expectedRevision!=publisher_.lastRevision() ||
           expectedRevision==std::numeric_limits<std::uint64_t>::max()) {
            report.fault=Fault::staleRevision;return report;
        }
        // A latency-budgeted vendor FX lease can be prepared off-RT and
        // published on the SAME physical device/patch as the existing
        // ChannelId graph. Neither WIRE nor this in-place FX edit can
        // change physical pins. Other structural edits still disarm.
        if(verifiedInternalFxOnly && !fx.owner) {
            report.fault=Fault::invalidCandidate;return report;
        }
        if(!allOutputsDisarmed && !verifiedInternalWireOnly &&
           !verifiedInternalFxOnly) {
            report.fault=Fault::outputArmed;return report;
        }
        auto next=std::make_unique<Publisher::Snapshot>();
        next->deviceEpoch=proposedDeviceEpoch;
        next->graphRevision=expectedRevision+1;
        auto prepared=std::make_unique<Prepared>();
        if(!DynamicPreparedMixerBlock::compile(
              roster,request,prepared->graph,report.graphError)) {
            report.fault=Fault::invalidGraph;return report;
        }
        if(!prepared->graph.prepareLatency([&](ChannelId id) noexcept {
               return fx.latency?fx.latency(fx.context,id):0;
           },request.maxScratchBytes,report.graphError)) {
            report.fault=Fault::invalidGraph;return report;
        }
        // The Console data and PCM schedule must originate from one and the
        // same accepted roster. No browser-side inferred BUS assignments.
        if(!DynamicConsoleRoutingView::build(roster,prepared->routing)) {
            report.fault=Fault::invalidGraph;return report;
        }
        prepared->physicalInputs=request.physicalInputs;
        prepared->fx=std::move(fx); // one accepted control-plane publication
        prepared->monitor=std::any_of(roster.channels().begin(),
          roster.channels().end(),[](const ChannelStripDefinition& row) {
              return row.role==ChannelRole::monitor;
          });
        next->prepared=std::move(prepared);
        const auto status=publisher_.publish(std::move(next),
                                             expectedRevision,activeDeviceEpoch);
        switch(status) {
            case Publisher::Result::applied: {
                // Every accepted structural PCM revision revokes old speaker
                // pin identity and DSP; a UI draft cannot survive by accident.
                speakerDraft_.reset();
                speakerPrepared_.reset();
                const auto source=musicTempo_.selectedSource();
                if(source.value) {
                    const auto* published=publisher_.controlPrepared(
                        proposedDeviceEpoch,expectedRevision+1);
                    const auto* channel=published?
                        published->routing.find(source):nullptr;
                    const bool retained=published&&channel&&
                        (channel->role==ChannelRole::input||
                         channel->role==ChannelRole::group||
                         channel->role==ChannelRole::fxReturn)&&
                        published->graph.channelScheduled(source);
                    musicTempo_.onGraphCommitted(proposedDeviceEpoch,
                        expectedRevision,expectedRevision+1,retained);
                }
                report.revision=expectedRevision+1;return report;
            }
            case Publisher::Result::staleDevice:
                report.fault=Fault::staleDevice;break;
            case Publisher::Result::staleRevision:
                report.fault=Fault::staleRevision;break;
            case Publisher::Result::retirementFull:
                report.fault=Fault::retirementFull;break;
            default:report.fault=Fault::invalidCandidate;break;
        }
        report.revision=publisher_.lastRevision();
        return report;
    }

    // M5: same-publisher CONTROL transaction. Required caller-side proof:
    // inventory was constructed from the ACTUAL active JUCE device's
    // enabled output mask, never from browser-stored ordinal labels.
    // No side effects on existing ChannelId PCM or hardware pin ownership.
    // ALL speaker output DSP remains permanently DISCONNECTED at M5.
    SpeakerDraftAck acceptSpeakerDraft(
        const armix::lms::SpeakerSystemRequest& request,
        const armix::lms::NativePcmOutputInventory& inventory,
        ChannelId mainSource,std::uint64_t expectedDesignRevision) {
        SpeakerDraftAck ack;
        ack.deviceEpoch=request.deviceEpoch;
        ack.graphRevision=request.graphRevision;
        ack.designRevision=speakerDraft_?speakerDraft_->designRevision:0;
        const auto* accepted=publisher_.controlPrepared(
            request.deviceEpoch,request.graphRevision);
        if(!accepted || !request.deviceEpoch || !request.graphRevision ||
           !inventory.acceptedChannelIdPcm ||
           !inventory.deviceEpoch || !inventory.acceptedGraphRevision ||
           inventory.deviceEpoch!=request.deviceEpoch ||
           inventory.acceptedGraphRevision!=request.graphRevision) {
            ack.fault=SpeakerDraftFault::staleDeviceOrGraph;return ack;
        }
        if(expectedDesignRevision!=ack.designRevision ||
           expectedDesignRevision==std::numeric_limits<std::uint64_t>::max()){
            ack.fault=SpeakerDraftFault::staleDraft;return ack;
        }
        if(!mainSource.value || accepted->routing.main!=mainSource ||
           inventory.mainChannelId!=mainSource.value){
            ack.fault=SpeakerDraftFault::invalidSource;return ack;
        }
        if(request.maxFrames!=accepted->graph.capacity()) {
            ack.fault=SpeakerDraftFault::invalidDsp;
            ack.planFault=armix::lms::SpeakerPlanFault::invalidFormat;
            return ack;
        }
        try {
            const auto reserved=armix::lms::NativeSpeakerOutputReservation::
                preflight(request,inventory,mainSource.value);
            if(!reserved.ok() || reserved.mayWritePhysicalAudio){
                ack.fault=SpeakerDraftFault::invalidReservation;
                ack.reservationFault=reserved.fault;return ack;
            }
            std::unique_ptr<armix::lms::PreparedSpeakerSystem> dsp;
            const auto candidate=armix::lms::PreparedSpeakerSystem::
                prepare(request,dsp);
            if(!candidate.ok() || !dsp){
                ack.fault=SpeakerDraftFault::invalidDsp;
                ack.planFault=candidate.fault;return ack;
            }
            // Everything above is speculative. Build a complete readback
            // BEFORE publishing either pointer: an exception leaves the last
            // accepted draft and PCM untouched (transactional rollback).
            auto snapshot=std::make_unique<SpeakerDraftReadback>();
            snapshot->deviceEpoch=request.deviceEpoch;
            snapshot->graphRevision=request.graphRevision;
            snapshot->designRevision=expectedDesignRevision+1;
            snapshot->mainSource=mainSource;
            snapshot->deviceIdentity=inventory.deviceIdentity;
            snapshot->design=request;
            snapshot->reservedOutputOrdinals=reserved.reservedCallbackOrdinals;
            snapshot->preparedScratchBytes=candidate.scratchBytes;
            speakerPrepared_=std::move(dsp);
            speakerDraft_=std::move(snapshot);
            ack.designRevision=speakerDraft_->designRevision;
            ack.preparedScratchBytes=candidate.scratchBytes;
            ack.validatedDraft=true;
            // No hardware output ACL exists in this transaction.
            ack.hardwareApplied=false;
            return ack;
        } catch(...) {
            ack.fault=SpeakerDraftFault::preparationFailed;
            return ack;
        }
    }

    // Copy the returned values on CONTROL before modifying the accepted PCM.
    // Any graph/device change makes old draft readback unavailable.
    [[nodiscard]] const SpeakerDraftReadback* speakerDraftReadback(
        std::uint64_t epoch,std::uint64_t graphRevision) const noexcept {
        if(!speakerDraft_ || speakerDraft_->deviceEpoch!=epoch ||
           speakerDraft_->graphRevision!=graphRevision ||
           !publisher_.controlPrepared(epoch,graphRevision))return nullptr;
        return speakerDraft_.get();
    }

    // Closing a session, disconnecting device, or clearing a physical patch
    // must invalidate readback before another control request arrives.
    void revokeSpeakerDraft() noexcept {
        speakerDraft_.reset();
        speakerPrepared_.reset();
    }

    // Sole AUDIO CALLBACK path when connected by DYN1 live migration:
    // no alloc, mutex, wait, refcount, graph sorting or VST creation.
    // Caller-supplied FX owns its native ChannelId-keyed VST instance lease.
    // outputMuted is the physical output safety gate, not a UI preview.
    template<class Fx>
    bool render(std::uint64_t deviceEpoch,const float* const* physical,
                int physicalCount,float* mainL,float* mainR,int frames,
                bool outputMuted,Fx&& effect,
                float* monitorL=nullptr,float* monitorR=nullptr,
                std::span<float> channelPeaksL={},
                std::span<float> channelPeaksR={},
                std::span<const DynamicPlayerPcmBlock> playerPcm={},
                float* const* hardwareOutputs=nullptr,
                int hardwareOutputCount=0,
                MainPcmObserver observer={}) noexcept {

        static_assert(std::is_nothrow_invocable_v<Fx,ChannelId,float*,float*,int>,
                      "Realtime VST dispatch must be noexcept");
        const auto clearOutputs=[&]() noexcept {
            // A rejected/stale/muted native render must silence *every*
            // ASIO destination, not only MAIN/MONITOR. Otherwise a previous
            // external tap can remain in a driver-provided output buffer.
            // Bounded callback-ordinal traversal; no allocations or locks.
            if(frames<=0 || frames>8192)return;
            if(hardwareOutputs && hardwareOutputCount>0 && hardwareOutputCount<=256)
                for(int c=0;c<hardwareOutputCount;++c)
                    if(hardwareOutputs[c])std::fill_n(hardwareOutputs[c],frames,0.f);
            if(mainL)std::fill_n(mainL,frames,0.f);
            if(mainR && mainR!=mainL)std::fill_n(mainR,frames,0.f);
            if(monitorL)std::fill_n(monitorL,frames,0.f);
            if(monitorR && monitorR!=monitorL)std::fill_n(monitorR,frames,0.f);
        };
        if(frames<=0 || frames>8192 || !mainL || !mainR ||
           mainL==mainR || physicalCount<0 || physicalCount>256 ||
           (physicalCount>0 && !physical) ||
           (monitorL==nullptr)!=(monitorR==nullptr) ||
           (monitorL && (monitorL==monitorR || monitorL==mainL ||
                         monitorL==mainR || monitorR==mainL ||
                         monitorR==mainR))) {
            if(frames>0 && frames<=8192)clearOutputs();
            return false;
        }
        if(outputMuted) {
            clearOutputs();
            return false;
        }
        auto lease=publisher_.acquire();
        const auto* snap=lease.get(deviceEpoch);
        if(!snap || !snap->prepared ||
           physicalCount!=snap->prepared->physicalInputs ||
           (snap->prepared->monitor && !monitorL) ||
           frames>snap->prepared->graph.capacity()) {
            // A real, current-epoch callback with a changed input count or
            // unsafe quantum must not retain last block's EXTERNAL LIVE
            // readback after every hardware buffer was silenced.
            if(snap && snap->prepared)
                snap->prepared->graph.invalidateAppliedExternalOutputs();
            clearOutputs();return false;
        }
        // A single callback producer owns the prepared scratch. In case a
        // broken backend calls simultaneously, fail closed rather than race.
        armix::rt::RealtimeRenderPermit exclusive{rendering_};
        if(!exclusive) {
            clearOutputs();return false;
        }
        // One sample domain for all ChannelIds and their FX racks. This
        // fixed-time callback-local scope also fences bypassed/stale inserts;
        // no second transport thread, allocation or callback is introduced.
        const auto startFrame=musicTempo_.beginAudioBlock(deviceEpoch,frames);
        NativeBeatFrameScope frameScope{deviceEpoch,snap->graphRevision,startFrame};
        // Prepared FX takes precedence over the legacy external callback.
        // NEVER invoke both: that would double-compress or double-reverb.
        // Snapshot ownership pins the M4 instance throughout this callback;
        // only control-thread retirement releases the shared owner.
        const auto dispatch=[&](ChannelId id,float* l,float* r,int n) noexcept {
            // Analysis source is the installed ChannelId MUSIC PCM, before
            // its VST rack; never microphone/echo/main unless explicitly
            // selected and role-checked on CONTROL. No analysis on AUDIO.
            musicTempo_.capture(id,l,r,n,snap->deviceEpoch,snap->graphRevision);
            const auto& acceptedFx=snap->prepared->fx;
            if(acceptedFx.render)
                acceptedFx.render(acceptedFx.context,id,l,r,n);
            else effect(id,l,r,n);
        };
        const bool ok=snap->prepared->graph.process(
             physical,physicalCount,mainL,mainR,frames,
             dispatch,monitorL,monitorR,channelPeaksL,channelPeaksR,playerPcm,
              hardwareOutputs,hardwareOutputCount);
        if(ok && observer.observe)
            observer.observe(observer.context,mainL,mainR,frames);
        if(!ok) {
            // A rejected physical callback (including aliased independent
            // amplifier buffers) emits silence, not a stale LIVE snapshot.
            // The approved GUI will report PENDING until a later successful
            // PCM callback re-publishes APPLIED on the same ChannelId graph.
            snap->prepared->graph.invalidateAppliedExternalOutputs();
            clearOutputs();
        }
        return ok;
    }

    // CONTROL ONLY: choose an actual active backing input/GROUP/FX return.
    // Preserve the one authoritative PCM graph and reject stale selectors.
    bool setMusicTempoSource(std::uint64_t epoch,std::uint64_t revision,
                             ChannelId source,double sampleRate) noexcept {
        const auto* p=publisher_.controlPrepared(epoch,revision);
        if(!p)return false;
        if(source.value) {
            const auto* ch=p->routing.find(source);
            if(!ch || (ch->role!=ChannelRole::input &&
                       ch->role!=ChannelRole::group &&
                       ch->role!=ChannelRole::fxReturn) ||
               !p->graph.channelScheduled(source))return false;
        }
        return musicTempo_.select(source,epoch,revision,sampleRate);
    }
    NativeMusicTempoMonitor::State pollMusicTempo(std::uint64_t epoch,
                                                 std::uint64_t revision) noexcept {
        return musicTempo_.poll(epoch,revision);
    }
    // CONTROL ONLY; pointer is valid until the next structural commit.
    // The caller must COPY needed fields before yielding the writer thread.
    // Future WebView event serializes this only after native ACK, never draft.
    const DynamicConsoleRoutingView* acceptedRouting(
        std::uint64_t epoch,std::uint64_t revision) const noexcept {
        const auto* current=publisher_.controlPrepared(epoch,revision);
        return current?&current->routing:nullptr;
    }

    // O1 CONTROL only: one epoch/revision-guarded target in the same PCM graph.
    // Caller validates actual driver pins and MAIN/MONITOR collision first.
    bool setExternalOutput(std::uint64_t epoch,std::uint64_t revision,
                           ChannelId id,int left,int right,int outputCount) noexcept {
        const bool updated=publisher_.updateActiveControl(epoch,revision,
            [&](Prepared& active) noexcept {
                return active.graph.setExternalOutput(id,left,right,outputCount);
            });
        if(updated)revokeSpeakerDraft(); // physical pin inventory changed
        return updated;
    }
    std::pair<int,int> externalOutput(std::uint64_t epoch,
                std::uint64_t revision,ChannelId id) const noexcept {
        const auto* p=publisher_.controlPrepared(epoch,revision);
        return p?p->graph.externalOutput(id):std::pair<int,int>{-1,-1};
    }

    std::pair<int,int> appliedExternalOutput(std::uint64_t epoch,
                std::uint64_t revision,ChannelId id) const noexcept {
        const auto* p=publisher_.controlPrepared(epoch,revision);
        return p?p->graph.appliedExternalOutput(id):std::pair<int,int>{-1,-1};
    }

    // M3C realtime parameters on the accepted ChannelId graph ONLY.
    // Caller must propagate the real device epoch and native ACK revision.
    // Rejected/stale edits leave the last accepted PCM unchanged.
    bool setPhysicalSource(std::uint64_t epoch,std::uint64_t revision,
                           ChannelId id,int inputL,int inputR,bool stereo) noexcept {
        return publisher_.updateActiveControl(epoch,revision,
            [&](Prepared& active) noexcept {
                return active.graph.setPhysicalSource(id,inputL,inputR,stereo);
            });
    }
    bool setChannelMix(std::uint64_t epoch,std::uint64_t revision,
                       ChannelId id,float gain,float pan) noexcept {
        return publisher_.updateActiveControl(epoch,revision,
            [&](Prepared& active) noexcept {
                return active.graph.setChannelMix(id,gain,pan);
            });
    }
    bool setChannelInputGain(std::uint64_t epoch,std::uint64_t revision,
                             ChannelId id,float gain) noexcept {
        return publisher_.updateActiveControl(epoch,revision,
            [&](Prepared& active) noexcept {
                return active.graph.setChannelInputGain(id,gain);
            });
    }
    bool setChannelMuteSolo(std::uint64_t epoch,std::uint64_t revision,
                            ChannelId id,bool mute,bool solo) noexcept {
        return publisher_.updateActiveControl(epoch,revision,
            [&](Prepared& active) noexcept {
                return active.graph.setChannelMuteSolo(id,mute,solo);
            });
    }
    bool setParallelSendGain(std::uint64_t epoch,std::uint64_t revision,
                             ChannelId source,ChannelId destination,
                             bool preFader,float gain) noexcept {
        return publisher_.updateActiveControl(epoch,revision,
            [&](Prepared& active) noexcept {
                if(!std::isfinite(gain)||gain<0.f||gain>4.f)return false;
                const bool admitted=std::any_of(active.routing.edges.begin(),active.routing.edges.end(),
                    [&](const auto& edge){return edge.from==source&&edge.to==destination&&
                        edge.preFader==preFader&&!edge.primaryOutput;});
                if(!admitted)return false;
                if(active.graph.channelScheduled(source)&&!active.graph.setParallelSendGain(
                    source,destination,preFader,gain))return false;
                // A dormant send has no scratch/atomic DSP edge yet. Its
                // accepted gain is still editable and seeds the next source
                // preparation through the authoritative remembered roster.
                // Routing readback is CONTROL-only; AUDIO reads only the
                // graph's atomic gain target and preallocated 64-sample slew.
                for(auto& edge:active.routing.edges)
                    if(edge.from==source&&edge.to==destination&&
                       edge.preFader==preFader&&!edge.primaryOutput)
                        edge.gainLinear=gain;
                for(auto& row:active.routing.channels)if(row.id==source)
                    for(auto& edge:row.parallelSends)
                        if(edge.destination==destination&&edge.preFader==preFader)
                            edge.gain=gain;
                return true;
            });
    }
    bool setParallelSendEnabled(std::uint64_t epoch,std::uint64_t revision,
                                ChannelId source,ChannelId destination,
                                bool preFader,bool enabled) noexcept {
        return publisher_.updateActiveControl(epoch,revision,
            [&](Prepared& active) noexcept {
                const bool admitted=std::any_of(active.routing.edges.begin(),
                    active.routing.edges.end(),[&](const auto& edge){
                        return edge.from==source&&edge.to==destination&&
                          edge.preFader==preFader&&!edge.primaryOutput;
                    });
                if(!admitted)return false;
                if(active.graph.channelScheduled(source)&&
                   !active.graph.setParallelSendEnabled(source,destination,preFader,enabled))
                    return false;
                for(auto& edge:active.routing.edges)
                    if(edge.from==source&&edge.to==destination&&
                       edge.preFader==preFader&&!edge.primaryOutput)edge.enabled=enabled;
                for(auto& row:active.routing.channels)if(row.id==source)
                    for(auto& edge:row.parallelSends)
                        if(edge.destination==destination&&edge.preFader==preFader)
                            edge.enabled=enabled;
                return true;
            });
    }
    bool setWireEnabled(std::uint64_t epoch,std::uint64_t revision,
                        ChannelId source,std::uint64_t edgeId,bool enabled) noexcept {
        const auto* routing=acceptedRouting(epoch,revision);
        if(!routing||!edgeId)return false;
        for(const auto& edge:routing->edges)
            if(edge.edgeId==edgeId&&edge.from==source&&!edge.primaryOutput)
                return setParallelSendEnabled(epoch,revision,source,edge.to,
                                               edge.preFader,enabled);
        return false;
    }
    bool setWireGain(std::uint64_t epoch,std::uint64_t revision,
                     ChannelId source,std::uint64_t edgeId,float gain) noexcept {
        const auto* routing=acceptedRouting(epoch,revision);
        if(!routing||!edgeId)return false;
        for(const auto& edge:routing->edges)
            if(edge.edgeId==edgeId&&edge.from==source&&!edge.primaryOutput)
                return setParallelSendGain(epoch,revision,source,edge.to,edge.preFader,gain);
        return false;
    }

    // CONTROL ONLY; never destroy the last live graph in a callback.
    int latencySamples(std::uint64_t epoch,std::uint64_t revision) const noexcept {
        const auto* p=publisher_.controlPrepared(epoch,revision);
        return p?p->graph.latencySamples():0;
    }
    void unpublish() noexcept {revokeSpeakerDraft();publisher_.unpublish();}
    void pollRetirement() noexcept {publisher_.pollRetirement();}
    std::uint64_t acceptedRevision() const noexcept {
        return publisher_.lastRevision(); // CONTROL only
    }
    std::size_t pendingRetirements() const noexcept {
        return publisher_.pendingRetirements(); // CONTROL only
    }
private:
    Publisher publisher_;
    NativeMusicTempoMonitor musicTempo_{};
    std::atomic_flag rendering_=ATOMIC_FLAG_INIT;
};
} // namespace armix::mixer
