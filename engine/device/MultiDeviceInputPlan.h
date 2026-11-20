// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "LiveInputPortRegistry.h"
#include "../core/DynamicChannelRoster.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <span>
#include <string>
#include <string_view>

// IO-2 CONTROL-prepared, device-qualified physical source mapping.
// This does NOT open a driver, mutate .smix, publish a graph or run any DSP.
// The prepared slot ordinals are the only values handed to the existing
// DynamicSourcePatch / DynamicGraphRequest physical input namespace.
//
// Immutable after successful prepare; the primary render callback may call
// assemble() without allocation, locks, driver calls, strings or graph edits.
// A missing/stale secondary yields nullptr pointers (native graph treats it
// as a silent source), never a same-numbered pin from a different device.
namespace armix::device {

struct CaptureEndpointView final {
    // CONTROL supplies this token only after verifying actual driver/endpoint
    // registration. A friendly name/driver CLSID alone is NOT physical proof.
    std::string_view verifiedDeviceKey;
    std::uint64_t generation=0;
    bool primary=false;
    std::span<const int> enabledPhysicalPinsInCallbackOrder{};
    std::uint64_t deviceLease=0; // native-issued, unique within accepted roster
};

struct QualifiedInputPin final {
    std::string_view deviceKey;
    int physicalPin=-1;
};

struct QualifiedChannelSource final {
    armix::mixer::ChannelId channel{};
    QualifiedInputPin left{}, right{};
    bool stereo=false;
};

struct PreparedChannelSource final {
    armix::mixer::ChannelId channel{};
    int inputL=-1,inputR=-1; // virtual, dense callback ordinals
    bool stereo=false;
};

struct MultiDeviceInputFault final {
    enum class Kind : std::uint8_t {
        none, invalidPrimaryEpoch, invalidDeviceCount, missingPrimary,
        invalidDeviceIdentity, invalidGeneration, duplicateDevice,
        invalidDeviceLease, duplicateDeviceLease,
        invalidDevicePins, tooManyInputs, unsupportedSecondaryChannels,
        invalidChannel, duplicateChannel,
        missingDevice, unavailablePin, invalidStereoPair
    } kind=Kind::none;
    armix::mixer::ChannelId channel{};
    int device=-1,physicalPin=-1;
};

class MultiDeviceInputPlan final {
public:
    static constexpr int maxDevices=8;
    // The existing DynamicPreparedMixerBlock physicalInputs contract is 256.
    static constexpr int maxVirtualInputs=256;
    static constexpr int maxAssignments=256;
    static constexpr int maxCallbackFrames=8192;

    struct InputSlot final {
        std::uint8_t device=0;
        int packedOrdinal=-1; // index in exactly this driver's callback
        std::uint64_t generation=0;
        std::uint64_t deviceLease=0;
    };
    struct DeviceReadback final {
        std::string verifiedKey; // CONTROL only, never compared in callback
        std::uint64_t generation=0,deviceLease=0;
        int firstVirtual=0,activePins=0;
        bool primary=false;
    };
    struct CaptureBlock final {
        const float* const* packedChannels=nullptr;
        int channels=0,frames=0;
        std::uint64_t generation=0;
        bool admitted=false; // actual native capture/ASRC readback
        std::uint64_t deviceLease=0; // disambiguates reordered equal epochs
    };
    struct VirtualFrame final {
        std::array<const float*,maxVirtualInputs> pointers{};
        int count=0,available=0;
    };

    // CONTROL ONLY. Prepare into a DETACHED candidate (never the live plan).
    // All-or-nothing validation leaves destination unchanged on failure.
    // The existing realtime graph publisher must publish its immutable
    // snapshot at the safe render boundary; this assignment is NOT an
    // atomic callback-visible update and must not race with assemble().
    static bool prepare(
        std::uint64_t primaryEpoch,
        std::span<const CaptureEndpointView> devices,
        std::span<const QualifiedChannelSource> bindings,
        std::span<const armix::mixer::ChannelStripDefinition> roster,
        MultiDeviceInputPlan& destination,
        MultiDeviceInputFault& error)
    {
        error={};
        if(primaryEpoch==0) {
            error.kind=MultiDeviceInputFault::Kind::invalidPrimaryEpoch;
            return false;
        }
        if(devices.empty()||devices.size()>static_cast<std::size_t>(maxDevices)) {
            error.kind=MultiDeviceInputFault::Kind::invalidDeviceCount;
            return false;
        }
        if(!devices.front().primary) {
            error.kind=MultiDeviceInputFault::Kind::missingPrimary;
            return false;
        }
        if(bindings.size()>static_cast<std::size_t>(maxAssignments)) {
            error.kind=MultiDeviceInputFault::Kind::invalidChannel;
            return false;
        }

        MultiDeviceInputPlan candidate;
        candidate.primaryEpoch_=primaryEpoch;
        candidate.deviceCount_=static_cast<int>(devices.size());
        std::array<LiveInputPortRegistry,maxDevices> registries{};
        for(std::size_t d=0;d<devices.size();++d) {
            const auto& src=devices[d];
            error.device=static_cast<int>(d);
            if(src.verifiedDeviceKey.empty()) {
                error.kind=MultiDeviceInputFault::Kind::invalidDeviceIdentity;
                return false;
            }
            if(src.generation==0 || (d==0&&src.generation!=primaryEpoch)) {
                error.kind=MultiDeviceInputFault::Kind::invalidGeneration;
                return false;
            }
            if(src.deviceLease==0) {
                error.kind=MultiDeviceInputFault::Kind::invalidDeviceLease;
                return false;
            }
            if(d!=0&&src.primary) {
                error.kind=MultiDeviceInputFault::Kind::missingPrimary;
                return false;
            }
            for(std::size_t prev=0;prev<d;++prev)
                if(candidate.devices_[prev].verifiedKey==src.verifiedDeviceKey) {
                    error.kind=MultiDeviceInputFault::Kind::duplicateDevice;
                    return false;
                } else if(candidate.devices_[prev].deviceLease==src.deviceLease) {
                    error.kind=MultiDeviceInputFault::Kind::duplicateDeviceLease;
                    return false;
                }
            if(d!=0&&src.enabledPhysicalPinsInCallbackOrder.size()>16) {
                // IO-1 SecondaryCaptureClockStream max ASRC channels = 16.
                error.kind=MultiDeviceInputFault::Kind::unsupportedSecondaryChannels;
                return false;
            }
            if(src.enabledPhysicalPinsInCallbackOrder.size() >
               static_cast<std::size_t>(maxVirtualInputs-candidate.inputCount_)) {
                error.kind=MultiDeviceInputFault::Kind::tooManyInputs;
                return false;
            }
            LiveInputPortRegistry::Fault portFault{};
            if(!LiveInputPortRegistry::prepare(src.generation,
                 src.enabledPhysicalPinsInCallbackOrder,registries[d],portFault)) {
                error.kind=MultiDeviceInputFault::Kind::invalidDevicePins;
                return false;
            }
            auto& prepared=candidate.devices_[d];
            prepared.verifiedKey=std::string(src.verifiedDeviceKey);
            prepared.generation=src.generation;
            prepared.deviceLease=src.deviceLease;
            prepared.primary=src.primary;
            prepared.firstVirtual=candidate.inputCount_;
            prepared.activePins=registries[d].enabledCount;
            for(int pin=0;pin<prepared.activePins;++pin) {
                auto& slot=candidate.slots_[candidate.inputCount_++];
                slot.device=static_cast<std::uint8_t>(d);
                slot.packedOrdinal=pin;
                slot.generation=src.generation;
                slot.deviceLease=src.deviceLease;
            }
        }

        for(const auto& binding:bindings) {
            error.channel=binding.channel;
            error.device=-1;
            error.physicalPin=-1;
            const auto definition=std::find_if(roster.begin(),roster.end(),
                [&](const auto& channel){return channel.id==binding.channel;});
            if(binding.channel.value==0 || definition==roster.end() ||
               definition->role!=armix::mixer::ChannelRole::input) {
                error.kind=MultiDeviceInputFault::Kind::invalidChannel;
                return false;
            }
            // Native source format and channel/insert processing format are
            // intentionally independent (mono mic -> stereo VST, or stereo
            // capture -> mono processing). Match DynamicPreparedMixerBlock.
            for(int prev=0;prev<candidate.assignmentCount_;++prev)
                if(candidate.assignments_[prev].channel==binding.channel) {
                    error.kind=MultiDeviceInputFault::Kind::duplicateChannel;
                    return false;
                }

            const auto resolve=[&](const QualifiedInputPin& pin)->int {
                for(int d=0;d<candidate.deviceCount_;++d) {
                    if(candidate.devices_[d].verifiedKey!=pin.deviceKey)continue;
                    error.device=d;
                    error.physicalPin=pin.physicalPin;
                    const int packed=registries[d].callbackIndex(pin.physicalPin);
                    if(packed<0) {
                        error.kind=MultiDeviceInputFault::Kind::unavailablePin;
                        return -1;
                    }
                    return candidate.devices_[d].firstVirtual+packed;
                }
                error.kind=MultiDeviceInputFault::Kind::missingDevice;
                return -1;
            };

            PreparedChannelSource patch;
            patch.channel=binding.channel;
            patch.stereo=binding.stereo;
            patch.inputL=resolve(binding.left);
            if(patch.inputL<0)return false;
            if(binding.stereo) {
                if(binding.right.deviceKey!=binding.left.deviceKey ||
                   binding.right.physicalPin==binding.left.physicalPin) {
                    error.kind=MultiDeviceInputFault::Kind::invalidStereoPair;
                    return false;
                }
                patch.inputR=resolve(binding.right);
                if(patch.inputR<0)return false;
            } else if(!binding.right.deviceKey.empty() ||
                      binding.right.physicalPin!=-1) {
                error.kind=MultiDeviceInputFault::Kind::invalidStereoPair;
                return false;
            }
            candidate.assignments_[candidate.assignmentCount_++]=patch;
        }
        error={};
        destination=std::move(candidate);
        return true;
    }

    // PRIMARY RENDER callback only. The caller provides blocks in the exact
    // CONTROL-prepared device order; their generations and frame counts are
    // checked before any pointer is admitted. Null = silence, not port 0.
    // No allocation, no lock, no string operation, no DSP invocation.
    bool assemble(std::uint64_t actualPrimaryEpoch,int frames,
                  std::span<const CaptureBlock> blocks,
                  VirtualFrame& frame) const noexcept
    {
        frame={};
        if(!primaryEpoch_||actualPrimaryEpoch!=primaryEpoch_||
           frames<=0||frames>maxCallbackFrames||
           blocks.size()!=static_cast<std::size_t>(deviceCount_))
            return false;
        frame.count=inputCount_;
        for(int i=0;i<inputCount_;++i) {
            const auto& slot=slots_[i];
            const auto& block=blocks[slot.device];
            if(!block.admitted||block.generation!=slot.generation||
               block.deviceLease!=slot.deviceLease||
               block.frames!=frames||!block.packedChannels||
               block.channels!=devices_[slot.device].activePins||
               !block.packedChannels[slot.packedOrdinal])
                continue; // fail SILENT on stale or unavailable device only
            frame.pointers[i]=block.packedChannels[slot.packedOrdinal];
            ++frame.available;
        }
        return true;
    }

    int virtualInputCount() const noexcept {return inputCount_;}
    int deviceCount() const noexcept {return deviceCount_;}
    int sourceCount() const noexcept {return assignmentCount_;}
    std::uint64_t primaryEpoch() const noexcept {return primaryEpoch_;}
    const InputSlot& slot(int index) const noexcept {return slots_[index];}
    const PreparedChannelSource& source(int index) const noexcept {
        return assignments_[index];
    }
    // IO-2D: explicit SOURCE NONE / Delete Track staging. Keep *every*
    // verified device generation, lease and virtual slot unchanged; remove
    // only one ChannelId's PCM assignment in a DETACHED CONTROL candidate.
    // The entire graph+map publishes as one immutable snapshot on success.
    bool withoutSource(armix::mixer::ChannelId removed,
                       MultiDeviceInputPlan& destination) const {
        if(!primaryEpoch_ || !removed.value)return false;
        MultiDeviceInputPlan staged=*this;
        int write=0;
        bool found=false;
        for(int i=0;i<assignmentCount_;++i){
            if(assignments_[i].channel==removed){found=true;continue;}
            staged.assignments_[write++]=assignments_[i];
        }
        if(!found)return false;
        staged.assignmentCount_=write;
        destination=std::move(staged);
        return true;
    }
    // CONTROL only. Never inspect a device name on the audio callback.
    const DeviceReadback& device(int index) const noexcept {
        return devices_[index];
    }

private:
    std::uint64_t primaryEpoch_=0;
    int deviceCount_=0,inputCount_=0,assignmentCount_=0;
    std::array<DeviceReadback,maxDevices> devices_{};
    std::array<InputSlot,maxVirtualInputs> slots_{};
    std::array<PreparedChannelSource,maxAssignments> assignments_{};
};

} // namespace armix::device
