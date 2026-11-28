// SPDX-License-Identifier: AGPL-3.0-only
// 30-second native Win32 owner regression, no JUCE/VST/ASIO startup.
#if defined(NDEBUG)
#undef NDEBUG // Release FAT must execute every HWND ownership assertion.
#endif
#if !defined(_WIN32)
#error This HWND test runs only on Windows
#endif
#include "../src/NativeFxWin32Owner.h"
// Interface-first: do not poison unrelated C++ before loading the SDK.
#if defined(near) || defined(far) || defined(min) || defined(max) || defined(_WINDOWS_)
#error Native FX interface leaks Windows SDK macros
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmreg.h>
#include <cassert>
#include <cstdio>

int main(){
    const auto instance=::GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.lpfnWndProc=::DefWindowProcW;
    cls.hInstance=instance;
    cls.lpszClassName=L"ArSonMixOwnedFxRegression";
    assert(::RegisterClassW(&cls)!=0);
    constexpr DWORD style=WS_OVERLAPPEDWINDOW;
    auto mixer=::CreateWindowExW(0,cls.lpszClassName,L"Mixer",style,
                                 100,100,650,470,nullptr,nullptr,instance,nullptr);
    auto popup=::CreateWindowExW(0,cls.lpszClassName,L"Native FX",style,
                                 200,180,420,300,nullptr,nullptr,instance,nullptr);
    auto alternate=::CreateWindowExW(0,cls.lpszClassName,L"Unrelated",style,
                                 240,210,200,140,nullptr,nullptr,instance,nullptr);
    assert(mixer && popup && alternate);
    assert(::GetWindow(popup,GW_OWNER)==nullptr);
    assert(armix::ui::win32::attachOwnedPopup(popup,mixer));
    assert(::GetWindow(popup,GW_OWNER)==mixer);
    armix::ui::win32::preferDarkTitleBar(popup); // graceful if OS DWM unsupported
    // Repeated open has no owner churn, even when hidden and shown again.
    assert(armix::ui::win32::attachOwnedPopup(popup,mixer));
    ::ShowWindow(popup,SW_HIDE);
    ::ShowWindow(popup,SW_SHOWNOACTIVATE);
    assert(armix::ui::win32::attachOwnedPopup(popup,mixer));
    assert(::GetWindow(popup,GW_OWNER)==mixer);
    assert((::GetWindowLongPtrW(popup,GWL_STYLE)&WS_CHILD)==0);
    assert((::GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_TOPMOST)==0);
    assert(::GetAncestor(popup,GA_ROOT)==popup);
    assert(::GetWindow(alternate,GW_OWNER)==nullptr);
    assert(!armix::ui::win32::attachOwnedPopup(popup,popup));
    assert(!armix::ui::win32::attachOwnedPopup(nullptr,mixer));
    assert(::GetWindow(popup,GW_OWNER)==mixer);
    ::ShowWindow(mixer,SW_SHOWNOACTIVATE);
    assert(armix::ui::win32::attachOwnedPopup(alternate,mixer));
    for(int i=0;i<100;++i){
        ::ShowWindow(popup,SW_HIDE);
        assert(!::IsWindowVisible(popup));
        assert(armix::ui::win32::ensureOwnedPopupVisible(alternate,mixer));
        assert(armix::ui::win32::ensureOwnedPopupVisible(popup,mixer));
        assert(::IsWindowVisible(alternate)&&::IsWindowVisible(popup));
        assert(::GetWindow(popup,GW_OWNER)==mixer);
        assert((::GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_TOPMOST)==0);
    }
    // Overlapping sibling HWNDs: activate alternately, preserving geometry,
    // keyboard focus, visibility and owner. The OS may deny foreground on
    // service runners; arrange our own active window and report explicitly.
    ::ShowWindow(mixer,SW_SHOW);
    ::SetForegroundWindow(mixer);
    ::SetActiveWindow(mixer);
    RECT before{};assert(::GetWindowRect(popup,&before));
    const auto foreground=::GetForegroundWindow();
    if(!foreground||::GetAncestor(foreground,GA_ROOTOWNER)==mixer){
        const auto higher=[](HWND first,HWND second){
            for(auto h=::GetWindow(first,GW_HWNDNEXT);h;h=::GetWindow(h,GW_HWNDNEXT))
                if(h==second)return true;
            return false;
        };
        for(int i=0;i<100;++i){
            assert(armix::ui::win32::raiseOwnedPopup(popup,mixer)&&higher(popup,alternate));
            assert(armix::ui::win32::raiseOwnedPopup(alternate,mixer)&&higher(alternate,popup));
        }
        RECT after{};assert(::GetWindowRect(popup,&after)&&::EqualRect(&before,&after));
        assert(::GetForegroundWindow()==foreground);
        assert((::GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_TOPMOST)==0);
        ::EnableWindow(mixer,FALSE);
        assert(!armix::ui::win32::raiseOwnedPopup(popup,mixer));
        ::EnableWindow(mixer,TRUE);
        std::puts("PASS: owned sibling z-order and unchanged geometry/focus");
    }else{
        assert(!armix::ui::win32::raiseOwnedPopup(popup,mixer));
        std::puts("LIMITATION: runner denied foreground; only foreign-app refusal verified");
    }
    // Simulate an owner peer change while a popup is open. The same
    // editor HWND keeps geometry/visibility; no new TOPMOST or focus policy.
    auto newMixer=::CreateWindowExW(0,cls.lpszClassName,L"Replacement mixer",style,
                                    110,110,650,470,nullptr,nullptr,instance,nullptr);
    assert(newMixer);
    ::ShowWindow(newMixer,SW_SHOWNOACTIVATE);
    RECT initial{};assert(::GetWindowRect(popup,&initial));
    const auto beforeOwnerChange=::GetForegroundWindow();
    assert(armix::ui::win32::attachOwnedPopup(popup,newMixer));
    assert(::GetWindow(popup,GW_OWNER)==newMixer);
    RECT afterOwnerChange{};
    assert(::GetWindowRect(popup,&afterOwnerChange)&&::EqualRect(&initial,&afterOwnerChange));
    assert(::IsWindowVisible(popup));
    assert(::GetForegroundWindow()==beforeOwnerChange);
    assert((::GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_TOPMOST)==0);
    ::ShowWindow(popup,SW_HIDE);
    assert(armix::ui::win32::attachOwnedPopup(popup,mixer));
    assert(!::IsWindowVisible(popup)); // ownership repair does not reopen hidden FX
    ::DestroyWindow(newMixer);
    assert(armix::ui::win32::ensureOwnedPopupVisible(popup,mixer));
    std::puts("PASS: popup owner rebinding preserves geometry and hidden state");
    ::ShowWindow(mixer,SW_MINIMIZE);
    assert(!armix::ui::win32::ensureOwnedPopupVisible(popup,mixer));
    ::ShowWindow(mixer,SW_RESTORE);
    assert(armix::ui::win32::ensureOwnedPopupVisible(popup,mixer));
    ::DestroyWindow(popup);
    ::DestroyWindow(alternate);
    ::DestroyWindow(mixer);
    ::UnregisterClassW(cls.lpszClassName,instance);
    std::puts("PASS: FX HWND owned by mixer, reusable after hide, never system TOPMOST");
}
