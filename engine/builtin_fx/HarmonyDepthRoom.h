// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace armix::harmony {
// ONE deterministic backing-only stereo ambience field, not 5 reverbs.
// A 4x4 orthonormal Hadamard feedback stage distributes early line energy.
// Loop gains and pole coefficients are prepared off the realtime callback;
// the total energy gain is strictly < 1 before frequency damping.
class BackingDepthRoom final {
public:
    static constexpr std::size_t branches=4;
    static constexpr std::size_t capacity=8192;
    struct Output {float left=0.f,right=0.f;};
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        constexpr std::array<float,branches> delayMs{{23.f,29.f,34.f,41.f}};
        for(std::size_t i=0;i<branches;++i){
            delay_[i]=static_cast<std::size_t>(std::lround(
                static_cast<double>(delayMs[i])*.001*rate));
            if(delay_[i]<2||delay_[i]>=capacity)return false;
            // Nominal room RT60 ~0.55 s at low frequencies, shorter at
            // high frequencies through the one-pole damping filter.
            feedback_[i]=static_cast<float>(std::pow(
                10.,-3.*static_cast<double>(delay_[i])/(rate*.55)));
            if(!std::isfinite(feedback_[i])||feedback_[i]>=1.f)return false;
            lines_[i].fill(0.f);damped_[i]=0.f;
        }
        dampingAlpha_=static_cast<float>(
            1.-std::exp(-2.*3.141592653589793*3200./rate));
        cursor_=0;
        return dampingAlpha_>0.f&&dampingAlpha_<1.f;
    }
    Output process(float inputL,float inputR) noexcept {
        std::array<float,branches> d{};
        for(std::size_t i=0;i<branches;++i){
            const auto p=(cursor_+capacity-delay_[i])&(capacity-1);
            const float delayed=lines_[i][p];
            damped_[i]+=dampingAlpha_*(delayed-damped_[i]);
            d[i]=damped_[i];
        }
        // Orthonormal matrix: Hadamard/2, no amplifying eigenvalue.
        const float h0=.5f*(d[0]+d[1]+d[2]+d[3]);
        const float h1=.5f*(d[0]-d[1]+d[2]-d[3]);
        const float h2=.5f*(d[0]+d[1]-d[2]-d[3]);
        const float h3=.5f*(d[0]-d[1]-d[2]+d[3]);
        lines_[0][cursor_]=.22f*inputL+feedback_[0]*h0;
        lines_[1][cursor_]=.22f*inputR+feedback_[1]*h1;
        lines_[2][cursor_]=.22f*(.75f*inputL+.25f*inputR)+feedback_[2]*h2;
        lines_[3][cursor_]=.22f*(.25f*inputL+.75f*inputR)+feedback_[3]*h3;
        cursor_=(cursor_+1)&(capacity-1);
        // Positive-polarity return: not artificially L=-R or anti-phase.
        return { .55f*(d[0]+d[2]), .55f*(d[1]+d[3]) };
    }
    std::size_t delaySamples(std::size_t i) const noexcept {
        return delay_[i];
    }
    float feedbackGain(std::size_t i) const noexcept {
        return feedback_[i];
    }
private:
    std::array<std::array<float,capacity>,branches> lines_{};
    std::array<std::size_t,branches> delay_{};
    std::array<float,branches> feedback_{},damped_{};
    float dampingAlpha_=0.f;
    std::size_t cursor_=0;
};
} // namespace armix::harmony
