// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "ChannelEq.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <limits>

// Real-time bridge between one JUCE control-thread owner and the ONLY native
// audio callback. The DSP is native; this is NOT a VST3 loader or second engine.
namespace armix::builtin_fx {
namespace detail {
template<typename T, std::size_t Capacity> class SpscRing final {
    static_assert(Capacity > 1 && Capacity < 65536);
public:
    // Exactly ONE producer and ONE consumer; serial control events only.
    bool push(const T& value) noexcept {
        const auto write = write_.load(std::memory_order_relaxed);
        if (write - read_.load(std::memory_order_acquire) >= Capacity) return false;
        storage_[write % Capacity] = value;
        write_.store(write + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& value) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        value = storage_[read % Capacity];
        read_.store(read + 1, std::memory_order_release);
        return true;
    }
    [[nodiscard]] bool canPush() const noexcept {
        return write_.load(std::memory_order_relaxed) -
               read_.load(std::memory_order_acquire) < Capacity;
    }
private:
    alignas(64) std::atomic<std::uint32_t> write_{0};
    alignas(64) std::atomic<std::uint32_t> read_{0};
    std::array<T, Capacity> storage_{};
};
} // namespace detail

class ChannelEqBank final {
public:
    static constexpr std::size_t laneCount = 14; // 10 physical CH + 4 AUX; no fake MASTER
    static constexpr std::size_t maxPendingPerLane = 32;
    struct Ack final {
        std::uint64_t requestId = 0;
        std::uint64_t deviceEpoch = 0;
        std::uint64_t graphRevision = 0;
        std::uint8_t lane = 0;
        std::uint8_t slot = 0;
        Band accepted{};
        bool applied = false;
    };
    ChannelEqBank() = default;
    ChannelEqBank(const ChannelEqBank&) = delete;
    ChannelEqBank& operator=(const ChannelEqBank&) = delete;

    // Call BEFORE publishing a graph to the audio callback. Never prepare a
    // published graph; its state is exclusively advanced by the audio owner.
    bool prepare(double sampleRate) noexcept {
        if (ready_) return false;
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0)
            return false;
        for (auto& eq : eqs_) if (!eq.prepare(sampleRate)) return false;
        sampleRate_ = sampleRate;
        ready_ = true;
        return true;
    }
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int addedLatencySamples() const noexcept { return 0; }

    // Control thread: trigonometry/validation happens HERE, never on audio.
    // Success only means QUEUED, not applied. Host must wait for pollAck()
    // and then publish an authoritative accepted snapshot to the GUI.
    bool queueBandEdit(std::size_t lane, std::size_t slot, const Band& band,
                       std::uint64_t requestId, std::uint64_t deviceEpoch,
                       std::uint64_t graphRevision) noexcept {
        if (!ready_ || lane >= laneCount || slot >= ChannelEq::bandCount ||
            requestId == 0 || deviceEpoch == 0 || graphRevision == 0)
            return false;
        ChannelEq::PreparedBandEdit prepared{};
        if (!ChannelEq::prepareBandEdit(slot, band, sampleRate_, prepared)) return false;
        return pending_[lane].push(Command{requestId,deviceEpoch,graphRevision,
                      static_cast<std::uint8_t>(lane),static_cast<std::uint8_t>(slot),prepared});
    }
    // Only on UNPUBLISHED graphs / offline fixtures. No concurrent callback.
    bool setBandOffline(std::size_t lane, std::size_t slot,const Band& band) noexcept {
        return ready_ && lane < laneCount && eqs_[lane].setBand(slot,band);
    }
    // AUDIO OWNER ONLY. At most one filter change per strip per block, and
    // never start another 5ms topology fade before the previous one ends.
    // A full acknowledgement ring BACKPRESSURES the request queue; it must
    // never silently apply a command whose ACK was lost.
    void render(std::size_t lane, float* left, float* right,
                std::size_t frames) noexcept {
        if (!ready_ || lane >= laneCount || !left || !right || frames == 0) return;
        auto& eq = eqs_[lane];
        if (!eq.transitioning() && acknowledgements_[lane].canPush()) {
            Command command{};
            if (pending_[lane].pop(command)) {
                const bool applied = eq.applyPreparedBand(command.edit);
                Ack ack{command.requestId,command.deviceEpoch,command.graphRevision,
                        command.lane,command.slot,command.edit.band,applied};
                // Can only fail if another thread illegally writes the same
                // SPSC consumer ACK ring. The producer cannot fill it.
                (void)acknowledgements_[lane].push(ack);
            }
        }
        eq.processStereo(left,right,frames);
    }
    // Control thread: drain after audio block; validate the ACK's graph epoch
    // and revision AGAIN before changing the public accepted UI state.
    bool pollAck(std::size_t lane, Ack& result) noexcept {
        return lane < laneCount && acknowledgements_[lane].pop(result);
    }
    // Tests and UNPUBLISHED topology authoring only; querying while render()
    // runs is a data race. Live readbacks MUST use the ACK value.
    [[nodiscard]] Band offlineBand(std::size_t lane, std::size_t slot) const noexcept {
        return lane < laneCount ? eqs_[lane].band(slot) : Band{};
    }
private:
    struct Command final {
        std::uint64_t requestId=0,deviceEpoch=0,graphRevision=0;
        std::uint8_t lane=0,slot=0;
        ChannelEq::PreparedBandEdit edit{};
    };
    std::array<ChannelEq,laneCount> eqs_{};
    std::array<detail::SpscRing<Command,maxPendingPerLane>,laneCount> pending_{};
    std::array<detail::SpscRing<Ack,maxPendingPerLane>,laneCount> acknowledgements_{};
    double sampleRate_=0.0;
    bool ready_=false;
};
} // namespace armix::builtin_fx
