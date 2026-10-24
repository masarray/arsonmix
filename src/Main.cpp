// SPDX-License-Identifier: AGPL-3.0-only
// Native VST3 FX catalog, plugin rack and audio-device bridge.
// Audio runs exclusively in JUCE AudioIODeviceCallback; WebView is control only.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include "LiveAudioEngine.h"
#include "PluginCatalog.h"
#include <cstring>
#include <optional>
#include <memory>

namespace
{
juce::File settingsDirectory()
{
    return juce::File::getSpecialLocation (
        juce::File::userApplicationDataDirectory).getChildFile ("AR-MIX-Studio");
}

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
    else if (path == "/bridge.js")
        { data = BinaryData::bridge_js; size = BinaryData::bridge_jsSize; mime = "text/javascript"; }
    else return std::nullopt;

    juce::WebBrowserComponent::Resource resource;
    resource.data.resize (static_cast<size_t> (size));
    std::memcpy (resource.data.data(), data, static_cast<size_t> (size));
    resource.mimeType = mime;
    return resource;
}

class Browser final : public juce::WebBrowserComponent
{
public:
    explicit Browser (Options options) : WebBrowserComponent (std::move (options)) {}
    bool loaded = false;
    void pageFinishedLoading (const juce::String&) override { loaded = true; }
};

class PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow (const juce::String& title, juce::AudioProcessorEditor* editor)
        : DocumentWindow (title, juce::Colour (0xff14212a), allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (true, true);
        centreWithSize (juce::jmax (380, editor->getWidth()), juce::jmax (300, editor->getHeight()));
        setVisible (true);
    }
    void closeButtonPressed() override { setVisible (false); }
};

class ConsoleWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    ConsoleWindow()
        : DocumentWindow ("AR MIX Studio — Native VST3 FX Host",
                         juce::Colour (0xff101921), allButtons),
          catalog_ (settingsDirectory()),
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
        settingsDirectory().createDirectory();
        catalog_.onChanged = [this] { emitCatalog(); };
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (1100, 650, 3840, 2160);
        setContentNonOwned (&browser_, false);
        centreWithSize (1650, 930);
        setVisible (true);

        auto saved = juce::XmlDocument::parse (settingsDirectory().getChildFile ("audio-devices.xml"));
        startupError_ = engine_.start (saved.get());
        browser_.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        startTimerHz (12);
        if (catalog_.entries().empty())
            catalog_.scanDefaults();
    }

    ~ConsoleWindow() override
    {
        stopTimer();
        catalog_.onChanged = nullptr;
        editorWindow_.reset();
        engine_.stop();
        browser_.setVisible (false);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    LiveAudioEngine engine_;
    PluginCatalog catalog_;
    Browser browser_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<PluginEditorWindow> editorWindow_;
    juce::String startupError_, lastAudioXml_;

    void emitNotice (const juce::String& message, bool error = false)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("text", message);
        o->setProperty ("error", error);
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixNotice", juce::var (o.release()));
    }

    void emitCatalog()
    {
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixCatalog", catalog_.snapshot());
    }

    void emitChain()
    {
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixChain", engine_.chainState());
    }

    void onParameter (const juce::var& args)
    {
        const auto name = args.getProperty ("parameter", {}).toString();
        const int channel = static_cast<int> (args.getProperty ("channel", -2));
        const auto value = args.getProperty ("value", {});
        const auto valueNumber = static_cast<float> (static_cast<double> (value));
        if (name == "masterGain" && (value.isDouble() || value.isInt()))
            engine_.setMasterGainDb (valueNumber);
        else if (name == "faderDb" && channel == 1 && (value.isDouble() || value.isInt()))
            engine_.setChannelGainDb (valueNumber);
        else if (name == "mute" && channel == 1 && value.isBool())
            engine_.setChannelMuted (bool (value));
    }

    void showDeviceSetup()
    {
        juce::DialogWindow::LaunchOptions opts;
        opts.dialogTitle = "AR MIX — ASIO / WASAPI Audio Setup";
        opts.dialogBackgroundColour = juce::Colour (0xff14212a);
        auto* selector = new juce::AudioDeviceSelectorComponent (
            engine_.devices(), 1, 2, 1, 2, false, false, false, false);
        selector->setSize (720, 560);
        opts.content.setOwned (selector);
        opts.useNativeTitleBar = true;
        opts.resizable = true;
        opts.launchAsync();
    }

    void chooseImport()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Import an individual VST3 module or bundle", juce::File{}, "*", true);
        chooser_->launchAsync (
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& f)
            {
                const auto file = f.getResult();
                if (file == juce::File{}) return;
                if (file.hasFileExtension (".dll"))
                {
                    emitNotice ("Legacy VST2 DLL cannot be imported as VST3. Select a .vst3 module.", true);
                    return;
                }
                catalog_.importVst3 (file);
                emitCatalog();
            });
    }

    void chooseFolder()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Add a VST3 plugin folder to your catalog", juce::File{}, "*", true);
        chooser_->launchAsync (
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& f)
            {
                const auto folder = f.getResult();
                if (folder != juce::File{})
                    catalog_.addDirectory (folder);
            });
    }

    void onAction (const juce::var& args)
    {
        const auto action = args.getProperty ("action", {}).toString();
        const auto value = args.getProperty ("value", {});
        const int slot = static_cast<int> (args.getProperty ("slot", -1));

        if (action == "audioSetup") showDeviceSetup();
        else if (action == "scanVst3") catalog_.scanDefaults();
        else if (action == "addScanFolder") chooseFolder();
        else if (action == "importVst3") chooseImport();
        else if (action == "addCatalog")
        {
            const auto* plugin = catalog_.find (args.getProperty ("id", {}).toString());
            if (!plugin)
                return emitNotice ("Plugin not found in catalog. Rescan your VST3 folders.", true);
            editorWindow_.reset();
            const auto error = engine_.addVst3 (plugin->path, plugin->uid, plugin->key());
            if (error.isNotEmpty()) emitNotice (error, true);
            else { emitChain(); emitNotice (plugin->name + " inserted into the LIVE FX chain."); }
        }
        else if (action == "fxRemove")
        {
            editorWindow_.reset();
            if (engine_.removeAt (slot)) emitChain();
        }
        else if (action == "fxMove")
        {
            editorWindow_.reset();
            if (engine_.moveSlot (slot, slot + static_cast<int> (value))) emitChain();
        }
        else if (action == "fxBypass")
        {
            if (value.isBool() && engine_.setBypass (slot, bool (value))) emitChain();
        }
        else if (action == "fxEditor")
        {
            editorWindow_.reset();
            if (auto* processor = engine_.pluginAt (slot))
            {
                if (auto* editor = processor->createEditorIfNeeded())
                    editorWindow_ = std::make_unique<PluginEditorWindow> (
                        processor->getName(), editor);
                else
                    emitNotice ("This VST3 does not expose a graphical editor.", true);
            }
        }
        else if (action == "monitor" && value.isBool())
            engine_.setMonitorEnabled (bool (value));
        else if (action == "refreshState")
        {
            emitCatalog();
            emitChain();
        }
    }

    void timerCallback() override
    {
        if (!browser_.loaded) return;

        // Audio-device preferences are saved on the message thread only.
        if (auto xml = engine_.devices().createStateXml())
        {
            const auto text = xml->toString();
            if (text != lastAudioXml_)
            {
                lastAudioXml_ = text;
                settingsDirectory().getChildFile ("audio-devices.xml").replaceWithText (text);
            }
        }
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
    const juce::String getApplicationVersion() override { return "0.5.0-native-catalog"; }
    void initialise (const juce::String&) override { window_ = std::make_unique<ConsoleWindow>(); }
    void shutdown() override { window_.reset(); }
private:
    std::unique_ptr<ConsoleWindow> window_;
};
}
START_JUCE_APPLICATION (ConsoleApp)
