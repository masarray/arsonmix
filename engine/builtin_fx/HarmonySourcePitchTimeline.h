// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace armix::harmony {
// Fixed recent measured-F0 evidence in SOURCE sample coordinates, not
// completion timestamps. Only the audio owner publishes/queries it.
// Nearest accepted observation; no interpolation across large note jumps,
// no stale extrapolation. Candidate deliberately independent of tonal plan.
class SourcePitchTimeline final {
public:
    struct Observation {double at=0.;float period=0.f,confidence=0.f;};
    static constexpr std::size_t capacity=16;
    struct Cursor {
        Observation observation{};
        std::uint64_t revision=0,refreshes=0;
        double left=0.,right=0.;
        bool valid=false,hasLeft=false,hasRight=false;
    };
    void reset() noexcept {size_=0;next_=0;pendingCount_=0;++revision_;}
    static double sourceCenter(std::uint64_t newest,int decimation,float lag) noexcept {
        // Center of the combined 160-point correlation support.
        // Lowpass group delay is not explicitly estimated; this is nominal.
        return static_cast<double>(newest)-.5*(159.+lag)*decimation;
    }
    bool publish(Observation x) noexcept {
        if(!std::isfinite(x.at)||x.at<0.||!std::isfinite(x.period)||x.period<8.f||
           x.period>3200.f||!std::isfinite(x.confidence)||x.confidence<.86f||x.confidence>1.f)return false;
        if(size_&&x.at<=ordered(size_-1).at)return false;
        data_[next_]=x;next_=(next_+1)&(capacity-1);
        size_=std::min(capacity,size_+1);++revision_;return true;
    }
    // Independent source-only confirmation. Three consecutive strong
    // frames agree on a large change before backdating their evidence.
    // This never edits the musical note or the original F0 tracker.
    void observe(Observation x,double maxGap) noexcept {
        if(!std::isfinite(x.at)||x.at<0.||!std::isfinite(x.period)||
           x.period<8.f||x.period>3200.f||!std::isfinite(x.confidence)||
           x.confidence<.86f||x.confidence>1.f){pendingCount_=0;return;}
        if(size_&&x.at<=ordered(size_-1).at){pendingCount_=0;return;}
        if(size_&&std::abs(x.period-ordered(size_-1).period)<=.25f*ordered(size_-1).period){
            pendingCount_=0;publish(x);return;
        }
        if(pendingCount_){
            const auto& last=pending_[pendingCount_-1];
            if(x.at<=last.at||x.at-last.at>maxGap||
               std::abs(x.period-last.period)>.12f*last.period)pendingCount_=0;
        }
        pending_[pendingCount_++]=x;
        if(pendingCount_==pending_.size()){
            for(const auto& obs:pending_)publish(obs);
            pendingCount_=0;
        }
    }
    void interrupt() noexcept {pendingCount_=0;}
    Observation nearest(double at,double maxDistance)const noexcept {
        if(!size_||!std::isfinite(at)||at<0.||!std::isfinite(maxDistance)||maxDistance<0.)return {};
        const auto& best=ordered(nearestIndex(at));
        return std::abs(best.at-at)<=maxDistance?best:Observation{};
    }
    // During normal playback the same observation remains nearest for
    // roughly a complete analysis hop. Validate the cached Voronoi cell
    // with the SAME distance/tie rule, and re-search only when its
    // neighbors win or new evidence invalidates it. Bounded even after
    // a sample-clock jump or a nonmonotonic offline query.
    Observation nearestCached(double at,double maxDistance,Cursor& c)const noexcept {
        if(!size_||!std::isfinite(at)||at<0.||!std::isfinite(maxDistance)||maxDistance<0.)return {};
        double distance=std::abs(at-c.observation.at);
        if(!c.valid||c.revision!=revision_||
           (c.hasLeft&&std::abs(at-c.left)<=distance)||
           (c.hasRight&&std::abs(at-c.right)<distance)){
            const auto ix=nearestIndex(at);
            c.observation=ordered(ix);c.revision=revision_;c.valid=true;
            c.hasLeft=ix>0;c.hasRight=ix+1<size_;
            if(c.hasLeft)c.left=ordered(ix-1).at;
            if(c.hasRight)c.right=ordered(ix+1).at;
            ++c.refreshes;distance=std::abs(at-c.observation.at);
        }
        return distance<=maxDistance?c.observation:Observation{};
    }
private:
    std::size_t nearestIndex(double at)const noexcept {
        std::size_t lo=0,hi=size_;
        while(lo<hi){const auto m=lo+(hi-lo)/2;if(ordered(m).at<at)lo=m+1;else hi=m;}
        const auto right=std::min(lo,size_-1),left=lo?lo-1:0;
        return std::abs(at-ordered(left).at)<=std::abs(at-ordered(right).at)?left:right;
    }
    const Observation& ordered(std::size_t i)const noexcept {
        return data_[(next_+capacity-size_+i)&(capacity-1)];
    }
    std::array<Observation,capacity> data_{};
    std::size_t size_=0,next_=0;
    std::array<Observation,3> pending_{};
    std::size_t pendingCount_=0;
    std::uint64_t revision_=0;
};
} // namespace armix::harmony
