// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <span>

// P1E — CONTROL-thread-only preparation from the *opened driver's active*
// channel mask. Physical channel IDs are zero-based; callback pointers are
// packed into [0..enabledCount) in the driver's enabled-channel order.
// Device names/ports must be verified by caller against the same session.
// This header deliberately has no JUCE dependency and does no callback work.
namespace armix::device {
struct LiveInputPortRegistry final {
    static constexpr int limit = 256;
    enum class Fault : std::uint8_t {
        none, invalidEpoch, invalidPort, duplicatePort, overflow
    };
    std::array<int,limit> callbackByPhysical {};
    std::array<int,limit> physicalByCallback {};
    std::uint64_t epoch = 0;
    int enabledCount = 0;

    static bool prepare (std::uint64_t generation,
                         std::span<const int> physicalEnabledInCallbackOrder,
                         LiveInputPortRegistry& destination,
                         Fault& fault) noexcept {
        fault=Fault::none;
        if(generation==0) { fault=Fault::invalidEpoch; return false; }
        if(physicalEnabledInCallbackOrder.size()>limit) {
            fault=Fault::overflow;return false;
        }
        LiveInputPortRegistry next;
        next.callbackByPhysical.fill(-1);
        next.physicalByCallback.fill(-1);
        for(std::size_t n=0;n<physicalEnabledInCallbackOrder.size();++n) {
            const int id=physicalEnabledInCallbackOrder[n];
            if(id<0 || id>=limit) {
                fault=Fault::invalidPort;return false;
            }
            if(next.callbackByPhysical[id]!=-1) {
                fault=Fault::duplicatePort;return false;
            }
            next.callbackByPhysical[id]=static_cast<int>(n);
            next.physicalByCallback[n]=id;
        }
        next.epoch=generation;
        next.enabledCount=static_cast<int>(physicalEnabledInCallbackOrder.size());
        destination=next;
        return true;
    }
    int callbackIndex(int port) const noexcept {
        return port>=0 && port<limit ? callbackByPhysical[port] : -1;
    }
    int physicalIndex(int callback) const noexcept {
        return callback>=0 && callback<enabledCount
            ? physicalByCallback[callback] : -1;
    }
    bool matches(std::uint64_t generation,int callbackCount) const noexcept {
        return epoch!=0 && epoch==generation && callbackCount==enabledCount;
    }
};
} // namespace armix::device
