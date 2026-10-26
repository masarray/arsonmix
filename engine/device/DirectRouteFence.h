// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>

namespace armix::device
{
// Safety gate for a pinned DIRECT audio backend. Never guesses or silently
// accepts a substituted physical route. Control plane performs hashing only
// at device start/explicit user acceptance; RT reads ONE atomic bool.
// Device names are driver-reported labels, NOT immutable hardware IDs.
class DirectRouteFence final
{
public:
    enum class Fault : std::uint8_t
    {
        none, awaitingApproval, unexpectedRoute
    };
    void suspend() noexcept
    {
        permitted_.store(false, std::memory_order_release);
        fault_.store(Fault::awaitingApproval, std::memory_order_release);
    }
    // After JUCE has successfully opened a chosen route and validated masks.
    // Explicit confirmation during AUDIO SETUP can supersede a prior fault.
    bool approve(std::string_view type, std::string_view device) noexcept
    {
        const auto id=fingerprint(type, device);
        if (!id)
        {
            suspend();
            return false;
        }
        permitted_.store(false, std::memory_order_release);
        expected_.store(id, std::memory_order_release);
        fault_.store(Fault::none, std::memory_order_release);
        permitted_.store(true, std::memory_order_release);
        return true;
    }
    // Only our own controlled DIRECT restoration may re-arm the prior
    // selected driver, and only when it matches exactly. Unlike approve(),
    // this never authorizes a *different* route. A user can approve a new
    // backend explicitly via the AUDIO SETUP close/commit action.
    bool resumeExpected(std::string_view type, std::string_view device) noexcept
    {
        const auto expected=expected_.load(std::memory_order_acquire);
        if (expected == 0 || expected != fingerprint(type, device))
            return false;
        fault_.store(Fault::none, std::memory_order_release);
        permitted_.store(true, std::memory_order_release);
        return true;
    }
    // Invoked on JUCE device START, not the processBlock/audio sample loop.
    // If a different backend/device silently appears, latch fault. Reopening
    // the old device must NOT silently clear this failure.
    bool observeStart(std::string_view type, std::string_view device) noexcept
    {
        const auto expected=expected_.load(std::memory_order_acquire);
        if (!permitted_.load(std::memory_order_acquire) || expected == 0)
            return false;
        if (expected != fingerprint(type, device))
        {
            permitted_.store(false, std::memory_order_release);
            fault_.store(Fault::unexpectedRoute, std::memory_order_release);
            return false;
        }
        return true;
    }
    bool permitted() const noexcept
    {
        return permitted_.load(std::memory_order_acquire);
    }
    Fault fault() const noexcept { return fault_.load(std::memory_order_acquire); }
    static const char* reason(Fault f) noexcept
    {
        switch(f)
        {
            case Fault::none: return "NONE";
            case Fault::awaitingApproval: return "AWAITING_USER_DEVICE_APPROVAL";
            case Fault::unexpectedRoute: return "UNEXPECTED_DIRECT_BACKEND_OR_DEVICE";
        }
        return "UNKNOWN";
    }
    static std::uint64_t fingerprint(std::string_view type,
                                      std::string_view device) noexcept
    {
        if (type.empty() || device.empty()) return 0;
        std::uint64_t hash=14695981039346656037ull;
        for (const unsigned char c : type)
            hash=(hash ^ c)*1099511628211ull;
        hash=(hash ^ 0xffu)*1099511628211ull;
        for (const unsigned char c : device)
            hash=(hash ^ c)*1099511628211ull;
        return hash ? hash : 1; // reserve zero for never-approved
    }
private:
    std::atomic<std::uint64_t> expected_{0};
    std::atomic<bool> permitted_{false};
    std::atomic<Fault> fault_{Fault::awaitingApproval};
};
} // namespace armix::device
