// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <array>
namespace armix::rt {
// Adjacent successful armed native renders within one epoch/revision.
// Same host timestamps, no extra clock, allocation, OS call or action.
// Late starts are heuristic evidence, NOT driver xrun or causation.
class RenderPressureEvidence final {
public:
    static constexpr unsigned windowPairs=64;
    struct Snapshot {
        bool valid=false;
        unsigned withinThenOnTime=0,withinThenLate=0,overThenOnTime=0,overThenLate=0;
    };
    // One uninterrupted eligible render run. Publish only on boundaries,
    // never per callback; reader is bounded and does not spin.
    struct Scope {
        std::uint64_t token=0,epoch=0,revision=0;
        bool operator==(const Scope&) const noexcept = default;
    };
    Scope scope() const noexcept {
        const auto token=scopeToken_.load();
        if(!token)return {};
        const Scope value{token,scopeEpoch_.load(),scopeRevision_.load()};
        return scopeToken_.load()==token?value:Scope{};
    }
    void breakSequence() noexcept {
        if(previousStart_||count_){
            scopeToken_.store(0);
            previousStart_=0;count_=0;bins_={};
            published_.store(0,std::memory_order_release);
        }
    }
    void finish(std::uint64_t epoch,std::uint64_t revision,std::uint64_t start,
                std::uint64_t elapsed,double period) noexcept {
        if(!epoch||!revision||!start||!std::isfinite(period)||period<=0.){
            breakSequence();return;
        }
        if(previousStart_&&(epoch!=epoch_||revision!=revision_||
            start<=previousStart_||start-previousStart_<previousElapsed_))
            breakSequence();
        if(previousStart_){
            const bool late=static_cast<double>(start-previousStart_)>1.5*previousPeriod_;
            ++bins_[(previousOver_?2u:0u)+(late?1u:0u)];
            if(++count_==windowPairs){
                std::uint64_t word=std::uint64_t{1}<<63;
                for(unsigned i=0;i<4;++i)word|=static_cast<std::uint64_t>(bins_[i])<<(7*i);
                published_.store(word,std::memory_order_release);
                count_=0;bins_={};
            }
        }
        if(!previousStart_){
            scopeEpoch_.store(epoch);
            scopeRevision_.store(revision);
            if(++scopeSerial_!=0)scopeToken_.store(scopeSerial_);
        }
        epoch_=epoch;revision_=revision;previousStart_=start;previousElapsed_=elapsed;
        previousPeriod_=period;previousOver_=static_cast<double>(elapsed)>period;
    }
    Snapshot snapshot() const noexcept {
        const auto word=published_.load(std::memory_order_acquire);
        return {bool(word>>63),static_cast<unsigned>(word&127u),
            static_cast<unsigned>((word>>7)&127u),static_cast<unsigned>((word>>14)&127u),
            static_cast<unsigned>((word>>21)&127u)};
    }
private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::uint64_t epoch_=0,revision_=0,previousStart_=0,previousElapsed_=0;
    double previousPeriod_=0.;
    bool previousOver_=false;
    unsigned count_=0;
    std::array<unsigned,4> bins_{};
    std::atomic<std::uint64_t> published_{0};
    std::uint64_t scopeSerial_=0; // audio owner, lifetime counter
    std::atomic<std::uint64_t> scopeToken_{0},scopeEpoch_{0},scopeRevision_{0};
};
}
