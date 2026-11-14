// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Compact, progressive, allocation-free waveform summary. Decoder worker
// alone writes; JUCE UI may read atomic bins without touching file/FFT/audio.
// No duplicated full-file scan competes with singer monitoring.
namespace armix::player {
class PlayerWaveformIndex final {
public:
    static constexpr std::size_t bins=256;
    PlayerWaveformIndex() noexcept {reset(0);}
    void reset(std::uint64_t frames) noexcept {
        total_.store(frames,std::memory_order_release);
        for(auto& peak:peaks_)peak.store(0,std::memory_order_relaxed);
    }
    void observe(const float* l,const float* r,int frames,
                 std::uint64_t start) noexcept {
        if(!l||!r||frames<=0)return;
        const auto total=total_.load(std::memory_order_acquire);
        if(!total)return;
        constexpr int hop=12;
        for(int i=0;i<frames;i+=hop) {
            const auto sample=start+static_cast<std::uint64_t>(i);
            if(sample>=total)break;
            const auto index=std::min(bins-1,
                static_cast<std::size_t>((static_cast<long double>(sample)*
                                           bins)/total));
            const float peak=std::max(std::abs(l[i]),std::abs(r[i]));
            if(!std::isfinite(peak))continue;
            const auto value=static_cast<std::uint16_t>(
                std::clamp(std::lround(peak*65535.f),0L,65535L));
            auto old=peaks_[index].load(std::memory_order_relaxed);
            while(old<value&&!peaks_[index].compare_exchange_weak(
                    old,value,std::memory_order_relaxed)) {}
        }
    }
    std::array<std::uint16_t,bins> snapshot() const noexcept {
        std::array<std::uint16_t,bins> values{};
        for(std::size_t i=0;i<bins;++i)
            values[i]=peaks_[i].load(std::memory_order_relaxed);
        return values;
    }
private:
    std::atomic<std::uint64_t> total_{0};
    std::array<std::atomic<std::uint16_t>,bins> peaks_{};
};
} // namespace armix::player
