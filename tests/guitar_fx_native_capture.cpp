// SPDX-License-Identifier: AGPL-3.0-only
// Windows JUCE NATIVE window capture: never HTML, browser, CSS, or a fake meter.
// This target is opt-in, not built into the shipping mixer.
#include "../engine/builtin_fx/GuitarRigEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>
#include <array>
#include <cstdio>
int main(int argc,char** argv) {
    if(argc!=2) {
        std::fprintf(stderr,"usage: GuitarEditorCapture <output-directory>\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceGui;
    armix::builtin_fx::GuitarRigProcessor processor;
    processor.prepareToPlay(48000.0,512);
    armix::builtin_fx::GuitarRigEditor editor(processor);
    juce::DocumentWindow window("ArSonMix Guitar FX / native JUCE evidence",
        juce::Colour::fromRGB(22,24,26),juce::DocumentWindow::closeButton);
    window.setUsingNativeTitleBar(true);
    window.setContentNonOwned(&editor,true);
    window.setTopLeftPosition(40,40);
    window.setVisible(true);
    // JUCE Button::triggerClick is asynchronous. A snapshot taken before the
    // MessageManager pumps would silently capture the SAME selected device
    // for every requested focus and incorrectly mark all images "successful".
    // This previously happened; never regress to static-identical screenshots.
    auto flushGui=[] {
        auto* mm=juce::MessageManager::getInstance();
        return mm!=nullptr && mm->runDispatchLoopUntil(120);
    };
    if(!flushGui())return 13;
    if(window.getPeer()==nullptr || editor.getPeer()==nullptr) {
        std::fprintf(stderr,"NATIVE_WINDOW_PEER_MISSING: screenshot is not qualified\n");
        return 3;
    }
    const juce::File folder(juce::String::fromUTF8(argv[1]));
    if(!folder.createDirectory().wasOk())return 4;
    juce::PNGImageFormat encoder;
    const auto save=[&](const char* label) {
        // Captures the actual attached JUCE component hierarchy, including
        // bitmap hardware, native sliders, TextButtons and Label. No HTML.
        const auto image=editor.createComponentSnapshot(
            editor.getLocalBounds(),true,1.0f);
        if(!image.isValid())return false;
        juce::FileOutputStream stream(folder.getChildFile(
            juce::String::fromUTF8(label)+".png"));
        if(!stream.openedOk())return false;
        return encoder.writeImageToStream(image,stream);
    };
    const auto click=[&](const juce::String& name) {
        for(int i=0;i<editor.getNumChildComponents();++i) {
            if(auto* button=dynamic_cast<juce::TextButton*>(
                editor.getChildComponent(i))) {
                if(button->getButtonText()==name) {
                    button->triggerClick();
                    return flushGui();
                }
            }
        }
        return false;
    };
    const auto screenshot=[&](const char* name) {
        if(!flushGui()||!save(name)) {
            std::fprintf(stderr,"NATIVE_SNAPSHOT_FAILED: %s\n",name);
            return false;
        }
        std::printf("NATIVE_JUCE_WINDOW_CAPTURE=%s\n",name);
        return true;
    };
    for(const auto& entry:std::array<std::pair<const char*,const char*>,4>{{
        {"GATE","01-gate"},{"OVERDRIVE","02-overdrive"},
        {"AMPLIFIER","03-amplifier"},{"CABINET","04-cabinet"}}}) {
        if(!click(entry.first)||!screenshot(entry.second))return 5;
    }
    if(!click("GATE")||!screenshot("05-gate-before-toggle"))return 6;
    bool switched=false;
    for(int i=0;i<editor.getNumChildComponents();++i) {
        if(auto* button=dynamic_cast<juce::TextButton*>(
                editor.getChildComponent(i))) {
            if(button->getTooltip()=="Toggle GATE") {
                button->triggerClick();
                switched=flushGui();
                break;
            }
        }
    }
    if(!switched||!screenshot("06-gate-after-toggle"))return 7;
    if(!click("AMPLIFIER"))return 8;
    editor.setSize(900,545);
    if(!screenshot("07-amplifier-900x545"))return 9;
    editor.setSize(1484,896);
    if(!screenshot("08-amplifier-1484x896"))return 10;
    // A/B is the real native bank switch, not an image-only cosmetic change.
    if(!click("B")||!screenshot("09-bank-b"))return 11;
    editor.setSize(1060,640);
    if(!click("A")||!screenshot("10-bank-a-request-queued"))return 12;
    window.setVisible(false);
    std::puts("NATIVE_JUCE_WINDOW_CAPTURE_SET=PASS (requires different-pixel proof)");
    return 0;
}
