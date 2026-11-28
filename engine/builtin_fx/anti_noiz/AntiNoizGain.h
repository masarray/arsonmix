// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Causal decision-directed magnitude estimator for explicit Voice only.
// Recursive estimate excludes user floor, neighbor smoothing and expander.
template<int MaxBins> class VoiceGain final {
public:
    void prepare(double rate,int hop) noexcept {
        memory_=float(std::exp(-double(hop)/(rate*.025)));reset();
    }
    void reset() noexcept {prior_.fill(0);}
    // AN-07E: bounded foreground restoration, only with independent
    // neighboring evidence. A dominant noise bin alone cannot request gain.
    static float restore(float gain,float gamma,float relativeNoise,
                         float temporal,float neighbor) noexcept {
        const float overlap=std::clamp((relativeNoise-1.f)*.5f,0.f,1.f);
        const float local=std::clamp((temporal-1.5f)*.5f,0.f,1.f);
        const float corroboration=std::clamp((neighbor-4.f)*.125f,0.f,1.f);
        const float present=std::clamp((gamma-1.f)*.5f,0.f,1.f);
        const float headroom=std::min(.15f,1.f-gain);
        return gain+headroom*overlap*local*corroboration*present;
    }
    float estimate(int bin,float gamma) noexcept {
        gamma=std::clamp(gamma,0.f,1e8f);
        const float instantaneous=std::max(0.f,gamma-1.f);
        const float xi=memory_*prior_[bin]+(1.f-memory_)*instantaneous;
        const float wiener=xi/(1.f+xi);
        prior_[bin]=gamma*wiener*wiener;
        // Restore high-SNR onsets without waiting for a prior-frame estimate.
        const float onset=std::clamp((gamma-4.f)/12.f,0.f,1.f);
        const float direct=instantaneous/std::max(1.f,gamma);
        // Apply Wiener as an amplitude gain. Taking its square root made
        // learned-noise attenuation needlessly shallow in noise-dominant bins.
        return wiener+onset*(direct-wiener);
    }
private:
    std::array<float,MaxBins> prior_{};
    float memory_=0;
};
} // namespace armix::builtin_fx::anti_noiz
