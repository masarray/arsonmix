// SPDX-License-Identifier: AGPL-3.0-only
// Genuine JUCE software-rendered Graphic EQ 31 fixture; this test uses the
// SAME compiled BinaryData as the installed Windows mixer (not Chromium).
// Runs unattended; never opens ASIO, speakers, files or native popups.
#include "../engine/builtin_fx/GraphicEq31Processor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>
#include <cstdlib>
#include <memory>

#define RENDER_CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr,"GEQ31_NATIVE_RENDER_FAIL: %s at %s:%d\\n", \
                     #expression,__FILE__,__LINE__); \
        return 1; \
    } \
} while (false)

// Band sliders live inside Viewport -> bandSurface_, not directly under
// the AudioProcessorEditor. A one-level findChildWithID() can never see them.
static juce::Component* findDescendantWithID(
    juce::Component& parent,const juce::String& id) {
    for(auto* child:parent.getChildren()) {
        if(child->getComponentID()==id)return child;
        if(auto* found=findDescendantWithID(*child,id))return found;
    }
    return nullptr;
}

static int indexY(float authoredY) {
    return 55+juce::roundToInt(authoredY*229.f/2042.f);
}

static int countLabelInk(const juce::Image& image,int band) {
    int dark=0;
    const int left=45+band*40+4;
    // Captions must be below the SVG end-stop (y=~245) and within
    // the original 229-px, 40-px-pitch fader viewport.
    for(int y=251;y<282;++y) for(int x=left;x<left+32;++x) {
        const auto p=image.getPixelAt(x,y);
        if(p.getRed()<116&&p.getGreen()<116&&p.getBlue()<116)
            ++dark;
    }
    return dark;
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    armix::builtin_fx::GraphicEq31Processor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    RENDER_CHECK(editor!=nullptr);
    RENDER_CHECK(editor->getWidth()==1430);
    RENDER_CHECK(editor->getHeight()==305);
    const auto shot=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.f);
    RENDER_CHECK(shot.isValid());
    RENDER_CHECK(shot.getWidth()==1430 && shot.getHeight()==305);
    int lowInk=0;
    for(int band=0;band<31;++band) {
        const auto ink=countLabelInk(shot,band);
        if(ink<10)++lowInk;
        std::printf("GEQ31_NATIVE_LABEL band=%d dark_pixels=%d\\n",band,ink);
    }
    // Every frequency caption (20 Hz ... 20 kHz) must be visible in
    // its own allocated cell, independently of missing local Inter fonts.
    RENDER_CHECK(lowInk==0);
    const int middleX=45+20;
    const auto ridge0=shot.getPixelAt(middleX,indexY(916.5f));
    std::printf("GEQ31_NATIVE_RIDGE y=%d rgb=%u,%u,%u\\n",
        indexY(916.5f),ridge0.getRed(),ridge0.getGreen(),ridge0.getBlue());
    RENDER_CHECK(ridge0.getRed()>220 && ridge0.getGreen()>220 &&
                 ridge0.getBlue()>220);

    // The real JUCE component must track both mechanical endstops while
    // keeping the white index optically aligned with the +12/0/-12 ticks.
    const auto slider=findDescendantWithID(*editor,"geq31-band-0");
    RENDER_CHECK(slider!=nullptr);
    auto* band=dynamic_cast<juce::Slider*>(slider);
    RENDER_CHECK(band!=nullptr);
    for(const auto value:{12.,-12.,0.}) {
        band->setValue(value,juce::sendNotificationSync);
        const auto newShot=editor->createComponentSnapshot(
            editor->getLocalBounds(),true,1.f);
        const auto y=indexY(value>0.?208.75f:value<0.?1627.92f:916.5f);
        const auto line=newShot.getPixelAt(middleX,y);
        RENDER_CHECK(line.getRed()>220 && line.getGreen()>220 &&
                     line.getBlue()>220);
    }
    std::puts("GEQ31_NATIVE_RENDER_PASS: 31 frequency captions, 0dB and +/-12dB ridges");
    return 0;
}
