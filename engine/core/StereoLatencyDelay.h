// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <vector>

namespace armix::dsp {
// CONTROL prepares storage; AUDIO owns the cursor. Zero delay is a true
// passthrough. No allocation, interpolation, lock or modulo in the sample path.
class StereoLatencyDelay final {
public:
    static constexpr int maximumSamples=131072;
    bool prepare(int samples) {
        if(samples<0 || samples>maximumSamples)return false;
        data_.assign(static_cast<std::size_t>(samples)*2,0.f);cursor_=0;
        return true;
    }
    void process(float& left,float& right) noexcept {
        if(data_.empty())return;
        const float l=data_[cursor_],r=data_[cursor_+1];
        data_[cursor_]=left;data_[cursor_+1]=right;
        left=l;right=r;cursor_+=2;
        if(cursor_==data_.size())cursor_=0;
    }
private:
    std::vector<float> data_;
    std::size_t cursor_=0;
};
}
