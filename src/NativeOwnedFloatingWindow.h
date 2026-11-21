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
// UI thread only. Preserve a reachable editor on surviving displays; an
// explicit FX Rack double-click always centres the SAME window in the mixer
// work area. Screen coordinates are JUCE logical pixels (per-monitor DPI).
// This moves a top-level peer; it never reparents WebView2 or touches PCM.
inline void placeFloatingEditor(juce::Component& editor,
                               juce::Component& mixer,
                               bool centreOnMixer) {
    const auto& displays=juce::Desktop::getInstance().getDisplays();
    const auto current=editor.getScreenBounds();
    if(!centreOnMixer) {
        if(const auto* display=displays.getDisplayForRect(current)) {
            const auto visible=current.getIntersection(display->userArea);
            if(visible.getWidth()>=juce::jmin(current.getWidth(),96) &&
               visible.getHeight()>=juce::jmin(current.getHeight(),48))
                return; // Preserve a reachable user placement on either monitor.
        }
    }
    const auto mixerBounds=mixer.getScreenBounds();
    const auto* display=displays.getDisplayForRect(mixerBounds);
    if(display==nullptr)display=displays.getPrimaryDisplay();
    if(display==nullptr)return;
    const auto area=display->userArea;
    const auto x=juce::jlimit(area.getX(),
        juce::jmax(area.getX(),area.getRight()-editor.getWidth()),
        mixerBounds.getCentreX()-editor.getWidth()/2);
    const auto y=juce::jlimit(area.getY(),
        juce::jmax(area.getY(),area.getBottom()-editor.getHeight()),
        mixerBounds.getCentreY()-editor.getHeight()/2);
    editor.setTopLeftPosition(x,y);
}
} // namespace armix::ui
