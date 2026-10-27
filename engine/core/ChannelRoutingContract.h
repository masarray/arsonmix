// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "MixerRoutingPlan.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

// P1B: device-independent, offline-compiled MONO/STEREO channel contract.
// An enabled physical port is NOT a callback pointer ordinal: sparse ASIO
// masks must be translated by the backend at device setup, never on audio RT.
// This unit deliberately does not activate any JUCE device, FX or mixer GUI.
namespace armix::mixer {

constexpr int maxPhysicalPorts = 256;

enum class ChannelFormat : std::uint8_t { followSource, mono, stereo };
enum class LayoutFault : std::uint8_t {
    none, invalidPhysicalPort, duplicateEnabledPort, unavailableSourcePort,
    invalidChannelFormat, basePlanRejected
};
struct LayoutError final {
    LayoutFault fault = LayoutFault::none;
    int strip = -1;
    int physicalPort = -1;
    Error baseError {};
};

// Backend-owned control code MUST supply ports in the exact callback pointer
// order negotiated with the driver. Enumerated labels alone are insufficient.
// The physical-port index is device-scoped; portability requires endpoint-ID
// and port-ID validation when restoring a saved Performance on a new PC.
struct InputPortMap final {
    std::array<int, maxPhysicalPorts> callbackIndex {};
    int enabledCount = 0;

    static bool prepare(std::span<const int> callbackOrderedPhysicalPorts,
                        InputPortMap& out, LayoutError& error) noexcept {
        error = {};
        if (callbackOrderedPhysicalPorts.size() > maxPhysicalPorts) {
            error.fault = LayoutFault::invalidPhysicalPort;
            return false;
        }
        InputPortMap candidate;
        candidate.callbackIndex.fill(-1);
        for (std::size_t i = 0; i < callbackOrderedPhysicalPorts.size(); ++i) {
            const int port = callbackOrderedPhysicalPorts[i];
            if (port < 0 || port >= maxPhysicalPorts) {
                error.fault = LayoutFault::invalidPhysicalPort;
                error.physicalPort = port;
                return false;
            }
            if (candidate.callbackIndex[port] != -1) {
                error.fault = LayoutFault::duplicateEnabledPort;
                error.physicalPort = port;
                return false;
            }
            candidate.callbackIndex[port] = static_cast<int>(i);
        }
        candidate.enabledCount = static_cast<int>(callbackOrderedPhysicalPorts.size());
        out = candidate;
        return true;
    }
    int callbackFor(int physicalPort) const noexcept {
        return physicalPort >= 0 && physicalPort < maxPhysicalPorts
            ? callbackIndex[physicalPort] : -1;
    }
};

struct ChannelRoutingConfig final {
    Configuration stripsAndMaster {};
    std::array<ChannelFormat, stripCount> formats {}; // all followSource by default
};

struct ChannelRoutingPlan final {
    // Existing P1A Plan remains authoritative for mixing/solo/master controls.
    Plan base {};
    std::array<ChannelFormat, stripCount> effectiveFormats {};
    std::array<bool, stripCount> foldStereoToMono {};
    InputPortMap inputPorts {};

    // Compile/reject on the control thread. Destination is only updated after
    // every source, physical pin, mode, gain, pan and master rule passes.
    static bool compile(const ChannelRoutingConfig& requested,
                        std::span<const int> callbackOrderedPhysicalPorts,
                        ChannelRoutingPlan& destination,
                        LayoutError& error) noexcept {
        error = {};
        InputPortMap ports;
        if (!InputPortMap::prepare(callbackOrderedPhysicalPorts, ports, error))
            return false;

        Configuration mapped = requested.stripsAndMaster;
        std::array<ChannelFormat, stripCount> effective {};
        std::array<bool, stripCount> folded {};
        for (int i=0; i<stripCount; ++i) {
            const auto& original = requested.stripsAndMaster.strips[i];
            auto& target = mapped.strips[i];
            const auto format = requested.formats[i];
            if (format != ChannelFormat::followSource &&
                format != ChannelFormat::mono && format != ChannelFormat::stereo) {
                error = { LayoutFault::invalidChannelFormat, i, -1, {} };
                return false;
            }
            if (original.source != SourceMode::disconnected &&
                original.source != SourceMode::mono && original.source != SourceMode::stereo) {
                error = { LayoutFault::basePlanRejected, i, -1, {Fault::invalidSourceMode,i} };
                return false;
            }
            effective[i] = format == ChannelFormat::followSource
                ? (original.source == SourceMode::stereo ? ChannelFormat::stereo : ChannelFormat::mono)
                : format;
            folded[i] = original.source == SourceMode::stereo &&
                        effective[i] == ChannelFormat::mono;
            if (original.source == SourceMode::disconnected) continue;
            target.inputA = ports.callbackFor(original.inputA);
            if (target.inputA < 0) {
                error = { LayoutFault::unavailableSourcePort, i, original.inputA, {} };
                return false;
            }
            if (original.source == SourceMode::stereo) {
                target.inputB = ports.callbackFor(original.inputB);
                if (target.inputB < 0) {
                    error = { LayoutFault::unavailableSourcePort, i, original.inputB, {} };
                    return false;
                }
            }
        }
        Plan candidate;
        Error baseError;
        if (!Plan::compile(mapped, ports.enabledCount, candidate, baseError)) {
            error = { LayoutFault::basePlanRejected, baseError.strip, -1, baseError };
            return false;
        }
        for (int i=0; i<stripCount; ++i) {
            const auto& original = requested.stripsAndMaster.strips[i];
            auto& out = candidate.strips[i];
            if (!out.active) continue;
            if (effective[i] == ChannelFormat::mono && original.source == SourceMode::stereo) {
                // The 0.5(L+R) fold happens BEFORE FX; equal-power mono pan
                // applies AFTER FX, with no raw L+R +6 dB double sum.
                out.leftGain = original.faderLinear * std::sqrt(0.5f*(1.f-original.pan));
                out.rightGain = original.faderLinear * std::sqrt(0.5f*(1.f+original.pan));

            }
        }
        ChannelRoutingPlan ready;
        ready.base = candidate;
        ready.inputPorts = ports;
        ready.effectiveFormats = effective;
        ready.foldStereoToMono = folded;
        destination = ready;
        return true;
    }

    // Audio thread: consume *preallocated* stereo scratch supplied by caller.
    // No format/device queries, allocation, mutex, trigonometry, or I/O here.
    bool extract(int strip, const float* const* input, int availableCallbackInputs,
                 float* left, float* right, int frames) const noexcept {
        if (!base.extract(strip, input, availableCallbackInputs, left, right, frames))
            return false;
        if (foldStereoToMono[strip]) {
            for (int i = 0; i < frames; ++i) {
                const float mono = 0.5f * (left[i] + right[i]);
                left[i] = mono;
                right[i] = mono;
            }
        }
        return true;
    }
    void accumulate(int strip, const float* left, const float* right,
                    float* masterL, float* masterR, int frames) const noexcept {
        base.accumulate(strip, left, right, masterL, masterR, frames);
    }
};
} // namespace armix::mixer
