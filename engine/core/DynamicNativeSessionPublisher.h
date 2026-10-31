// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DynamicPreparedMixerBlock.h"
#include "RealtimeRoutePublisher.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

// M3B: one callback-ready ChannelId session publisher, NOT another audio
// device/callback. Control thread prepares immutable graph + publisher lease;
// the existing JUCE callback may eventually use render() INSTEAD OF the old
// PreparedMixerBlock after CH1/Show/native VST owner migration succeeds.
// Never run both renderers or sum their outputs. No browser-only LIVE status.
namespace armix::mixer {
class DynamicNativeSessionPublisher final {
    struct Prepared final {
        DynamicPreparedMixerBlock graph;
        int physicalInputs=0;
        bool monitor=false;
    };
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
                        bool allOutputsDisarmed) {
        CommitResult report;report.revision=publisher_.lastRevision();
        if(proposedDeviceEpoch==0 ||
           proposedDeviceEpoch!=activeDeviceEpoch) {
            report.fault=Fault::staleDevice;return report;
        }
        if(expectedRevision!=publisher_.lastRevision() ||
           expectedRevision==std::numeric_limits<std::uint64_t>::max()) {
            report.fault=Fault::staleRevision;return report;
        }
        if(!allOutputsDisarmed) {
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
        prepared->physicalInputs=request.physicalInputs;
        prepared->monitor=std::any_of(roster.channels().begin(),
          roster.channels().end(),[](const ChannelStripDefinition& row) {
              return row.role==ChannelRole::monitor;
          });
        next->prepared=std::move(prepared);
        const auto status=publisher_.publish(std::move(next),
                                             expectedRevision,activeDeviceEpoch);
        switch(status) {
            case Publisher::Result::applied:
                report.revision=expectedRevision+1;return report;
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

    // Sole AUDIO CALLBACK path when connected by DYN1 live migration:
    // no alloc, mutex, wait, refcount, graph sorting or VST creation.
    // Caller-supplied FX owns its native ChannelId-keyed VST instance lease.
    // outputMuted is the physical output safety gate, not a UI preview.
    template<class Fx>
    bool render(std::uint64_t deviceEpoch,const float* const* physical,
                int physicalCount,float* mainL,float* mainR,int frames,
                bool outputMuted,Fx&& effect,
                float* monitorL=nullptr,float* monitorR=nullptr) noexcept {
        static_assert(std::is_nothrow_invocable_v<Fx,ChannelId,float*,float*,int>,
                      "Realtime VST dispatch must be noexcept");
        const auto clearOutputs=[&]() noexcept {
            if(frames>0 && mainL)std::fill_n(mainL,frames,0.f);
            if(frames>0 && mainR && mainR!=mainL)std::fill_n(mainR,frames,0.f);
            if(frames>0 && monitorL)std::fill_n(monitorL,frames,0.f);
            if(frames>0 && monitorR && monitorR!=monitorL)
                std::fill_n(monitorR,frames,0.f);
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
            clearOutputs();return false;
        }
        // A single callback producer owns the prepared scratch. In case a
        // broken backend calls simultaneously, fail closed rather than race.
        if(rendering_.test_and_set(std::memory_order_acquire)) {
            clearOutputs();return false;
        }
        const bool ok=snap->prepared->graph.process(
             physical,physicalCount,mainL,mainR,frames,
             std::forward<Fx>(effect),monitorL,monitorR);
        rendering_.clear(std::memory_order_release);
        if(!ok)clearOutputs();
        return ok;
    }

    // CONTROL ONLY; never destroy the last live graph in a callback.
    void unpublish() noexcept {publisher_.unpublish();}
    void pollRetirement() noexcept {publisher_.pollRetirement();}
    std::uint64_t acceptedRevision() const noexcept {
        return publisher_.lastRevision(); // CONTROL only
    }
    std::size_t pendingRetirements() const noexcept {
        return publisher_.pendingRetirements(); // CONTROL only
    }
private:
    Publisher publisher_;
    std::atomic_flag rendering_=ATOMIC_FLAG_INIT;
};
} // namespace armix::mixer
