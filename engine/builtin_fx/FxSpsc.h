// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Single control producer -> single audio consumer, or audio producer ->
// control consumer. Never allocate, wait, log or lock in the callback.
namespace armix::builtin_fx {
template<class T,std::size_t Capacity> class FxSpsc final {
    static_assert(Capacity>1);
public:
    bool push(const T& value) noexcept {
        const auto w=write_.load(std::memory_order_relaxed);
        if(w-read_.load(std::memory_order_acquire)>=Capacity)return false;
        data_[w%Capacity]=value;
        write_.store(w+1,std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto r=read_.load(std::memory_order_relaxed);
        if(r==write_.load(std::memory_order_acquire))return false;
        value=data_[r%Capacity];
        read_.store(r+1,std::memory_order_release);
        return true;
    }
    bool canPush() const noexcept {
        return write_.load(std::memory_order_relaxed)-
               read_.load(std::memory_order_acquire)<Capacity;
    }
    bool empty() const noexcept {
        return read_.load(std::memory_order_acquire)==
               write_.load(std::memory_order_acquire);
    }
private:
    alignas(64) std::atomic<std::uint32_t> write_{0};
    alignas(64) std::atomic<std::uint32_t> read_{0};
    std::array<T,Capacity> data_{};
};
} // namespace armix::builtin_fx
