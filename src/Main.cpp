// SPDX-License-Identifier: AGPL-3.0-only
// Native VST3 FX catalog, plugin rack and audio-device bridge.
// Audio runs exclusively in JUCE AudioIODeviceCallback; WebView is control only.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include "LiveAudioEngine.h"
#include "ConsoleLiveBridgeV6.h"
#include "ArSonMixStartupSplash.h"
#include "VstOrbitController.h"
#include "../engine/core/OrbitDistanceMapping.h"
#include "../engine/core/DearVrPolarPolicy.h"
#include "DiagnosticsRecorder.h"
#include "../engine/device/DirectRestoreBackoff.h"
#include "../engine/device/PeriodCapabilityEvidence.h"
#include "../engine/diagnostics/CrackleEvidenceWindow.h"
#if defined(_WIN32)
#include "../engine/device/WasapiCapabilityProbe.h"
#endif
#include "PluginCatalog.h"
#include "PortableWorkspace.h"
#include "StudioBackup.h"
#include "NativeLiveShowCapture.h"
#include "NativeDryShowRecall.h"
#include "NativeShowSaveWorker.h"
#include <algorithm>
#include <cstring>
#include <optional>
#include <memory>
#include <chrono>
#include <cmath>
#include <functional>

namespace
{
juce::File settingsDirectory()
{
    return armix::storage::PortableWorkspace::dataRoot();
}

juce::File machineSettingsDirectory()
{
    return armix::storage::PortableWorkspace::machineDataRoot();
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
    // JUCE 8.0.15 BinaryData strips hyphens from generated identifiers:
    // "plugin-ui.js" -> pluginui_js, "plugin-ui.css" -> pluginui_css.
    // Asset HTTP paths retain their hyphenated original names.
    // JUCE BinaryData strips dashes from the source filename: plugin-ui.js -> pluginui_js.
    else if (path == "/plugin-ui.js")
        { data = BinaryData::pluginui_js; size = BinaryData::pluginui_jsSize; mime = "text/javascript"; }
    else if (path == "/plugin-ui.css")
        { data = BinaryData::pluginui_css; size = BinaryData::pluginui_cssSize; mime = "text/css"; }
    else if (path == "/bridge.js")
        { data = BinaryData::bridge_js; size = BinaryData::bridge_jsSize; mime = "text/javascript"; }
    else if (path == "/monitor.js")
        { data = BinaryData::monitor_js; size = BinaryData::monitor_jsSize; mime = "text/javascript"; }
    else if (path == "/mixer-io.js")
        { data = BinaryData::mixerio_js; size = BinaryData::mixerio_jsSize; mime = "text/javascript"; }
    else if (path == "/mixer-io.css")
        { data = BinaryData::mixerio_css; size = BinaryData::mixerio_cssSize; mime = "text/css"; }
    else if (path == "/assets/audio-interface.svg")
        { data = BinaryData::audiointerface_svg; size = BinaryData::audiointerface_svgSize; mime = "image/svg+xml"; }
    else if (path == "/mixer-layout.js")
        { data = BinaryData::mixerlayout_js; size = BinaryData::mixerlayout_jsSize; mime = "text/javascript"; }
    else if (path == "/mixer-layout.css")
        { data = BinaryData::mixerlayout_css; size = BinaryData::mixerlayout_cssSize; mime = "text/css"; }
    else return std::nullopt;

    juce::WebBrowserComponent::Resource resource;
    resource.data.resize (static_cast<size_t> (size));
    std::memcpy (resource.data.data(), data, static_cast<size_t> (size));
    resource.mimeType = mime;
    return resource;
}

// Materialize browser cache BEFORE WebView2 construction (USB portable mode).
// This single options factory also drives the startup preflight below.
juce::WebBrowserComponent::Options webViewOptions()
{
    const auto folder = armix::storage::PortableWorkspace::browserDataRoot();
    folder.createDirectory();
    return juce::WebBrowserComponent::Options{}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (
            juce::WebBrowserComponent::Options::WinWebView2{}.withUserDataFolder (folder)
            .withBackgroundColour (juce::Colour (0xff101921)))
        .withKeepPageLoadedWhenBrowserIsHidden();
}

void showWebViewStartupError (const juce::String& detail)
{
    juce::NativeMessageBox::showAsync (
        juce::MessageBoxOptions{}
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle ("ArSonMix - WebView2 unavailable")
            .withMessage ("The offline ArSonMix mixer needs Microsoft Edge WebView2 Runtime.\\n\\n"
                          + detail + "\\n\\nDownload / repair from Microsoft:\\n"
                          "https://developer.microsoft.com/en-us/microsoft-edge/webview2/\\n\\n"
                          "No VST3, mixer presets or session data were deleted.")
            .withButton ("Close"),
        [] (int) { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
}

class Browser final : public juce::WebBrowserComponent
{
public:
    explicit Browser (Options options) : WebBrowserComponent (std::move (options)) {}
    bool loaded = false;
    // Paint a lightweight native first-frame while WebView2 spins up. This is
    // entirely static JUCE graphics, not an overlay CSS compositor or timer.
    // The embedded browser owns the content as soon as its authenticated
    // arMixGuiReady event arrives.
    void paint (juce::Graphics& g) override
    {
        // Always paint the WebView2 host dark, including after loaded=true.
        // On Windows minimize/restore the child compositor can repaint later;
        // a transparent native parent used to reveal a white blank frame.
        g.fillAll (juce::Colour (0xff101921));
        if (loaded) return;
        g.setColour (juce::Colour (0xff58cdeb));
        g.setFont (juce::FontOptions (22.0f, juce::Font::plain));
        g.drawText ("ArSonMix", getLocalBounds().withTrimmedTop (getHeight() / 2 - 36)
                       .removeFromTop (30), juce::Justification::centred);
        g.setColour (juce::Colour (0xff94adbb));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Loading mixer interface", getLocalBounds().withTrimmedTop (
                       getHeight() / 2 + 4).removeFromTop (24),
                    juce::Justification::centred);
    }
    // An Internet Explorer error page can trigger this callback too.
    // Only arMixGuiReady from our final JS script authenticates readiness.
    void pageFinishedLoading (const juce::String&) override {}
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

class AudioSetupWindow final : public juce::DocumentWindow
{
public:
    explicit AudioSetupWindow (juce::AudioDeviceManager& manager,
                               std::function<void()> onClosed)
        : juce::DocumentWindow ("AR MIX — ASIO / WASAPI Setup",
                                juce::Colour (0xff14212a), allButtons),
          onClosed_ (std::move (onClosed))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (580, 430, 1300, 1000);
        // Selector bounds are MAXIMUM enable-able device pins, not the two
        // mixer output channels. A 2x2 limit hid additional ASIO/virtual
        // MAONO capture pins and physical MONITOR 3/4. Actual enabled pin
        // masks still come exclusively from the chosen JUCE audio device.
        auto* selector = new juce::AudioDeviceSelectorComponent (
            manager, 1, 64, 1, 64, false, false, false, false);
        // The JUCE selector refreshes its own preferred height while devices
        // enumerate. It must NOT drive the top-level peer's dimensions.
        // Fixed window ownership avoids Windows DPI / first-show stretching.
        selector->setSize (720, 550);
        setContentOwned (selector, false);
        centreWithSize (740, 590);
    }
    void closeButtonPressed() override { setVisible (false); }
    void visibilityChanged() override
    {
        if (isVisible()) wasVisible_ = true;
        else if (wasVisible_)
        {
            wasVisible_ = false;
            if (onClosed_) onClosed_();
        }
    }
private:
    std::function<void()> onClosed_;
    bool wasVisible_ = false;
};

class ConsoleWindow final : public juce::DocumentWindow, private juce::Timer, private juce::ChangeListener
{
public:
    explicit ConsoleWindow (std::function<void(int)> onStartupStage)
        : DocumentWindow ("AR MIX Studio — Native VST3 FX Host",
                         juce::Colour (0xff101921), allButtons),
          catalog_ (settingsDirectory()),
          diagnostics_ (settingsDirectory().getChildFile ("diagnostics")),
          presets_ (settingsDirectory().getChildFile ("presets"), settingsDirectory()),
          onStartupStage_ (std::move (onStartupStage)),
          browser_ (webViewOptions()
              .withNativeIntegrationEnabled()
              .withEventListener ("arMixGuiReady", [this] (juce::var)
              {
                  browser_.loaded = true;
                  guiReadyTick_ = juce::Time::getMillisecondCounter();
                  browser_.repaint();
                  if (onStartupStage_) onStartupStage_ (1);
              })
              .withEventListener ("arMixParameter", [this] (juce::var args) { onParameter (args); })
              .withEventListener ("arMixAction", [this] (juce::var args) { onAction (args); })
              .withEventListener ("arMixConsoleSubscribe", [this] (juce::var args) {
                  // G0: a real M4 device/callback snapshot, never synthetic
                  // ChannelId LIVE status before the M5 production cutover.
                  if (args.getProperty("protocol","").toString()!="v6") return;
                  consoleSubscribed_=true;
                  if (browser_.loaded) emitNativeConsoleSnapshot(engine_.status());
              })
              .withEventListener ("arMixConsoleIntent", [this] (juce::var args) {
                  // S1: actual installed JUCE production path, never a
                  // dry-only C++ model or WebAudio replica. Only admitted
                  // fixed-M4 SOURCE and RT controls can mutate sound; CH11,
                  // GROUP/FX_RETURN remain rejected until one ChannelId
                  // callback really owns them.
                  const auto snapshot=armix::console::LiveConsoleBridgeV6::snapshot(engine_.status());
                  const auto ack=consoleBridge_.processProductionIntent(
                      args,snapshot,engine_,
                      [this](int lane,int slot,std::uint64_t epoch,
                             std::uint64_t revision,const juce::String& token)
                            ->juce::String {
                          if(nativeShowBusy_||studioBackupBusy_||
                             engine_.isGraphSwitchPending())
                              return "NATIVE_EDITOR_BUSY";
                          const auto owner=engine_.leaseRoutedFxForEditor(
                              lane,slot,epoch,revision,token);
                          if(!owner || !owner->processor)
                              return "NATIVE_EDITOR_STALE_INSTANCE";
                          try {
                              auto* editor=owner->processor->createEditorIfNeeded();
                              if(!editor)return "NATIVE_PLUGIN_HAS_NO_EDITOR";
                              closePluginEditor();
                              routedEditorOwner_=owner; // original JUCE instance
                              routedEditorEpoch_=static_cast<juce::int64>(epoch);
                              editorWindow_=std::make_unique<PluginEditorWindow>(
                                  owner->processor->getName(),editor);
                              return {};
                          } catch(...) {
                              closePluginEditor();
                              return "NATIVE_VENDOR_EDITOR_FAILED";
                          }
                      });
                  if (browser_.loaded) {
                      browser_.emitEventIfBrowserIsVisible("arMixConsoleAck",ack);
                      emitNativeConsoleSnapshot(engine_.status());
                  }
              })
              .withResourceProvider (assetFor))
    {
        settingsDirectory().createDirectory();
        catalog_.onChanged = [this] { emitCatalog(); emitScanProgress(); };
        catalog_.scanFactoryIfPresent(); // Only the signed-off local manifest; never global VST scans.
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (1100, 650, 3840, 2160);
        setContentNonOwned (&browser_, false);
        centreWithSize (1650, 930);
        // Keep native main window hidden during its expensive initial WebView2
        // construction. The opaque always-on-top splash owns first paint.
        // After one control tick, reveal the PEER behind splash if WebView2
        // requires a visible HWND to initialize. Never expose a white frame.
        setVisible (false);

        // Begin WebView navigation BEFORE any synchronous JUCE driver setup.
        // Device startup on cold PCs otherwise blocks the very first GUI.
        webViewStartupTick_ = juce::Time::getMillisecondCounter();
        browser_.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        startTimerHz (12);
        // The existing 12Hz message-thread tick starts the audio engine after
        // an authenticated GUI handshake and initial compositor paint.
        // Defer initial scan until explicitly requested in Plugin Manager.
        // Startup must not spend seconds enumerating third-party VST3 modules.
    }

    ~ConsoleWindow() override
    {
        stopTimer();
        orbit_.stop();
        catalog_.onChanged = nullptr;
        closePluginEditor();
        audioSetupWindow_.reset();
        engine_.devices().removeChangeListener (this);
        engine_.stop();
        browser_.setVisible (false);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    LiveAudioEngine engine_;
    armix::console::LiveConsoleBridgeV6 consoleBridge_;
    bool consoleSubscribed_=false;
    VstOrbitController orbit_;
    juce::int64 fxGraphRevision_ = 1; // control-thread monotonic graph generation
    PluginCatalog catalog_;
    DiagnosticsRecorder diagnostics_;
    armix::storage::NativePresetArchive presets_;
    std::function<void(int)> onStartupStage_;
    Browser browser_;
    armix::storage::StudioBackup::Worker studioBackupWorker_;
    armix::shows::NativeShowSaveWorker nativeShowWorker_;
    bool nativeShowBusy_ = false;
    bool studioBackupBusy_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::File lastPluginFolder_ = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    // Destruction order: editorWindow_ releases its JUCE GUI BEFORE the
    // retained vendor processor lease. No vendor destructor runs on RT.
    armix::mixer::LiveChannelFxGraph::InstanceLease routedEditorOwner_;
    juce::int64 routedEditorEpoch_=0;
    std::unique_ptr<PluginEditorWindow> editorWindow_;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow_;
    juce::String startupError_, lastAudioXml_;
    bool initialStateSent_ = false;
    bool webViewPeerWarmed_ = false;
    bool webViewStartupNoticeSent_ = false;
    juce::uint32 webViewStartupTick_ = 0;
    juce::uint32 guiReadyTick_ = 0;
    bool audioEngineStarted_ = false;
    bool setupLaunching_ = false;
    unsigned int timerTicks_ = 0;
    bool deviceSettingsDirty_ = false;
    bool unexpectedRouteNoticeSent_ = false;
    armix::device::DirectRestoreBackoff directRestore_;
    armix::diagnostics::CrackleEvidenceWindow crackleEvidence_; // 1Hz message-thread snapshots
    juce::String lastDirectRestoreError_;
    juce::var pendingPresetUI_;
    juce::String pendingPresetName_;
    // Structural FX edits are acknowledged only AFTER RT handoff and
    // control-thread retirement. Never tell a picker that a staged insert
    // is LIVE while the old graph is still rendering.
    int pendingFxAddRequestId_=-1;
    juce::String pendingFxAddName_;
    unsigned int pendingFxAddElapsedMs_=0;
    int pendingFxRemoveSlot_=-1;

    static std::uint64_t monotonicMilliseconds() noexcept
    {
        return static_cast<std::uint64_t> (
            std::chrono::duration_cast<std::chrono::milliseconds> (
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    // Audio evidence is produced only by the JUCE message thread. Counter
    // sources are strictly typed: void/negative/nonintegral values remain
    // UNAVAILABLE, not silently converted to observed zero.
    static armix::diagnostics::CrackleEvidenceWindow::Reading evidenceCounter (
        const juce::var& value) noexcept
    {
        using Reading = armix::diagnostics::CrackleEvidenceWindow::Reading;
        if (!(value.isInt() || value.isInt64() || value.isDouble()))
            return Reading {};
        const double amount = static_cast<double>(value);
        if (!std::isfinite(amount) || amount < 0.0 ||
            amount > 9007199254740991.0 || std::floor(amount) != amount)
            return Reading {};
        return {static_cast<std::uint64_t>(amount), true};
    }

    static armix::diagnostics::CrackleEvidenceWindow::Sample makeEvidenceSample (
        const juce::var& state)
    {
        using Window = armix::diagnostics::CrackleEvidenceWindow;
        Window::Sample out;
        out.atMs = monotonicMilliseconds();
        out.connected = bool(state.getProperty("connected", false));
        const auto epoch = evidenceCounter(state.getProperty("deviceGeneration", {}));
        if (!epoch.available) out.connected = false;
        else out.deviceGeneration = epoch.count;
        // Length-prefix identity and control-plane modes: delimiters in user
        // device names can never collide or combine unrelated sessions.
        auto addScope = [&] (const char* key)
        {
            auto token=state.getProperty(key, {}).toString().toStdString();
            out.routeScope += std::to_string(token.size()) + ":" + token;
        };
        for (const auto* key : {"liveBackendClass","inputDevice","outputDevice",
                                "diagnosticRoute","rate","buffer","monitor",
                                "channelMute","masterMute","dryTest",
                                "bridgeCaptureEndpointIdHash",
                                "bridgeRenderEndpointIdHash",
                                "backendXrunMeaning"})
            addScope(key);

        auto set=[&](Window::Counter id,const char* name)
        {
            out.counters[static_cast<std::size_t>(id)] =
                evidenceCounter(state.getProperty(name, {}));
        };
        set(Window::Counter::renderedCallbacks, "callbackCount");
        set(Window::Counter::overBudgetCallbacks, "overBudgetCount");
        set(Window::Counter::lateCallbackIntervals, "lateCallbackIntervals");
        set(Window::Counter::callbackGapIntervals, "callbackGapCount");
        const auto meaning=state.getProperty("backendXrunMeaning", {}).toString();
        if (meaning == "WASAPI_CAPTURE_DISCONTINUITIES_ONLY")
            set(Window::Counter::wasapiCaptureDiscontinuities, "backendXrunCount");
        else if (meaning == "DRIVER_DEFINED")
            set(Window::Counter::otherDriverXruns, "backendXrunCount");
        // A missing ASRC bridge deliberately emits N/A instead of 0 losses.
        set(Window::Counter::bridgeUnderflowFrames, "bridgeUnderflowFrames");
        set(Window::Counter::bridgePrimingFrames, "bridgePrimingFrames");
        set(Window::Counter::bridgeOverflowFrames, "bridgeOverflowFrames");
        set(Window::Counter::bridgeNonFiniteInputSamples, "bridgeNonFiniteCaptureSamples");
        set(Window::Counter::bridgeCaptureCallbacks, "bridgeCaptureCallbacks");
        set(Window::Counter::bridgeRenderCallbacks, "bridgeRenderCallbacks");
        return out;
    }

    juce::String crackleWindowReport (const juce::var& status) const
    {
        using Window = armix::diagnostics::CrackleEvidenceWindow;
        const auto window = crackleEvidence_.mark (makeEvidenceSample(status));
        juce::String report = "PRE_MARK_EVIDENCE_5S=";
        report += Window::reasonName(window.reason);
        report += " | elapsedMs=" + juce::String(static_cast<juce::int64>(window.elapsedMs));
        report += " | RENDER_HARDWARE_XRUN=UNAVAILABLE_THROUGH_JUCE";
        for (std::size_t index=0; index<Window::counterCount; ++index)
        {
            const auto id=static_cast<Window::Counter>(index);
            const auto delta=window.changes[index];
            report += " | ";
            report += Window::name(id);
            report += "=";
            report += delta.comparable
                ? juce::String(static_cast<juce::int64>(delta.value))
                : juce::String("N/A");
        }
        report += " | CORRELATION_ONLY_NOT_CAUSAL_PROOF";
        return report;
    }

    void annotateRecovery (juce::var& state) const
    {
        auto* object = state.getDynamicObject();
        if (object == nullptr) return;
        const auto snapshot = directRestore_.snapshot();
        using State = armix::device::DirectRestoreBackoff::State;
        const char* label = snapshot.state == State::idle ? "IDLE"
                          : snapshot.state == State::waiting ? "WAITING" : "EXHAUSTED";
        object->setProperty ("directRestoreState", label);
        object->setProperty ("directRestoreAttempts", static_cast<int> (snapshot.attempts));
        object->setProperty ("directRestoreNextMs",
            static_cast<double> (directRestore_.remainingMs (monotonicMilliseconds())));
        object->setProperty ("directRestoreGeneration", static_cast<double> (snapshot.generation));
        object->setProperty ("directRestoreError", lastDirectRestoreError_);
        object->setProperty ("directRestorePending", engine_.hasPendingDirectRestore());
    }

    // Device reopens happen on the JUCE message thread, never the RT callback.
    // Restoring DIRECT never silently re-enables a faulting bridged route.
    void serviceDirectRestore()
    {
        using Gate = armix::device::DirectRestoreBackoff;
        const auto now = monotonicMilliseconds();
        // Independent drivers can stop callbacks without emitting JUCE error.
        // Read existing atomic progress on message thread before failback gate.
        engine_.pollIndependentClockHealth (now);
        auto snapshot = directRestore_.snapshot();
        if (snapshot.state == Gate::State::idle &&
            (engine_.independentClockFaulted() ||
             (!engine_.independentClockActive() && engine_.hasPendingDirectRestore())))
        {
            const auto generation = directRestore_.begin (now);
            diagnostics_.record ("DIRECT_RESTORE_BEGIN",
                juce::String ("Generation ") + juce::String (static_cast<juce::int64> (generation)));
            snapshot = directRestore_.snapshot();
        }
        if (!directRestore_.due (snapshot.generation, now)) return;

        const bool wasIndependent = engine_.independentClockActive();
        const auto stateBeforeRestore = engine_.status();
        const auto faultStatus = juce::JSON::toString (stateBeforeRestore, false);
        // DiagnosticsRecorder bounds event details to 950 characters. Keep
        // the independent stream fault source and durations FIRST, before the
        // potentially long full status JSON, so field reports retain evidence
        // after closing the adapter resets its watchdog.
        const auto watchdogSummary = juce::String ("format=")
            + stateBeforeRestore.getProperty ("bridgeNegotiationFault", "NONE").toString()
            + " watchdog="
            + stateBeforeRestore.getProperty ("bridgeWatchdogFault", "NONE").toString()
            + " captureSilentMs="
            + stateBeforeRestore.getProperty ("bridgeCaptureSilentMs", 0).toString()
            + " renderSilentMs="
            + stateBeforeRestore.getProperty ("bridgeRenderSilentMs", 0).toString()
            + " continuity="
            + stateBeforeRestore.getProperty ("bridgeContinuityFault", "NONE").toString()
            + " lossBp="
            + stateBeforeRestore.getProperty ("bridgeContinuityLossBp", 0).toString()
            + " badWindows="
            + stateBeforeRestore.getProperty ("bridgeContinuityBadWindows", 0).toString();
        const auto error = engine_.setIndependentClockEnabled (false);
        if (error.isEmpty())
        {
            directRestore_.recovered (snapshot.generation);
            lastDirectRestoreError_.clear();
            deviceSettingsDirty_ = true;
            diagnostics_.record ("SMART_CLOCK_RECOVERY",
                juce::String ("DIRECT_RESTORED after ") + juce::String (snapshot.attempts + 1) +
                " attempt(s) | " + watchdogSummary + " | " + faultStatus);
            if (engine_.isOutputMuted())
                emitNotice ("DIRECT device reopened. Output remains muted for safety; unmute only when ready.", true);
            else
                emitNotice (wasIndependent
                    ? "Independent device fault: safe DIRECT audio restored."
                    : "DIRECT audio device restored after a bounded retry.", true);
            return;
        }

        directRestore_.failed (snapshot.generation, now);
        lastDirectRestoreError_ = error;
        const auto next = directRestore_.snapshot();
        diagnostics_.record ("SMART_CLOCK_RECOVERY",
            juce::String ("RESTORE_FAIL ") + juce::String (next.attempts) + "/" +
            juce::String (static_cast<int> (Gate::maxAttempts)) + ": " + error +
            " | " + watchdogSummary + " | " + faultStatus);
        if (next.state == Gate::State::exhausted)
            emitNotice ("Automatic DIRECT recovery exhausted; audio remains safe/muted. "
                        "Check AUDIO SETUP or retry DIRECT manually. " + error, true);
        else if (next.attempts == 1)
            emitNotice ("Device interrupted. DIRECT restore is retrying with bounded backoff.", true);
    }

    void emitNotice (const juce::String& message, bool error = false)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("text", message);
        o->setProperty ("error", error);
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixNotice", juce::var (o.release()));
    }

    void emitNativeConsoleSnapshot(const juce::var& engineStatus)
    {
        if(browser_.loaded && consoleSubscribed_)
            browser_.emitEventIfBrowserIsVisible("arMixConsoleSnapshot",
                armix::console::LiveConsoleBridgeV6::snapshot(engineStatus));
    }

    void emitCatalog()
    {
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixCatalog", catalog_.snapshot());
    }

    // Every plugin insert request gets a structured result. A toast behind
    // a modal is not an actionable failure report for live performers.
    void emitFxAddResult (int requestId, const juce::String& name,
                          const juce::String& error, unsigned int elapsedMs)
    {
        auto response = std::make_unique<juce::DynamicObject>();
        response->setProperty ("requestId", requestId);
        response->setProperty ("name", name);
        response->setProperty ("ok", error.isEmpty());
        response->setProperty ("error", error);
        response->setProperty ("elapsedMs", static_cast<int> (elapsedMs));
        response->setProperty ("chainSize", engine_.chainSize());
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixAddResult", juce::var (response.release()));
    }

    void emitPresets()
    {
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixPresets", presets_.list());
    }

    void closePluginEditor() noexcept {
        // JUCE destroys the editor before releasing the owner's processor.
        editorWindow_.reset();
        routedEditorOwner_.reset();
        routedEditorEpoch_=0;
    }

    void emitChain()
    {
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixChain", engine_.chainState());
    }

    // Valid metre labels only. A naked "30" is NOT proof that VST3 represents metres.
    static bool isMetreText(const juce::String& text)
    {
        const auto t=text.trim().toLowerCase();
        return t.endsWith(" m") || t.endsWith(" meter") || t.endsWith(" meters")
            || t.endsWith(" metre") || t.endsWith(" metres");
    }

    static bool distanceMeterBounds(juce::AudioProcessorParameter* p,double& minimum,double& maximum)
    {
        if(!p)return false;
        const auto a=p->getText(0.f,48),b=p->getText(1.f,48);
        const auto mid=p->getText(0.5f,48);
        if(!isMetreText(a)||!isMetreText(b)||!isMetreText(mid))return false;
        minimum=a.getDoubleValue();maximum=b.getDoubleValue();
        const double middle=mid.getDoubleValue();
        return std::isfinite(minimum)&&std::isfinite(maximum)&&
            minimum>=0&&maximum>minimum&&maximum<=10000.0 &&
            middle>minimum&&middle<maximum;
    }

    // Converts a physical metre setpoint by inverting the plugin's own text display.
    // Works with nonlinear vendor mappings; never invents a 0–30m linear scale.
    static bool metresToNormalized(juce::AudioProcessorParameter* p,double metres,float& normalized)
    {
        double loMeters=0,hiMeters=0;
        if(!distanceMeterBounds(p,loMeters,hiMeters)||
           !std::isfinite(metres)||metres<loMeters||metres>hiMeters)return false;
        return armix::core::normalizedForDisplayedSetpoint(
            metres,loMeters,hiMeters,
            [p](float value) {return p->getText(value,48).getDoubleValue();},
            normalized);
    }

    void emitOrbitState (int slot,int previewShape=-1)
    {
        auto event = std::make_unique<juce::DynamicObject>();
        event->setProperty ("slot", slot);
        event->setProperty ("revision", juce::String (fxGraphRevision_));
        const auto& stats = orbit_.stats();
        event->setProperty ("controlTicks", static_cast<juce::int64> (stats.ticks));
        event->setProperty ("parameterWrites", static_cast<juce::int64> (stats.writes));
        event->setProperty ("coalesced", static_cast<juce::int64> (stats.coalesced));
        event->setProperty ("lateTicks", static_cast<juce::int64> (stats.lateTicks));
        event->setProperty ("maxGapMs", stats.maxGapMs);
        event->setProperty ("active", orbit_.active());
        event->setProperty ("activeSlot", orbit_.slot());
        event->setProperty ("activeParameter", orbit_.parameterIndex());
        event->setProperty ("periodSeconds", orbit_.secondsPerRevolution());
        event->setProperty ("reverse", orbit_.reverse());
        event->setProperty ("shape", orbit_.shape());
        event->setProperty ("polarMode", orbit_.polarStatus());
        event->setProperty ("distanceIndex", orbit_.distanceParameterIndex());
        event->setProperty ("elevationIndex", orbit_.elevationParameterIndex());
        event->setProperty ("phase", orbit_.phase());
        event->setProperty("nearNormalized",orbit_.nearDistance());
        event->setProperty("farNormalized",orbit_.farDistance());
        event->setProperty("previewShape",previewShape);
        const int pathShape=armix::core::validOrbitShape(previewShape)?previewShape:orbit_.shape();
        // Sample the *same C++ pure trajectory* as the engine so the WebView
        // draws matching paths without vendor calls or WebView events at 60Hz.
        juce::Array<juce::var> path;
        for(int i=0;i<=64;++i)
        {
            const auto p=armix::core::orbitMotionPoint(i/64.0,pathShape,
                orbit_.nearDistance(),orbit_.farDistance());
            auto pt=std::make_unique<juce::DynamicObject>();
            pt->setProperty("x",p.x);
            pt->setProperty("y",p.y);
            pt->setProperty("z",p.z);
            path.add(juce::var(pt.release()));
        }
        event->setProperty("motionPath",juce::var(path));

        juce::Array<juce::var> listed;
        int preferred = -1, preferredDistance = -1, preferredElevation = -1;
        auto* plugin = engine_.isGraphSwitchPending() ? nullptr : engine_.pluginAt (slot);
        if (plugin != nullptr)
        {
            event->setProperty ("plugin", plugin->getName());
            const auto& params = plugin->getParameters();
            for (int i = 0; i < params.size(); ++i)
            {
                auto* param = params[i];
                if (param == nullptr || !param->isAutomatable() || param->getNumSteps() < 72)
                    continue;
                const auto name = param->getName (100);
                auto item = std::make_unique<juce::DynamicObject>();
                item->setProperty ("index", i);
                item->setProperty ("name", name);
                item->setProperty ("minText", param->getText (0.f, 32));
                item->setProperty ("maxText", param->getText (1.f, 32));
                double minMetres=0,maxMetres=0;
                item->setProperty("unit",distanceMeterBounds(param,minMetres,maxMetres)?"m":"%");
                item->setProperty("minMetres",minMetres);
                item->setProperty("maxMetres",maxMetres);
                listed.add (juce::var (item.release()));
                if (name.equalsIgnoreCase ("Azimuth"))
                    preferred = i;
                else if (preferred < 0 && name.containsIgnoreCase ("azimuth") &&
                         !name.containsIgnoreCase ("scale"))
                    preferred = i;
                if (name.equalsIgnoreCase("Distance"))
                    preferredDistance = i;
                if (name.equalsIgnoreCase("Elevation"))
                    preferredElevation = i;
            }
        }
        event->setProperty ("preferredParameter",
            orbit_.active() && orbit_.slot() == slot ? orbit_.parameterIndex() : preferred);
        // Display near/far setpoints for the plugin parameter's *actual* units.
        int setpointParam=orbit_.active()&&orbit_.slot()==slot?
            orbit_.distanceParameterIndex():preferredDistance;
        auto* dist=setpointParam>=0&&plugin!=nullptr&&
            setpointParam<plugin->getParameters().size()?
            plugin->getParameters()[setpointParam]:nullptr;
        double minMetres=0,maxMetres=0;
        const bool metres=distanceMeterBounds(dist,minMetres,maxMetres);
        event->setProperty("distanceUnit",metres?"m":"%");
        event->setProperty("distanceMin",metres?minMetres:0.0);
        event->setProperty("distanceMax",metres?maxMetres:100.0);
        const bool activeTarget=orbit_.active()&&orbit_.slot()==slot;
        const double nearValue=activeTarget?orbit_.nearDistance():0.2;
        const double farValue=activeTarget?orbit_.farDistance():0.7;
        if(metres){
            // Defaults are nearby audible positions, *not* the plugin's maximum range.
            event->setProperty("nearSetpoint",activeTarget?
                dist->getText(static_cast<float>(nearValue),48).getDoubleValue():
                std::clamp(1.0,minMetres,maxMetres));
            event->setProperty("farSetpoint",activeTarget?
                dist->getText(static_cast<float>(farValue),48).getDoubleValue():
                std::clamp(3.0,minMetres,maxMetres));
        }else{
            event->setProperty("nearSetpoint",nearValue*100.0);
            event->setProperty("farSetpoint",farValue*100.0);
        }
        event->setProperty ("preferredDistance",preferredDistance);
        event->setProperty ("preferredElevation",preferredElevation);
        event->setProperty ("parameters", juce::var (listed));
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixOrbitState", juce::var (event.release()));
    }

    // Stop while the VST3 instance still exists and synchronize its UI state.
    void stopOrbit()
    {
        const int previousSlot = orbit_.slot();
        if (!orbit_.active()) return;
        const auto result = orbit_.stats();
        orbit_.stop();
        diagnostics_.record ("FX_ORBIT_CONTROL",
            "ticks=" + juce::String (static_cast<juce::int64> (result.ticks)) +
            " writes=" + juce::String (static_cast<juce::int64> (result.writes)) +
            " coalesced=" + juce::String (static_cast<juce::int64> (result.coalesced)) +
            " lateTicks=" + juce::String (static_cast<juce::int64> (result.lateTicks)) +
            " maxTimerGapMs=" + juce::String (result.maxGapMs, 1) +
            " | timer delay is NOT a hardware xrun");
        emitOrbitState (previousSlot);
    }

    void onParameter (const juce::var& args)
    {
        const auto name = args.getProperty ("parameter", {}).toString();
        const int channel = static_cast<int> (args.getProperty ("channel", -2));
        const auto value = args.getProperty ("value", {});
        const auto valueNumber = static_cast<float> (static_cast<double> (value));
        // P1I: exact acknowledged epoch+revision fences all live strip edits.
        if(engine_.dryRoutingLabActive() && channel>=1 &&
           channel<=LiveDryRoutingLab::count &&
           (name=="faderDb"||name=="pan"||name=="mute"||name=="solo")) {
            const auto ep=args.getProperty("routeEpoch",juce::var());
            const auto rev=args.getProperty("routeRevision",juce::var());
            if(!ep.isVoid()&&!rev.isVoid() &&
               ((name=="mute"||name=="solo")?value.isBool():
                  (value.isDouble()||value.isInt()))) {
                const auto epoch=static_cast<std::uint64_t>(static_cast<juce::int64>(ep));
                const auto revision=static_cast<std::uint64_t>(static_cast<juce::int64>(rev));
                const float liveValue=value.isBool()?(bool(value)?1.f:0.f):valueNumber;
                (void)engine_.setDryRoutingLabParameter(channel-1,epoch,revision,
                    name,liveValue);
            }
            return;
        }
        if (name == "masterGain" && (value.isDouble() || value.isInt()))
            engine_.setMasterGainDb (valueNumber);
        // The app.js bridge converts strip index 0 to channel ID 1.
        else if (name == "faderDb" && channel == 1 && (value.isDouble() || value.isInt()))
            engine_.setChannelGainDb (valueNumber);
        else if (name == "pan" && channel == 1 && (value.isDouble() || value.isInt()))
            engine_.setChannelPan (valueNumber * 0.01f);
        else if (name == "mute" && channel == 1 && value.isBool())
            engine_.setChannelMuted (bool (value));
        else if (name == "masterMute" && value.isBool())
            engine_.setMasterMuted (bool (value));
        else if (name == "masterMono" && value.isBool())
            engine_.setMasterMono (bool (value));
    }

    void emitScanProgress()
    {
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixScanProgress", catalog_.progress());
    }


    // DEVICE1: the WebView2 matrix is the operator GUI; JUCE remains the
    // single device owner. No hardware calls ever run from the audio callback.
    // DEVICE2: reading the device list must never repeatedly scan vendor
    // drivers simply because the user opened the I/O matrix. JUCE has already
    // enumerated device types during normal startup. An explicit REFRESH is the
    // only additional scan, and is refused while any output is armed.
    void emitDeviceInventory(bool explicitRescan)
    {
        const bool scanSafe=engine_.allOutputsDisarmed()
            && !engine_.independentClockActive()
            && !engine_.hasTemporaryDiagnosticDevice()
            && !engine_.isGraphSwitchPending()
            && !nativeShowBusy_ && !studioBackupBusy_;
        const auto started=monotonicMilliseconds();
        juce::String scanState=explicitRescan?(scanSafe?"SCANNED":"BLOCKED_ARMED"):"CACHED";
        auto inventory=std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> types;
        auto& manager=engine_.devices();
        for(auto* type:manager.getAvailableDeviceTypes())
        {
            if(type==nullptr)continue;
            if(explicitRescan && scanSafe)
            {
                try { type->scanForDevices(); }
                catch(const std::exception&) { scanState="SCAN_ERROR"; }
                catch(...) { scanState="SCAN_ERROR"; }
            }
            auto item=std::make_unique<juce::DynamicObject>();
            item->setProperty("name",type->getTypeName());
            item->setProperty("active",type->getTypeName()==manager.getCurrentAudioDeviceType());
            item->setProperty("separate",type->hasSeparateInputsAndOutputs());
            juce::Array<juce::var> inputs,outputs;
            for(const auto& n:type->getDeviceNames(true))inputs.add(n);
            for(const auto& n:type->getDeviceNames(false))outputs.add(n);
            item->setProperty("inputs",juce::var(inputs));
            item->setProperty("outputs",juce::var(outputs));
            types.add(juce::var(item.release()));
        }
        const auto elapsed=monotonicMilliseconds()-started;
        inventory->setProperty("types",juce::var(types));
        inventory->setProperty("currentType",manager.getCurrentAudioDeviceType());
        inventory->setProperty("active",currentDeviceSnapshot());
        inventory->setProperty("scanState",scanState);
        inventory->setProperty("scanDurationMs",static_cast<double>(elapsed));
        if(explicitRescan)
            diagnostics_.record("DEVICE_REFRESH",
                scanState+" | "+juce::String(static_cast<juce::int64>(elapsed))+" ms");
        if(browser_.loaded)
            browser_.emitEventIfBrowserIsVisible("arMixAudioDeviceInventory",
                juce::var(inventory.release()));
    }

    juce::var currentDeviceSnapshot()
    {
        // This readback is used for UI and ACK, never to claim that a staged
        // select value is already open/playing on hardware.
        auto o=std::make_unique<juce::DynamicObject>();
        auto& manager=engine_.devices();
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        manager.getAudioDeviceSetup(setup);
        auto* device=manager.getCurrentAudioDevice();
        o->setProperty("backend",manager.getCurrentAudioDeviceType());
        o->setProperty("captureDevice",setup.inputDeviceName);
        o->setProperty("playbackDevice",setup.outputDeviceName);
        o->setProperty("rate",device!=nullptr&&device->isOpen()?device->getCurrentSampleRate():0.0);
        o->setProperty("buffer",device!=nullptr&&device->isOpen()?device->getCurrentBufferSizeSamples():0);
        o->setProperty("open",device!=nullptr&&device->isOpen());
        o->setProperty("authorized",engine_.directRouteAuthorized());
        o->setProperty("outputMuted",engine_.isOutputMuted());
        o->setProperty("deviceGeneration",engine_.status().getProperty("deviceGeneration",0));
        juce::Array<juce::var> inputNames,outputNames,inputPins,outputPins,rates,buffers;
        if(device!=nullptr&&device->isOpen()) {
            const auto inputs=device->getInputChannelNames();
            const auto outputs=device->getOutputChannelNames();
            const auto inputMask=device->getActiveInputChannels();
            const auto outputMask=device->getActiveOutputChannels();
            for(int i=0;i<juce::jmin(64,inputs.size());++i) {
                inputNames.add(inputs[i]);
                if(inputMask[i])inputPins.add(i);
            }
            for(int i=0;i<juce::jmin(64,outputs.size());++i) {
                outputNames.add(outputs[i]);
                if(outputMask[i])outputPins.add(i);
            }
            for(const auto value:device->getAvailableSampleRates()) rates.add(value);
            for(const auto value:device->getAvailableBufferSizes()) buffers.add(value);
            o->setProperty("hasControlPanel",device->hasControlPanel());
        }else o->setProperty("hasControlPanel",false);
        o->setProperty("inputNames",juce::var(inputNames));
        o->setProperty("outputNames",juce::var(outputNames));
        o->setProperty("enabledInputs",juce::var(inputPins));
        o->setProperty("enabledOutputs",juce::var(outputPins));
        o->setProperty("rates",juce::var(rates));
        o->setProperty("buffers",juce::var(buffers));
        return juce::var(o.release());
    }

    void applyCustomAudioDevice(const juce::var& args)
    {
        // Synchronous manager operations stay on JUCE's established message
        // thread. A vendor driver may block; never pretend JS async fixes it.
        const auto id=args.getProperty("requestId",{}).toString().substring(0,64);
        const auto oldGeneration=engine_.status().getProperty("deviceGeneration",0);
        auto send=[&](bool ok,const juce::String& state,const juce::String& error)
        {
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",id);
            ack->setProperty("ok",ok);
            ack->setProperty("state",state);
            ack->setProperty("error",error);
            ack->setProperty("active",currentDeviceSnapshot());
            ack->setProperty("outputMuted",engine_.isOutputMuted());
            if(browser_.loaded)
                browser_.emitEventIfBrowserIsVisible("arMixAudioDeviceApplyAck",juce::var(ack.release()));
        };
        if(id.isEmpty()||id.length()>64||!(args.getProperty("expectedDeviceGeneration",{}).isDouble()||
                args.getProperty("expectedDeviceGeneration",{}).isInt()||
                args.getProperty("expectedDeviceGeneration",{}).isInt64())||
            static_cast<double>(args.getProperty("expectedDeviceGeneration",{})) !=
                static_cast<double>(oldGeneration))
            return send(false,"REJECTED","Stale device generation; refresh DEVICE / DRIVER");
        const auto nativeRoute=engine_.status().getProperty("dryRoutingLab",{});
        if(nativeShowBusy_||studioBackupBusy_||engine_.isGraphSwitchPending()||
            static_cast<bool>(nativeRoute.getProperty("pending",false))||
            engine_.independentClockActive()||engine_.hasTemporaryDiagnosticDevice()||
            !engine_.allOutputsDisarmed())
            return send(false,"REJECTED",
                "MUTE MAIN and secondary output; finish Show/FX/diagnostic/clock transition first");
        const auto config=args.getProperty("config",{});
        if(!config.isObject())return send(false,"REJECTED","Missing device configuration");
        const auto typeName=config.getProperty("backend",{}).toString();
        const auto capture=config.getProperty("captureDevice",{}).toString();
        const auto playback=config.getProperty("playbackDevice",{}).toString();
        if(typeName.isEmpty()||capture.isEmpty()||playback.isEmpty()||
           typeName.length()>160||capture.length()>250||playback.length()>250)
            return send(false,"REJECTED","Choose actual capture and playback devices");
        auto& manager=engine_.devices();
        juce::AudioIODeviceType* selectedType=nullptr;
        for(auto* type:manager.getAvailableDeviceTypes())
            if(type!=nullptr&&type->getTypeName()==typeName)selectedType=type;
        if(selectedType==nullptr)return send(false,"REJECTED","Audio backend is not installed");
        // Do not let JS submit arbitrary names or combine unmatched ASIO devices.
        const auto namesIn=selectedType->getDeviceNames(true);
        const auto namesOut=selectedType->getDeviceNames(false);
        if(!namesIn.contains(capture)||!namesOut.contains(playback)||
            (!selectedType->hasSeparateInputsAndOutputs()&&capture!=playback))
            return send(false,"REJECTED","Device names or duplex pairing are not reported by JUCE");
        auto sample=config.getProperty("sampleRate",{}), period=config.getProperty("bufferSize",{});
        if(!(sample.isInt()||sample.isDouble()||sample.isInt64())||
           !(period.isInt()||period.isDouble()||period.isInt64()))
            return send(false,"REJECTED","Rate and buffer must be numeric or AUTO (0)");
        const double rate=static_cast<double>(sample);
        const double block=static_cast<double>(period);
        if(!std::isfinite(rate)||!std::isfinite(block)||
           (rate!=0&&(rate<8000||rate>384000))||
           (block!=0&&(block<16||block>8192||std::floor(block)!=block)))
            return send(false,"REJECTED","Invalid device sample rate or buffer size");
        const bool unchangedDevice=manager.getCurrentAudioDeviceType()==typeName;
        juce::AudioDeviceManager::AudioDeviceSetup before;
        manager.getAudioDeviceSetup(before);
        const auto previousType=manager.getCurrentAudioDeviceType();
        const bool sameEndpoint=unchangedDevice&&before.inputDeviceName==capture
                             &&before.outputDeviceName==playback;
        juce::AudioDeviceManager::AudioDeviceSetup candidate=before;
        candidate.inputDeviceName=capture;
        candidate.outputDeviceName=playback;
        candidate.sampleRate=rate;
        candidate.bufferSize=static_cast<int>(block);
        // Pins are explicit only when candidate is the CURRENT active device.
        // Other device pin labels do not exist until JUCE actually opens it.
        // Its first APPLY uses safe defaults; then users refine real pin masks.
        const auto pinInput=config.getProperty("enabledInputs",{});
        const auto pinOutput=config.getProperty("enabledOutputs",{});
        if(sameEndpoint && pinInput.isArray() && pinOutput.isArray()) {
            auto* active=manager.getCurrentAudioDevice();
            if(active==nullptr||!active->isOpen())
                return send(false,"REJECTED","Current device is not open; refresh");
            const auto inCount=juce::jmin(64,active->getInputChannelNames().size());
            const auto outCount=juce::jmin(64,active->getOutputChannelNames().size());
            auto parsePins=[](const juce::var& values,int limit,juce::BigInteger& mask)->bool {
                const auto* array=values.getArray();
                if(array==nullptr||array->size()>64)return false;
                for(const auto& p:*array) {
                    if(!(p.isInt()||p.isInt64()))return false;
                    const int index=static_cast<int>(p);
                    if(index<0||index>=limit||mask[index])return false;
                    mask.setBit(index);
                }
                return true;
            };
            juce::BigInteger inMask,outMask;
            if(!parsePins(pinInput,inCount,inMask)||!parsePins(pinOutput,outCount,outMask)||
               inMask.countNumberOfSetBits()<1||outMask.countNumberOfSetBits()<2)
                return send(false,"REJECTED","Choose >=1 verified capture and >=2 playback pins");
            candidate.useDefaultInputChannels=false;
            candidate.useDefaultOutputChannels=false;
            candidate.inputChannels=inMask;
            candidate.outputChannels=outMask;
        }else {
            candidate.useDefaultInputChannels=true;
            candidate.useDefaultOutputChannels=true;
        }
        auto* active=manager.getCurrentAudioDevice();
        if(sameEndpoint && active!=nullptr && active->isOpen()) {
            if(rate>0&&!active->getAvailableSampleRates().contains(rate))
                return send(false,"REJECTED","Requested sample rate not reported by selected device");
            if(block>0&&!active->getAvailableBufferSizes().contains(static_cast<int>(block)))
                return send(false,"REJECTED","Requested buffer size not reported by selected device");
        }

        // Full device transaction: never enable audio during an intermediate
        // backend auto-open, and never persist an unverified partial selection.
        // A missing prior endpoint can leave the direct callback detached;
        // reuse the existing explicit replacement path instead of creating a
        // second callback or trusting the unreachable device.
        if(engine_.hasPendingDirectRestore() &&
           !engine_.abandonPendingDirectRestoreForSetup())
            return send(false,"REJECTED","Failed to prepare an explicit replacement for the missing device");
        const auto start=monotonicMilliseconds();
        engine_.beginManualAudioSetup();
        if(!unchangedDevice)manager.setCurrentAudioDeviceType(typeName,false);
        auto error=manager.getCurrentAudioDeviceType()==typeName
            ?manager.setAudioDeviceSetup(candidate,true)
            :juce::String("Selected audio backend could not be opened");
        juce::AudioDeviceManager::AudioDeviceSetup actual;
        manager.getAudioDeviceSetup(actual);
        auto* opened=manager.getCurrentAudioDevice();
        if(error.isEmpty()&&(opened==nullptr||!opened->isOpen()||
              manager.getCurrentAudioDeviceType()!=typeName||
              actual.inputDeviceName!=capture||actual.outputDeviceName!=playback||
              (rate>0&&std::abs(actual.sampleRate-rate)>1.0)||
              (block>0&&actual.bufferSize!=static_cast<int>(block))||
              (!candidate.useDefaultInputChannels&&actual.inputChannels!=candidate.inputChannels)||
              (!candidate.useDefaultOutputChannels&&actual.outputChannels!=candidate.outputChannels)||
              opened->getActiveInputChannels()!=actual.inputChannels||
              opened->getActiveOutputChannels()!=actual.outputChannels||
              opened->getActiveInputChannels().countNumberOfSetBits()<1||
              opened->getActiveOutputChannels().countNumberOfSetBits()<2||
              std::abs(opened->getCurrentSampleRate()-actual.sampleRate)>1.0||
              opened->getCurrentBufferSizeSamples()!=actual.bufferSize))
            error="Driver readback mismatch; device setting was not accepted";
        if(error.isEmpty()&&!engine_.finishManualAudioSetup())
            error="Device opened but direct safe production session was rejected";
        if(error.isEmpty()) {
            deviceSettingsDirty_=true;
            directRestore_.cancel();
            diagnostics_.record("CUSTOM_AUDIO_APPLY_OK",
                typeName+" | "+juce::String(static_cast<int>(monotonicMilliseconds()-start))+" ms");
            return send(true,"ACCEPTED_MUTED",{});
        }
        // One rollback, never blind default endpoint fallback, no auto-unmute.
        engine_.beginManualAudioSetup();
        if(manager.getCurrentAudioDeviceType()!=previousType&&previousType.isNotEmpty())
            manager.setCurrentAudioDeviceType(previousType,false);
        const auto restored=previousType.isNotEmpty()&&
            manager.getCurrentAudioDeviceType()==previousType&&
            manager.setAudioDeviceSetup(before,true).isEmpty();
        juce::AudioDeviceManager::AudioDeviceSetup checked;
        manager.getAudioDeviceSetup(checked);
        const bool verified=restored&&
            checked.inputDeviceName==before.inputDeviceName&&
            checked.outputDeviceName==before.outputDeviceName&&
            checked.inputChannels==before.inputChannels&&
            checked.outputChannels==before.outputChannels&&
            manager.getCurrentAudioDevice()!=nullptr&&
            manager.getCurrentAudioDevice()->isOpen()&&engine_.finishManualAudioSetup();
        diagnostics_.record("CUSTOM_AUDIO_APPLY_FAIL",error+" | rollback="+
            (verified?"RESTORED":"RECOVERY_REQUIRED")+" | "+
            juce::String(static_cast<int>(monotonicMilliseconds()-start))+" ms");
        send(false,verified?"RESTORED_MUTED":"RECOVERY_REQUIRED",error);
    }

    void showDeviceSetup()
    {
        diagnostics_.record ("SETUP", "Open audio device setup");
        if (setupLaunching_) return;
        engine_.beginManualAudioSetup();

        // The native JUCE device list is cached after first construction:
        // reopening the settings window avoids repeating full widget setup.
        if (audioSetupWindow_ != nullptr)
        {
            audioSetupWindow_->setVisible (true);
            audioSetupWindow_->toFront (true);
            return;
        }

        setupLaunching_ = true;
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<ConsoleWindow> (this)]
        {
            if (safe == nullptr) return;
            safe->audioSetupWindow_ =
                std::make_unique<AudioSetupWindow> (safe->engine_.devices(),
                    [safe]
                    {
                        if (safe == nullptr) return;
                        if (safe->engine_.finishManualAudioSetup())
                        {
                            safe->deviceSettingsDirty_ = true;
                            safe->startupError_.clear();
                            safe->diagnostics_.record ("DIRECT_ROUTE_APPROVED",
                                "User confirmed selected ASIO/WASAPI backend in AUDIO SETUP");
                        }
                        else
                        {
                            safe->diagnostics_.record ("DIRECT_ROUTE_REJECTED",
                                "No valid full-duplex device; audio remains gated");
                            safe->emitNotice ("Choose a working capture and render device. Audio remains muted.", true);
                        }
                    });
            safe->audioSetupWindow_->setVisible (true);
            safe->audioSetupWindow_->toFront (true);
            // The peer now exists: perform one initial JUCE layout on the
            // message thread, rather than relying on a manual window drag.
            safe->audioSetupWindow_->resized();
            safe->audioSetupWindow_->repaint();
            safe->setupLaunching_ = false;
        });
    }

    // A Windows native Explorer picker cannot select ordinary files AND folders in
    // the same operation. Split the modes instead of silently falling back to
    // JUCE's non-native browser. .vst3 bundles are folders on Windows.
    void chooseImport (bool copyToVault = true, bool selectBundle = false)
    {
        if (catalog_.busy()) return;
        const auto title = selectBundle
            ? "Select a .vst3 bundle folder"
            : "Select a .vst3 plugin file";
        chooser_ = std::make_unique<juce::FileChooser> (
            title, lastPluginFolder_, selectBundle ? "*" : "*.vst3", true);
        const auto chooserFlags = juce::FileBrowserComponent::openMode |
            (selectBundle ? juce::FileBrowserComponent::canSelectDirectories
                          : juce::FileBrowserComponent::canSelectFiles);
        chooser_->launchAsync (chooserFlags,
            [safe = juce::Component::SafePointer<ConsoleWindow> (this), copyToVault]
            (const juce::FileChooser& picker)
            {
                if (safe == nullptr) return;
                const auto file = picker.getResult();
                if (file == juce::File{}) return;
                safe->lastPluginFolder_ = file.getParentDirectory();
                if (!file.hasFileExtension (".vst3"))
                    return safe->emitNotice ("Select a .vst3 module or bundle, not a VST2 .dll or ordinary folder.", true);
                if (safe->catalog_.busy())
                    return safe->emitNotice ("Plugin scanner is busy. Retry after the current import finishes.", true);
                safe->catalog_.importVst3 (file, copyToVault);
                safe->emitCatalog();
            });
    }

    // The selected location receives a NEW, directly runnable versioned
    // portable folder. IO/hashing stay on StudioBackup's worker thread.
    void chooseStudioBackup()
    {
        if (studioBackupBusy_ || nativeShowBusy_ || catalog_.busy() || engine_.isGraphSwitchPending())
            return emitNotice ("Finish VST import or FX recall before backing up Studio.", true);
        // Large USB copies cause disk contention even on an IO worker. Avoid
        // starting a backup during a live performance.
        if (!engine_.allOutputsDisarmed())
            return emitNotice ("Mute audio output before starting backup to avoid USB IO disturbing live sound.", true);
        chooser_ = std::make_unique<juce::FileChooser> (
            "Choose a destination folder for a new ArSonMix backup",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*", true);
        chooser_->launchAsync (
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<ConsoleWindow> (this)] (const juce::FileChooser& picker)
            {
                if (safe == nullptr) return;
                const auto destination = picker.getResult();
                if (destination == juce::File()) return;
                if (safe->studioBackupBusy_ || safe->nativeShowBusy_ || safe->catalog_.busy() ||
                    safe->engine_.isGraphSwitchPending() || !safe->engine_.allOutputsDisarmed())
                    return safe->emitNotice ("Plugin import/FX recall is active; backup was not started.", true);
                const auto app = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
                // Source and destination must both be writable/valid. No VST
                // editor or audio device selection is changed by backup.
                const auto started = safe->studioBackupWorker_.launch (app, settingsDirectory(),
                    destination, [safe] (armix::storage::StudioBackup::Result result)
                    {
                        if (safe == nullptr) return;
                        safe->studioBackupBusy_ = false;
                        if (!result.ok())
                        {
                            safe->diagnostics_.record ("STUDIO_BACKUP_FAIL", result.error);
                            safe->emitNotice ("Studio backup failed; previous backups unchanged: " + result.error, true);
                            return;
                        }
                        safe->diagnostics_.record ("STUDIO_BACKUP_VERIFIED",
                            result.folder.getFullPathName() + " | " +
                            juce::String (result.files) + " files | bytes=" + juce::String (result.bytes));
                        const auto warning = result.externalPluginReferences > 0
                            ? " WARNING: " + juce::String (result.externalPluginReferences) +
                              " VST catalog entries still reference files outside Studio."
                            : juce::String();
                        safe->emitNotice ("Native Studio backup verified: " +
                            result.folder.getFileName() + ". " +
                            juce::String (result.files) + " files copied." + warning +
                            " Browser-only scenes and third-party activations are not included.");
                        result.folder.revealToUser();
                    });
                if (!started)
                    return safe->emitNotice ("Could not start backup worker.", true);
                safe->studioBackupBusy_ = true;
                safe->emitNotice ("Backing up Studio and VST files to a new folder. " 
                                  "SHA256 readback runs off the audio thread.");
            });
    }

    void chooseFolder()
    {
        if (catalog_.busy()) return;
        chooser_ = std::make_unique<juce::FileChooser> (
            "Add a VST3 plugin reference folder", lastPluginFolder_, "*", true);
        chooser_->launchAsync (
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [safe = juce::Component::SafePointer<ConsoleWindow> (this)] (const juce::FileChooser& picker)
            {
                if (safe == nullptr) return;
                const auto folder = picker.getResult();
                if (folder == juce::File{}) return;
                safe->lastPluginFolder_ = folder;
                safe->catalog_.addDirectory (folder);
            });
    }

    // Executed only on JUCE message thread on explicit user request.
    // No disk read, network request or audio-thread interaction is required.
    // Clipboard contents are private until the user explicitly pastes them.
    juce::String makeAudioReport()
    {
        auto status = engine_.status();
        annotateRecovery (status);
        auto* device = engine_.diagnosticRenderDevice();
        auto* capture = engine_.diagnosticCaptureDevice();
        juce::String result;
        result.preallocateBytes (64 * 1024);
        result << "ARSONMIX / SMART AUDIO DIAGNOSTIC REPORT v2\n";
        result << "Timestamp: " << juce::Time::getCurrentTime().toISO8601 (true) << "\n";
        result << "Build: experimental-P0 clock-instrumentation / native JUCE 8.0.15\n";
        result << "Platform: " << juce::SystemStats::getOperatingSystemName() << "\n";
        result << "CPU: " << juce::SystemStats::getCpuModel() << "\n";
        result << "Logical cores: " << juce::SystemStats::getNumCpus()
               << " | RAM: " << juce::SystemStats::getMemorySizeInMegabytes() << " MiB\n";
        result << "Telemetry: true hardware render underruns UNAVAILABLE; callback time/gap are proxies only.\n";
        result << "CALLBACK CADENCE: START-to-START estimates use PREVIOUS callback frame count; "
                  "normal variable block sizes are not driver xruns. 1.5x/2.3x flags "
                  "are scheduling heuristics only, not WASAPI/ASIO underrun evidence.\n";
        result << "No user audio samples, passwords or plugin vendor state are included.\n";
        result << "Check device names and local paths before sharing this report.\n\n";
        result << "=== DEVICE CAPABILITIES (backend-reported) ===\n";
        if (device != nullptr)
        {
            result << "Input latency: "
                   << (capture ? capture->getInputLatencyInSamples() : -1)
                   << " frames | Output latency: " << device->getOutputLatencyInSamples()
                   << " frames | Native render buffer: "
                   << device->getCurrentBufferSizeSamples() << " frames\n";
            result << "Capture backend device: "
                   << (capture ? capture->getName() : juce::String ("Unavailable"))
                   << " | Render backend device: " << device->getName() << "\n";
            result << "Reported render sample rates: ";
            for (auto v : device->getAvailableSampleRates()) result << v << " ";
            const auto reportedBuffers = device->getAvailableBufferSizes();
            result << "\nReported render buffer sizes: ";
            for (auto v : reportedBuffers) result << v << " ";
            result << "\n";
            const auto evidence = armix::device::PeriodCapabilityEvidence::inspect (
                device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(),
                std::span<const int> (reportedBuffers.getRawDataPointer(),
                                      static_cast<std::size_t> (reportedBuffers.size())));
            result << "BUFFER POLICY (read-only; no device change): ";
            if (!evidence.validFormat)
                result << "INVALID_NEGOTIATED_FORMAT\n";
            else
            {
                result << "actualPeriodMs=" << evidence.nominalCallbackMs
                       << " activeFramesAdvertised="
                       << (evidence.activePeriodAdvertised ? "YES" : "NO_OR_UNREPORTED")
                       << " minimumAdvertisedFrames=" << evidence.smallestReportedFrames
                       << " conservativeCandidateFrames=" << evidence.conservativeReportedFrames
                       << " conservativePeriodMs=" << evidence.conservativeCallbackMs
                       << " | NOT_FAT_QUALIFIED_DO_NOT_AUTO_APPLY\n";
            }
            result << "These periods are driver/JUCE reported (WASAPI shared can"
                      " select an OS period independently). This is not measured"
                      " round-trip latency or proof of stable low latency.\n";
        }
        else result << "No active device\n";
#if defined(_WIN32)
        // ASIO JUCE names are only labels: report the COM driver class bound
        // to the selected name from the Windows registry ON THIS EXPLICIT
        // REPORT REQUEST, not in the 12 Hz UI status or realtime callback.
        result << "\n=== WINDOWS ASIO DRIVER REGISTRATION (NOT HARDWARE ID) ===\n";
        if (device != nullptr && device->getTypeName() == "ASIO")
        {
            const auto resolved = armix::device::AsioRegistryIdentityProbe::resolve (
                std::wstring (device->getName().toWideCharPointer()));
            result << armix::device::AsioRegistrationPolicy::reason (
                resolved.registration.error) << "\n";
            if (resolved.registration.ok())
                result << "Registered driver class hash: "
                       << juce::String::toHexString (static_cast<juce::int64> (
                           armix::device::AsioRegistryIdentityProbe::diagnosticFingerprint (
                               resolved.registration.identity))) << "\n";
            result << "CLSID identifies a Windows software COM driver class, "
                      "NOT a physical USB device, audio clock, or proof the "
                      "currently opened COM instance is that class.\n";
        }
        else result << "Not using an ASIO DIRECT backend\n";
        result << "\n=== WINDOWS NATIVE WASAPI CAPABILITY PROBE (NOT LIVE LATENCY) ===\n";
        result << armix::device::WasapiCapabilityProbe::report() << "\n";
#endif
        result << "\n=== STATUS SNAPSHOT (measured / source-labeled) ===\n";
        result << juce::JSON::toString (status, true) << "\n";
        result << "\n=== RECENT EVENTS (latest 48, capture on control thread) ===\n";
        result << diagnostics_.recentReport (48);
        result << "\n=== IMPORTANT EVENTS (retained across long sessions) ===\n";
        result << diagnostics_.significantReport();
        result << "\n=== NEXT ACTION ===\n";
        result << "Copy/paste this report to the ArSonMix engineering thread. "
                  "Add a short description of the audible crackle and its mode, if any.\n";
        return result;
    }

    void onAction (const juce::var& args)
    {
        const auto action = args.getProperty ("action", {}).toString();
        const auto value = args.getProperty ("value", {});
        const int slot = static_cast<int> (args.getProperty ("slot", -1));
        // User may adjust live levels, but cannot alter the persisted VST
        // library/preset while a background snapshot is being copied.
        if ((studioBackupBusy_ || nativeShowBusy_) && (action.startsWith ("preset") ||
            action.startsWith ("import") || action.startsWith ("fx") ||
            action == "addCatalog" || action == "addScanFolder" ||
            action == "scanVst3" || action == "routingDryApply" ||
            action == "routingFxBypass"))
        {
            const auto reason=juce::String(
                "Studio backup in progress. Finish backup before editing saved FX or VST library.");
            // R1 matrix must ALWAYS get a response after APPLY, including
            // upstream rejection before the normal routing command handler.
            // No graph or device state may be mutated while Studio IO is busy.
            if((action=="routingDryApply" || action=="routingFxBypass") &&
               browser_.loaded)
            {
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("ok",false);
                ack->setProperty("error",reason);
                ack->setProperty("state",engine_.status().getProperty("dryRoutingLab",{}));
                browser_.emitEventIfBrowserIsVisible(
                    action=="routingDryApply"?"arMixRoutingLabAck":"arMixRoutingFxBypassAck",
                    juce::var(ack.release()));
            }
            return emitNotice(reason,true);
        }

        // Native structural VST3 edits use one prepared graph handoff in DIRECT mode.
        // Independent capture/render stream lifecycle is still gated separately.
        if (engine_.independentClockActive() && action == "addCatalog")
        {
            const int requestId = static_cast<int> (args.getProperty ("requestId", 0));
            const auto* plugin = catalog_.find (args.getProperty ("id", {}).toString());
            const auto name = plugin ? plugin->name : juce::String ("Unknown VST3");
            const auto error = juce::String ("Return to DIRECT mode before editing VST3 graph.");
            emitFxAddResult (requestId, name, error, 0);
            return emitNotice (error, true);
        }
        if (engine_.independentClockActive() &&
            (action == "fxRemove" || action == "fxMove" ||
             action == "presetLoad" || action == "presetSave"))
            return emitNotice ("Return to DIRECT mode before editing VST3 graph (live-safe graph edits are a later gate).", true);

        // A user-controlled hardware dry routing FAT, not a change to the
        // existing 1-channel VST3 production chain. Explicit Apply/Stop.
        // M1 RT source switch on an existing physical channel. Unlike
        // structural routingDryApply, it does not unpatch outputs, rebuild
        // VST3s, or restart the device; input PCM fades inside one graph.
        if(action=="routingSourceSwitch"){
            const int requestId=static_cast<int>(args.getProperty("requestId",0));
            const int channel=static_cast<int>(args.getProperty("channel",-1));
            const auto mode=args.getProperty("mode",juce::var()).toString();
            const auto ep=args.getProperty("deviceEpoch",juce::var());
            const auto rv=args.getProperty("revision",juce::var());
            const int left=static_cast<int>(args.getProperty("left",-1));
            const int right=static_cast<int>(args.getProperty("right",-1));
            const juce::String error=requestId>0&&!ep.isVoid()&&!rv.isVoid()&&
                !nativeShowBusy_&&!studioBackupBusy_?
                engine_.switchDryRoutingLabSource(channel,
                    static_cast<std::uint64_t>(static_cast<juce::int64>(ep)),
                    static_cast<std::uint64_t>(static_cast<juce::int64>(rv)),
                    mode,left,right):
                juce::String("SOURCE SWITCH: stale request or Show/backup busy");
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",requestId);
            ack->setProperty("channel",channel);
            ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            ack->setProperty("state",engine_.status().getProperty("dryRoutingLab",{}));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixSourceSwitchAck",juce::var(ack.release()));
            if(error.isNotEmpty())emitNotice(error,true);
            return;
        }
        if (action == "routingDryApply")
        {
            LiveDryRoutingLab::FxSelections selected{};
            juce::String error;
            // Legacy fxId remains slot 0. Ordered fxIds[] enables G5B while
            // preserving the same device epoch/revision/MUTE/ACK transaction.
            // Never accept caller-supplied filesystem paths.
            const auto resolveLane=[&](const juce::var& row,int lane,const juce::String& label) {
                const auto raw=row.getProperty("fxIds",juce::var());
                const auto old=row.getProperty("fxId",juce::var()).toString();
                juce::StringArray ids;
                if(!raw.isVoid()) {
                    auto* array=raw.getArray();
                    if(!array || array->size()>LiveDryRoutingLab::maxFxPerLane) {
                        error=label+": FX chain must have at most 8 entries";return;
                    }
                    for(const auto& value:*array) {
                        if(!value.isString()) {error=label+": FX ID must be a string";return;}
                        ids.add(value.toString());
                    }
                    if(old.isNotEmpty() && (ids.isEmpty()||ids[0]!=old)) {
                        error=label+": legacy fxId conflicts with ordered fxIds";return;
                    }
                }else if(old.isNotEmpty())ids.add(old);
                for(int slot=0;slot<ids.size();++slot) {
                    const auto id=ids[slot];
                    if(id.isEmpty()){error=label+": FX chain contains empty slot";return;}
                    const auto* entry=catalog_.find(id);
                    if(!entry) {
                        error=label+": slot "+juce::String(slot+1)+
                            " VST3 missing from catalog. Rescan before APPLY.";return;
                    }
                    selected[lane][slot]={entry->key(),entry->path,entry->name,entry->uid};
                }
            };
            const auto rowsValue=value.getProperty("channels",juce::var());
            auto* rows=rowsValue.getArray();
            if(rows&&rows->size()==LiveDryRoutingLab::count)
                for(int i=0;i<rows->size()&&error.isEmpty();++i)
                    resolveLane(rows->getReference(i),i,"CH "+juce::String(i+1));
            const auto auxValue=value.getProperty("auxes",juce::var());
            if(error.isEmpty()&&!auxValue.isVoid()){
                auto* auxRows=auxValue.getArray();
                if(!auxRows||auxRows->size()!=LiveDryRoutingLab::auxCount)
                    error="Exactly four AUX returns are required";
                else for(int b=0;b<LiveDryRoutingLab::auxCount&&error.isEmpty();++b)
                    resolveLane(auxRows->getReference(b),LiveDryRoutingLab::count+b,
                                "AUX "+juce::String(b+1));
            }
            if(error.isEmpty()) {
                // Structural replacement can remove the vendor object.
                // Close its editor before attempting graph publication.
                closePluginEditor();
                try {
                    error=engine_.applyDryRoutingLab(value,selected);
                } catch(const std::exception& ex) {
                    error="VST3 preparation failed: "+juce::String(ex.what());
                } catch(...) {
                    error="VST3 preparation failed (unknown vendor exception)";
                }
            }
            auto reply=std::make_unique<juce::DynamicObject>();
            reply->setProperty("ok",error.isEmpty());
            reply->setProperty("error",error);
            const auto status=engine_.status().getProperty("dryRoutingLab",{});
            reply->setProperty("state",status);
            if(browser_.loaded)
                browser_.emitEventIfBrowserIsVisible(
                    "arMixRoutingLabAck",juce::var(reply.release()));
            if(error.isNotEmpty())return emitNotice(error,true);
            diagnostics_.record("LIVE_DRY_ROUTING_LAB_APPLIED",
                "Actual physical mask and epoch verified | native VST3 per-strip "
                "count="+juce::String(static_cast<int>(status.getProperty("fxCount",0))));
            return emitNotice(
                "Native 10-channel Routing LAB active: "+
                juce::String(static_cast<int>(status.getProperty("fxCount",0)))+
                " independent VST3 inserts, real PRE/POST sends. "
                "Existing CH1 FX Rack and Orbit stay bound to their original processors. "
                "Structural CH1 FX changes remain gated until the next production lifecycle milestone.");
        }
        // M1 BUS MASTER: an acknowledged AUX return level is a live parameter,
        // not an FX graph mutation. Positive native ACK is the UI authority.
        if(action=="routingAuxBusGain") {
            const int bus=static_cast<int>(args.getProperty("bus",-1));
            const int requestId=static_cast<int>(args.getProperty("requestId",0));
            const auto ep=args.getProperty("deviceEpoch",juce::var());
            const auto rv=args.getProperty("revision",juce::var());
            const bool numeric=value.isDouble()||value.isInt();
            const float gain=numeric?static_cast<float>(static_cast<double>(value)):-1.f;
            const bool ok=!nativeShowBusy_&&!studioBackupBusy_&&!ep.isVoid()&&!rv.isVoid()&&
                numeric&&requestId>0&&engine_.setDryRoutingLabAuxBusGain(
                    bus,static_cast<std::uint64_t>(static_cast<juce::int64>(ep)),
                    static_cast<std::uint64_t>(static_cast<juce::int64>(rv)),gain);
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("ok",ok);
            ack->setProperty("bus",bus);
            ack->setProperty("requestId",requestId);
            ack->setProperty("gain",static_cast<double>(gain));
            ack->setProperty("error",ok?juce::String():
                "AUX master rejected: device/route changed, bus disabled or invalid gain");
            ack->setProperty("state",engine_.status().getProperty("dryRoutingLab",{}));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixAuxBusGainAck",juce::var(ack.release()));
            return;
        }
        // M1 operator realtime PARAM: existing AUX POST gain changes while
        // audio is running. Structural sends remain guarded MUTE/APPLY.
        if(action=="routingAuxSendGain") {
            const int source=static_cast<int>(args.getProperty("from",-1));
            const int bus=static_cast<int>(args.getProperty("bus",-1));
            const int requestId=static_cast<int>(args.getProperty("requestId",0));
            const auto ep=args.getProperty("deviceEpoch",juce::var());
            const auto rv=args.getProperty("revision",juce::var());
            const bool number=value.isDouble()||value.isInt();
            const float gain=number?static_cast<float>(static_cast<double>(value)):-1.f;
            const bool ok=!nativeShowBusy_&&!studioBackupBusy_&&!ep.isVoid()&&!rv.isVoid() &&
                number && requestId>0 &&
                engine_.setDryRoutingLabAuxSendGain(source,bus,
                    static_cast<std::uint64_t>(static_cast<juce::int64>(ep)),
                    static_cast<std::uint64_t>(static_cast<juce::int64>(rv)),gain);
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("ok",ok);
            ack->setProperty("requestId",requestId);
            ack->setProperty("from",source);
            ack->setProperty("bus",bus);
            ack->setProperty("gain",static_cast<double>(gain));
            ack->setProperty("error",ok?juce::String():
                "Live AUX gain rejected: route revision/device changed or POST send missing");
            ack->setProperty("state",engine_.status().getProperty("dryRoutingLab",{}));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixAuxSendGainAck",juce::var(ack.release()));
            if(!ok)emitNotice("Live AUX send rejected; refresh accepted routing.",true);
            return;
        }
        // G5C native per-instance BYPASS never recompiles the graph and
        // does not require muting the output. All identity gates must agree.
        if (action == "routingFxBypass") {
            const int lane=static_cast<int>(args.getProperty("channel",-1));
            const auto token=args.getProperty("instanceId",{}).toString();
            const auto epoch=static_cast<std::uint64_t>(
                static_cast<juce::int64>(args.getProperty("deviceEpoch",juce::var(0))));
            const auto revision=static_cast<std::uint64_t>(
                static_cast<juce::int64>(args.getProperty("revision",juce::var(0))));
            const bool ok=value.isBool()&&engine_.setDryRoutingLabFxBypass(
                lane,slot,epoch,revision,token,static_cast<bool>(value));
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("ok",ok);
            ack->setProperty("error",ok?juce::String():
                "FX bypass rejected: route revision, instance or device changed");
            ack->setProperty("state",engine_.status().getProperty("dryRoutingLab",{}));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixRoutingFxBypassAck",juce::var(ack.release()));
            if(ok)diagnostics_.record("ROUTED_FX_BYPASS",
                "CH/AUX "+juce::String(lane+1)+" slot "+juce::String(slot+1)+
                (static_cast<bool>(value)?" OFF":" ON"));
            else emitNotice("FX bypass rejected: refresh the FX Rack after native ACK.",true);
            return;
        }
        if (action == "routingFxEditor") {
            const int lane=static_cast<int>(args.getProperty("channel",-1));
            const auto token=args.getProperty("instanceId",{}).toString();
            const auto epoch=static_cast<std::uint64_t>(
                static_cast<juce::int64>(args.getProperty("deviceEpoch",juce::var(0))));
            const auto revision=static_cast<std::uint64_t>(
                static_cast<juce::int64>(args.getProperty("revision",juce::var(0))));
            bool opened=false;
            juce::String error="FX editor unavailable: device, revision or instance changed";
            if(!nativeShowBusy_&&!studioBackupBusy_&&!engine_.isGraphSwitchPending()) {
                const auto owner=engine_.leaseRoutedFxForEditor(
                    lane,slot,epoch,revision,token);
                if(owner && owner->processor) {
                    try {
                        if(auto* editor=owner->processor->createEditorIfNeeded()) {
                            closePluginEditor();
                            routedEditorOwner_=owner;
                            routedEditorEpoch_=static_cast<juce::int64>(epoch);
                            editorWindow_=std::make_unique<PluginEditorWindow>(
                                owner->processor->getName(),editor);
                            opened=true;
                            error.clear();
                        } else error="This VST3 has no native editor";
                    } catch(const std::exception& ex) {
                        closePluginEditor();
                        error="Vendor editor failed: "+juce::String(ex.what());
                    } catch(...) {
                        closePluginEditor();
                        error="Vendor editor failed (plugin exception)";
                    }
                }
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("ok",opened);
            ack->setProperty("error",error);
            ack->setProperty("instanceId",token);
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixRoutingFxEditorAck",juce::var(ack.release()));
            if(opened)diagnostics_.record("ROUTED_FX_EDITOR_OPENED",
                "lane="+juce::String(lane)+" slot="+juce::String(slot));
            else emitNotice(error,true);
            return;
        }
        if (action == "routingDryShowSave")
        {
            const auto reject=[this](const juce::String& reason) {
                if(browser_.loaded) {
                    auto ack=std::make_unique<juce::DynamicObject>();
                    ack->setProperty("ok",false);
                    ack->setProperty("error",reason);
                    browser_.emitEventIfBrowserIsVisible("arMixNativeShowSaveAck",
                        juce::var(ack.release()));
                }
                emitNotice("Show not saved: "+reason,true);
            };
            if (nativeShowBusy_ || studioBackupBusy_ || catalog_.busy() ||
                engine_.isGraphSwitchPending() || engine_.hasTemporaryDiagnosticDevice())
                return reject("Finish the current Studio task before capturing a native Show.");
            // Never save the unacknowledged JS draft. Message-thread capture
            // reads the accepted LIVE graph, updated faders and exact device epoch.
            // VST3 is fail-closed until a quiescent vendor state capture gate exists.
            armix::shows::SnapshotV2 captured;
            const auto name=juce::String("Native Show ")+juce::Time::getCurrentTime().formatted("%Y%m%d %H%M%S");
            const auto nativeStatus=engine_.status();
            const auto lab=nativeStatus.getProperty("dryRoutingLab",{});
            const auto epoch=static_cast<std::uint64_t>(
                static_cast<juce::int64>(lab.getProperty("epoch",juce::var(0))));
            const auto revision=static_cast<std::uint64_t>(
                static_cast<juce::int64>(lab.getProperty("revision",juce::var(0))));
            auto error=armix::shows::NativeLiveShowCapture::captureDry(
                nativeStatus,name,captured,true);
            std::vector<armix::shows::BinaryState> blobs;
            if(error.isEmpty()) {
                // Do not read vendor parameters while an editor is open.
                // Output MUST already be muted by captureDry and the engine.
                closePluginEditor();
                std::vector<LiveDryRoutingLab::CapturedFxState> vendorStates;
                error=engine_.captureRoutedFxStates(epoch,revision,vendorStates);
                if(error.isEmpty()) {
                    const auto expected=lab.getProperty("fxCount",juce::var());
                    if(!expected.isInt() ||
                       static_cast<int>(expected)!=static_cast<int>(vendorStates.size()))
                        error="Native FX count changed during Show capture";
                }
                if(error.isEmpty()) {
                    juce::StringArray seen;
                    try {
                        for(const auto& state:vendorStates) {
                            const auto* catalog=catalog_.find(state.catalogId);
                            if(!catalog||state.lane<0||
                               state.lane>=LiveDryRoutingLab::fxSlots ||
                               state.slot<0||state.slot>=LiveDryRoutingLab::maxFxPerLane) {
                                error="VST3 catalog identity missing during Show capture";break;
                            }
                            auto& fx=state.lane<LiveDryRoutingLab::count
                                ?captured.strips[static_cast<std::size_t>(state.lane)].fx
                                :captured.aux[static_cast<std::size_t>(
                                    state.lane-LiveDryRoutingLab::count)].fx;
                            if(static_cast<int>(fx.size())!=state.slot) {
                                error="FX snapshot slot order changed";break;
                            }
                            armix::shows::BinaryState binary;
                            binary.bytes=state.binary;
                            binary.sha256=juce::SHA256(binary.bytes).toHexString();
                            fx.push_back({catalog->key(),catalog->uid,binary.sha256,
                                          state.bypass});
                            if(!seen.contains(binary.sha256)) {
                                seen.add(binary.sha256);
                                blobs.push_back(std::move(binary));
                            }
                        }
                        if(error.isEmpty()) {
                            captured.sourceBuild="G5E_NATIVE_FX_SHOW_CAPTURE";
                            armix::shows::SnapshotV2 verified;
                            if(!armix::shows::Codec::decode(
                                armix::shows::Codec::encode(captured),verified))
                                error="Native Show FX binary references invalid";
                            else captured=std::move(verified);
                        }
                    } catch(...) {error="VST3 Show capture allocation or hashing failure";}
                }
            }
            if(error.isNotEmpty()) {
                diagnostics_.record("NATIVE_SHOW_REJECTED",error);
                return reject(error);
            }
            auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
            const bool started=nativeShowWorker_.launch(settingsDirectory(),
                std::move(captured),std::move(blobs),[safe](armix::shows::NativeShowSaveWorker::Result result) {
                    if(safe==nullptr)return;
                    safe->nativeShowBusy_=false;
                    if(!result.ok()) {
                        safe->diagnostics_.record("NATIVE_SHOW_SAVE_FAIL",result.error);
                        safe->emitNotice("Native Show not saved: "+result.error,true);
                    }else{
                        safe->diagnostics_.record("NATIVE_FX_SHOW_SAVED",
                            result.showId+" | "+result.snapshotId);
                        safe->emitNotice("Verified Show saved with routed VST3 state: "+
                            result.name+". Use BROWSE SHOWS and RECALL while OUTPUT is muted.");
                    }
                    if(safe->browser_.loaded) {
                        auto ack=std::make_unique<juce::DynamicObject>();
                        ack->setProperty("ok",result.ok());
                        ack->setProperty("error",result.error);
                        ack->setProperty("showId",result.ok()?result.showId:juce::String());
                        ack->setProperty("snapshotId",result.ok()?result.snapshotId:juce::String());
                        safe->browser_.emitEventIfBrowserIsVisible("arMixNativeShowSaveAck",
                            juce::var(ack.release()));
                    }
                });
            if(!started)return reject("Native Show worker is busy. No session was modified.");
            nativeShowBusy_=true;
            return emitNotice("Saving ACK-verified routing and binary VST3 state on the Show IO worker.");
        }
        if (action == "routingDryShowList")
        {
            auto rejectList=[this](const juce::String& error) {
                if(browser_.loaded) {
                    auto ack=std::make_unique<juce::DynamicObject>();
                    ack->setProperty("ok",false);
                    ack->setProperty("error",error);
                    browser_.emitEventIfBrowserIsVisible(
                        "arMixNativeShowListAck",juce::var(ack.release()));
                }
                emitNotice("Native Show browse rejected: "+error,true);
            };
            if(nativeShowBusy_ || studioBackupBusy_)
                return rejectList("Finish current Show IO or USB backup first.");
            auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
            const auto started=nativeShowWorker_.list(settingsDirectory(),
                [safe](armix::shows::NativeShowSaveWorker::Result result) {
                    if(safe==nullptr)return;
                    safe->nativeShowBusy_=false;
                    auto ack=std::make_unique<juce::DynamicObject>();
                    ack->setProperty("ok",result.ok());
                    ack->setProperty("error",result.error);
                    juce::Array<juce::var> items;
                    for(const auto& item:result.items) {
                        auto row=std::make_unique<juce::DynamicObject>();
                        row->setProperty("id",item.id);
                        row->setProperty("name",item.name);
                        items.add(juce::var(row.release()));
                    }
                    ack->setProperty("items",juce::var(items));
                    if(safe->browser_.loaded)
                        safe->browser_.emitEventIfBrowserIsVisible(
                            "arMixNativeShowListAck",juce::var(ack.release()));
                    if(result.error.isNotEmpty())
                        safe->emitNotice("Native Show browsing failed: "+result.error,true);
                });
            if(!started)return rejectList("Cannot start native Show browser.");
            nativeShowBusy_=true;
            return;
        }
        if (action == "routingDryShowRecall")
        {
            const auto id=value.toString();
            auto reject=[this](const juce::String& error) {
                if(browser_.loaded) {
                    auto ack=std::make_unique<juce::DynamicObject>();
                    ack->setProperty("ok",false);
                    ack->setProperty("error",error);
                    browser_.emitEventIfBrowserIsVisible(
                        "arMixNativeShowRecallAck",juce::var(ack.release()));
                }
                emitNotice("Native DRY Show unchanged: "+error,true);
            };
            if(!armix::shows::detail::uuid(id))
                return reject("Select a native Show UUID from the saved list.");
            if(nativeShowBusy_ || studioBackupBusy_ || catalog_.busy() ||
               engine_.isGraphSwitchPending() || !engine_.allOutputsDisarmed())
                return reject("Mute native output and finish ongoing Studio work.");
            const auto d=engine_.status().getProperty("dryRoutingLab",{});
            const auto ep=d.getProperty("epoch",{}),rev=d.getProperty("revision",{});
            if(!static_cast<bool>(d.getProperty("ready",false)) ||
               !(ep.isInt()||ep.isInt64()) ||
               !(rev.isInt()||rev.isInt64()))
                return reject("Current native Routing LAB must be active and ACK'd.");
            const auto epoch=static_cast<juce::int64>(ep);
            const auto revision=static_cast<juce::int64>(rev);
            auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
            const bool started=nativeShowWorker_.read(settingsDirectory(),
                id,epoch,revision,
                [safe](armix::shows::NativeShowSaveWorker::Result result) {
                    if(safe==nullptr)return;
                    juce::String error=result.error;
                    juce::var command;
                    if(error.isEmpty()) {
                        const auto state=safe->engine_.status();
                        const auto lab=state.getProperty("dryRoutingLab",{});
                        const auto ep=static_cast<juce::int64>(
                            lab.getProperty("epoch",juce::var(0)));
                        const auto rev=static_cast<juce::int64>(
                            lab.getProperty("revision",juce::var(0)));
                        if(ep!=result.requestEpoch || rev!=result.requestRevision)
                            error="Device epoch or route revision changed during disk read";
                        else error=armix::shows::NativeDryShowRecall::prepare(
                            result.loaded,state,command,true);
                        if(error.isEmpty()) {
                            // All blob bytes were hash-verified on the IO worker.
                            // Resolve the exact current native catalog UID and
                            // build a fully prepared candidate, never mutate
                            // existing vendor processors still in the RT graph.
                            try {
                                LiveDryRoutingLab::FxSelections selections{};
                                LiveDryRoutingLab::FxRestores restores{};
                                const auto attach=[&](int lane,
                                    const std::vector<armix::shows::FxRef>& refs)
                                      ->juce::String {
                                    if(refs.size()>LiveDryRoutingLab::maxFxPerLane)
                                        return "FX Show rack exceeds live insert capacity";
                                    for(size_t slot=0;slot<refs.size();++slot) {
                                        const auto& ref=refs[slot];
                                        const auto* entry=safe->catalog_.find(ref.catalogId);
                                        if(!entry || entry->uid!=ref.uid)
                                            return "Saved VST3 missing/changed in current catalog: "+
                                                ref.catalogId;
                                        const auto binary=std::find_if(
                                            result.loadedStates.begin(),
                                            result.loadedStates.end(),
                                            [&](const armix::shows::BinaryState& b) {
                                                return b.sha256.equalsIgnoreCase(ref.stateSha256);
                                            });
                                        if(binary==result.loadedStates.end() ||
                                           binary->bytes.getSize()>8U*1024U*1024U)
                                            return "Verified vendor binary state missing: "+
                                                ref.catalogId;
                                        selections[lane][slot]={
                                            entry->key(),entry->path,entry->name,entry->uid};
                                        auto& target=restores[lane][slot];
                                        target.present=true;
                                        target.bypass=ref.bypass;
                                        target.binary=&binary->bytes; // synchronous borrow
                                    }
                                    return {};
                                };
                                for(int lane=0;lane<LiveDryRoutingLab::fxSlots &&
                                               error.isEmpty();++lane) {
                                    if(lane<LiveDryRoutingLab::count)
                                        error=attach(lane,result.loaded.strips[
                                            static_cast<size_t>(lane)].fx);
                                    else error=attach(lane,result.loaded.aux[
                                        static_cast<size_t>(
                                            lane-LiveDryRoutingLab::count)].fx);
                                }
                                if(error.isEmpty()) {
                                    safe->closePluginEditor();
                                    error=safe->engine_.applyDryRoutingLab(
                                        command,selections,&restores);
                                }
                            } catch(const std::exception& ex) {
                                error="Native VST3 Show preflight exception: "+
                                    juce::String(ex.what());
                            } catch(...) {
                                error="Native VST3 Show preflight failed";
                            }
                        }
                        if(error.isEmpty()) {
                            // Only AFTER native graph publication, and while
                            // physical output remains muted. Never auto-unmute.
                            safe->engine_.setMasterGainLinear(
                                static_cast<float>(result.loaded.masterGain));
                            safe->engine_.setMasterMuted(result.loaded.masterMute);
                            safe->engine_.setMasterMono(result.loaded.masterMono);
                            safe->diagnostics_.record("NATIVE_VST3_SHOW_RECALLED",
                                result.showId+" | "+result.snapshotId);
                        }
                    }
                    safe->nativeShowBusy_=false;
                    auto ack=std::make_unique<juce::DynamicObject>();
                    ack->setProperty("ok",error.isEmpty());
                    ack->setProperty("error",error);
                    ack->setProperty("showId",result.showId);
                    ack->setProperty("state",safe->engine_.status().getProperty(
                        "dryRoutingLab",{}));
                    if(safe->browser_.loaded)
                        safe->browser_.emitEventIfBrowserIsVisible(
                            "arMixNativeShowRecallAck",juce::var(ack.release()));
                    if(error.isNotEmpty())
                        safe->emitNotice("Native Show recall rejected: "+error,true);
                    else safe->emitNotice(
                        "Verified native Show routing and VST3 state restored in a newly prepared graph. OUTPUT remains MUTED; verify physical routing and plugin sound before unmuting.");
                });
            if(!started)return reject("Cannot launch native Show readback worker.");
            nativeShowBusy_=true;
            return;
        }
        // OUTPUT1: generic selected auxiliary playback destination.
        // Device selection, ASRC preparation and callback attach are CONTROL
        // thread operations. Only the existing native mixer produces PCM.
        if(action=="secondaryOutputConnect" ||
           action=="secondaryOutputArm" || action=="secondaryOutputStop") {
            juce::String error;
            if(studioBackupBusy_ || nativeShowBusy_)
                error="Complete active Show/backup operation before editing secondary output";
            else if(action=="secondaryOutputConnect") {
                if(!value.isString() || value.toString().length()>240)
                    error="Select a real named Windows playback endpoint";
                else error=engine_.connectSecondaryOutput(value.toString());
            } else if(action=="secondaryOutputArm") {
                if(!value.isBool()) error="Expected explicit Secondary output ARM true/false";
                else error=engine_.armSecondaryOutput(bool(value));
            } else engine_.disconnectSecondaryOutput();
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            const auto latest=engine_.status();
            ack->setProperty("ready",latest.getProperty("secondaryOutputReady",false));
            ack->setProperty("armed",latest.getProperty("secondaryOutputArmed",false));
            ack->setProperty("endpoint",latest.getProperty("secondaryOutputName",""));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixSecondaryOutputAck",juce::var(ack.release()));
            diagnostics_.record(error.isEmpty()?"SECONDARY_OUTPUT_CHANGED":"SECONDARY_OUTPUT_REJECTED",
                error.isNotEmpty()?error:action);
            return emitNotice(error.isNotEmpty()?error:
                (action=="secondaryOutputStop"?"Additional playback output disconnected":
                 action=="secondaryOutputArm"?(engine_.secondaryArmed()
                    ?"SECONDARY OUTPUT ARMED — MAIN mapped to selected playback endpoint":
                     "SECONDARY OUTPUT DISARMED"):
                    "Playback endpoint connected. ARM OUTPUT to send MAIN."),
                error.isNotEmpty());
        }
        if (action == "routingDryStop")
        {
            // Backward-compatible command name: CLEAR PATCH keeps the one
            // native 10CH+4AUX topology, with ALL physical inputs disconnected.
            // Never enable the legacy CH1 signal path on STOP or hotplug.
            closePluginEditor();
            const auto error=engine_.clearProductionRouting();
            diagnostics_.record(error.isEmpty()?"NATIVE_INPUT_PATCH_CLEARED":
                                 "NATIVE_INPUT_PATCH_CLEAR_FAILED",
                                error.isEmpty()?"No input assigned; output stays MUTED":error);
            if(browser_.loaded) {
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("ok",error.isEmpty());
                ack->setProperty("error",error);
                ack->setProperty("state",engine_.status().getProperty("dryRoutingLab",{}));
                browser_.emitEventIfBrowserIsVisible(
                    "arMixRoutingLabAck",juce::var(ack.release()));
            }
            return emitNotice(error.isEmpty()
                ?"All INPUT PATCH assignments cleared. Native mixer stays ready, OUTPUT MUTED."
                :"Input patch cleared; native session is SILENT: "+error,
                error.isNotEmpty());
        }

        if (action == "smartClock")
        {
            if (!value.isBool()) return emitNotice ("Invalid smart clock command", true);
            // Close third-party editors before device/processor preparation.
            stopOrbit();
            closePluginEditor();
            const auto error = engine_.setIndependentClockEnabled (bool (value));
            if (error.isNotEmpty())
            {
                diagnostics_.record ("SMART_CLOCK_FAIL", error);
                return emitNotice (error, true);
            }
            directRestore_.cancel();
            lastDirectRestoreError_.clear();
            diagnostics_.record ("SMART_CLOCK_MODE", juce::JSON::toString (engine_.status(), false));
            emitNotice (engine_.independentClockActive()
                ? "Independent capture/render clocks enabled (EXPERIMENTAL). Render-master ASRC, rollback on device fault."
                : "Direct JUCE audio path restored; no extra SRC buffer.");
        }
        else if (action == "copyDiagnosticReport")
        {
            diagnostics_.record ("REPORT_COPY", "User explicitly requested one-click report");
            const auto report = makeAudioReport();
            juce::SystemClipboard::copyTextToClipboard (report);
            auto response = std::make_unique<juce::DynamicObject>();
            response->setProperty ("bytes", static_cast<double> (report.getNumBytesAsUTF8()));
            response->setProperty ("generatedAt", juce::Time::getCurrentTime().toISO8601 (true));
            if (browser_.loaded)
                browser_.emitEventIfBrowserIsVisible ("arMixDiagnosticReportCopied", juce::var (response.release()));
            emitNotice ("Audio report copied! Paste it directly into the engineering chat.");
        }
        else if (action == "audioDeviceInventory")
        {
            // Fast read-only snapshot, no forced third-party ASIO scan.
            emitDeviceInventory(false);
        }
        else if (action == "audioDeviceRefresh")
        {
            // Slow vendor rescan: deliberate operator request, MUTE required.
            emitDeviceInventory(true);
        }
        else if (action == "audioDeviceApply")
        {
            applyCustomAudioDevice(args);
        }
        else if (action == "audioSetup")
        {
            if (engine_.independentClockActive())
            {
                const auto revert = engine_.setIndependentClockEnabled (false);
                // A graph handoff can block stopping the active independent
                // callbacks. Never open the device selector while they run.
                if (revert.isNotEmpty() && engine_.independentClockActive())
                    return emitNotice (revert, true);
                if (revert.isNotEmpty())
                    diagnostics_.record ("DIRECT_RESTORE_SETUP_FALLBACK", revert);
                else deviceSettingsDirty_ = true;
            }
            if (engine_.hasPendingDirectRestore())
            {
                // Explicit user intent overrides retrying a missing original
                // endpoint. Re-register the native callback so choosing a
                // replacement device works, but keep output muted for safety.
                if (!engine_.abandonPendingDirectRestoreForSetup())
                    return emitNotice ("Unable to prepare replacement audio device selection.", true);
                diagnostics_.record ("DIRECT_RESTORE_ABANDON",
                    "User selected AUDIO SETUP to replace unavailable prior endpoint");
                deviceSettingsDirty_ = false; // never persist an empty device
                emitNotice ("Original endpoint unavailable. Choose a replacement in AUDIO SETUP; output remains muted.", true);
            }
            directRestore_.cancel();
            lastDirectRestoreError_.clear();
            // One-sided device routes are temporary and must never leak into
            // normal user configuration or the persisted audio-devices.xml.
            if (engine_.hasTemporaryDiagnosticDevice())
            {
                const auto error = engine_.setDiagnosticRoute (armix::diagnostics::Route::normal);
                if (error.isNotEmpty()) return emitNotice (error, true);
                deviceSettingsDirty_ = true;
            }
            showDeviceSetup();
        }
        else if (action == "diagnosticRoute")
        {
            if (engine_.independentClockActive() || engine_.hasPendingDirectRestore())
            {
                const auto revert = engine_.setIndependentClockEnabled (false);
                if (revert.isNotEmpty()) return emitNotice (revert, true);
                directRestore_.cancel();
                lastDirectRestoreError_.clear();
                deviceSettingsDirty_ = true;
            }
            using Route = armix::diagnostics::Route;
            const auto selected = value.toString();
            auto route = Route::normal;
            if (selected == "OUTPUT_ONLY_TONE") route = Route::outputOnlyTone;
            else if (selected == "CAPTURE_ONLY") route = Route::captureOnly;
            else if (selected == "DRY_THROUGH") route = Route::dryThrough;
            else if (selected != "NORMAL") return emitNotice ("Unsupported diagnostic mode", true);
            if (audioSetupWindow_ != nullptr) audioSetupWindow_->setVisible (false);
            const auto error = engine_.setDiagnosticRoute (route);
            if (error.isNotEmpty())
            {
                diagnostics_.record ("AUDIO_TEST_FAIL", selected + " | " + error);
                return emitNotice (error, true);
            }
            // A live user setup, never the isolated test route, is persisted.
            if (route == Route::normal) deviceSettingsDirty_ = true;
            diagnostics_.record ("AUDIO_TEST_MODE", selected + " | " +
                juce::JSON::toString (engine_.status(), false));
            emitNotice ("Audio diagnostic: " + selected +
                ". Use low headphone volume and MARK CRACKLE on audible faults.");
        }
        else if (action == "diagnosticMark")
        {
            const auto status = engine_.status();
            diagnostics_.record ("AUDIO_GLITCH_MARK", juce::JSON::toString (status, false));
            diagnostics_.record ("AUDIO_GLITCH_WINDOW", crackleWindowReport (status));
            emitNotice ("Crackle marker saved with source-labeled pre-mark evidence (up to 5 s).");
        }
        else if (action == "backupStudio")
            chooseStudioBackup();
        else if (action == "presetList")
            emitPresets();
        else if (action == "presetSave")
        {
            if (engine_.hasTemporaryDiagnosticDevice())
                return emitNotice ("Return to NORMAL before saving FX graphs.", true);
            const auto name = armix::storage::NativePresetArchive::cleanName (
                args.getProperty ("name", {}).toString());
            if (name.isEmpty())
                return emitNotice ("Choose a valid preset name", true);
            if (!engine_.allOutputsDisarmed())
                return emitNotice ("Mute output in SIGNAL CHECK before saving a native sound preset. This avoids interrupting your live performance.", true);
            stopOrbit();
            closePluginEditor();
            armix::storage::Snapshot snapshot;
            snapshot.name = name;
            auto error = engine_.captureSnapshot (snapshot);
            if (error.isEmpty()) error = presets_.save (snapshot);
            if (error.isNotEmpty())
            {
                diagnostics_.record ("PRESET_SAVE_FAIL", name + ": " + error);
                emitNotice (error, true);
            }
            else
            {
                diagnostics_.record ("PRESET_SAVED", name + " (" + juce::String (snapshot.effects.size()) + " VST3)");
                emitPresets();
                emitNotice ("Native VST3 preset saved: " + name);
            }
        }
        else if (action == "presetLoad")
        {
            if (engine_.hasTemporaryDiagnosticDevice())
                return emitNotice ("Return to NORMAL before recalling FX graphs.", true);
            const auto name = args.getProperty ("name", {}).toString();
            if (engine_.isGraphSwitchPending())
                return emitNotice ("FX recall is already transitioning; retry shortly.", true);
            stopOrbit();
            closePluginEditor();
            armix::storage::Snapshot snapshot;
            auto error = presets_.load (name, snapshot);
            if (error.isEmpty()) error = engine_.restoreSnapshot (snapshot);
            if (error.isNotEmpty())
            {
                diagnostics_.record ("PRESET_LOAD_FAIL", name + ": " + error);
                emitNotice ("Preset unchanged: " + error, true);
            }
            else
            {
                // UI reflects state only AFTER the audio thread acknowledges
                // the staged plan; avoid claiming active while it is still old.
                auto updated = std::make_unique<juce::DynamicObject>();
                updated->setProperty ("name", name);
                updated->setProperty ("channelDb", snapshot.channelDb);
                updated->setProperty ("masterDb", snapshot.masterDb);
                updated->setProperty ("pan", snapshot.pan * 100.f);
                updated->setProperty ("channelMute", snapshot.channelMute);
                updated->setProperty ("masterMute", snapshot.masterMute);
                pendingPresetUI_ = juce::var (updated.release());
                pendingPresetName_ = name;
                diagnostics_.record ("PRESET_GRAPH_QUEUED", name + " (" +
                    juce::String (snapshot.effects.size()) + " VST3)");
                emitNotice ("Preparing live transition to sound: " + name);
            }
        }
        else if (action == "openDiagnostics")
        {
            const auto file = diagnostics_.getLogFile();
            if (file.existsAsFile()) file.revealToUser();
            else file.getParentDirectory().revealToUser();
        }
        else if (action == "scanVst3")
        {
            diagnostics_.record ("PLUGIN_SCAN", "Scan standard VST3 folders");
            catalog_.scanDefaults();
        }
        else if (action == "addScanFolder")
        {
            chooseFolder();
        }
        else if (action == "importVst3")
        {
            chooseImport (true); // Copy full VST3 into Studio by default.
        }
        else if (action == "importVst3InPlace")
        {
            chooseImport (false); // Explicit user choice: original external path.
        }
        else if (action == "importVst3Bundle")
        {
            chooseImport (true, true); // Native folder chooser, copied to Studio.
        }
        else if (action == "importVst3BundleInPlace")
        {
            chooseImport (false, true); // Explicit external bundle reference.
        }
        else if (action == "setPluginFavorite")
        {
            // User-owned portable catalog preference, not a VST instance edit.
            const auto id = args.getProperty ("id", {}).toString();
            const bool favorite = static_cast<bool> (args.getProperty ("value", false));
            if (!catalog_.setFavorite (id, favorite))
                emitNotice ("Favorite not changed: plugin is no longer indexed.", true);
        }
        else if (action == "addCatalog")
        {
            const auto requestId = static_cast<int> (args.getProperty ("requestId", 0));
            if (engine_.hasTemporaryDiagnosticDevice())
            {
                const auto* blocked = catalog_.find (args.getProperty ("id", {}).toString());
                const auto name = blocked ? blocked->name : juce::String ("Unknown VST3");
                const auto error = juce::String ("Return to NORMAL before adding VST3 effects.");
                emitFxAddResult (requestId, name, error, 0);
                return emitNotice (error, true);
            }
            const auto requestedId = args.getProperty ("id", {}).toString();
            const auto* plugin = catalog_.find (requestedId);
            const auto name = plugin != nullptr ? plugin->name : juce::String ("Unknown VST3");
            const auto started = juce::Time::getMillisecondCounter();
            juce::String error;

            if (plugin == nullptr)
                error = "Plugin is no longer in the catalog. Rescan in PLUGINS and try again.";
            else
            {
                diagnostics_.record ("FX_ADD_REQUEST", name + " | " + plugin->path);
                // Appending a new VST3 preserves existing AudioPluginInstance addresses.
                // Orbit references a still-live processor and must continue through ADD.
                // Orbit retains the same vendor instance across the prepared graph handoff.
                const bool keepOrbit=orbit_.active();
                if(keepOrbit)orbit_.pauseForGraphEdit();
                closePluginEditor();
                try
                {
                    error = engine_.addVst3 (plugin->path, plugin->uid, plugin->key());
                }
                catch (const std::exception& ex)
                {
                    error = "VST3 instantiation threw an exception: " + juce::String (ex.what());
                }
                catch (...)
                {
                    error = "VST3 instantiation failed with an unknown exception";
                }
                if(keepOrbit && error.isNotEmpty())orbit_.resumeAfterGraphEdit();
            }
            const auto elapsed = juce::Time::getMillisecondCounter() - started;
            if (error.isNotEmpty())
            {
                emitFxAddResult (requestId,name,error,elapsed);
                diagnostics_.record("FX_LOAD_FAIL",name+" | "+error+
                    " | elapsedMs="+juce::String(elapsed));
                emitNotice(name+": "+error,true);
            }
            else
            {
                pendingFxAddRequestId_=requestId;
                pendingFxAddName_=name;
                pendingFxAddElapsedMs_=elapsed;
                diagnostics_.record("FX_ADD_STAGED",name);
                // The timer sends positive arMixAddResult only once the
                // callback has published the graph and retired old leases.
            }
        }
        else if (action == "fxRemove")
        {
            if (engine_.hasTemporaryDiagnosticDevice())
                return emitNotice ("Return to NORMAL before editing VST3.", true);
            const auto* previous=orbit_.processor();
            const bool removedOrbit=previous && engine_.pluginAt(slot)==previous;
            if(orbit_.active())orbit_.pauseForGraphEdit();
            closePluginEditor();
            const bool changed=engine_.removeAt(slot);
            if(changed) {
                // Orbit stays paused until the callback and control thread
                // acknowledge the NEW logical graph; only stop a deleted
                // target early to avoid dangling vendor parameter handles.
                if(removedOrbit)stopOrbit();
                pendingFxRemoveSlot_=slot;
                diagnostics_.record("FX_REMOVE_STAGED",juce::String(slot));
            } else {
                if(orbit_.active())orbit_.resumeAfterGraphEdit();
                emitNotice("FX removal rejected: device unavailable or graph busy",true);
            }
        }
        else if (action == "fxMove")
        {
            if (engine_.hasTemporaryDiagnosticDevice())
                return emitNotice ("Return to NORMAL before rearranging VST3 effects.", true);
            const auto* orbitPlugin=orbit_.processor();
            orbit_.pauseForGraphEdit();
            // Logical FX reorder preserves the existing vendor editor and stable plugin identity.
            const bool changed=engine_.moveSlot(slot,slot+static_cast<int>(value));
            if(orbitPlugin && orbit_.active()){
                for(int i=0;i<engine_.chainSize();++i)
                    if(engine_.pluginAt(i)==orbitPlugin){orbit_.rebindSlot(i);break;}
                orbit_.resumeAfterGraphEdit();
            }
            if(changed){
                ++fxGraphRevision_;
                diagnostics_.record("FX_MOVE",juce::String(slot));
                emitChain();
                if(orbit_.active())emitOrbitState(orbit_.slot());
            }
        }
        else if (action == "fxBypass")
        {
            if (value.isBool() && engine_.setBypass (slot, bool (value))) { diagnostics_.record ("FX_BYPASS", juce::String (slot) + (bool (value) ? " ON" : " OFF")); emitChain(); }
        }
        else if (action == "orbitInspect")
        {
            const auto requested=args.getProperty("shape",-1);
            const int shape=requested.isInt()?static_cast<int>(requested):-1;
            emitOrbitState(slot,shape);
        }
        else if (action == "orbitStart")
        {
            if (engine_.isGraphSwitchPending())
                return emitNotice ("Finish FX preset recall before starting Orbit.", true);
            const auto requestedRevision = args.getProperty ("revision", {}).toString();
            if (requestedRevision.isEmpty() || requestedRevision != juce::String (fxGraphRevision_))
                return emitNotice ("FX graph changed. Reopen ORBIT to verify this instance.", true);
            const auto requestedPluginId = args.getProperty ("pluginId", {}).toString();
            const auto chain = engine_.chainState();
            const auto* currentSlots = chain.getArray();
            if (requestedPluginId.isEmpty() || currentSlots == nullptr || slot < 0 ||
                slot >= currentSlots->size() ||
                (*currentSlots)[slot].getProperty ("id", {}).toString() != requestedPluginId)
                return emitNotice ("FX chain changed. Reopen ORBIT to select the current plugin.", true);
            const auto paramValue = args.getProperty ("parameterIndex", -1);
            const auto secondsValue = args.getProperty ("seconds", 20.0);
            const auto reverseValue = args.getProperty ("reverse", false);
            if (!(paramValue.isInt() || paramValue.isInt64()) ||
                !(secondsValue.isDouble() || secondsValue.isInt()) ||
                !reverseValue.isBool())
                return emitNotice ("Invalid Orbit settings.", true);
            const int parameterIndex = static_cast<int> (paramValue);
            auto* targetPlugin=engine_.pluginAt(slot);
            if(!targetPlugin || !armix::core::isDearVrPro2(targetPlugin->getName().toStdString()))
                return emitNotice("Orbit for dearVR PRO 2 only.",true);
            const auto shapeArg=args.getProperty("shape",0);
            const auto distanceArg=args.getProperty("distanceIndex",-1);
            const auto elevationArg=args.getProperty("elevationIndex",-1);
            const auto nearArg=args.getProperty("nearDistance",-1.0);
            const auto farArg=args.getProperty("farDistance",-1.0);
            const auto unitArg=args.getProperty("distanceUnit",{}).toString();
            if(!shapeArg.isInt()||!distanceArg.isInt()||!elevationArg.isInt()||
                !(nearArg.isDouble()||nearArg.isInt())||
                !(farArg.isDouble()||farArg.isInt()))
                return emitNotice("Invalid motion setpoint types.",true);
            const int shape=static_cast<int>(shapeArg);
            const int distanceIndex=static_cast<int>(distanceArg);
            if(!armix::core::validOrbitShape(shape))
                return emitNotice("Unknown Orbit trajectory.",true);
            const double rawNear=static_cast<double>(nearArg);
            const double rawFar=static_cast<double>(farArg);
            float nearValue=0.f,farValue=1.f;
            if(unitArg=="m"){
                auto* plugin=engine_.pluginAt(slot);
                if(!plugin||distanceIndex<0||distanceIndex>=plugin->getParameters().size())
                    return emitNotice("Choose a Distance parameter supporting metres.",true);
                auto* param=plugin->getParameters()[distanceIndex];
                if(!metresToNormalized(param,rawNear,nearValue)||
                   !metresToNormalized(param,rawFar,farValue))
                    return emitNotice("This VST3 cannot represent the requested metres exactly. Choose a supported value.",true);
            }else if(unitArg=="%"){
                if(!std::isfinite(rawNear)||!std::isfinite(rawFar)||
                   rawNear<0||rawFar>100)
                    return emitNotice("Distance must be between 0 and 100% of the VST3 parameter.",true);
                nearValue=static_cast<float>(rawNear/100.0);
                farValue=static_cast<float>(rawFar/100.0);
            }else return emitNotice("Unknown distance units.",true);
            if(nearValue>farValue)return emitNotice("Near must not exceed Far.",true);
            if (orbit_.active() && orbit_.slot() != slot) stopOrbit();
            const auto error = orbit_.start(engine_.pluginAt(slot),slot,parameterIndex,
                static_cast<double>(secondsValue),bool(reverseValue),shape,
                distanceIndex,static_cast<int>(elevationArg),nearValue,farValue);
            if (error.isNotEmpty()) return emitNotice ("Orbit: " + error, true);
            diagnostics_.record ("FX_ORBIT_POLAR",orbit_.polarStatus());
            diagnostics_.record ("FX_ORBIT_START", "slot=" + juce::String (slot) +
                " parameter=" + juce::String (parameterIndex) +
                " revolutionSec=" + juce::String (orbit_.secondsPerRevolution()) +
                " shape=" + juce::String (orbit_.shape()) +
                " near=" + juce::String(orbit_.nearDistance(),4) +
                " far=" + juce::String(orbit_.farDistance(),4));
            emitOrbitState (slot);
            emitNotice(orbit_.polarStatus()=="MANUAL_REQUIRED"
                ? "Orbit started. Set POSITION → POLAR in dearVR; vendor did not expose a verified mode parameter."
                : "dearVR Orbit started in POLAR mode.");
        }
        else if (action == "orbitStop")
        {
            stopOrbit();
            diagnostics_.record ("FX_ORBIT_STOP", "User requested stop");
            emitOrbitState (slot);
        }
        else if (action == "fxEditor")
        {
            // Pending callback handoff may retire pluginAt() immediately after this event.
            // Never open an editor for an old graph waiting to be destroyed.
            if (engine_.isGraphSwitchPending())
                return emitNotice ("FX recall is transitioning; open the editor after recall completes.", true);
            closePluginEditor();
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
        {
            // P0 panic MUTE is always accepted. Explicit ARM is verified
            // against the actual native route, never against UI appearance.
            const bool arm=bool(value);
            juce::String fault;
            if (arm && (nativeShowBusy_ || studioBackupBusy_))
                fault="Finish Show/backup work before arming hardware OUTPUT.";
            const auto expectedEpoch=args.getProperty("expectedEpoch",juce::var());
            const auto expectedRevision=args.getProperty("expectedRevision",juce::var());
            if (arm && fault.isEmpty() && !expectedEpoch.isVoid() &&
                static_cast<std::uint64_t>(static_cast<juce::int64>(expectedEpoch))
                    !=engine_.dryRoutingLabEpoch())
                fault="Audio device changed during START AUDIO; recheck routing.";
            if (arm && fault.isEmpty() && !expectedRevision.isVoid() &&
                static_cast<std::uint64_t>(static_cast<juce::int64>(expectedRevision))
                    !=engine_.dryRoutingLabRevision())
                fault="Native route revision changed; review hardware patch.";
            // Isolated output-tone testing remains supported in SIGNAL CHECK.
            if (arm && fault.isEmpty() &&
                engine_.diagnosticRoute()==armix::diagnostics::Route::normal)
                fault=engine_.liveOutputArmFault();
            if (fault.isEmpty()) {
                engine_.setMonitorEnabled(arm);
                diagnostics_.record("OUTPUT_MUTE",arm?"OFF":"ON");
            }
            if (browser_.loaded) {
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("ok",fault.isEmpty());
                ack->setProperty("armed",engine_.isMonitorEnabled());
                ack->setProperty("error",fault);
                ack->setProperty("deviceEpoch",
                    static_cast<double>(engine_.dryRoutingLabEpoch()));
                browser_.emitEventIfBrowserIsVisible("arMixAudioArmAck",
                    juce::var(ack.release()));
            }
            if (fault.isNotEmpty()) emitNotice(fault,true);
        }
        else if (action == "dryTest" && value.isBool())
        {
            engine_.setDryTestEnabled (bool (value));
            diagnostics_.record ("DRY_TEST", bool (value) ? "ON" : "OFF");
        }
        else if (action == "refreshState")
        {
            emitCatalog();
            emitChain();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // Save device state only after a real change, not on every 3-second tick.
        deviceSettingsDirty_ = true;
        if(routedEditorOwner_)closePluginEditor();
        diagnostics_.record ("DEVICE_CHANGE", "Device manager event");
    }

    void timerCallback() override
    {
        ++timerTicks_;
        // A hidden JUCE/WebView2 peer may defer first navigation. Expose the
        // compositor behind the opaque startup splash after the first tick;
        // the user sees the completed mixer only after the ready handshake.
        if (!webViewPeerWarmed_)
        {
            webViewPeerWarmed_ = true;
            setVisible (true);
            toBack();
        }
        // Existing message-thread 12 Hz timer: no extra worker or DSP work.
        // Never allow a hidden broken GUI to continue unattended audio.
        if (!browser_.loaded && !webViewStartupNoticeSent_ &&
            juce::Time::getMillisecondCounter() - webViewStartupTick_ > 10000u)
        {
            webViewStartupNoticeSent_ = true;
            diagnostics_.record ("WEBVIEW2_GUI_READY_TIMEOUT", "No native ArSonMix UI handshake");
            engine_.stop();
            if (onStartupStage_) onStartupStage_ (3); // dismiss splash before error dialog
            showWebViewStartupError ("The embedded mixer did not initialize in 10 seconds."
                                     " Check the WebView2 Runtime and browser-cache folder permissions.");
            return;
        }
        // First visible GUI frame must precede device discovery. Reuse the
        // existing timer; never add a worker or poll the audio callback.
        if (!audioEngineStarted_)
        {
            if (!browser_.loaded ||
                juce::Time::getMillisecondCounter() - guiReadyTick_ < 150u)
                return;
            audioEngineStarted_ = true;
            auto saved = juce::XmlDocument::parse (
                machineSettingsDirectory().getChildFile ("audio-devices.xml"));
            startupError_ = engine_.start (saved.get());
            engine_.devices().addChangeListener (this);
            diagnostics_.record ("STARTUP",
                startupError_.isEmpty() ? "Audio device initialized" : startupError_);
            if (onStartupStage_) onStartupStage_ (2);
        }
        // ONE actionable notice per unexpected DIRECT reroute. The audio
        // callback has already zero-filled output atomically; status, log and
        // WebView notifications belong only on this JUCE control timer.
        const auto diagnosticRoute = engine_.diagnosticRoute();
        const bool blocked = engine_.unexpectedDirectRoute()
            && !engine_.independentClockActive()
            && diagnosticRoute != armix::diagnostics::Route::outputOnlyTone
            && diagnosticRoute != armix::diagnostics::Route::captureOnly;
        if (blocked && !unexpectedRouteNoticeSent_)
        {
            unexpectedRouteNoticeSent_ = true;
            diagnostics_.record ("DIRECT_ROUTE_BLOCKED",
                "A device or backend changed unexpectedly; host output held at zero");
            emitNotice ("Audio device changed unexpectedly. Live output muted; confirm your ASIO/WASAPI device in AUDIO SETUP.", true);
        }
        if (!blocked) unexpectedRouteNoticeSent_ = false;
        // Audio thread publishes only fault flags. A bounded message-thread
        // restore transaction handles device failures without hot-plug spin.
        serviceDirectRestore();
        if (engine_.finalizeGraphSwitch())
        {
            ++fxGraphRevision_;
            diagnostics_.record ("GRAPH_SWAP_ACK", pendingPresetName_);
            emitChain();
            if(pendingFxAddRequestId_>=0)
            {
                if(orbit_.active())orbit_.resumeAfterGraphEdit();
                emitFxAddResult(pendingFxAddRequestId_,pendingFxAddName_,{},
                                pendingFxAddElapsedMs_);
                diagnostics_.record("FX_ADD_ACK",pendingFxAddName_);
                emitNotice(pendingFxAddName_+" inserted into Channel 1 FX Rack.");
                pendingFxAddRequestId_=-1;
                pendingFxAddName_.clear();
                pendingFxAddElapsedMs_=0;
            }
            if(pendingFxRemoveSlot_>=0)
            {
                // The replacement graph has NOW become authoritative.
                // Rebind Orbit by stable processor identity, not by its old
                // slot index (which may shift when an earlier FX is removed).
                if(orbit_.active() && orbit_.processor())
                {
                    const auto* retained=orbit_.processor();
                    int target=-1;
                    for(int i=0;i<engine_.chainSize();++i)
                        if(engine_.pluginAt(i)==retained){target=i;break;}
                    if(target>=0){orbit_.rebindSlot(target);orbit_.resumeAfterGraphEdit();}
                    else stopOrbit();
                }
                diagnostics_.record("FX_REMOVE_ACK",juce::String(pendingFxRemoveSlot_));
                pendingFxRemoveSlot_=-1;
            }
            if(orbit_.active())emitOrbitState(orbit_.slot());
            if (pendingPresetUI_.isObject() && browser_.loaded)
                browser_.emitEventIfBrowserIsVisible ("arMixNativePresetLoaded", pendingPresetUI_);
            if (pendingPresetName_.isNotEmpty())
                emitNotice ("Live sound recalled: " + pendingPresetName_);
            pendingPresetUI_ = juce::var();
            pendingPresetName_.clear();
        }
        if (timerTicks_ % 12 == 0)
        {
            auto health = engine_.status();
            crackleEvidence_.push (makeEvidenceSample(health));
            diagnostics_.record ("AUDIO_HEALTH", juce::JSON::toString (health, false));
        }

        if (deviceSettingsDirty_)
        {
            deviceSettingsDirty_ = false;
            // DeviceManager signals even when a temporary capture-only /
            // output-only route is selected. Never persist that test route.
            if (!engine_.hasTemporaryDiagnosticDevice() && !engine_.independentClockActive() &&
                engine_.directRouteAuthorized() &&
                (audioSetupWindow_ == nullptr || !audioSetupWindow_->isVisible()) &&
                engine_.devices().getCurrentAudioDevice() != nullptr)
            if (auto xml = engine_.devices().createStateXml())
            {
                const auto text = xml->toString();
                if (text != lastAudioXml_)
                {
                    // A clean portable PC has no AppData/AR-MIX-Studio
                    // directory yet. Create the machine-local parent before
                    // writing the approved ASIO/WASAPI device XML.
                    const auto machineFolder = machineSettingsDirectory();
                    const auto folderReady = machineFolder.createDirectory().wasOk();
                    if (!folderReady ||
                        !machineFolder.getChildFile ("audio-devices.xml").replaceWithText (text))
                    {
                        diagnostics_.record ("DEVICE_SAVE_FAIL",
                            "Cannot persist audio setup in machine AppData");
                        emitNotice ("Audio setup is active but could not be saved on this PC. Check AppData folder permissions.", true);
                    }
                    else lastAudioXml_ = text;
                }
            }
        }
        if (!browser_.loaded) return;
        if (!initialStateSent_)
        {
            initialStateSent_ = true;
            emitCatalog();
            emitScanProgress();
            emitChain();
            emitPresets();
        }
        if (catalog_.busy() && timerTicks_ % 3 == 0)
            emitScanProgress();
        auto state = engine_.status();
        if(routedEditorOwner_) {
            // Unplug/reopen may bypass normal GUI actions. Never keep a
            // vendor editor on an invalid device epoch or removed instance.
            const auto route=state.getProperty("dryRoutingLab",{});
            bool retained=false;
            if(static_cast<bool>(route.getProperty("ready",false)) &&
               static_cast<juce::int64>(route.getProperty("epoch",juce::var(0)))
                   ==routedEditorEpoch_) {
                const auto rows=route.getProperty("fxInstances",juce::var());
                if(const auto* lanes=rows.getArray())
                    for(const auto& lane:*lanes)
                        if(const auto* slots=lane.getArray())
                            for(const auto& slot:*slots)
                                if(slot.getProperty("instanceId",{}).toString()
                                      ==routedEditorOwner_->instanceId)
                                    retained=true;
            }
            if(!retained)closePluginEditor();
        }
        annotateRecovery (state);
        state.getDynamicObject()->setProperty ("diagnosticsDropped",
            static_cast<double> (diagnostics_.lostRecords()));
        state.getDynamicObject()->setProperty ("diagnosticsTruncated",
            static_cast<double> (diagnostics_.truncatedRecords()));
        if (startupError_.isNotEmpty() && !bool (state.getProperty ("connected", false)))
            state.getDynamicObject()->setProperty ("error", startupError_);
        browser_.emitEventIfBrowserIsVisible ("arMixStatus", state);
        // Four status readbacks/second on the control thread. No JUCE
        // callback mutation, graph pointer lease or extra signal engine.
        if(consoleSubscribed_ && timerTicks_%3==0)
            emitNativeConsoleSnapshot(state);
    }
};

class ConsoleApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "ArSonMix"; }
    const juce::String getApplicationVersion() override { return "0.5.0-native-catalog"; }
    // Prevent competing audio-device owners and WebView2 sessions.
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (window_)
        {
            window_->setMinimised (false);
            window_->toFront (true);
            window_->grabKeyboardFocus();
        }
    }
    void initialise (const juce::String&) override
    {
        // Paint the approved dark native splash BEFORE browser cache I/O,
        // WebView2 initialization, device scan or native console construction.
        splash_ = std::make_unique<ArSonMixStartupSplash>();
        startTimer (90); // one-shot handoff after Windows first paint
    }
    void timerCallback() override
    {
        stopTimer();
        const auto folder = armix::storage::PortableWorkspace::browserDataRoot();
        if (!folder.createDirectory().wasOk() && !folder.isDirectory())
        {
            splash_.reset();
            showWebViewStartupError ("Cannot create the browser-cache folder: " + folder.getFullPathName());
            return;
        }
        if (!juce::WebBrowserComponent::areOptionsSupported (webViewOptions()))
        {
            splash_.reset();
            showWebViewStartupError ("WebView2 Runtime could not be initialized on this machine.");
            return;
        }
        window_ = std::make_unique<ConsoleWindow> ([this] (int stage)
        {
            if (splash_) splash_->setStage (stage);
            if (stage >= 2)
            {
                splash_.reset(); // no overlay over live mixer after engine init
                if (window_) window_->toFront (true);
            }
        });
        // Opaque cover exactly matches the large HWND, not a small 740×420
        // card leaving white WebView2 edges exposed during first paint.
        if (splash_ && window_)
        {
            splash_->setBounds (window_->getBounds());
            splash_->toFront (true);
        }
    }
    void shutdown() override
    {
        stopTimer();
        window_.reset();
        splash_.reset();
    }
private:
    std::unique_ptr<ArSonMixStartupSplash> splash_;
    std::unique_ptr<ConsoleWindow> window_;
};
}
START_JUCE_APPLICATION (ConsoleApp)
