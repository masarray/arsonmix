// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// This is a compile-time catalog of NATIVE effects, not a VST3/FactoryFX manifest.
// Never advertise these as usable in the operator picker until the native PCM
// owner and ACK/snapshot adapter have actually been integrated.
namespace armix::builtin_fx {
inline constexpr std::string_view channelEqId = "armix.builtin.channel-eq.v1";
inline constexpr std::string_view channelCompressorId = "armix.builtin.channel-compressor.v1";
inline constexpr std::string_view vocalThrowId = "armix.builtin.vocal-throw.v1";

enum class FilterKind : std::uint8_t { highPass, lowShelf, bell, highShelf, lowPass };
enum class Slope : std::uint8_t { db12 = 12, db24 = 24 };
struct SlotSpec {
    std::string_view id;
    FilterKind kind;
    float defaultHz;
};
inline constexpr std::array<SlotSpec, 8> channelEqSlots{{
    {"hpf",       FilterKind::highPass, 80.0f},
    {"low-shelf", FilterKind::lowShelf, 160.0f},
    {"peq-1",     FilterKind::bell, 250.0f},
    {"peq-2",     FilterKind::bell, 500.0f},
    {"peq-3",     FilterKind::bell, 1500.0f},
    {"peq-4",     FilterKind::bell, 4500.0f},
    {"high-shelf",FilterKind::highShelf, 9500.0f},
    {"lpf",       FilterKind::lowPass, 18000.0f}
}};
static_assert(channelEqSlots.size() == 8);
} // namespace armix::builtin_fx
