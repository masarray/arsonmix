// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <limits>

namespace armix::device {
// CONTROL-thread scheduler for an ASIO4ALL driver whose WDM pin inventory
// may be transient during a control-panel restart. It never opens a device,
// touches a vendor plugin, or enters the realtime PCM callback.
// Only actual driver reopen/correction calls consume the bounded budget;
// readback and native graph publication remain eligible after it is spent.
class AsioRestartRecoveryGate final {
public:
    static constexpr unsigned maxDriverTransactions=3;
    static constexpr std::uint64_t settleMs=350;

    void observeEpoch(std::uint64_t epoch,std::uint64_t nowMs) noexcept {
        if(epoch==0 || epoch==epoch_)return;
        epoch_=epoch;
        driverTransactions_=0;
        bindFailures_=0;
        nextMs_=add(nowMs,settleMs);
    }
    // A restart caused by *our own* verified reopen/correction is not a new
    // opportunity for an unlimited series of expensive vendor calls.
    void carryInternalEpoch(std::uint64_t epoch) noexcept {
        if(epoch!=0)epoch_=epoch;
    }
    bool due(std::uint64_t nowMs) const noexcept {
        return epoch_!=0 && nowMs>=nextMs_;
    }
    bool mayChangeDriver() const noexcept {
        return driverTransactions_<maxDriverTransactions;
    }
    void waitForReadback(std::uint64_t nowMs) noexcept {
        defer(nowMs,750);
    }
    void driverTransaction(std::uint64_t nowMs) noexcept {
        if(driverTransactions_<maxDriverTransactions)++driverTransactions_;
        defer(nowMs,driverTransactions_==1?1200:driverTransactions_==2?4000:8000);
    }
    void bindFailed(std::uint64_t nowMs) noexcept {
        if(bindFailures_<6)++bindFailures_;
        // Rebind is not a vendor reopen: keep retrying at low frequency so a
        // transient not-ready graph can recover without a manual REMAP.
        const std::uint64_t pause=1200ull<<(bindFailures_-1);
        defer(nowMs,pause>30000?30000:pause);
    }
    void recovered() noexcept {
        driverTransactions_=0;
        bindFailures_=0;
        nextMs_=0;
    }
    unsigned driverTransactions() const noexcept {return driverTransactions_;}
    std::uint64_t trackedEpoch() const noexcept {return epoch_;}
private:
    static std::uint64_t add(std::uint64_t a,std::uint64_t b) noexcept {
        return a>std::numeric_limits<std::uint64_t>::max()-b?
            std::numeric_limits<std::uint64_t>::max():a+b;
    }
    void defer(std::uint64_t nowMs,std::uint64_t millis) noexcept {
        const auto deadline=add(nowMs,millis);
        if(deadline>nextMs_)nextMs_=deadline;
    }
    std::uint64_t epoch_=0,nextMs_=0;
    unsigned driverTransactions_=0,bindFailures_=0;
};
} // namespace armix::device
