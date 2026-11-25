// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "../core/RealtimeSlew64.h"
#include <atomic>
#include <cmath>
namespace armix::player {
// CONTROL publishes a bounded level; AUDIO applies it after the A/B fade
// and before INPUT GAIN or inserts. Never amplify, allocate or lock.
class PlayerOutputTrim final {
public:
    PlayerOutputTrim() noexcept { slew_.reset(.5f); }
    void setPercent(double value) noexcept {
        if(std::isfinite(value)&&value>=0.0&&value<=100.0)
            percent_.store(static_cast<int>(std::lround(value)),std::memory_order_relaxed);
    }
    int percent() const noexcept {return percent_.load(std::memory_order_relaxed);}
    void process(float* left,float* right,int frames) noexcept {
        slew_.retarget(static_cast<float>(percent())*.01f);
        for(int i=0;i<frames;++i) {
            const float gain=slew_.next();
            left[i]*=gain;right[i]*=gain;
        }
    }
private:
    std::atomic<int> percent_{50};
    armix::dsp::RealtimeSlew64 slew_;
};
}
