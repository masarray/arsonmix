// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Fixed-profile magnitude suppression; not a source classifier.
struct ManualSpectral final {
    static float target(float evidence,float inverseNoiseScale,float floor,float protection) noexcept {
        const float gamma=std::max(1.f,evidence*inverseNoiseScale);
        const float estimate=std::sqrt(std::max(0.f,1.f-1.f/gamma));
        const float support=std::clamp((evidence-1.5f)/6.f,0.f,1.f);
        return 1.f-(1.f-floor)*(1.f-estimate)*(1.f-protection*support);
    }
    static float smooth(float local,float left,float right,float artifact) noexcept {
        return local+artifact*std::max(0.f,.25f*left+.5f*local+.25f*right-local);
    }
};
}
