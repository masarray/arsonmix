// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#if defined(_WIN32)
// Do not leak Win32 min/max or obsolete segmented-pointer macros into
// JUCE/DSP headers included after this adapter.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

namespace armix::ui::win32 {
// Native Windows caption colour is OS-owned. Prefer its supported dark
// presentation to match the mixer; unsupported Windows versions are benign.
// The plugin remains an ordinary non-topmost document window.
inline void preferDarkTitleBar(HWND hwnd) noexcept {
    if(!hwnd || !::IsWindow(hwnd))return;
    const BOOL dark=TRUE;
    constexpr DWORD modernAttribute=20; // DWMWA_USE_IMMERSIVE_DARK_MODE
    const auto hr=::DwmSetWindowAttribute(hwnd,modernAttribute,&dark,sizeof(dark));
    if(FAILED(hr)) {
        constexpr DWORD olderAttribute=19; // Earlier Windows 10 preview API
        (void)::DwmSetWindowAttribute(hwnd,olderAttribute,&dark,sizeof(dark));
    }
}
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
// A JUCE visible flag alone is not a Windows presentation receipt.
// Recover an OS-hidden owned popup only while its mixer is actually visible.
// No foreground stealing, TOPMOST, owner change or audio work.
inline bool ensureOwnedPopupVisible(HWND popup, HWND mixer) noexcept {
    if(!popup||!mixer||!::IsWindow(popup)||!::IsWindow(mixer)||
       !::IsWindowVisible(mixer)||::IsIconic(mixer)||
       !attachOwnedPopup(popup,mixer))return false;
    if(::IsIconic(popup))::ShowWindow(popup,SW_RESTORE);
    if(!::IsWindowVisible(popup))::ShowWindow(popup,SW_SHOWNOACTIVATE);
    return ::IsWindowVisible(popup)&&!::IsIconic(popup)&&
           ::GetWindow(popup,GW_OWNER)==mixer;
}
} // namespace armix::ui::win32
#endif
