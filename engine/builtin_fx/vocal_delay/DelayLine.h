// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>
namespace armix::builtin_fx::vocal_delay {
// Single audio-thread owner. Reset invalidates history in O(1), not a bulk clear.
class DelayLine final {
public:
    struct Tap { std::size_t whole=1; float fraction=0.f; };
    void prepare(std::size_t frames) { data_.assign(frames,0.f); write_=valid_=0; }
    void invalidate() noexcept { valid_=0; }
    float read(Tap tap) const noexcept {
        const auto a=at(tap.whole);
        return tap.fraction==0.f?a:a+tap.fraction*(at(tap.whole+1)-a);
    }
    void push(float x) noexcept {
        data_[write_]=x;
        if(++write_==data_.size())write_=0;
        if(valid_<data_.size())++valid_;
    }
    std::size_t capacity() const noexcept { return data_.size(); }
private:
    float at(std::size_t age) const noexcept {
        if(age==0||age>valid_)return 0.f;
        return data_[write_>=age?write_-age:data_.size()-(age-write_)];
    }
    std::vector<float> data_;
    std::size_t write_=0,valid_=0;
};
} // namespace armix::builtin_fx::vocal_delay
