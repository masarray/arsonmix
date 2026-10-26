// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>

namespace armix::device
{
// Safety gate for a pinned DIRECT backend and active input/output channel map.
// Driver names are labels, NOT immutable hardware IDs. Control-plane hashing
// runs at startup/user approval; RT consumes ONE atomic bool without overhead.
// JUCE device lifecycle serializes control-plane transitions.
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
    bool approve(std::string_view type, std::string_view device,
                 std::string_view inputMask, std::string_view outputMask) noexcept
    {
        const auto id=fingerprint(type, device, inputMask, outputMask);
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
    bool resumeExpected(std::string_view type, std::string_view device,
                        std::string_view inputMask, std::string_view outputMask) noexcept
    {
        const auto expected=expected_.load(std::memory_order_acquire);
        if (expected == 0 || expected != fingerprint(type, device, inputMask, outputMask))
            return false;
        fault_.store(Fault::none, std::memory_order_release);
        permitted_.store(true, std::memory_order_release);
        return true;
    }
    // Invoked on JUCE device START, not the processBlock/audio sample loop.
    // If a different backend/device silently appears, latch fault. Reopening
    // the old device must NOT silently clear this failure.
    bool observeStart(std::string_view type, std::string_view device,
                      std::string_view inputMask, std::string_view outputMask) noexcept
    {
        const auto expected=expected_.load(std::memory_order_acquire);
        if (!permitted_.load(std::memory_order_acquire) || expected == 0)
            return false;
        if (expected != fingerprint(type, device, inputMask, outputMask))
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
            case Fault::unexpectedRoute: return "UNEXPECTED_DIRECT_BACKEND_DEVICE_OR_CHANNEL_MASK";
        }
        return "UNKNOWN";
    }
    // Canonical base-16 JUCE BigInteger masks, no leading zeroes.
    // Length delimits each field to avoid concatenation ambiguity.
    static std::uint64_t fingerprint(std::string_view type,
                                     std::string_view device,
                                     std::string_view inputMask,
                                     std::string_view outputMask) noexcept
    {
        if (type.empty() || device.empty() || !validMask(inputMask)
            || !validMask(outputMask)) return 0;
        std::uint64_t hash=14695981039346656037ull;
        append(hash, type);
        append(hash, device);
        append(hash, inputMask);
        append(hash, outputMask);
        return hash ? hash : 1; // reserve zero for never-approved
    }
private:
    static bool validMask(std::string_view mask) noexcept
    {
        if (mask.empty() || mask.front() == '0') return false;
        for (const unsigned char c : mask)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
        return true;
    }
    static void append(std::uint64_t& hash, std::string_view field) noexcept
    {
        const auto length=static_cast<std::uint64_t>(field.size());
        for (unsigned shift=0; shift<64; shift+=8)
            hash=(hash ^ ((length >> shift) & 0xffu))*1099511628211ull;
        for (const unsigned char c : field)
            hash=(hash ^ c)*1099511628211ull;
    }
    std::atomic<std::uint64_t> expected_{0};
    std::atomic<bool> permitted_{false};
    std::atomic<Fault> fault_{Fault::awaitingApproval};
};
} // namespace armix::device
