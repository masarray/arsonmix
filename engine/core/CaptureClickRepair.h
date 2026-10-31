// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace armix::rt {
// P0: no extra FIFO, resampler, or callback latency. Only a verified stereo
// WASAPI DIRECT exceptional block is copied into caller-prepared scratch.
// The upstream evidence is a heuristic, NOT a driver-proven discontinuity.
// Healthy PCM never crosses this memory copy or changes a single sample.
class CaptureClickRepair final {
public:
    static constexpr int minZeroHead=32;
    static constexpr int maxRamp=64;
    void reset() noexcept {
        previous_.fill(0.f);
        repairedBlocks_.store(0,std::memory_order_relaxed);
        repairedFrames_.store(0,std::memory_order_relaxed);
        sanitizedSamples_.store(0,std::memory_order_relaxed);
    }
    void clearCarry() noexcept { previous_.fill(0.f); }
    bool process(const float* const* input,int channels,int frames,
                 int candidateHead,bool hasNonFinite,
                 float* outL,float* outR) noexcept {
        if(!input || channels!=2 || frames<=0 || !input[0] || !input[1]) {
            clearCarry();return false;
        }
        const auto previous=previous_;
        previous_[0]=std::isfinite(input[0][frames-1])?input[0][frames-1]:0.f;
        previous_[1]=std::isfinite(input[1][frames-1])?input[1][frames-1]:0.f;
        const bool fade=candidateHead>=minZeroHead && candidateHead<frames &&
            !hasNonFinite &&
            (std::abs(previous[0])>=0.002f || std::abs(previous[1])>=0.002f);
        if(!fade&&!hasNonFinite)return false; // common RT fast path
        if(!outL||!outR)return false;
        std::uint64_t invalid=0;
        for(int i=0;i<frames;++i) {
            const float l=input[0][i],r=input[1][i];
            const bool okL=std::isfinite(l),okR=std::isfinite(r);
            invalid+=static_cast<unsigned>(!okL)+static_cast<unsigned>(!okR);
            outL[i]=okL?l:0.f;
            outR[i]=okR?r:0.f;
        }
        if(invalid)sanitizedSamples_.fetch_add(invalid,std::memory_order_relaxed);
        if(fade) {
            const int tail=std::min(maxRamp,candidateHead);
            const int resume=std::min(maxRamp,frames-candidateHead);
            // Fade the last valid sample into an exact zero head rather than
            // abruptly cutting at a callback boundary; ease resumption too.
            // Bounded 1.33ms envelope at 48k; zero new samples of latency.
            for(int i=0;i<tail;++i) {
                const float g=1.f-static_cast<float>(i+1)/static_cast<float>(tail);
                outL[i]=previous[0]*g;outR[i]=previous[1]*g;
            }
            for(int i=0;i<resume;++i) {
                const int p=candidateHead+i;
                const float g=static_cast<float>(i+1)/static_cast<float>(resume);
                outL[p]*=g;outR[p]*=g;
            }
            repairedBlocks_.fetch_add(1,std::memory_order_relaxed);
            repairedFrames_.fetch_add(static_cast<std::uint64_t>(tail+resume),
                                      std::memory_order_relaxed);
        }
        return true;
    }
    std::uint64_t repairedBlocks() const noexcept {
        return repairedBlocks_.load(std::memory_order_relaxed);
    }
    std::uint64_t repairedFrames() const noexcept {
        return repairedFrames_.load(std::memory_order_relaxed);
    }
    std::uint64_t sanitizedSamples() const noexcept {
        return sanitizedSamples_.load(std::memory_order_relaxed);
    }
    static constexpr const char* provenance() noexcept {
        return "STEREO_WASAPI_ZERO_HEAD_BOUNDED_DECLICK_NOT_CLOCK_DRIFT_OR_RENDER_XRUN_FIX";
    }
private:
    std::array<float,2> previous_{}; // only native callback owns it
    std::atomic<std::uint64_t> repairedBlocks_{0},repairedFrames_{0},sanitizedSamples_{0};
};
}
