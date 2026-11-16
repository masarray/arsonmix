// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "NativeFxWin32Owner.h"

namespace armix::ui {
// UI/message-thread only. An HWND owner keeps plugin windows above the mixer
// without making them system-wide topmost or child components of WebView2.
inline bool attachOwnedFloatingWindow(juce::Component& floating,
                                      juce::Component& mixer) noexcept {
#if defined(_WIN32)
    const auto* popupPeer=floating.getPeer();
    const auto* mixerPeer=mixer.getPeer();
    if(popupPeer==nullptr || mixerPeer==nullptr)return false;
    const auto child=static_cast<HWND>(popupPeer->getNativeHandle());
    const auto owner=static_cast<HWND>(mixerPeer->getNativeHandle());
    const bool attached=win32::attachOwnedPopup(child,owner);
    win32::preferDarkTitleBar(child);
    return attached;
#else
    (void)floating;
    (void)mixer;
    return true; // Native non-Windows foreground policy remains JUCE-owned.
#endif
}
} // namespace armix::ui
