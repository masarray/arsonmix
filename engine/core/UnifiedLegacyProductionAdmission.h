// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "LegacyLiveGraphMigration.h"
#include "DynamicChannelFxLeaseBank.h"
#include "DynamicNativeSessionPublisher.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// M-U1: full M4 10CH + 4 AUX + MONITOR PCM and real VST leases are admitted
// as ONE identity-preserving dynamic session. No extra audio callback.
// This is CONTROL-only preparation; the physical callback cutover is a
// separate safety transaction and must select only ONE renderer.
// Occupied CH1 external/Orbit may be admitted ONLY with its original
// processor/editor owner and validated ChannelId; never silently drop VST.
namespace armix::mixer {
struct VerifiedNativeOutputs final {
    int activeOutputChannels=0; // EXACT current enabled callback output count
    int mainL=-1,mainR=-1,monitorL=-1,monitorR=-1;
    bool monitorEnabled=false;
    bool valid() const noexcept {
        if(activeOutputChannels<2||activeOutputChannels>256 ||
           mainL<0||mainR<0||mainL>=activeOutputChannels||
           mainR>=activeOutputChannels||mainL==mainR)return false;
        if(!monitorEnabled)return monitorL==-1&&monitorR==-1;
        return monitorL>=0&&monitorR>=0&&
               monitorL<activeOutputChannels&&
               monitorR<activeOutputChannels&&
               monitorL!=monitorR&&monitorL!=mainL&&
               monitorL!=mainR&&monitorR!=mainL&&monitorR!=mainR;
    }
};
struct UnifiedLegacyAccepted final {
    LegacyMigratedGraph graph{};
    std::shared_ptr<DynamicChannelFxLeaseBank> fxOwners{};
    VerifiedNativeOutputs hardware{};
    std::uint64_t deviceEpoch=0,pcmRevision=0;
};
class UnifiedLegacyProductionAdmission final {
public:
    enum class Fault : std::uint8_t {
        none,invalidOutputPins,staleHardware,outputArmed,
        legacyCh1ExternalFx,staleFxGraph,legacyGraphInvalid,
        fxLeaseInvalid,nativeRejected
    };
    struct Report final {
        Fault fault=Fault::none;
        LegacyMigrationError migration{};
        DynamicChannelFxLeaseBank::Fault fx=DynamicChannelFxLeaseBank::Fault::none;
        DynamicNativeSessionPublisher::CommitResult native{};
        bool ok() const noexcept {return fault==Fault::none;}
    };

    // Must be invoked on the SAME serialized M4 native control executor as
    // hardware APPLY, Show capture and editor state. Prior accepted graph,
    // leases and user outputs stay intact on any rejection.
    static Report compileAndCommit(
        const ChannelRoutingConfig& channels,
        std::span<const int> callbackOrderedPhysicalPorts,
        const SendRequest& sends,const AuxBusRequest& aux,
        const std::array<float,stripCount+auxBusCount>& monitorGains,
        const LiveChannelFxGraph& acceptedM4Fx,
        bool legacyExternalCh1FxActive,
        const VerifiedNativeOutputs& hardware,
        int maxFrames,std::size_t scratchBudget,
        std::uint64_t expectedPcmRevision,
        std::uint64_t deviceEpoch,std::uint64_t currentHardwareEpoch,
        bool allOutputsDisarmed,
        DynamicNativeSessionPublisher& publisher,
        UnifiedLegacyAccepted& accepted,
        DynamicChannelFxLeaseBank::ExternalLease externalCh1={}) {
        Report result;
        if(!hardware.valid() ||
           hardware.monitorEnabled!=acceptedM4Fx.monitorEnabled() ||
           hardware.mainL!=acceptedM4Fx.mainOutputLeft() ||
           hardware.mainR!=acceptedM4Fx.mainOutputRight() ||
           (hardware.monitorEnabled &&
            (hardware.monitorL!=acceptedM4Fx.monitorOutputLeft()||
             hardware.monitorR!=acceptedM4Fx.monitorOutputRight()))) {
            result.fault=Fault::invalidOutputPins;return result;
        }
        if(!deviceEpoch || deviceEpoch!=currentHardwareEpoch) {
            result.fault=Fault::staleHardware;return result;
        }
        if(!allOutputsDisarmed) {
            result.fault=Fault::outputArmed;return result;
        }
        if(legacyExternalCh1FxActive!=externalCh1.populated() ||
           (legacyExternalCh1FxActive && !externalCh1.valid())) {
            result.fault=Fault::legacyCh1ExternalFx;return result;
        }
        // FxInstance owns one already-prepared stereo block. Do not let the
        // dynamic callback deliver a larger quantum and silently bypass it.
        if(maxFrames<1||maxFrames>acceptedM4Fx.capacity()||
           maxFrames>acceptedM4Fx.maxFramesPerPlugin()) {
            result.fault=Fault::staleFxGraph;return result;
        }
        if(expectedPcmRevision!=publisher.acceptedRevision()) {
            result.fault=Fault::nativeRejected;
            result.native.fault=DynamicNativeSessionPublisher::Fault::staleRevision;
            return result;
        }
        LegacyMigratedGraph candidate;
        if(!LegacyGraphMigration::compile(
             channels,callbackOrderedPhysicalPorts,sends,aux,
             hardware.monitorEnabled,monitorGains,maxFrames,scratchBudget,
             candidate,result.migration)) {
            result.fault=Fault::legacyGraphInvalid;return result;
        }
        using Bank=DynamicChannelFxLeaseBank;
        if(externalCh1.populated() &&
           (externalCh1.id!=candidate.ids.inputs[0] ||
            acceptedM4Fx.insertsOn(0)!=0)) {
            result.fault=Fault::legacyCh1ExternalFx;return result;
        }
        std::array<Bank::Assignment,stripCount+auxBusCount> lanes{};
        for(int i=0;i<stripCount+auxBusCount;++i) {
            const auto id=i<stripCount?candidate.ids.inputs[i]:
                         candidate.ids.aux[i-stripCount];
            const auto* row=candidate.roster.find(id);
            if(!row||!Bank::captureLegacy(i,id,row->format,
                                          acceptedM4Fx,lanes[i])) {
                result.fault=Fault::fxLeaseInvalid;
                result.fx=Bank::Fault::missingLegacyIdentity;
                return result;
            }
        }
        Bank bank;
        result.fx=Bank::prepare(deviceEpoch,expectedPcmRevision+1,lanes,bank,
                                std::move(externalCh1));
        if(result.fx!=Bank::Fault::none) {
            result.fault=Fault::fxLeaseInvalid;return result;
        }
        auto owner=std::make_shared<Bank>(std::move(bank));
        DynamicNativeSessionPublisher::FxPlan plan;
        plan.owner=std::static_pointer_cast<void>(owner);
        plan.context=owner.get();
        plan.render=+[](void* ctx,ChannelId id,float* l,float* r,int n) noexcept {
            (*static_cast<Bank*>(ctx))(id,l,r,n);
        };
        plan.deviceEpoch=deviceEpoch;plan.graphRevision=expectedPcmRevision+1;
        result.native=publisher.commit(candidate.roster,candidate.request,
            expectedPcmRevision,deviceEpoch,currentHardwareEpoch,
            allOutputsDisarmed,std::move(plan));
        if(!result.native.ok()) {
            result.fault=Fault::nativeRejected;return result;
        }
        UnifiedLegacyAccepted next;
        next.graph=std::move(candidate);
        next.fxOwners=std::move(owner);
        next.hardware=hardware;
        next.deviceEpoch=deviceEpoch;
        next.pcmRevision=result.native.revision;
        accepted=std::move(next);
        return result;
    }
};
} // namespace armix::mixer
