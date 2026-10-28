// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "LiveInputPortRegistry.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <cstdint>

// P1F: control-thread hardware topology attestation. The JUCE callback's
// input pointers are PACKED in ascending order of the actual active input
// mask. Never use the physical channel number as a callback pointer ordinal.
// This component DOES NOT reopen devices, nor enable disabled pins.
namespace armix::device {
struct JuceActivePortSnapshot final {
    enum class Fault : std::uint8_t {
        none, nullDevice, uninitialisedDevice, emptyInput, emptyOutput,
        tooManyInputs, tooManyOutputs, inputRegistryRejected,
        deviceChangedDuringRead
    };
    static constexpr int limit=LiveInputPortRegistry::limit;
    LiveInputPortRegistry inputs {};
    std::array<int,limit> outputByCallback {};
    int outputCount=0;
    std::uint64_t epoch=0;
    juce::String deviceName, typeName;

    // Accept a frozen copy of masks so unit tests can exercise sparse
    // numbering without requiring an ASIO device. This executes off RT.
    static bool fromMasks(std::uint64_t epoch,
                          const juce::BigInteger& inputMask,
                          const juce::BigInteger& outputMask,
                          JuceActivePortSnapshot& destination,
                          Fault& error) noexcept {
        error=Fault::none;
        std::array<int,limit> inputIndices{}, outputIndices{};
        int in=0,out=0;
        for(int p=inputMask.findNextSetBit(0);p>=0;
            p=inputMask.findNextSetBit(p+1)) {
            if(p>=limit || in>=limit){error=Fault::tooManyInputs;return false;}
            inputIndices[in++]=p;
        }
        for(int p=outputMask.findNextSetBit(0);p>=0;
            p=outputMask.findNextSetBit(p+1)) {
            if(p>=limit || out>=limit){error=Fault::tooManyOutputs;return false;}
            outputIndices[out++]=p;
        }
        if(in==0){error=Fault::emptyInput;return false;}
        if(out==0){error=Fault::emptyOutput;return false;}
        JuceActivePortSnapshot candidate;
        LiveInputPortRegistry::Fault inputFault{};
        if(!LiveInputPortRegistry::prepare(
            epoch,std::span<const int>(inputIndices.data(),static_cast<std::size_t>(in)),
            candidate.inputs,inputFault)) {
            error=Fault::inputRegistryRejected;return false;
        }
        std::copy_n(outputIndices.begin(),out,candidate.outputByCallback.begin());
        candidate.outputCount=out;
        candidate.epoch=epoch;
        destination=std::move(candidate);
        return true;
    }

    // Call only from device lifecycle/configuration control thread after
    // the selected DEVICESETUP is verified. Name/type alone are not the
    // security identity: backend ASIO registration/endpoint fence remains
    // authoritative outside this helper.
    static bool capture(juce::AudioIODevice* device,std::uint64_t epoch,
                        JuceActivePortSnapshot& destination,
                        Fault& error) {
        error=Fault::none;
        if(!device){error=Fault::nullDevice;return false;}
        if(!device->isOpen()){error=Fault::uninitialisedDevice;return false;}
        const auto in=device->getActiveInputChannels();
        const auto out=device->getActiveOutputChannels();
        JuceActivePortSnapshot candidate;
        if(!fromMasks(epoch,in,out,candidate,error))return false;
        // A driver may be reconfigured asynchronously. Reject if masks
        // changed across the control-thread snapshot read.
        if(in!=device->getActiveInputChannels() ||
           out!=device->getActiveOutputChannels()) {
            error=Fault::deviceChangedDuringRead;return false;
        }
        candidate.deviceName=device->getName();
        candidate.typeName=device->getTypeName();
        destination=std::move(candidate);
        return true;
    }
    int outputPhysicalPort(int callbackIndex) const noexcept {
        return callbackIndex>=0 && callbackIndex<outputCount
            ? outputByCallback[callbackIndex]:-1;
    }
    bool matchesCallback(std::uint64_t generation,int inputCount,
                         int outputChannels) const noexcept {
        return epoch!=0 && epoch==generation &&
               inputs.matches(generation,inputCount) &&
               outputCount==outputChannels;
    }
};
} // namespace armix::device
