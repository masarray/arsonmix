// SPDX-License-Identifier: AGPL-3.0-only
// P1 native Windows first-sound host: mic -> one VST3 -> master output.
// The desktop HTML remains a presentation layer, never the audio callback.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include "LiveAudioEngine.h"
#include <cstring>
#include <optional>

namespace {
std::optional<juce::WebBrowserComponent::Resource> assetFor (const juce::String& path)
{
    const char* data = nullptr;
    int size = 0;
    juce::String mime;
    if (path == "/" || path == "/index.html")
        { data = BinaryData::index_html; size = BinaryData::index_htmlSize; mime = "text/html"; }
    else if (path == "/styles.css")
        { data = BinaryData::styles_css; size = BinaryData::styles_cssSize; mime = "text/css"; }
    else if (path == "/app.js")
        { data = BinaryData::app_js; size = BinaryData::app_jsSize; mime = "text/javascript"; }
    else if (path == "/native-bridge.js")
        { data = BinaryData::native_bridge_js; size = BinaryData::native_bridge_jsSize; mime = "text/javascript"; }
    else
        return std::nullopt;

    juce::WebBrowserComponent::Resource result;
    result.data.resize (static_cast<size_t> (size));
    std::memcpy (result.data.data(), data, static_cast<size_t> (size));
    result.mimeType = mime;
    return result;
}

class Browser final : public juce::WebBrowserComponent
{
public:
    explicit Browser (Options opts) : juce::WebBrowserComponent (std::move (opts)) {}
    bool ready = false;
    void pageFinishedLoading (const juce::String&) override { ready = true; }
};

class ConsoleWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    ConsoleWindow()
        : juce::DocumentWindow ("AR MIX Studio — P1 FIRST SOUND",
                                juce::Colour (0xff101921), allButtons),
          browser_ (juce::WebBrowserComponent::Options{}
              .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
              .withWinWebView2Options (
                  juce::WebBrowserComponent::Options::WinWebView2{}
                      .withUserDataFolder (
                          juce::File::getSpecialLocation (
                              juce::File::userApplicationDataDirectory)
                              .getChildFile ("AR-MIX-Studio-WebView2")))
              .withNativeIntegrationEnabled()
              .withEventListener ("arMixParameter", [this] (juce::var args) { onParameter (args); })
              .withEventListener ("arMixAction", [this] (juce::var args) { onAction (args); })
              .withResourceProvider (assetFor))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (1100, 650, 3840, 2160);
        setContentNonOwned (&browser_, false);
        centreWithSize (1650, 930);
        setVisible (true);
        startupError_ = engine_.start();
        browser_.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        startTimerHz (15);
    }

    ~ConsoleWindow() override
    {
        stopTimer();
        browser_.setVisible (false);
        engine_.stop();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    // Engine is constructed before and destroyed after Browser.
    LiveAudioEngine engine_;
    Browser browser_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String startupError_;

    void onParameter (const juce::var& args)
    {
        // Events from WebView2 arrive on the message thread, not RT.
        const auto name = args.getProperty ("parameter", {}).toString();
        const auto channel = static_cast<int> (args.getProperty ("channel", -2));
        const auto value = args.getProperty ("value", {});
        if (name == "masterGain" && value.isDouble())
            engine_.setMasterGainDb (static_cast<float> (double (value)));
        else if (name == "masterGain" && value.isInt())
            engine_.setMasterGainDb (static_cast<float> (int (value)));
        else if (name == "masterMute" && value.isBool() && bool (value))
            engine_.setMonitorEnabled (false);
        // P1 explicitly ignores channels 2-12, demo FX chain, and generic FX
        // parameters. Future typed graph routing will map their IDs.
        juce::ignoreUnused (channel);
    }

    void onAction (const juce::var& args)
    {
        const auto action = args.getProperty ("action", {}).toString();
        if (action == "audioSetup")
        {
            juce::DialogWindow::LaunchOptions opt;
            opt.dialogTitle = "AR MIX — Windows Audio Device Setup";
            opt.dialogBackgroundColour = juce::Colour (0xff14212a);
            auto* selector = new juce::AudioDeviceSelectorComponent (
                engine_.devices(), 1, 2, 1, 2, false, false, false, false);
            selector->setSize (650, 520);
            opt.content.setOwned (selector);
            opt.useNativeTitleBar = true;
            opt.resizable = true;
            opt.launchAsync();
        }
        else if (action == "chooseVst3")
        {
            chooser_ = std::make_unique<juce::FileChooser> (
                "Load VST3 effect", juce::File{}, "*.vst3", true);
            chooser_->launchAsync (
                juce::FileBrowserComponent::openMode |
                    juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& chooser)
                {
                    const auto file = chooser.getResult();
                    if (file == juce::File{})
                        return;
                    const auto error = engine_.loadVst3 (file);
                    if (error.isNotEmpty())
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::MessageBoxIconType::WarningIcon, "VST3 Load", error);
                });
        }
        else if (action == "unloadVst3")
            engine_.unloadPlugin();
        else if (action == "monitor" && args.getProperty ("value", {}).isBool())
            engine_.setMonitorEnabled (bool (args.getProperty ("value", false)));
        else if (action == "bypass" && args.getProperty ("value", {}).isBool())
            engine_.setBypassed (bool (args.getProperty ("value", false)));
    }

    void timerCallback() override
    {
        if (! browser_.ready || ! browser_.isVisible())
            return;
        auto state = engine_.status();
        if (startupError_.isNotEmpty())
            state.getDynamicObject()->setProperty ("error", startupError_);
        browser_.emitEventIfBrowserIsVisible ("arMixStatus", state);
    }
};

class ConsoleApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "AR MIX Studio"; }
    const juce::String getApplicationVersion() override { return "0.4.0-p1"; }
    void initialise (const juce::String&) override { window_ = std::make_unique<ConsoleWindow>(); }
    void shutdown() override { window_.reset(); }
private:
    std::unique_ptr<ConsoleWindow> window_;
};
} // namespace

START_JUCE_APPLICATION (ConsoleApp)
