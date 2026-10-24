// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace armix::rt {
// SPSC only: one dedicated producer (control dispatch thread), one audio consumer.
// Do not use with multiple WebView callback producers directly.
// Power-of-two capacity, one reserved cell. This class never allocates on push/pop.
template<class T, std::size_t Capacity>
class SpscQueue final {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0);
    static_assert(std::is_trivially_copyable_v<T>);
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
    std::array<T, Capacity> items_{};
public:
    bool tryPush(const T& v) noexcept {
        const auto w=write_.load(std::memory_order_relaxed);
        if (w-read_.load(std::memory_order_acquire) >= Capacity-1) return false;
        items_[w & (Capacity-1)] = v;
        write_.store(w+1,std::memory_order_release);
        return true;
    }
    bool tryPop(T& result) noexcept {
        const auto r=read_.load(std::memory_order_relaxed);
        if (r==write_.load(std::memory_order_acquire)) return false;
        result=items_[r & (Capacity-1)];
        read_.store(r+1,std::memory_order_release);
        return true;
    }
};

// Values are coalesced per parameter: dragging a fader updates its latest state.
// One control-dispatch writer and audio reader. No float atomics retained by WebView.
// Must be used for scalar continuously-varying controls only, not structural changes.
template<std::size_t NumParameters>
class ParameterBank final {
    struct Cell {
        std::atomic<float> value{0.f};
        std::atomic<std::uint32_t> revision{0};
    };
    std::array<Cell,NumParameters> parameters_{};
public:
    void publish(std::size_t id, float v) noexcept {
        if (id>=NumParameters) return;
        parameters_[id].value.store(v,std::memory_order_relaxed);
        parameters_[id].revision.fetch_add(1,std::memory_order_release);
    }
    // Called from audio thread once per block. Caller remembers lastRevision[id].
    bool readChanged(std::size_t id,std::uint32_t& lastRevision,float& out) const noexcept {
        if (id>=NumParameters) return false;
        const auto ver=parameters_[id].revision.load(std::memory_order_acquire);
        if (ver==lastRevision) return false;
        out=parameters_[id].value.load(std::memory_order_relaxed);
        lastRevision=ver;
        return true;
    }
};

struct DiscreteEvent final {
    enum class Kind : std::uint32_t { mute, solo, transport, sampleClockMarker };
    Kind kind{};
    std::uint32_t channel{};
    std::uint32_t value{};
};
} // namespace armix::rt
