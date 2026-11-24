// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include "NativeFxWin32Owner.h"

namespace armix::ui {
// A host must clear JUCE's active-editor registration BEFORE deleting the
// component. JUCE 8.0.15 AudioProcessorEditor::~AudioProcessorEditor asserts
// this contract; DocumentWindow content ownership alone cannot satisfy it.
// Message-thread only; processor lease must outlive this unique owner.
struct HostedAudioEditorDeleter final {
    void operator()(juce::AudioProcessorEditor* editor) const noexcept {
        if(!editor)return;
        editor->processor.editorBeingDeleted(editor);
        delete editor;
    }
};
using HostedAudioEditor=std::unique_ptr<juce::AudioProcessorEditor,HostedAudioEditorDeleter>;
inline HostedAudioEditor createHostedAudioEditor(juce::AudioProcessor& processor) {
    // Never steal an existing editor from another host/window.
    if(processor.getActiveEditor()!=nullptr)return {};
    return HostedAudioEditor(processor.createEditorAndMakeActive());
}

// UI/message-thread only. An HWND owner keeps plugin windows above the mixer
// without making them system-wide topmost or child components of WebView2.
inline bool attachOwnedFloatingWindow(juce::Component& floating,
                                      juce::Component& mixer) noexcept {
#if defined(_WIN32)
    const auto* popupPeer=floating.getPeer();
    const auto* mixerPeer=mixer.getPeer();
    if(popupPeer==nullptr || mixerPeer==nullptr)return false;
    const auto child=popupPeer->getNativeHandle();
    const auto owner=mixerPeer->getNativeHandle();
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
// Called only after the existing JUCE present() sequence. Native HWND
// confirmation makes the success receipt meaningful even after peer recreation.
inline bool verifyOwnedFloatingPresentation(juce::DocumentWindow& editor,
                                              juce::Component& mixer) noexcept {
    if(!editor.isVisible()||editor.isMinimised()||!editor.getContentComponent())
        return false;
#if defined(_WIN32)
    const auto* peer=editor.getPeer();
    const auto* owner=mixer.getPeer();
    if(!peer||!owner)return false;
    return win32::ensureOwnedPopupVisible(peer->getNativeHandle(),
                                          owner->getNativeHandle());
#else
    return editor.isShowing();
#endif
}
} // namespace armix::ui
