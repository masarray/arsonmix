// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>

// Production render ownership, never an audio engine or a second callback.
// The approved GUI can project ONE native engine. Until the full ChannelId
// VST/editor/Show cutover is admitted, only the existing M4 route owns PCM.
// The legacy stereo path is diagnostic-only, never a hidden fallback.
namespace armix::mixer {
enum class NativeRenderOwner : std::uint8_t {
    failSilentUnpatched,
    acceptedM4Session,
    isolatedDiagnostic
};

struct NativeRenderAuthority final {
    static constexpr NativeRenderOwner select(
        bool normalAudio, bool m4SessionActive) noexcept {
        // A verified accepted M4 route always owns the callback: a diagnostic
        // mode must never start processing a second stereo FX chain over it.
        if(m4SessionActive)return NativeRenderOwner::acceptedM4Session;
        if(normalAudio)return NativeRenderOwner::failSilentUnpatched;
        return NativeRenderOwner::isolatedDiagnostic;
    }
    static constexpr bool isProduction(NativeRenderOwner owner) noexcept {
        return owner==NativeRenderOwner::acceptedM4Session;
    }
    static constexpr bool mayRunDiagnostic(NativeRenderOwner owner) noexcept {
        return owner==NativeRenderOwner::isolatedDiagnostic;
    }
};
static_assert(NativeRenderAuthority::select(true,false)==
              NativeRenderOwner::failSilentUnpatched);
static_assert(NativeRenderAuthority::select(true,true)==
              NativeRenderOwner::acceptedM4Session);
static_assert(NativeRenderAuthority::select(false,true)==
              NativeRenderOwner::acceptedM4Session);
static_assert(NativeRenderAuthority::select(false,false)==
              NativeRenderOwner::isolatedDiagnostic);
} // namespace armix::mixer
