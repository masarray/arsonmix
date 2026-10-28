// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../device/JuceActivePortSnapshot.h"
#include "PreparedMixerBlock.h"
#include "RealtimeRoutePublisher.h"
#include <cstdint>
#include <memory>
#include <utility>

// P1F: complete, device-epoch-bound prepared DSP render session.
// Configures actual opened JUCE input masks on the CONTROL thread; does not
// silently enable physical pins, restart devices or allocate during rendering.
// No VST lifecycle coupling: FX callback is supplied by the host after it has
// independently negotiated compatible plugin buses and proven instance ownership.
namespace armix::mixer {
class PreparedNativeMixerSession final {
public:
    struct BoundGraph final {
        armix::device::JuceActivePortSnapshot ports;
        std::unique_ptr<PreparedMixerBlock> mixer;
    };
    using Publisher=armix::rt::RealtimeRoutePublisher<BoundGraph,8>;
    using PublishResult=Publisher::Result;

    struct PrepareResult {
        enum class Fault : unsigned char {
            none, invalidTopology, graphRejected, invalidBufferCapacity
        } fault=Fault::none;
        armix::device::JuceActivePortSnapshot::Fault portFault{};
        RenderError graphError{};
    };

    static std::unique_ptr<Publisher::Snapshot> prepare(
        const ChannelRoutingConfig& channels,
        const SendRequest& sends,
        const juce::BigInteger& activeInputMask,
        const juce::BigInteger& activeOutputMask,
        std::uint64_t deviceEpoch,
        std::uint64_t revision,
        int maxFrames,
        PrepareResult& result) {
        result={};
        if(maxFrames<=0 || maxFrames>PreparedMixerBlock::hardFrameLimit) {
            result.fault=PrepareResult::Fault::invalidBufferCapacity;
            return {};
        }
        armix::device::JuceActivePortSnapshot ports;
        if(!armix::device::JuceActivePortSnapshot::fromMasks(
               deviceEpoch,activeInputMask,activeOutputMask,
               ports,result.portFault)) {
            result.fault=PrepareResult::Fault::invalidTopology;
            return {};
        }
        std::array<int,armix::device::LiveInputPortRegistry::limit> ordered{};
        for(int i=0;i<ports.inputs.enabledCount;++i)
            ordered[i]=ports.inputs.physicalIndex(i);
        PreparedRenderGraph candidate;
        if(!PreparedRenderGraph::compile(
               channels,std::span<const int>(ordered.data(),
                     static_cast<std::size_t>(ports.inputs.enabledCount)),
               sends,candidate,result.graphError)) {
            result.fault=PrepareResult::Fault::graphRejected;
            return {};
        }
        auto snapshot=std::make_unique<Publisher::Snapshot>();
        snapshot->deviceEpoch=deviceEpoch;
        snapshot->graphRevision=revision;
        snapshot->prepared=std::make_unique<BoundGraph>();
        snapshot->prepared->ports=ports;
        snapshot->prepared->mixer=std::make_unique<PreparedMixerBlock>(
            candidate,maxFrames);
        return snapshot;
    }

    // CONTROL thread: publication must be serialized with device lifecycle,
    // and caller must provide last-ACK revision + current device generation.
    PublishResult publish(std::unique_ptr<Publisher::Snapshot> candidate,
                          std::uint64_t expectedRevision,
                          std::uint64_t currentDeviceEpoch) noexcept {
        return publisher_.publish(std::move(candidate),expectedRevision,
                                  currentDeviceEpoch);
    }
    void unpublish() noexcept { publisher_.unpublish(); }
    bool pollRetirement() noexcept { return publisher_.pollRetirement(); }
    std::uint64_t lastRevision() const noexcept {return publisher_.lastRevision();}

    // RT: acquire a lease bound to the device generation; refuse changed
    // callback I/O counts. No spin, locks, allocations or temporary buffers.
    // A rejected callback is silent. Caller guarantees output buffers hold
    // at least frames values.
    template<class Fx>
    bool render(const float* const* inputs,int inputChannels,
                float* outputL,float* outputR,int outputChannels,
                int frames,std::uint64_t deviceEpoch,
                Fx&& fx,RenderLevels* observed=nullptr) noexcept {
        if(!outputL || !outputR || frames<=0)return false;
        auto lease=publisher_.acquire();
        const auto* snap=lease.get(deviceEpoch);
        if(!snap || !snap->prepared || !snap->prepared->mixer ||
           !snap->prepared->ports.matchesCallback(
                deviceEpoch,inputChannels,outputChannels)) {
            std::fill_n(outputL,frames,0.f);
            std::fill_n(outputR,frames,0.f);
            if(observed)*observed={};
            return false;
        }
        return snap->prepared->mixer->process(inputs,inputChannels,
            outputL,outputR,frames,std::forward<Fx>(fx),observed);
    }
private:
    Publisher publisher_{};
};
} // namespace armix::mixer
