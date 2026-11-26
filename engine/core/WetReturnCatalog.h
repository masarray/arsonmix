// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <string_view>
namespace armix::mixer {
// Document/control identity only. Installed PCM additionally requires the
// actual FxInstance's immutable bypassToSilence contract.
inline constexpr bool isWetReturnCatalog(std::string_view id) noexcept {
    return id=="armix.builtin.vocal-reverb-return.v1" ||
           id=="armix.builtin.vocal-delay-return.v1";
}
}
