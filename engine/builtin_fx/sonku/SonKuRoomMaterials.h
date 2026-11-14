// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Independent, parametric acoustic research material: NOT a measured room IR.
// One surface absorption TABLE is used by both 6-boundary image-source ER
// and the late FDN Eyring-style RT60 calibration. Frequencies are *coarse*
// LF (<250Hz), MID (250-4.8kHz) and HF (>4.8kHz); no proprietary assets.
// All sqrt/log/exp occur at prepare(), NEVER in the PCM callback.
namespace armix::sonku::room82 {
enum class Family : std::uint8_t { Studio=0, Chamber=1, Hall=2 };
static constexpr int boundaries=6, bands=3;
using Bands=std::array<float,bands>;
using Surfaces=std::array<Bands,boundaries>;
struct Dimensions {float width,depth,height;};
inline constexpr std::array<Dimensions,3> dimensions{{
    {7.2f,7.2f,3.2f},
    {14.4f,14.4f,4.8f},
    {28.8f,28.8f,7.2f}
}};
// Order: left,right,front,back,floor,ceiling.
// This is INTENSITY ABSORPTION (0..1), not impulse amplitude nor a known
// dearVR preset. Profiles are plausible illustrative materials only.
inline constexpr std::array<Surfaces,3> absorption{{
    {{ // damped studio, realistic shorter MID/HF decay
        {{.16f,.26f,.35f}}, {{.16f,.26f,.35f}},
        {{.16f,.28f,.40f}}, {{.16f,.28f,.40f}},
        {{.14f,.25f,.48f}}, {{.23f,.38f,.54f}}
    }},
    {{ // lightly furnished medium chamber
        {{.22f,.24f,.32f}}, {{.22f,.24f,.32f}},
        {{.22f,.25f,.34f}}, {{.22f,.25f,.34f}},
        {{.20f,.24f,.40f}}, {{.24f,.27f,.43f}}
    }},
    {{ // larger reverberant hall
        {{.19f,.26f,.37f}}, {{.19f,.26f,.37f}},
        {{.18f,.25f,.36f}}, {{.18f,.25f,.36f}},
        {{.20f,.28f,.48f}}, {{.22f,.28f,.50f}}
    }}
}};
constexpr Family familyForWidth(float width) noexcept {
    return width<10.f?Family::Studio:width<20.f?Family::Chamber:Family::Hall;
}
constexpr int index(Family family) noexcept {
    return static_cast<int>(family);
}
constexpr bool validMaterials() noexcept {
    for(const auto& room:absorption)
        for(const auto& surface:room)
            for(float a:surface)
                if(!(a>0.f && a<.95f))return false;
    return true;
}
static_assert(validMaterials());
inline std::array<Surfaces,3> preparePressure() noexcept {
    std::array<Surfaces,3> pressure{};
    for(int room=0;room<3;++room)
        for(int wall=0;wall<boundaries;++wall)
            for(int band=0;band<bands;++band)
                pressure[room][wall][band]=std::sqrt(
                    1.f-absorption[room][wall][band]);
    return pressure;
}
// Eyring energy-decay approximation for a centered rectangular room.
// The 6 surface areas match the image-source wall IDs.
inline Bands eyringRt60(Family family) noexcept {
    constexpr float eyringConstant=.161f; // s*m^-1
    const auto& d=dimensions[index(family)];
    const float side=d.depth*d.height,front=d.width*d.height;
    const float floor=d.width*d.depth;
    const std::array<float,boundaries> area{side,side,front,front,floor,floor};
    const float surface=2.f*(side+front+floor);
    const float volume=d.width*d.depth*d.height;
    Bands seconds{};
    for(int band=0;band<bands;++band) {
        float absorbed=0.f;
        for(int wall=0;wall<boundaries;++wall)
            absorbed+=area[wall]*absorption[index(family)][wall][band];
        const float average=std::clamp(absorbed/surface,.01f,.92f);
        seconds[band]=std::clamp(
            eyringConstant*volume/(-surface*std::log1p(-average)),
            .18f,3.2f);
    }
    return seconds;
}
} // namespace armix::sonku::room82
