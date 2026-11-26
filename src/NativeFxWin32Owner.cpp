// SPDX-License-Identifier: AGPL-3.0-only
#include "NativeFxWin32Owner.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

// Keep every Win32 declaration and SDK macro in this implementation TU.
// Never undefine near/far: later SDK multimedia headers require them.
namespace armix::ui::win32 {
// Native Windows caption colour is OS-owned. Prefer its supported dark
// presentation to match the mixer; unsupported Windows versions are benign.
// The plugin remains an ordinary non-topmost document window.
void preferDarkTitleBar(void* nativeWindow) noexcept {
    const auto hwnd=static_cast<HWND>(nativeWindow);
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
bool attachOwnedPopup(void* nativePopup, void* nativeOwner) noexcept {
    const auto popup=static_cast<HWND>(nativePopup);
    const auto owner=static_cast<HWND>(nativeOwner);
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
bool ensureOwnedPopupVisible(void* nativePopup, void* nativeMixer) noexcept {
    const auto popup=static_cast<HWND>(nativePopup);
    const auto mixer=static_cast<HWND>(nativeMixer);
    if(!popup||!mixer||!::IsWindow(popup)||!::IsWindow(mixer)||
       !::IsWindowVisible(mixer)||::IsIconic(mixer)||
       !attachOwnedPopup(popup,mixer))return false;
    if(::IsIconic(popup))::ShowWindow(popup,SW_RESTORE);
    if(!::IsWindowVisible(popup))::ShowWindow(popup,SW_SHOWNOACTIVATE);
    return ::IsWindowVisible(popup)&&!::IsIconic(popup)&&
           ::GetWindow(popup,GW_OWNER)==mixer;
}
// Trusted UI activation only. Refuse stale requests after app switch or
// while the native owner is disabled by a modal. HWND_TOP is non-topmost;
// NOACTIVATE keeps keyboard input in the original vendor/WebView control.
bool raiseOwnedPopup(void* nativePopup, void* nativeMixer) noexcept {
    const auto popup=static_cast<HWND>(nativePopup);
    const auto mixer=static_cast<HWND>(nativeMixer);
    if(!popup||!mixer||!::IsWindow(popup)||!::IsWindow(mixer)||
       ::GetWindow(popup,GW_OWNER)!=mixer||!::IsWindowVisible(popup)||
       !::IsWindowVisible(mixer)||::IsIconic(mixer)||::IsIconic(popup)||
       !::IsWindowEnabled(mixer)||!::IsWindowEnabled(popup))return false;
    const auto foreground=::GetForegroundWindow();
    if(foreground&&::GetAncestor(foreground,GA_ROOTOWNER)!=mixer)return false;
    return ::SetWindowPos(popup,HWND_TOP,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER)!=FALSE;
}
} // namespace armix::ui::win32
#endif
