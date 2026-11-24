// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
namespace armix::harmony {
// A narrow-space double may be almost opposite in phase to its lead.
// Estimate the signed projection of the effective wet stem onto the lead
// over 15ms. Reduce only the negatively coherent double, never dry or
// polarity. At Space=1 the original double path is exactly unchanged.
class DoublerMonoGuard final {
public:
    void prepare(double rate) noexcept {
        evidence_=static_cast<float>(1.-std::exp(-1./(rate*.015)));
        attack_=static_cast<float>(1.-std::exp(-1./(rate*.008)));
        release_=static_cast<float>(1.-std::exp(-1./(rate*.060)));
        energy_=cross_=0.f;gain_=1.f;
    }
    float process(float dry,float backing,float space,float modeBlend) noexcept {
        energy_+=evidence_*(dry*dry-energy_);
        cross_+=evidence_*(dry*backing-cross_);
        // Permit at most a -0.5 projection on the dry axis once settled.
        // This is an averaged broadband estimate, not a per-frequency
        // cancellation guarantee; do not use instant sample division.
        const float target=energy_>1.e-7f&&cross_<-.5f*energy_?
            std::clamp(.5f*energy_/-cross_,0.f,1.f):1.f;
        gain_+=(target-gain_)*(target<gain_?attack_:release_);
        const float narrow=1.f-std::clamp(space,0.f,1.f);
        return 1.f-(1.f-gain_)*narrow*narrow*modeBlend;
    }
private:
    float evidence_=0.f,attack_=0.f,release_=0.f,energy_=0.f,cross_=0.f,gain_=1.f;
};
} // namespace armix::harmony
