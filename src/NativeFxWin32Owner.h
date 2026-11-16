// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#if defined(_WIN32)
#include <windows.h>

namespace armix::ui::win32 {
// UI thread only. GWLP_HWNDPARENT means owner for top-level WS_POPUP/
// WS_OVERLAPPED windows. Do NOT SetParent(), create WS_CHILD, or set TOPMOST.
inline bool attachOwnedPopup(HWND popup, HWND owner) noexcept {
    if (popup == nullptr || owner == nullptr || popup == owner ||
        !::IsWindow(popup) || !::IsWindow(owner) ||
        (::GetWindowLongPtrW(popup,GWL_STYLE) & WS_CHILD) != 0)
        return false;
    if (::GetWindow(popup,GW_OWNER)==owner) return true;
    ::SetLastError(0);
    const auto previous=::SetWindowLongPtrW(
        popup,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(owner));
    if (previous==0 && ::GetLastError()!=0) return false;
    return ::GetWindow(popup,GW_OWNER)==owner;
}
} // namespace armix::ui::win32
#endif
