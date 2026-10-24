// SPDX-License-Identifier: AGPL-3.0-only
// AR MIX Studio UI integration prototype. No sound processing is implemented.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include <BinaryData.h>
#include <cstring>

namespace
{
std::optional<juce::WebBrowserComponent::Resource> assetFor(const juce::String& path)
{
    int size = 0;
    const char* data = nullptr;
    juce::String mime;
    if (path == "/" || path == "/index.html")
    {
        data = BinaryData::index_html; size = BinaryData::index_htmlSize; mime = "text/html";
    }
    else if (path == "/styles.css")
    {
        data = BinaryData::styles_css; size = BinaryData::styles_cssSize; mime = "text/css";
    }
    else if (path == "/app.js")
    {
        data = BinaryData::app_js; size = BinaryData::app_jsSize; mime = "text/javascript";
    }
    else return std::nullopt;

    juce::WebBrowserComponent::Resource result;
    result.data.resize(static_cast<size_t>(size));
    std::memcpy(result.data.data(), data, static_cast<size_t>(size));
    result.mimeType = mime;
    return result;
}

class ConsoleWindow final : public juce::DocumentWindow
{
public:
    ConsoleWindow()
        : juce::DocumentWindow("AR MIX Studio — FX Host UI prototype",
                               juce::Colour(0xff101921),
                               juce::DocumentWindow::allButtons),
          browser(juce::WebBrowserComponent::Options{}
                      .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
                      .withWinWebView2Options(
                          juce::WebBrowserComponent::Options::WinWebView2{}
                            .withUserDataFolder(juce::File::getSpecialLocation(
                                juce::File::userApplicationDataDirectory)
                                .getChildFile("AR-MIX-Studio-WebView2")))
                      .withNativeIntegrationEnabled()
                      .withEventListener("arMixParameter", [](juce::var args)
                      {
                          // This is the JUCE message thread, NOT the audio callback.
                          // Forward validated state changes through a lock-free queue
                          // when a real audio engine is added.
                          DBG("AR MIX UI event: " + juce::JSON::toString(args));
                      })
                      .withResourceProvider(assetFor))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setResizeLimits(1100, 650, 3840, 2160);
        setContentNonOwned(&browser, false);
        centreWithSize(1650, 930);
        setVisible(true);
        browser.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
private:
    juce::WebBrowserComponent browser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConsoleWindow)
};

class ConsoleApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "AR MIX Console"; }
    const juce::String getApplicationVersion() override { return "0.3.0"; }
    void initialise(const juce::String&) override { window = std::make_unique<ConsoleWindow>(); }
    void shutdown() override { window.reset(); }
private:
    std::unique_ptr<ConsoleWindow> window;
};
} // namespace
START_JUCE_APPLICATION(ConsoleApp)
