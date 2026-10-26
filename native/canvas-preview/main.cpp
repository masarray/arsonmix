#include "NativeMixerCanvas.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

namespace {
class MixerWindow final : public juce::DocumentWindow
{
public:
    MixerWindow()
        : juce::DocumentWindow ("ArSonMix - Native JUCE Mixer Canvas",
                                juce::Colour::fromRGB (14, 18, 23),
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new armix::native_canvas::MixerCanvas(), true);
        setResizable (true, false);
        setResizeLimits (1200, 680, 2560, 1600);
        centreWithSize (1440, 820);
        setVisible (true);
    }
    void closeButtonPressed() override
    {
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
    }
};
class CanvasPreviewApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ArSonMix Native Mixer Canvas"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise (const juce::String&) override { window = std::make_unique<MixerWindow>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MixerWindow> window;
};
}
START_JUCE_APPLICATION (CanvasPreviewApplication)
