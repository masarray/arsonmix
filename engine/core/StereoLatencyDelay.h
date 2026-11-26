// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
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
    [[nodiscard]] bool active() const noexcept {return !data_.empty();}
    // Disconnected tap: retain the exact chronological delay history,
    // without reading discarded samples or running per-sample route logic.
    // If a block replaces the whole ring, only its last delay-length
    // frames matter; normalize the cursor to the oldest retained frame.
    void advanceHistory(const float* left,const float* right,int frames) noexcept {
        if(!active()||frames<=0)return;
        const auto length=data_.size()/2;
        auto count=static_cast<std::size_t>(frames);
        if(count>=length){
            left+=count-length;right+=count-length;
            count=length;cursor_=0;
        }
        const auto first=std::min(count,(data_.size()-cursor_)/2);
        for(std::size_t i=0;i<first;++i){
            data_[cursor_+2*i]=left[i];data_[cursor_+2*i+1]=right[i];
        }
        cursor_+=2*first;
        if(cursor_==data_.size())cursor_=0;
        const auto rest=count-first;
        if(rest){
            for(std::size_t i=0;i<rest;++i){
                data_[2*i]=left[first+i];data_[2*i+1]=right[first+i];
            }
            cursor_=2*rest;
        }
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
