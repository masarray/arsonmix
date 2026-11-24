// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#if defined(_WIN32)
namespace armix::ui::win32 {
// Opaque native handles match JUCE ComponentPeer::getNativeHandle().
// The implementation owns all Windows SDK headers in a separate TU;
// including this interface must never define or undefine SDK macros.
// UI/message-thread only. No ownership of the passed HWNDs.
void preferDarkTitleBar(void* nativeWindow) noexcept;
bool attachOwnedPopup(void* nativePopup, void* nativeOwner) noexcept;
bool ensureOwnedPopupVisible(void* nativePopup, void* nativeMixer) noexcept;
} // namespace armix::ui::win32
#endif
