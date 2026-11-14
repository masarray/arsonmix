// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Research-only late-field onset diffuser. Three Schroeder all-pass stages
// distribute impulse energy without replacing the six real image-source
// boundaries or imposing an arbitrary lowpass/EQ. A unity-magnitude allpass
// stage is mathematically energy-preserving in steady state (not a wet boost).
// Incommensurate ~7/12/18ms time constants are prepared OFF the callback.
// This is an original implementation, not dearVR/AMBEO code or BRIR data.
#include <algorithm>
#include <array>
#include <cmath>

namespace armix::sonku::p82 {
class LateInputDiffuser final {
public:
    static constexpr int ringFrames=1024;
    static constexpr int stageCount=3;
    static constexpr float feedback=.5f; // |g|<1 guarantees stable allpass

    bool prepare(double sampleRate) noexcept {
        if(!std::isfinite(sampleRate) ||
           (std::abs(sampleRate-44100.)>.1 &&
            std::abs(sampleRate-48000.)>.1))return false;
        constexpr std::array<double,stageCount> seconds{
            .0073,.0117,.0179};
        for(int s=0;s<stageCount;++s) {
            const int samples=static_cast<int>(std::lround(
                seconds[s]*sampleRate));
            if(samples<1||samples>=ringFrames-1)return false;
            stages_[s].delay=samples;
        }
        reset();
        return true;
    }
    void reset() noexcept {
        for(auto& stage:stages_) {
            for(auto& channel:stage.data)channel.fill(0.f);
            stage.write=0;stage.valid=0;
        }
    }
    // OFF/ON fence: never clear ~24KB from the realtime audio callback.
    // Read only samples written since the new activation.
    void invalidate() noexcept {
        for(auto& stage:stages_)stage.valid=0;
    }
    void process(float& mid,float& side) noexcept {
        for(auto& stage:stages_) {
            const int read=(stage.write-stage.delay)&(ringFrames-1);
            const bool fresh=stage.valid>=stage.delay;
            const float oldM=fresh?stage.data[0][read]:0.f;
            const float oldS=fresh?stage.data[1][read]:0.f;
            const float nextM=oldM-feedback*mid;
            const float nextS=oldS-feedback*side;
            stage.data[0][stage.write]=mid+feedback*nextM;
            stage.data[1][stage.write]=side+feedback*nextS;
            mid=nextM;side=nextS;
            stage.write=(stage.write+1)&(ringFrames-1);
            if(stage.valid<ringFrames)++stage.valid;
        }
    }
private:
    struct Stage {
        std::array<std::array<float,ringFrames>,2> data{};
        int delay=1,write=0,valid=0;
    };
    static_assert((ringFrames&(ringFrames-1))==0);
    std::array<Stage,stageCount> stages_{};
};
} // namespace armix::sonku::p82
