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
    void reset() noexcept {size_=0;next_=0;}
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
        size_=std::min(capacity,size_+1);return true;
    }
    Observation nearest(double at,double maxDistance)const noexcept {
        if(!size_||!std::isfinite(at)||at<0.||!std::isfinite(maxDistance)||maxDistance<0.)return {};
        // lower_bound on chronological circular view, <=5 iterations.
        std::size_t lo=0,hi=size_;
        while(lo<hi){const auto m=lo+(hi-lo)/2;if(ordered(m).at<at)lo=m+1;else hi=m;}
        const auto right=std::min(lo,size_-1),left=lo?lo-1:0;
        const auto& a=ordered(left);const auto& b=ordered(right);
        const auto& best=std::abs(at-a.at)<=std::abs(at-b.at)?a:b;
        return std::abs(best.at-at)<=maxDistance?best:Observation{};
    }
private:
    const Observation& ordered(std::size_t i)const noexcept {
        return data_[(next_+capacity-size_+i)&(capacity-1)];
    }
    std::array<Observation,capacity> data_{};
    std::size_t size_=0,next_=0;
};
} // namespace armix::harmony
