// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace armix::ui {
// UI/message-thread only. A Win32 owner (NOT WS_CHILD parenting and NOT
// WS_EX_TOPMOST) keeps native editors above their mixer while letting other
// applications cover the whole ArSonMix group. Never used in PCM callbacks.
inline bool attachOwnedFloatingWindow(juce::Component& floating,
                                      juce::Component& mixer) noexcept {
#if defined(_WIN32)
    const auto* popupPeer = floating.getPeer();
    const auto* mixerPeer = mixer.getPeer();
    if (popupPeer == nullptr || mixerPeer == nullptr) return false;
    const auto popup = static_cast<HWND>(popupPeer->getNativeHandle());
    const auto owner = static_cast<HWND>(mixerPeer->getNativeHandle());
    if (popup == nullptr || owner == nullptr || popup == owner ||
        !::IsWindow(popup) || !::IsWindow(owner) ||
        (::GetWindowLongPtrW(popup, GWL_STYLE) & WS_CHILD) != 0)
        return false;
    if (::GetWindow(popup, GW_OWNER) == owner) return true;
    // For a top-level window GWLP_HWNDPARENT sets an HWND *owner*, not a
    // child HWND parent. This preserves genuine independent FX windows.
    ::SetLastError(0);
    const auto prior = ::SetWindowLongPtrW(
        popup, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
    if (prior == 0 && ::GetLastError() != 0) return false;
    return ::GetWindow(popup, GW_OWNER) == owner;
#else
    (void) floating;
    (void) mixer;
    return true;  // Other platforms use JUCE normal foreground semantics.
#endif
}
} // namespace armix::ui
