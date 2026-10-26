// SPDX-License-Identifier: AGPL-3.0-only
// Native VST3 FX catalog, plugin rack and audio-device bridge.
// Audio runs exclusively in JUCE AudioIODeviceCallback; WebView is control only.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include "LiveAudioEngine.h"
#include "DiagnosticsRecorder.h"
#include "../engine/device/DirectRestoreBackoff.h"
#include "../engine/device/PeriodCapabilityEvidence.h"
#if defined(_WIN32)
#include "../engine/device/WasapiCapabilityProbe.h"
#endif
#include "PluginCatalog.h"
#include <cstring>
#include <optional>
#include <memory>
#include <chrono>
#include <functional>

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
    else if (path == "/monitor.js")
        { data = BinaryData::monitor_js; size = BinaryData::monitor_jsSize; mime = "text/javascript"; }
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
        auto* selector = new juce::AudioDeviceSelectorComponent (
            manager, 1, 2, 1, 2, false, false, false, false);
        setContentOwned (selector, true);
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
    ConsoleWindow()
        : DocumentWindow ("AR MIX Studio — Native VST3 FX Host",
                         juce::Colour (0xff101921), allButtons),
          catalog_ (settingsDirectory()),
          diagnostics_ (settingsDirectory().getChildFile ("diagnostics")),
          presets_ (settingsDirectory().getChildFile ("presets")),
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
        catalog_.onChanged = [this] { emitCatalog(); emitScanProgress(); };
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setResizeLimits (1100, 650, 3840, 2160);
        setContentNonOwned (&browser_, false);
        centreWithSize (1650, 930);
        setVisible (true);

        auto saved = juce::XmlDocument::parse (settingsDirectory().getChildFile ("audio-devices.xml"));
        startupError_ = engine_.start (saved.get());
        engine_.devices().addChangeListener (this);
        diagnostics_.record ("STARTUP", startupError_.isEmpty() ? "Audio device initialized" : startupError_);
        browser_.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
        startTimerHz (12);
        // Defer initial scan until explicitly requested in Plugin Manager.
        // Startup must not spend seconds enumerating third-party VST3 modules.
    }

    ~ConsoleWindow() override
    {
        stopTimer();
        catalog_.onChanged = nullptr;
        editorWindow_.reset();
        audioSetupWindow_.reset();
        engine_.devices().removeChangeListener (this);
        engine_.stop();
        browser_.setVisible (false);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    LiveAudioEngine engine_;
    PluginCatalog catalog_;
    DiagnosticsRecorder diagnostics_;
    armix::storage::NativePresetArchive presets_;
    Browser browser_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<PluginEditorWindow> editorWindow_;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow_;
    juce::String startupError_, lastAudioXml_;
    bool initialStateSent_ = false;
    bool setupLaunching_ = false;
    unsigned int timerTicks_ = 0;
    bool deviceSettingsDirty_ = false;
    bool unexpectedRouteNoticeSent_ = false;
    armix::device::DirectRestoreBackoff directRestore_;
    juce::String lastDirectRestoreError_;
    juce::var pendingPresetUI_;
    juce::String pendingPresetName_;

    static std::uint64_t monotonicMilliseconds() noexcept
    {
        return static_cast<std::uint64_t> (
            std::chrono::duration_cast<std::chrono::milliseconds> (
                std::chrono::steady_clock::now().time_since_epoch()).count());
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
        // The app.js bridge converts strip index 0 to channel ID 1.
        else if (name == "faderDb" && channel == 1 && (value.isDouble() || value.isInt()))
            engine_.setChannelGainDb (valueNumber);
        else if (name == "pan" && channel == 1 && (value.isDouble() || value.isInt()))
            engine_.setChannelPan (valueNumber * 0.01f);
        else if (name == "mute" && channel == 1 && value.isBool())
            engine_.setChannelMuted (bool (value));
        else if (name == "masterMute" && value.isBool())
            engine_.setMasterMuted (bool (value));
    }

    void emitScanProgress()
    {
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixScanProgress", catalog_.progress());
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
            safe->setupLaunching_ = false;
        });
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

        // Structural plugin operations currently detach the original callback
        // and must never mutate a running independent render thread.
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

        if (action == "smartClock")
        {
            if (!value.isBool()) return emitNotice ("Invalid smart clock command", true);
            // Close third-party editors before device/processor preparation.
            editorWindow_.reset();
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
            diagnostics_.record ("AUDIO_GLITCH_MARK", juce::JSON::toString (engine_.status(), false));
            emitNotice ("Crackle marker saved with current driver and callback counters.");
        }
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
            if (!engine_.isOutputMuted())
                return emitNotice ("Mute output in SIGNAL CHECK before saving a native sound preset. This avoids interrupting your live performance.", true);
            editorWindow_.reset();
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
            editorWindow_.reset();
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
            chooseImport();
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
                editorWindow_.reset();
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
            }
            const auto elapsed = juce::Time::getMillisecondCounter() - started;
            emitFxAddResult (requestId, name, error, elapsed);
            if (error.isNotEmpty())
            {
                diagnostics_.record ("FX_LOAD_FAIL", name + " | " + error +
                    " | elapsedMs=" + juce::String (elapsed));
                emitNotice (name + ": " + error, true);
            }
            else
            {
                diagnostics_.record ("FX_LOAD", name + " | elapsedMs=" + juce::String (elapsed));
                emitChain();
                emitNotice (name + " inserted into Channel 1 FX Rack.");
            }
        }
        else if (action == "fxRemove")
        {
            if (engine_.hasTemporaryDiagnosticDevice())
                return emitNotice ("Return to NORMAL before editing VST3.", true);
            editorWindow_.reset();
            if (engine_.removeAt (slot)) { diagnostics_.record ("FX_REMOVE", juce::String (slot)); emitChain(); }
        }
        else if (action == "fxMove")
        {
            if (engine_.hasTemporaryDiagnosticDevice())
                return emitNotice ("Return to NORMAL before rearranging VST3 effects.", true);
            editorWindow_.reset();
            if (engine_.moveSlot (slot, slot + static_cast<int> (value))) { diagnostics_.record ("FX_MOVE", juce::String (slot)); emitChain(); }
        }
        else if (action == "fxBypass")
        {
            if (value.isBool() && engine_.setBypass (slot, bool (value))) { diagnostics_.record ("FX_BYPASS", juce::String (slot) + (bool (value) ? " ON" : " OFF")); emitChain(); }
        }
        else if (action == "fxEditor")
        {
            // Pending callback handoff may retire pluginAt() immediately after this event.
            // Never open an editor for an old graph waiting to be destroyed.
            if (engine_.isGraphSwitchPending())
                return emitNotice ("FX recall is transitioning; open the editor after recall completes.", true);
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
        {
            engine_.setMonitorEnabled (bool (value));
            diagnostics_.record ("OUTPUT_MUTE", bool (value) ? "OFF" : "ON");
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
        diagnostics_.record ("DEVICE_CHANGE", "Device manager event");
    }

    void timerCallback() override
    {
        ++timerTicks_;
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
            diagnostics_.record ("GRAPH_SWAP_ACK", pendingPresetName_);
            emitChain();
            if (pendingPresetUI_.isObject() && browser_.loaded)
                browser_.emitEventIfBrowserIsVisible ("arMixNativePresetLoaded", pendingPresetUI_);
            if (pendingPresetName_.isNotEmpty())
                emitNotice ("Live sound recalled: " + pendingPresetName_);
            pendingPresetUI_ = juce::var();
            pendingPresetName_.clear();
        }
        if (timerTicks_ % 12 == 0)
            diagnostics_.record ("AUDIO_HEALTH", juce::JSON::toString (engine_.status(), false));

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
                    lastAudioXml_ = text;
                    if (!settingsDirectory().getChildFile ("audio-devices.xml").replaceWithText (text))
                        diagnostics_.record ("DEVICE_SAVE_FAIL", "Unable to persist device settings");
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
        annotateRecovery (state);
        state.getDynamicObject()->setProperty ("diagnosticsDropped",
            static_cast<double> (diagnostics_.lostRecords()));
        state.getDynamicObject()->setProperty ("diagnosticsTruncated",
            static_cast<double> (diagnostics_.truncatedRecords()));
        if (startupError_.isNotEmpty() && !bool (state.getProperty ("connected", false)))
            state.getDynamicObject()->setProperty ("error", startupError_);
        browser_.emitEventIfBrowserIsVisible ("arMixStatus", state);
    }
};

class ConsoleApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ArSonMix"; }
    const juce::String getApplicationVersion() override { return "0.5.0-native-catalog"; }
    void initialise (const juce::String&) override { window_ = std::make_unique<ConsoleWindow>(); }
    void shutdown() override { window_.reset(); }
private:
    std::unique_ptr<ConsoleWindow> window_;
};
}
START_JUCE_APPLICATION (ConsoleApp)
