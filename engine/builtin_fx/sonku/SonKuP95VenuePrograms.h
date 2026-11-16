// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>

// P9.5 acoustic scene CONTRACT (not proprietary measured BRIR/IR).
// One authoritative table for native validation, room XYZ and LF geometry.
// Existing IDs 0..3 are NEVER renumbered: old .smix loads unchanged.
// New IDs 4..7 are accepted only by explicit opt-in P9.5 builds.
namespace armix::sonku::p95 {
struct Venue {
    float width=0.f,depth=0.f,height=0.f,maxDistance=10.f;
    float rt60Low=0.f,rt60Mid=0.f,rt60High=0.f;
};
inline constexpr std::array<Venue,8> venues{{
  {7.2f,7.2f,3.2f,10.f,0.f,0.f,0.f},     // 0 OFF, dimensions unused
  {7.2f,7.2f,3.2f,3.1f,0.f,0.f,0.f},     // 1 original square
  {14.4f,14.4f,4.8f,6.7f,0.f,0.f,0.f},  // 2 original chamber
  {28.8f,28.8f,7.2f,10.f,0.f,0.f,0.f}, // 3 original large
  {10.f,8.f,4.0f,3.1f,.92f,.78f,.52f}, // 4 LARGE STUDIO
  {22.f,28.f,10.f,10.f,2.05f,1.75f,1.16f}, // 5 CINEMA
  {32.f,36.f,12.f,10.f,2.75f,2.40f,1.55f}, // 6 LIVE VENUE
  {39.f,39.f,16.f,10.f,3.10f,2.85f,1.78f}  // 7 ARENA
}};
inline constexpr bool validScene(int scene) noexcept {
    return scene>=0 && scene<static_cast<int>(venues.size());
}
inline constexpr const Venue& byId(int scene) noexcept {
    return venues[scene>=0&&scene<static_cast<int>(venues.size())?scene:0];
}
static_assert(venues[3].width==28.8f && venues[7].width<=40.f);
} // namespace armix::sonku::p95
