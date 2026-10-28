// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

// One control-thread writer and any number of bounded RT readers.
// IMPORTANT: Separate "reader count" and "active pointer" atomics DO NOT
// protect reclamation; a reader can enter after a zero-count observation
// and acquire a pointer which is being freed. Gate reader reservation and
// writer publication with ONE sequentially-consistent atomic state.
//
// State bit 0: writer publishing/unpublishing (RT fails closed, NO SPIN).
// High bits: +2 per active reader. Any reader registered before writer
// publication retains old lifetime until it releases the lease.
// In contrast, readers registering after the gate clears see only NEW active.
// Retired memory is reclaimed only on control thread, and only when there
// are no preexisting readers. No heap/lock/device call on RT read path.
namespace armix::rt {
template<class Prepared, std::size_t RetiredCapacity=8>
class RealtimeRoutePublisher final {
    static_assert(RetiredCapacity>=2);
    static constexpr std::uint32_t writerFlag=1U, readerUnit=2U;
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
            auto activity=owner_->activity_.load(std::memory_order_seq_cst);
            // Do not spin/wait inside the audio callback. One silent block is
            // preferable to scheduling jitter while control thread publishes.
            if((activity&writerFlag)!=0 || activity>UINT32_MAX-readerUnit)
                return;
            if(!owner_->activity_.compare_exchange_strong(
                activity,activity+readerUnit,
                std::memory_order_seq_cst,std::memory_order_seq_cst))
                return;
            registered_=true;
            item_=owner_->active_.load(std::memory_order_seq_cst);
        }
        ReadLease(const ReadLease&)=delete;
        ReadLease& operator=(const ReadLease&)=delete;
        ReadLease(ReadLease&&)=delete;
        ReadLease& operator=(ReadLease&&)=delete;
        ~ReadLease() noexcept {
            if(registered_)
                owner_->activity_.fetch_sub(readerUnit,std::memory_order_seq_cst);
        }
        const Snapshot* get(std::uint64_t currentEpoch) const noexcept {
            return item_ && item_->deviceEpoch==currentEpoch
                ? item_ : nullptr;
        }
    private:
        RealtimeRoutePublisher* owner_;
        const Snapshot* item_=nullptr;
        bool registered_=false;
    };

    RealtimeRoutePublisher()=default;
    RealtimeRoutePublisher(const RealtimeRoutePublisher&)=delete;
    RealtimeRoutePublisher& operator=(const RealtimeRoutePublisher&)=delete;

    // CONTROL thread only. Expected revision is last acknowledged revision;
    // candidate is fully preallocated and validated before any RT publication.
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
        pollRetirement();
        if(owned_ && retiredCount_>=RetiredCapacity)
            return Result::retirementFull;
        // Single writer. Publish/retire happens only with the low-bit gate set.
        activity_.fetch_or(writerFlag,std::memory_order_seq_cst);
        if(owned_)retired_[retiredCount_++]=std::move(owned_);
        owned_=std::move(next);
        active_.store(owned_.get(),std::memory_order_seq_cst);
        activity_.fetch_and(~writerFlag,std::memory_order_seq_cst);
        revision_=owned_->graphRevision;
        return Result::applied;
    }

    // Immediately gate the last route to silence. Do NOT delete a graph
    // while the audio callback could own a ReadLease to its snapshot.
    void unpublish() noexcept {
        activity_.fetch_or(writerFlag,std::memory_order_seq_cst);
        active_.store(nullptr,std::memory_order_seq_cst);
        activity_.fetch_and(~writerFlag,std::memory_order_seq_cst);
        revision_=0;
        pollRetirement();
        if(!owned_)return;
        if(retiredCount_<RetiredCapacity)
            retired_[retiredCount_++]=std::move(owned_);
        // Saturated: preserve unpublished ownership until quiescent.
        // The next publication is rejected unless safe retirement completes.
    }

    // CONTROL only: cannot reclaim any snapshot whose pointer might still
    // be held by a reader which registered before its publication.
    bool pollRetirement() noexcept {
        if((activity_.load(std::memory_order_seq_cst)&~writerFlag)!=0)
            return false;
        for(std::size_t i=0;i<retiredCount_;++i)retired_[i].reset();
        retiredCount_=0;
        return true;
    }
    // Only the single control-thread writer calls this. The editor MUST
    // mutate atomic runtime parameters, never processor lifetime or topology.
    // The owned snapshot is stable on the writer thread; RT reads atomics.
    template<class Update>
    bool updateActiveControl(std::uint64_t epoch,std::uint64_t expectedRevision,
                             Update&& update) noexcept {
        if(!owned_||!owned_->prepared||owned_->deviceEpoch!=epoch||
           owned_->graphRevision!=expectedRevision||revision_!=expectedRevision)
            return false;
        return update(*owned_->prepared);
    }
    // CONTROL thread only. The owner stays alive while compiling a candidate.
    // NEVER cache the returned pointer in the audio callback or across a
    // subsequent publication. Use only immutable identity/FX shared ownership.
    const Prepared* controlPrepared(std::uint64_t epoch,
                                   std::uint64_t expectedRevision) const noexcept {
        return owned_ && owned_->prepared && revision_==expectedRevision &&
               owned_->deviceEpoch==epoch &&
               owned_->graphRevision==expectedRevision
                   ?owned_->prepared.get():nullptr;
    }
    [[nodiscard]] ReadLease acquire() noexcept {return ReadLease(*this);}
    std::uint64_t lastRevision() const noexcept {return revision_;} // CONTROL
    std::size_t pendingRetirements() const noexcept {return retiredCount_;} // CONTROL
    bool readersPresent() const noexcept {
        return (activity_.load(std::memory_order_seq_cst)&~writerFlag)!=0;
    }
private:
    std::atomic<const Snapshot*> active_{nullptr};
    std::atomic<std::uint32_t> activity_{0};
    std::unique_ptr<Snapshot> owned_;
    std::array<std::unique_ptr<Snapshot>,RetiredCapacity> retired_{};
    std::size_t retiredCount_=0;
    std::uint64_t revision_=0;
};
} // namespace armix::rt
