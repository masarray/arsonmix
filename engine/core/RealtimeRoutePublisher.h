// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

// Realtime path: two atomics per callback, one pointer read, no heap, locks,
// waiting, deallocation or device/GUI operations. One CONTROL writer.
// Prepared owns all DSP buffers; plugin objects MUST NOT be concurrently
// processed on old/new graphs (plugin lifecycle gate lives above this class).
// Old graph destruction happens only from pollRetirement() after RT readers
// have left. Do not destroy the exchange while audio callbacks are attached.
namespace armix::rt {
template<class Prepared, std::size_t RetiredCapacity=8>
class RealtimeRoutePublisher final {
    static_assert(RetiredCapacity>=2);
public:
    struct Snapshot final {
        std::uint64_t deviceEpoch=0;
        std::uint64_t graphRevision=0;
        std::unique_ptr<Prepared> prepared;
    };
    enum class Result : std::uint8_t {
        applied, invalidCandidate, staleDevice, staleRevision, retirementFull
    };
    class ReadLease final {
    public:
        explicit ReadLease (RealtimeRoutePublisher& owner) noexcept : owner_(&owner) {
            owner_->readers_.fetch_add(1,std::memory_order_acq_rel);
            item_=owner_->active_.load(std::memory_order_acquire);
        }
        ReadLease(const ReadLease&)=delete;
        ReadLease& operator=(const ReadLease&)=delete;
        ReadLease(ReadLease&&)=delete;
        ReadLease& operator=(ReadLease&&)=delete;
        ~ReadLease() noexcept {
            owner_->readers_.fetch_sub(1,std::memory_order_release);
        }
        const Snapshot* get(std::uint64_t currentEpoch) const noexcept {
            return item_ && item_->deviceEpoch==currentEpoch
                ? item_ : nullptr;
        }
    private:
        RealtimeRoutePublisher* owner_;
        const Snapshot* item_=nullptr;
    };

    RealtimeRoutePublisher()=default;
    RealtimeRoutePublisher(const RealtimeRoutePublisher&)=delete;
    RealtimeRoutePublisher& operator=(const RealtimeRoutePublisher&)=delete;

    // CONTROL thread, with fully prepared DSP state and explicit revision.
    // Expected revision always refers to the last ACKnowledged publication.
    // All failure outcomes preserve the audio callback's last valid graph.
    Result publish(std::unique_ptr<Snapshot> next,
                   std::uint64_t expectedRevision,
                   std::uint64_t deviceEpochNow) noexcept {
        if(!next || !next->prepared || next->deviceEpoch==0 ||
           next->graphRevision==0 || next->graphRevision<=expectedRevision)
            return Result::invalidCandidate;
        if(next->deviceEpoch!=deviceEpochNow)
            return Result::staleDevice;
        if(expectedRevision!=revision_)
            return Result::staleRevision;
        pollRetirement(); // control thread only
        if(owned_ && retiredCount_>=RetiredCapacity)
            return Result::retirementFull;
        if(owned_) retired_[retiredCount_++]=std::move(owned_);
        owned_=std::move(next);
        // Publish ownership last; readers always see a live allocation.
        active_.store(owned_.get(),std::memory_order_release);
        revision_=owned_->graphRevision;
        return Result::applied;
    }

    // CONTROL thread: gate new callback usage, then retire safely.
    // Caller must ensure the audio device is stopped before destroying this
    // object; asynchronous stop merely disconnects the new graph view.
    void unpublish() noexcept {
        pollRetirement();
        if(owned_) {
            if(retiredCount_<RetiredCapacity)
                retired_[retiredCount_++]=std::move(owned_);
            else {
                // No drop/free while the audio thread may use the pointer.
                // The host must quiesce and call pollRetirement() first.
                return;
            }
        }
        active_.store(nullptr,std::memory_order_release);
        revision_=0;
    }
    // CONTROL thread only. No free until every acquire() lease has exited.
    bool pollRetirement() noexcept {
        if(readers_.load(std::memory_order_acquire)!=0) return false;
        for(std::size_t i=0;i<retiredCount_;++i) retired_[i].reset();
        retiredCount_=0;
        return true;
    }
    [[nodiscard]] ReadLease acquire() noexcept {return ReadLease(*this);}
    std::uint64_t lastRevision() const noexcept {return revision_;} // CONTROL only
    std::size_t pendingRetirements() const noexcept {return retiredCount_;} // CONTROL only
    bool readersPresent() const noexcept {
        return readers_.load(std::memory_order_acquire)!=0;
    }
private:
    std::atomic<const Snapshot*> active_{nullptr};
    std::atomic<std::uint32_t> readers_{0};
    std::unique_ptr<Snapshot> owned_;
    std::array<std::unique_ptr<Snapshot>,RetiredCapacity> retired_ {};
    std::size_t retiredCount_=0;
    std::uint64_t revision_=0;
};
} // namespace armix::rt
