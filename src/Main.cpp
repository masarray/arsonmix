// SPDX-License-Identifier: AGPL-3.0-only
// Native VST3 FX catalog, plugin rack and audio-device bridge.
// Audio runs exclusively in JUCE AudioIODeviceCallback; WebView is control only.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include "ApprovedGuiResources.h"
#include "ApprovedConsoleNativeTransportV6.h"
#include "../integration/ApprovedNativeRuntimeSnapshot.h"
#include "../integration/ApprovedNativeControlIntent.h"
#include "LiveAudioEngine.h"
#include "NativeSessionToken.h"
#include "ArSonMixStartupSplash.h"
#include "ArSonWindowChrome.h"
#include "VstOrbitController.h"
#include "../engine/core/OrbitDistanceMapping.h"
#include "../engine/core/DearVrPolarPolicy.h"
#include "DiagnosticsRecorder.h"
#include "../engine/device/DirectRestoreBackoff.h"
#include "../engine/device/AudioSetupRecoveryPolicy.h"
#include "../engine/device/AsioRestartRecoveryGate.h"
#include "../engine/device/PeriodCapabilityEvidence.h"
#include "../engine/diagnostics/CrackleEvidenceWindow.h"
#if defined(_WIN32)
#include "../engine/device/WasapiCapabilityProbe.h"
#include <windows.h>
#include <psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Psapi.lib")
#endif
#endif
#include "PluginCatalog.h"
#include "StartupVst3RecoveryGuard.h"
#include "PortableWorkspace.h"
#include "BuiltInFxUserPresetStore.h"
#include "BuiltInFxAcceptedSnapshot.h"
#include "StudioBackup.h"
#include "NativeLiveShowCapture.h"
#include "NativeDryShowRecall.h"
#include "NativeShowSaveWorker.h"
#include "NativeChannelAppearanceStore.h"
#include "NativeMixerSessionSaveWorker.h"
#include "NativeLogicalMixerControl.h"
#include "NativeHotkeyRegistry.h"
#include "NativeFxParameterHotkey.h"
#include "NativeFxControlRequest.h"
#include "NativeDeviceApplyCoordinator.h"
#include <algorithm>
#include <cstring>
#include <optional>
#include <memory>
#include <chrono>
#include <cmath>
#include <functional>
#include <vector>
#include "../engine/player/PlayerSetlistPolicy.h"
#include "../engine/player/PlayerLibraryPolicy.h"
#include <juce_core/juce_core.h>
#include <thread>

namespace
{
juce::Image approvedAppIcon()
{
    return juce::ImageFileFormat::loadFrom (
        BinaryData::arsonmix256_png, static_cast<size_t> (BinaryData::arsonmix256_pngSize));
}

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
    // Auto-generated from the SAME approved Pages GUI and PNG-button skin.
    // Legacy web/index.html and web/console-native.html cannot be served here.
    if (! resolveApprovedGuiResource (path, data, size, mime))
        return std::nullopt;

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
#if defined(_WIN32)
    // Windows must keep the proven WebView2 profile, loader and native bridge.
    return juce::WebBrowserComponent::Options{}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (
            juce::WebBrowserComponent::Options::WinWebView2{}.withUserDataFolder (folder)
            .withBackgroundColour (juce::Colour (0xff101921)))
        .withKeepPageLoadedWhenBrowserIsHidden();
#else
    // Same compiled /preview/ resource provider + JUCE native JS ACK/readback.
    // JUCE 8 uses WKWebView on macOS and WebKitGTK on Linux; no WebView2 fallback.
    return juce::WebBrowserComponent::Options{}
        .withBackend (juce::WebBrowserComponent::Options::Backend::defaultBackend)
        .withKeepPageLoadedWhenBrowserIsHidden();
#endif
}

void showWebViewStartupError (const juce::String& detail)
{
    juce::NativeMessageBox::showAsync (
        juce::MessageBoxOptions{}
            .withIconType (juce::MessageBoxIconType::WarningIcon)
#if defined(_WIN32)
            .withTitle ("ArSonMix - WebView2 unavailable")
            .withMessage ("The offline ArSonMix mixer needs Microsoft Edge WebView2 Runtime.\\n\\n"
                          + detail + "\\n\\nDownload / repair from Microsoft:\\n"
                          "https://developer.microsoft.com/en-us/microsoft-edge/webview2/\\n\\n"
                          "No VST3, mixer presets or session data were deleted.")
#else
            .withTitle ("ArSonMix - native WebKit unavailable")
            .withMessage ("ArSonMix requires the system WebKit browser backend "
                          "(WKWebView on macOS, WebKitGTK on Linux).\\n\\n"
                          + detail + "\\n\\nNo Show, plugin or preset data were deleted.")
#endif
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
        setIcon (approvedAppIcon());
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
        setIcon (approvedAppIcon());
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
        : DocumentWindow ("ArSonMiX — Native Mixer",
                         juce::Colour (0xff101921), allButtons),
          catalog_ (settingsDirectory()),
          diagnostics_ (settingsDirectory().getChildFile ("diagnostics")),
          presets_ (settingsDirectory().getChildFile ("presets"), settingsDirectory()),
          onStartupStage_ (std::move (onStartupStage)),
          browser_ (webViewOptions()
              .withNativeIntegrationEnabled()
              .withEventListener ("arMixGuiReady", [this] (juce::var ready)
              {
                  // A Pages-only demo cannot authenticate native session readiness.
                  if (ready.getProperty ("gui", "").toString() !=
                          "APPROVED_PAGES_PREVIEW_BUTTONS" ||
                      !static_cast<bool> (ready.getProperty ("nativeBridgeAttached", false)))
                  {
                      diagnostics_.record ("GUI_NATIVE_BRIDGE_MISSING",
                          "Ignoring Pages-only readiness notification");
                      return;
                  }
                  browser_.loaded = true;
                  guiReadyTick_ = juce::Time::getMillisecondCounter();
                  // WebView2 may finish its first navigation while this
                  // DocumentWindow is still hidden behind the splash. On
                  // 125%/150% DPI its child controller can keep that *old*
                  // bounds even though JUCE has already resized the content.
                  // Reapply actual browser bounds on the message thread,
                  // once after the native peer is visible: NO audio work.
                  const int cssWidth = static_cast<int> (ready.getProperty ("viewportWidth", 0));
                  const int cssHeight = static_cast<int> (ready.getProperty ("viewportHeight", 0));
                  if ((cssWidth > 0 && std::abs (cssWidth - browser_.getWidth()) > 2) ||
                      (cssHeight > 0 && std::abs (cssHeight - browser_.getHeight()) > 2))
                      diagnostics_.record ("WEBVIEW2_INITIAL_BOUNDS",
                          juce::String (cssWidth) + "x" + juce::String (cssHeight) +
                          " host=" + juce::String (browser_.getWidth()) + "x" +
                          juce::String (browser_.getHeight()));
                  juce::MessageManager::callAsync (
                      [safe = juce::Component::SafePointer<ConsoleWindow> (this)]
                      {
                          if (safe && safe->browser_.isShowing())
                              safe->browser_.resized();
                      });
                  browser_.repaint();
                  if (onStartupStage_) onStartupStage_ (1);
              })
              .withEventListener ("arMixChannelIdentitySet", [this] (juce::var args) {
                  handleNativeChannelIdentitySet(args);
              })
              .withEventListener ("arMixLogicalIntent", [this] (juce::var args) {
                  handleLogicalMixerIntent(args);
              })
              .withEventListener ("arMixHotkeyIntent", [this] (juce::var args) {
                  handleNativeHotkeyIntent(args);
              })
              .withEventListener ("arMixHotkeyCapture", [this] (juce::var args) {
                  hotkeys_.setCaptureMode(args.getProperty("enabled",false)==juce::var(true));
              })
              .withEventListener ("arMixFxParameterHotkeyInspect", [this] (juce::var args) {
                  inspectFxParameterHotkey(args);
              })
              .withEventListener ("arMixConsoleSubscribe", [this] (juce::var) {
                  // G0 real-device native producer, NEVER a synthetic ChannelId callback.
                  consoleSubscribed_ = true;
                  emitApprovedNativeSnapshot();
              })
              .withEventListener ("arMixConsoleIntent", [this] (juce::var args) {
                  // The native control thread owns ONLY accepted mutations.
                  // No GUI-side PCM, no legacy ordinal fallback and no fake ACK.
                  handleApprovedNativeControl(args);
              })
              .withEventListener ("arMixPerfControl", [this] (juce::var args) { onGuiPerfControl(args); })
              .withEventListener ("arMixPerfTrace", [this] (juce::var args) {
                  if (args.getProperty("schema",{}).toString() != "arsonmix.gui-perf-stage-a.v1") return;
                  auto evidence = juce::JSON::toString(args, false);
                  if (evidence.length() > 3500) evidence = evidence.substring(0, 3500) + " [TRUNCATED]";
                  diagnostics_.record("GUI_PERF_JS", evidence);
              })
              .withEventListener ("arMixParameter", [this] (juce::var args) { onParameter (args); })
              .withEventListener ("arMixAction", [this] (juce::var args) { onAction (args); })
              .withResourceProvider (assetFor))
    {
        settingsDirectory().createDirectory();
        hotkeys_.setHandler([this](const armix::hotkey::Mapping& mapping) {
            fireNativeHotkey(mapping);
        });
        restorePlayerLibrary(); // portable, local song paths only; NEVER autoplay on launch
        catalog_.onChanged = [this] { emitCatalog(); emitScanProgress(); };
        catalog_.scanFactoryIfPresent(); // Only the signed-off local manifest; never global VST scans.
#if defined(_WIN32)
        // JUCE owns the real window commands; unlike WebView2 JS, these are
        // synchronous native controls with JUCE's existing resize constrainer.
        setLookAndFeel (&mixerCaption_);
        setUsingNativeTitleBar (false);
        setTitleBarHeight (31);
#else
        setUsingNativeTitleBar (true);
#endif
        setIcon (approvedAppIcon());
        setResizable (true, false);
        setResizeLimits (1100, 650, 3840, 2160);
        setContentNonOwned (&browser_, false);
        // A fixed 1650x930 initial HWND exceeds the *logical* Windows work
        // area on high-DPI laptops. Clamp BEFORE WebView2 first navigation,
        // instead of letting Windows resize the outer peer without matching
        // the child compositor during its hidden first-frame startup.
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const auto available = display != nullptr
            ? display->userArea : juce::Rectangle<int> (0, 0, 1650, 930);
        centreWithSize (juce::jlimit (1100, 1650, available.getWidth() - 24),
                        juce::jlimit (650, 930, available.getHeight() - 24));
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

    // Rebind the embedded WebView2 controller whenever its JUCE content
    // window is resized. This catches the hidden-peer/show path, DPI moves
    // and normal operator window drags without a per-frame polling timer.
    void resized() override
    {
        juce::DocumentWindow::resized();
        if (getContentComponent() == &browser_ && browser_.isShowing())
            browser_.resized();
    }

    ~ConsoleWindow() override
    {
        hotkeys_.detach(); // release global keys before the JUCE HWND disappears
        stopTimer();
        orbit_.stop();
        catalog_.onChanged = nullptr;
        closePluginEditor();
        // Tear down the modal transaction first. A closing window must not
        // re-enter device rebind while ConsoleWindow is being destroyed.
        audioSetupLease_.reset();
        audioSetupWindow_.reset();
        engine_.devices().removeChangeListener (this);
        // Drain any earlier file work before the final muted checkpoint.
        // Remember MAIN before the shutdown mute changes the hardware state.
        const bool resumeMain=engine_.isMonitorEnabled();
        engine_.setOutputMuted(true);
        engine_.disconnectSecondaryOutput();
        mixerSessionWorker_.finishForShutdown(logicalVendorEditor_.has_value());
        saveClosedLogicalVendorEditor(true);
        mixerSessionWorker_.finishForShutdown();
        logicalVendorEditor_.reset();
        const auto lab=engine_.status().getProperty("dryRoutingLab",{});
        if(static_cast<bool>(lab.getProperty("nativeChannelIdPcmActive",false)) ||
           logicalMixer_.loaded()) {
            const auto error=saveMixerSession(resumeMain);
            if(error.isNotEmpty())
                diagnostics_.record("DYNAMIC_MIXER_EXIT_SAVE_REJECTED",error);
        }
        engine_.stop();
        // Normal shutdown is not an interrupted startup. Leave marker intact
        // on abnormal vendor termination (destructors never run).
        if(startupFxGuard_)(void)startupFxGuard_->complete();
        browser_.setVisible (false);
        setLookAndFeel (nullptr); // before the owned LookAndFeel is destroyed
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    armix::ui::MixerCaptionLookAndFeel mixerCaption_;
    LiveAudioEngine engine_;
    VstOrbitController orbit_;
    // The exact native ChannelId/insert identity retains its VST3 owner
    // while Orbit's JUCE message-thread timer holds raw parameter pointers.
    armix::mixer::LiveChannelFxGraph::InstanceLease nativeOrbitLease_;
    juce::String nativeOrbitChannelId_,nativeOrbitInstanceId_;
    std::uint64_t nativeOrbitEpoch_=0;
    juce::int64 fxGraphRevision_ = 1; // control-thread monotonic graph generation
    PluginCatalog catalog_;
    DiagnosticsRecorder diagnostics_;
    armix::appearance::Store channelAppearance_{settingsDirectory()};
    armix::storage::NativePresetArchive presets_;
    armix::storage::BuiltInFxUserPresetStore builtinFxUserPresets_{settingsDirectory()};
    std::function<void(int)> onStartupStage_;
    Browser browser_;
    armix::hotkey::Registry hotkeys_{settingsDirectory()};
    juce::StringArray consumedHotkeyRequests_;
    std::uint64_t firedHotkeySerial_=0;
    armix::storage::StudioBackup::Worker studioBackupWorker_;
    armix::shows::NativeShowSaveWorker nativeShowWorker_;
    armix::session::NativeMixerSessionSaveWorker mixerSessionWorker_;
    armix::session::NativeLogicalMixerControl logicalMixer_{
        settingsDirectory(),mixerSessionWorker_,[this](const juce::String& id) {
            mixerSessionId_=id;
            lastMixerSessionFingerprint_.clear();
            mixerSessionRecoveryPending_=true;
            mixerSessionStatus_="LOGICAL_SESSION_SAVED_UNBOUND";
            mixerSessionError_.clear();
        }};
    struct LogicalVendorEditor {
        armix::session::StoredInsert insert;
        juce::String workspaceToken;
        std::unique_ptr<juce::AudioProcessor> processor;
        std::unique_ptr<PluginEditorWindow> window;
    };
    std::optional<LogicalVendorEditor> logicalVendorEditor_;
    juce::String lastLogicalFxRequest_,lastLogicalFxInstance_,lastLogicalFxRevision_;
    juce::String mixerSessionId_,mixerSessionStatus_="NO_SAVED_SESSION";
    juce::String mixerSessionError_,lastMixerSessionFingerprint_;
    bool mixerSessionStartupInspected_=false;
    bool mixerSessionRecoveryPending_=false;
    std::unique_ptr<armix::startup::Vst3RestoreCrashGuard> startupFxGuard_;
    juce::uint32 startupFxGuardTick_=0;
    std::unique_ptr<juce::FileChooser> mixerFileChooser_;
    bool mixerFileDialogOpen_=false;
    juce::File lastMixerFile_;
    bool nativeShowBusy_ = false;
    bool studioBackupBusy_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::FileChooser> playerFileChooser_;
    std::vector<juce::File> playerLibrary_; // one compact default setlist
    struct PlayerTrackSettings {int key=0,tempo=100;};
    std::vector<PlayerTrackSettings> playerTrackSettings_; // parallel stable entry IDs
    int playerSelected_=-1,playerCurrent_=-1;
    int playerOutputPendingIndex_=-1; // only after explicit Choose output gesture
    bool playerAutoNext_=false;
    bool playerFolderBusy_=false; // GUI only; background scanner never touches this
    std::uint64_t lastAutoAdvanceGeneration_=0;
    juce::String playerError_;
    juce::File lastPluginFolder_ = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    // Destruction order: editorWindow_ releases its JUCE GUI BEFORE the
    // retained vendor processor lease. No vendor destructor runs on RT.
    armix::mixer::LiveChannelFxGraph::InstanceLease routedEditorOwner_;
    std::uint64_t routedEditorEpoch_=0;
    struct PendingNativeEq {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        int slot=-1;
        juce::uint32 started=0;
    } pendingNativeEq_;
    juce::String lastNativeEqRequest_,lastNativeEqInstance_;
    juce::String lastNativeEqEpoch_,lastNativeEqRevision_;
    struct PendingNativeComp {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        juce::uint32 started=0;
    } pendingNativeComp_;
    juce::String lastNativeCompRequest_,lastNativeCompInstance_;
    juce::String lastNativeCompEpoch_,lastNativeCompRevision_;
    struct PendingNativeHarmony {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        juce::uint32 started=0;
    } pendingNativeHarmony_;
    juce::String lastNativeHarmonyRequest_,lastNativeHarmonyInstance_;
    juce::String lastNativeHarmonyEpoch_,lastNativeHarmonyRevision_;
    struct PendingNativeThrow {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        juce::uint32 started=0;
        bool trigger=false;
    } pendingNativeThrow_;
    juce::String lastNativeThrowRequest_,lastNativeThrowInstance_;
    juce::String lastNativeThrowEpoch_,lastNativeThrowRevision_;
    armix::mixer::NativeMusicTempoMonitor::State lastMusicTempo_{};
    std::unique_ptr<PluginEditorWindow> editorWindow_;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow_;
    // Persistent modal lease: blocks an Apply/ASIO restart while JUCE's
    // device selector is open, not merely during the initial show() call.
    std::optional<armix::device::NativeDeviceApplyCoordinator::Lease> audioSetupLease_;
    juce::String startupError_, lastAudioXml_;
    bool initialStateSent_ = false;
    bool consoleSubscribed_ = false; // Real snapshot subscription; not FAT certification
    juce::StringArray acceptedControlRequests_; // bounded request replay fence
    juce::StringArray acceptedAppearanceRequests_; // identity request replay fence
    bool webViewPeerWarmed_ = false;
    bool webViewStartupNoticeSent_ = false;
    juce::uint32 webViewStartupTick_ = 0;
    juce::uint32 guiReadyTick_ = 0;
    bool audioEngineStarted_ = false;
    bool setupLaunching_ = false;
    unsigned int timerTicks_ = 0;
    // Opt-in and bounded: message-thread timing only, NEVER the audio callback.
    struct GuiPerfStats
    {
        bool active = false;
        unsigned int ticks = 0, statusReads = 0, snapshotEmits = 0, duplicateSnapshots = 0;
        double prevTickMs = 0.0, maxTickGapMs = 0.0;
        double totalTickMs = 0.0, maxTickMs = 0.0;
        double totalStatusMs = 0.0, maxStatusMs = 0.0;
        double totalSnapshotMs = 0.0, maxSnapshotMs = 0.0;
    } guiPerf_;

    void onGuiPerfControl(const juce::var& args)
    {
        const auto action = args.getProperty("action", {}).toString();
        if (action == "start")
        {
            guiPerf_ = {};
            guiPerf_.active = true;
            diagnostics_.record("GUI_PERF_CONTROL", "start");
        }
        else if (action == "stop" && guiPerf_.active)
        {
            guiPerf_.active = false;
            const auto average = [](double total, unsigned int count)
            {
                return count ? total / static_cast<double>(count) : 0.0;
            };
            auto object = std::make_unique<juce::DynamicObject>();
            object->setProperty("schema", "arsonmix.native-message-thread.v1");
            object->setProperty("timerTicks", static_cast<int>(guiPerf_.ticks));
            object->setProperty("timerMeanMs", average(guiPerf_.totalTickMs, guiPerf_.ticks));
            object->setProperty("timerMaxMs", guiPerf_.maxTickMs);
            object->setProperty("timerMaxGapMs", guiPerf_.maxTickGapMs);
            object->setProperty("statusReads", static_cast<int>(guiPerf_.statusReads));
            object->setProperty("statusMeanMs", average(guiPerf_.totalStatusMs, guiPerf_.statusReads));
            object->setProperty("statusMaxMs", guiPerf_.maxStatusMs);
            object->setProperty("snapshotEmits", static_cast<int>(guiPerf_.snapshotEmits));
            object->setProperty("snapshotMeanMs", average(guiPerf_.totalSnapshotMs, guiPerf_.snapshotEmits));
            object->setProperty("snapshotMaxMs", guiPerf_.maxSnapshotMs);
            object->setProperty("duplicateSnapshots", static_cast<int>(guiPerf_.duplicateSnapshots));
            const juce::var result(object.release());
            diagnostics_.record("GUI_PERF_NATIVE", juce::JSON::toString(result, false));
            browser_.emitEventIfBrowserIsVisible("arMixPerfNativeResult", result);
        }
    }
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

    // O(1) Windows OS counters at the existing 1 Hz AUDIO_HEALTH poll
    // (and explicit Mark Crackle); never inside audioDeviceIOCallback.
    // GlobalMemoryStatusEx load is whole-system RAM pressure, NOT process
    // usage. PROCESS_MEMORY_COUNTERS_EX PageFaultCount includes SOFT faults;
    // a Windows ETW hard-fault trace is needed to prove disk paging.
    static armix::diagnostics::CrackleEvidenceWindow::MemoryPressure
        readMemoryPressure() noexcept
    {
        using Memory = armix::diagnostics::CrackleEvidenceWindow::MemoryPressure;
        Memory out;
#if defined(_WIN32)
        MEMORYSTATUSEX system {};
        system.dwLength = sizeof(system);
        if(::GlobalMemoryStatusEx(&system) != FALSE)
        {
            out.systemAvailable = true;
            out.systemLoadPercent = system.dwMemoryLoad;
            out.availablePhysicalMiB = system.ullAvailPhys/(1024ull*1024ull);
        }
        PROCESS_MEMORY_COUNTERS_EX process {};
        if(::GetProcessMemoryInfo(::GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),
                static_cast<DWORD>(sizeof(process))) != FALSE)
        {
            out.processAvailable = true;
            out.processWorkingSetMiB = process.WorkingSetSize/(1024ull*1024ull);
            out.processPrivateCommitMiB = process.PrivateUsage/(1024ull*1024ull);
            out.processPageFaults = process.PageFaultCount;
        }
#endif
        return out;
    }
    static armix::diagnostics::CrackleEvidenceWindow::Sample makeEvidenceSample (
        const juce::var& state)
    {
        using Window = armix::diagnostics::CrackleEvidenceWindow;
        Window::Sample out;
        out.atMs = monotonicMilliseconds();
        out.memory = readMemoryPressure();
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

    static juce::String compactMemoryWindowReport (
        const armix::diagnostics::CrackleEvidenceWindow::Window& window)
    {
        const auto& before=window.memoryBefore;
        const auto& at=window.memoryAtMark;
        const auto fmt=[](std::uint64_t value,bool valid) {
            return valid ? juce::String(static_cast<juce::int64>(value))
                         : juce::String("N/A");
        };
        juce::String result="RAM_LOAD_START_PCT=";
        result+=fmt(before.systemLoadPercent,window.ready()&&before.systemAvailable);
        result+=" | RAM_LOAD_MARK_PCT=";
        result+=fmt(at.systemLoadPercent,window.ready()&&at.systemAvailable);
        result+=" | RAM_LOAD_PREMARK_PEAK_PCT=";
        result+=fmt(window.peakLoadPercent,window.loadComparable);
        result+=" | FREE_PHYSICAL_MIB=";
        result+=fmt(at.availablePhysicalMiB,window.ready()&&at.systemAvailable);
        result+=" | PROCESS_WORKING_SET_MIB=";
        result+=fmt(at.processWorkingSetMiB,window.ready()&&at.processAvailable);
        result+=" | PROCESS_PRIVATE_COMMIT_MIB=";
        result+=fmt(at.processPrivateCommitMiB,window.ready()&&at.processAvailable);
        result+=" | ALL_PAGE_FAULTS_DELTA=";
        result+=fmt(window.processPageFaults.value,
                    window.processPageFaults.comparable);
        result+=" | HARD_FAULTS=UNAVAILABLE | NOT_CAUSAL_PROOF";
        return result;
    }

    juce::String crackleWindowReport (
        const armix::diagnostics::CrackleEvidenceWindow::Window& window) const
    {
        using Window = armix::diagnostics::CrackleEvidenceWindow;
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
        const auto& before = window.memoryBefore;
        const auto& marked = window.memoryAtMark;
        report += " | SYSTEM_RAM_LOAD_BEFORE_PCT=";
        report += window.ready() && before.systemAvailable
            ? juce::String(before.systemLoadPercent):juce::String("N/A");
        report += " | SYSTEM_RAM_LOAD_AT_MARK_PCT=";
        report += window.ready() && marked.systemAvailable
            ? juce::String(marked.systemLoadPercent):juce::String("N/A");
        report += " | SYSTEM_RAM_LOAD_WINDOW_PEAK_PCT=";
        report += window.loadComparable
            ? juce::String(window.peakLoadPercent):juce::String("N/A");
        report += " | SYSTEM_AVAILABLE_PHYSICAL_MIB_AT_MARK=";
        report += window.ready() && marked.systemAvailable
            ? juce::String(static_cast<juce::int64>(marked.availablePhysicalMiB))
            : juce::String("N/A");
        report += " | ARSONMIX_WORKING_SET_MIB_AT_MARK=";
        report += window.ready() && marked.processAvailable
            ? juce::String(static_cast<juce::int64>(marked.processWorkingSetMiB))
            : juce::String("N/A");
        report += " | ARSONMIX_PRIVATE_COMMIT_MIB_AT_MARK=";
        report += window.ready() && marked.processAvailable
            ? juce::String(static_cast<juce::int64>(marked.processPrivateCommitMiB))
            : juce::String("N/A");
        report += " | PROCESS_ALL_PAGE_FAULTS_SOFT_OR_HARD_DELTA=";
        report += window.processPageFaults.comparable
            ? juce::String(static_cast<juce::int64>(window.processPageFaults.value))
            : juce::String("N/A");
        report += " | OS_HARD_PAGE_FAULTS=UNAVAILABLE_REQUIRES_ETW";
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

    void emitCatalog()
    {
        if(!browser_.loaded)return;
        auto catalog=catalog_.snapshot();
        juce::Array<juce::var> builtIns;
        for(const auto& fx:armix::builtin_fx::nativeBuiltInFx) {
            auto entry=std::make_unique<juce::DynamicObject>();
            entry->setProperty("id",fx.id);
            entry->setProperty("name",fx.name);
            entry->setProperty("category",fx.category);
            entry->setProperty("showInPicker",fx.showInPicker);
            builtIns.add(juce::var(entry.release()));
        }
        catalog.getDynamicObject()->setProperty("builtIns",juce::var(builtIns));
        browser_.emitEventIfBrowserIsVisible("arMixCatalog",catalog);
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

    void emitOrbitState (int slot,int previewShape=-1,
                         juce::AudioPluginInstance* nativePlugin=nullptr,
                         const juce::String& channelId={},
                         const juce::String& nativeRevision={},
                         const juce::String& insertId={})
    {
        auto event = std::make_unique<juce::DynamicObject>();
        event->setProperty ("slot", slot);
        event->setProperty ("revision", nativeRevision.isNotEmpty()?
            nativeRevision:juce::String (fxGraphRevision_));
        if(channelId.isNotEmpty()){
            event->setProperty("channelId",channelId);
            event->setProperty("instanceId",insertId);
        }
        const bool activeTarget=orbit_.active()&&orbit_.slot()==slot&&
            (channelId.isEmpty()?
                nativeOrbitChannelId_.isEmpty():
                nativeOrbitChannelId_==channelId&&nativeOrbitInstanceId_==insertId);
        const auto& stats = orbit_.stats();
        event->setProperty ("controlTicks", static_cast<juce::int64> (stats.ticks));
        event->setProperty ("parameterWrites", static_cast<juce::int64> (stats.writes));
        event->setProperty ("coalesced", static_cast<juce::int64> (stats.coalesced));
        event->setProperty ("lateTicks", static_cast<juce::int64> (stats.lateTicks));
        event->setProperty ("maxGapMs", stats.maxGapMs);
        event->setProperty ("active", activeTarget);
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
        auto* plugin = engine_.isGraphSwitchPending() ? nullptr :
            (nativePlugin ? nativePlugin : engine_.pluginAt (slot));
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
            activeTarget ? orbit_.parameterIndex() : preferred);
        // Display near/far setpoints for the plugin parameter's *actual* units.
        int setpointParam=activeTarget?
            orbit_.distanceParameterIndex():preferredDistance;
        auto* dist=setpointParam>=0&&plugin!=nullptr&&
            setpointParam<plugin->getParameters().size()?
            plugin->getParameters()[setpointParam]:nullptr;
        double minMetres=0,maxMetres=0;
        const bool metres=distanceMeterBounds(dist,minMetres,maxMetres);
        event->setProperty("distanceUnit",metres?"m":"%");
        event->setProperty("distanceMin",metres?minMetres:0.0);
        event->setProperty("distanceMax",metres?maxMetres:100.0);
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
            browser_.emitEventIfBrowserIsVisible (channelId.isEmpty()?
                "arMixOrbitState":"arMixNativeOrbitState",juce::var(event.release()));
    }

    // Stop before releasing the lease referenced by Orbit's raw vendor
    // parameter pointers. The old legacy event remains unchanged.
    void stopOrbit()
    {
        const int previousSlot=orbit_.slot();
        const auto nativeId=nativeOrbitChannelId_,insertId=nativeOrbitInstanceId_;
        auto* nativePlugin=nativeOrbitLease_&&nativeOrbitLease_->processor?
            dynamic_cast<juce::AudioPluginInstance*>(
                nativeOrbitLease_->processor.get()):nullptr;
        if(!orbit_.active()){
            nativeOrbitLease_.reset();nativeOrbitChannelId_.clear();
            nativeOrbitInstanceId_.clear();nativeOrbitEpoch_=0;
            return;
        }
        const auto result=orbit_.stats();
        orbit_.stop();
        diagnostics_.record("FX_ORBIT_CONTROL",
            "ticks="+juce::String(static_cast<juce::int64>(result.ticks))+
            " writes="+juce::String(static_cast<juce::int64>(result.writes))+
            " coalesced="+juce::String(static_cast<juce::int64>(result.coalesced))+
            " lateTicks="+juce::String(static_cast<juce::int64>(result.lateTicks))+
            " maxTimerGapMs="+juce::String(result.maxGapMs,1)+
            " | timer delay is NOT a hardware xrun");
        if(nativeId.isNotEmpty())
            emitOrbitState(previousSlot,-1,nativePlugin,nativeId,{},insertId);
        else emitOrbitState(previousSlot);
        nativeOrbitLease_.reset();nativeOrbitChannelId_.clear();
        nativeOrbitInstanceId_.clear();nativeOrbitEpoch_=0;
    }

    void ensureLogicalWorkspace() {
        const auto [found,error]=logicalMixer_.ensureLoaded();
        if(found && !mixerSessionStartupInspected_)
            mixerSessionRecoveryPending_=true;
        if(error.isEmpty()) {
            if(found && hotkeys_.activeSession()!=logicalMixer_.workspace().document().id) {
                const auto& document=logicalMixer_.workspace().document();
                (void)hotkeys_.activateSession(document.id);
                // Local shortcut journal is newer than checkpoint on crash.
                if(hotkeys_.sessionEntries().empty() && !document.hotkeys.empty()) {
                    const auto issue=hotkeys_.activateSession(document.id,&document.hotkeys);
                    if(issue.isNotEmpty())diagnostics_.record("HOTKEY_IMPORT_REJECTED",issue);
                }
            }
            return;
        }
        mixerSessionRecoveryPending_=true;
        mixerSessionStatus_="DAMAGED_SESSION";
        mixerSessionError_=error;
        diagnostics_.record("LOGICAL_MIXER_RESTORE_REJECTED",error);
    }

    juce::String prepareLogicalFx(const armix::session::NativeLogicalMixerWorkspace& workspace,
            armix::mixer::ChannelId channel,std::size_t slot,armix::session::StoredInsert& insert,
            std::unique_ptr<juce::AudioProcessor>& processor) {
        const auto* row=workspace.document().roster.find(channel);
        if(!row||slot>=row->insertCatalogIds.size())return "FX slot changed";
        const auto* saved=workspace.storedFx(channel,slot);
        insert=saved?*saved:armix::session::StoredInsert{};
        insert.channel=channel;
        insert.slot=slot;
        insert.catalogId=juce::String(row->insertCatalogIds[slot]);
        const bool builtin=armix::builtin_fx::findNativeBuiltInFx(insert.catalogId)!=nullptr;
        if(!builtin) {
            const auto* registered=catalog_.find(insert.catalogId);
            if(!registered || (saved && (saved->uid!=registered->uid || saved->path!=registered->path)))
                return "Installed VST3 identity unavailable: "+insert.catalogId;
            insert.path=registered->path;
            insert.uid=registered->uid;
            insert.name=registered->name;
        }
        const auto& states=workspace.states();
        const auto found=std::find_if(states.begin(),states.end(),[&](const auto& state){
            return state.sha256==insert.sha256;
        });
        if(saved && found==states.end())return "Saved FX state unavailable";
        const auto rate=workspace.document().binding.rate;
        try {
            const auto error=engine_.prepareUnboundMixerFx(insert,
                found==states.end()?nullptr:&*found,rate>0.?rate:48000.,processor);
            if(error.isEmpty()&&builtin)insert.name=processor->getName();
            return error;
        } catch(...) {
            return "FX preparation failed";
        }
    }

    bool logicalFxTarget(const juce::var& request,std::uint64_t& channel,std::size_t& slot) {
        return approvedNativeSnapshot().getProperty("logicalMixer",{}).isObject() &&
            logicalMixer_.matchesFxTarget(request,channel,slot);
    }

    std::unique_ptr<juce::AudioProcessor> openLogicalBuiltinFx(const juce::var& request,
            armix::session::StoredInsert& insert,const char* catalogId) {
        std::uint64_t channel=0;
        std::size_t slot=0;
        std::unique_ptr<juce::AudioProcessor> processor;
        if(!logicalFxTarget(request,channel,slot))return {};
        const auto* row=logicalMixer_.workspace().document().roster.find({channel});
        if(row && slot<row->insertCatalogIds.size() && row->insertCatalogIds[slot]==catalogId &&
           prepareLogicalFx(logicalMixer_.workspace(),{channel},slot,insert,processor).isEmpty())
            return processor;
        return {};
    }

    void openLogicalVendorEditor(const juce::var& request) {
        if(logicalVendorEditor_ || mixerSessionWorker_.busy() || logicalMixer_.readOnly()) {
            emitNotice("Close the current FX editor or wait for the mixer to finish saving",true);
            return;
        }
        std::uint64_t channel=0;
        std::size_t slot=0;
        if(!logicalFxTarget(request,channel,slot)) {
            emitNotice("The selected FX slot changed",true);
            return;
        }
        LogicalVendorEditor prepared;
        auto error=prepareLogicalFx(logicalMixer_.workspace(),{channel},slot,prepared.insert,prepared.processor);
        if(error.isEmpty()) {
            try {
                if(auto* editor=prepared.processor->createEditorIfNeeded()) {
                    prepared.window=std::make_unique<PluginEditorWindow>(
                        prepared.processor->getName(),editor);
                    prepared.workspaceToken=logicalMixer_.workspace().workspaceToken();
                    logicalVendorEditor_=std::move(prepared);
                    return;
                }
                error="This FX does not provide a vendor editor";
            } catch(...) {
                error="Vendor editor could not be opened";
            }
        }
        emitNotice(error,true);
    }

    void saveClosedLogicalVendorEditor(bool shutdown=false) {
        if(!logicalVendorEditor_ || mixerSessionWorker_.busy() ||
           (!shutdown && logicalVendorEditor_->window->isVisible()))return;
        auto& fx=*logicalVendorEditor_;
        if(fx.workspaceToken!=logicalMixer_.workspace().workspaceToken()) {
            logicalVendorEditor_.reset();
            return;
        }
        juce::MemoryBlock bytes;
        try {
            fx.processor->getStateInformation(bytes);
        } catch(...) {
            emitNotice("Vendor FX could not provide its settings",true);
            if(!shutdown)fx.window->setVisible(true);
            return;
        }
        if(!logicalMixer_.saveFx(fx.insert,std::move(bytes),[this](juce::String error){
                if(error.isEmpty())logicalVendorEditor_.reset();
                else {
                    emitNotice("FX settings could not be saved: "+error,true);
                    if(logicalVendorEditor_)logicalVendorEditor_->window->setVisible(true);
                }
           })) {
            emitNotice("FX settings could not be saved",true);
            if(!shutdown)fx.window->setVisible(true);
        }
    }

    template<class Processor>
    void completeLogicalFxEdit(const juce::var& args,armix::session::StoredInsert insert,
            Processor* processor,bool queued,const char* event) {
        auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
        auto reply=[safe,args,event](juce::String error){
            if(safe==nullptr)return;
            auto ack=std::make_unique<juce::DynamicObject>();
            for(const auto* key:{"requestId","channelId","deviceEpoch","graphRevision","instanceId"})
                ack->setProperty(key,args.getProperty(key,{}));
            if(error.isEmpty()) {
                safe->lastLogicalFxRequest_=args.getProperty("requestId",{}).toString();
                safe->lastLogicalFxInstance_=args.getProperty("instanceId",{}).toString();
                safe->lastLogicalFxRevision_=args.getProperty("graphRevision",{}).toString();
            }
            ack->setProperty("status",error.isEmpty()?"APPLIED":"REJECTED");
            ack->setProperty("errorCode",error.isEmpty()?"NONE":error);
            if(safe->browser_.loaded)safe->browser_.emitEventIfBrowserIsVisible(event,juce::var(ack.release()));
        };
        if(!queued || logicalMixer_.readOnly() || mixerSessionWorker_.busy() ||
           deviceApply_.busy() || nativeShowBusy_ || studioBackupBusy_)
            return reply("Offline FX settings could not be changed");
        // This isolated processor has no publisher or audio consumer. Drain
        // its typed settings packet on CONTROL without rendering PCM.
        processor->consumeRealtimeEdits();
        typename Processor::Completion applied{};
        if(!processor->pollCompletion(applied)||!applied.applied)
            return reply("Offline FX settings rejected");
        juce::MemoryBlock bytes;
        processor->getStateInformation(bytes);
        if(!logicalMixer_.saveFx(std::move(insert),std::move(bytes),reply))
            reply("Offline FX settings could not be saved");
    }

    // H2: verify real live VST3 lease and class identity. The positional slot
    // is a GUI hint only; moves resolve by the exact FX instance token.
    struct FxHotkeyLease final {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::AudioPluginInstance* plugin=nullptr;
        int slot=-1;
    };
    FxHotkeyLease resolveFxHotkey(const juce::String& channelId,
            const juce::String& insertId,const juce::String& catalogId) {
        FxHotkeyLease target;
        if(channelId.isEmpty()||insertId.isEmpty()||catalogId.isEmpty()||
           deviceApply_.busy()||nativeShowBusy_||studioBackupBusy_||
           engine_.independentClockActive()||engine_.isGraphSwitchPending())
            return target;
        const auto snapshot=approvedNativeSnapshot();
        const auto graph=snapshot.getProperty("graph",{});
        if(!static_cast<bool>(graph.getProperty("capabilities",{})
              .getProperty("callbackOwnsChannelIds",false)))return target;
        std::uint64_t id=0,epoch=0,revision=0;
        if(!armix::console::approvedNativeId(juce::var(channelId),id)||
           !armix::console::approvedNativeId(
                snapshot.getProperty("device",{}).getProperty("epoch",{}),epoch)||
           !armix::console::approvedNativeId(
                graph.getProperty("revision",{}),revision))return target;
        const auto rows=graph.getProperty("channels",{});
        if(!rows.isArray())return target;
        for(const auto& row:*rows.getArray()) {
            if(row.getProperty("id","").toString()!=channelId ||
               (row.getProperty("role","").toString()!="INPUT" &&
                row.getProperty("role","").toString()!="GROUP" &&
                row.getProperty("role","").toString()!="FX_RETURN"))continue;
            const auto inserts=row.getProperty("inserts",{});
            if(!inserts.isArray())return target;
            const auto* all=inserts.getArray();
            for(int slot=0;slot<all->size();++slot) {
                const auto entry=all->getReference(slot);
                if(entry.getProperty("id","").toString()!=insertId ||
                   entry.getProperty("catalogId","").toString()!=catalogId)
                    continue;
                auto owner=engine_.leaseNativeVst3Editor(epoch,revision,
                    armix::mixer::ChannelId{id},static_cast<std::size_t>(slot),insertId);
                if(!owner||owner->instanceId!=insertId||
                   owner->catalogId!=catalogId||!owner->processor)return target;
                auto* plugin=dynamic_cast<juce::AudioPluginInstance*>(owner->processor.get());
                if(!plugin)return target;
                juce::PluginDescription description;
                plugin->fillInPluginDescription(description);
                if(!description.pluginFormatName.equalsIgnoreCase("VST3"))return target;
                target.owner=std::move(owner);
                target.plugin=plugin;
                target.slot=slot;
                return target;
            }
            break;
        }
        return target;
    }
    bool fxParameterHotkeyAdmitted(const armix::hotkey::Mapping& mapping) {
        auto target=resolveFxHotkey(mapping.channelId,mapping.insertId,mapping.catalogId);
        if(!target.plugin)return false;
        auto* parameter=armix::hotkey::FxParameterPolicy::find(
            target.plugin->getParameters(),mapping.paramId);
        return parameter&&armix::hotkey::FxParameterPolicy::operationAllowed(
            *parameter,mapping.operation);
    }
    bool applyFxParameterHotkey(const armix::hotkey::Mapping& mapping) {
        auto target=resolveFxHotkey(mapping.channelId,mapping.insertId,mapping.catalogId);
        if(!target.plugin)return false;
        auto* parameter=armix::hotkey::FxParameterPolicy::find(
            target.plugin->getParameters(),mapping.paramId);
        float next=0.f;
        if(!parameter ||
           !armix::hotkey::FxParameterPolicy::desired(*parameter,mapping.operation,next))
            return false;
        // Call JUCE's notifying setter only on CONTROL, never on PCM callback.
        // Host-written beginChangeGesture is not mistaken for vendor Last Touched.
        target.owner->hostHotkeyWrite.store(true,std::memory_order_release);
        bool applied=false;
        try {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(next);
            parameter->endChangeGesture();
            applied=true;
        } catch(...) {
            try {parameter->endChangeGesture();}catch(...){}
        }
        target.owner->hostHotkeyWrite.store(false,std::memory_order_release);
        if(applied)
            diagnostics_.record("NATIVE_FX_PARAM_HOTKEY",
                mapping.channelId+" / "+mapping.insertId+" / "+mapping.paramId);
        return applied;
    }
    void inspectFxParameterHotkey(const juce::var& request) {
        const auto requestId=request.getProperty("requestId","").toString();
        const auto channelId=request.getProperty("channelId","").toString();
        const auto insertId=request.getProperty("insertId","").toString();
        const auto catalogId=request.getProperty("catalogId","").toString();
        const auto observed=approvedNativeSnapshot();
        const auto graph=observed.getProperty("graph",{});
        auto response=juce::var(new juce::DynamicObject());
        auto* result=response.getDynamicObject();
        result->setProperty("requestId",requestId);
        result->setProperty("channelId",channelId);
        result->setProperty("insertId",insertId);
        result->setProperty("catalogId",catalogId);
        result->setProperty("ok",false);
        result->setProperty("error","HOTKEY_FX_STALE_OR_UNAVAILABLE");
        const bool current=requestId.isNotEmpty()&&requestId.length()<=96 &&
            request.getProperty("deviceEpoch","").toString()==
                observed.getProperty("device",{}).getProperty("epoch","").toString() &&
            request.getProperty("revision","").toString()==
                graph.getProperty("revision","").toString() &&
            request.getProperty("rosterRevision","").toString()==
                graph.getProperty("rosterRevision","").toString();
        if(current) {
            auto target=resolveFxHotkey(channelId,insertId,catalogId);
            if(target.plugin) {
                const auto info=armix::hotkey::FxParameterPolicy::browse(
                    target.plugin->getParameters());
                juce::Array<juce::var> listed;
                const auto touched=target.owner->lastTouchedParameter.load(
                    std::memory_order_acquire);
                juce::String lastTouchedId;
                for(const auto& p:info) {
                    auto item=juce::var(new juce::DynamicObject());
                    auto* o=item.getDynamicObject();
                    o->setProperty("id",p.id);
                    o->setProperty("name",p.name);
                    o->setProperty("index",p.index); // UI hint, never saved
                    o->setProperty("toggle",p.toggle);
                    o->setProperty("steps",p.steps);
                    o->setProperty("value",p.normalized);
                    listed.add(item);
                    if(touched==p.index)lastTouchedId=p.id;
                }
                result->setProperty("parameters",juce::var(listed));
                result->setProperty("lastTouchedId",lastTouchedId);
                result->setProperty("slot",target.slot);
                result->setProperty("name",target.plugin->getName());
                result->setProperty("ok",!info.empty());
                result->setProperty("error",info.empty()?
                    "VST3_STABLE_PARAMETER_IDS_UNAVAILABLE":"NONE");
            }
        }
        if(browser_.loaded)
            browser_.emitEventIfBrowserIsVisible("arMixFxParameterHotkeyState",response);
    }

    // Native hotkeys share the accepted ChannelId mixer control path.
    // WM_HOTKEY is delivered on the JUCE message thread even while the
    // WebView is minimized, and never synthesizes a browser mouse click.
    void sendHotkeyRegistry(const juce::String& requestId={},
                            const juce::String& error={}) {
        auto state=hotkeys_.snapshot();
        const auto accepted=approvedNativeSnapshot();
        const auto graph=accepted.getProperty("graph",{});
        const auto caps=graph.getProperty("capabilities",{});
        const auto channels=graph.getProperty("channels",{});
        const auto ops=caps.getProperty("supportedOps",{});
        const auto* permitted=ops.getArray();
        const bool admitted=static_cast<bool>(
            caps.getProperty("callbackOwnsChannelIds",false));
        const auto rows=state.getProperty("entries",{});
        if(auto* entries=rows.getArray())for(auto& row:*entries) {
            if(row.getProperty("scope","").toString()!="SESSION")continue;
            const auto id=row.getProperty("channelId","").toString();
            const auto op=row.getProperty("operation","").toString();
            bool found=false,fxAvailable=false;
            if(channels.isArray())for(const auto& channel:*channels.getArray()) {
                if(channel.getProperty("id","").toString()!=id ||
                   (channel.getProperty("role","").toString()!="INPUT" &&
                    channel.getProperty("role","").toString()!="GROUP" &&
                    channel.getProperty("role","").toString()!="FX_RETURN"))continue;
                found=true;
                if(op=="SET_FX_RACK_BYPASS")
                    fxAvailable=channel.getProperty("fxRackBypassed",{}).isBool();
                else if(op=="SET_FX_SLOT_BYPASS") {
                    const auto inserts=channel.getProperty("inserts",{});
                    const int slot=static_cast<int>(row.getProperty("slot",-1));
                    if(auto* all=inserts.getArray())if(slot>=0&&slot<all->size())
                        fxAvailable=all->getReference(slot).getProperty("id","").toString()==
                            row.getProperty("insertId","").toString();
                }
                break;
            }
            const bool fxParam=op.startsWith("SET_FX_PARAM_");
            if(fxParam) {
                armix::hotkey::Mapping mapping;
                if(armix::hotkey::Registry::decode(row,mapping))
                    fxAvailable=fxParameterHotkeyAdmitted(mapping);
            }
            const bool fx=op=="SET_FX_RACK_BYPASS"||
                          op=="SET_FX_SLOT_BYPASS"||fxParam;
            const juce::String status=
                !admitted?"WAITING_NATIVE_AUDIO":
                !found?"NEEDS_REMAP":
                fx&&!fxAvailable?"NEEDS_REMAP":
                (!fx&&(!permitted||!permitted->contains(juce::var(op))))?
                    "ACTION_UNAVAILABLE":
                    row.getProperty("status","").toString();
            row.getDynamicObject()->setProperty("status",status);
        }
        if(requestId.isNotEmpty()) {
            state.getDynamicObject()->setProperty("requestId",requestId);
            state.getDynamicObject()->setProperty("status",
                error.isEmpty()?"ACCEPTED":"REJECTED");
            state.getDynamicObject()->setProperty("errorCode",
                error.isEmpty()?"NONE":error);
        }
        if(browser_.loaded)
            browser_.emitEventIfBrowserIsVisible("arMixHotkeyRegistry",state);
    }

    void handleNativeHotkeyIntent(const juce::var& args) {
        const auto action=args.getProperty("operation","").toString();
        const auto requestId=args.getProperty("requestId","").toString();
        if(action=="LIST")return sendHotkeyRegistry();
        // Capture releases OS chords; restore registrations before Save.
        hotkeys_.setCaptureMode(false);
        juce::String error;
        if(requestId.isEmpty()||requestId.length()>96 ||
           consumedHotkeyRequests_.contains(requestId))
            error="INVALID_OR_DUPLICATE_HOTKEY_REQUEST";
        else {
            consumedHotkeyRequests_.add(requestId);
            if(consumedHotkeyRequests_.size()>128)
                consumedHotkeyRequests_.remove(0);
            if(action=="DELETE") {
                const auto id=args.getProperty("mappingId",{}).toString();
                error=hotkeys_.remove(id);
            } else if(action=="REGISTER") {
                const auto supplied=args.getProperty("mapping",{});
                const auto scope=supplied.getProperty("scope","").toString();
                auto cleaned=juce::var(new juce::DynamicObject());
                for(const char* field:{"id","scope","sessionId","channelId",
                                       "operation","vk","modifiers","slot","insertId",
                                       "catalogId","paramId","paramName"}) {
                    const auto value=supplied.getProperty(field,{});
                    if(!value.isVoid())cleaned.getDynamicObject()->setProperty(field,value);
                }
                if(scope=="SESSION") {
                    const auto accepted=approvedNativeSnapshot();
                    const auto graph=accepted.getProperty("graph",{});
                    const auto channels=graph.getProperty("channels",{});
                    const auto caps=graph.getProperty("capabilities",{});
                    const auto id=supplied.getProperty("channelId",{}).toString();
                    const auto op=supplied.getProperty("operation","").toString();
                    const bool fxRack=op=="SET_FX_RACK_BYPASS";
                    const bool fxSlot=op=="SET_FX_SLOT_BYPASS";
                    const bool fxParam=op.startsWith("SET_FX_PARAM_");
                    bool found=false,fxAvailable=false;
                    if(static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",false)) &&
                       channels.isArray())for(const auto& c:*channels.getArray()) {
                        if(c.getProperty("id","").toString()!=id ||
                           (c.getProperty("role","").toString()!="INPUT" &&
                            c.getProperty("role","").toString()!="GROUP" &&
                            c.getProperty("role","").toString()!="FX_RETURN"))continue;
                        found=true;
                        if(fxRack)fxAvailable=c.getProperty("fxRackBypassed",{}).isBool();
                        else if(fxSlot) {
                            const auto inserts=c.getProperty("inserts",{});
                            const auto slot=supplied.getProperty("slot",{});
                            const auto token=supplied.getProperty("insertId",{});
                            if(slot.isInt()&&token.isString()&&inserts.isArray()) {
                                const int index=static_cast<int>(slot);
                                if(index>=0&&index<inserts.getArray()->size())
                                    fxAvailable=inserts.getArray()->getReference(index)
                                        .getProperty("id","").toString()==token.toString();
                            }
                        }
                        break;
                    }
                    if(!found)error="HOTKEY_CHANNEL_NOT_ADMITTED";
                    if(error.isEmpty()&&(fxRack||fxSlot)&&!fxAvailable)
                        error="HOTKEY_FX_INSTANCE_NOT_ADMITTED";
                    const auto ops=caps.getProperty("supportedOps",{});
                    const auto* allowed=ops.getArray();
                    if(error.isEmpty() && !fxRack && !fxSlot && !fxParam &&
                       op!="SET_MUTE" && op!="SET_SOLO")
                        error="HOTKEY_ACTION_UNSUPPORTED";
                    if(error.isEmpty() && !fxRack && !fxSlot && !fxParam &&
                       (!allowed || !allowed->contains(juce::var(op))))
                        error="HOTKEY_ACTION_NOT_ADMITTED";
                    if(error.isEmpty()) {
                        if(mixerSessionId_.isEmpty())
                            mixerSessionId_=juce::Uuid().toDashedString().toLowerCase();
                        if(hotkeys_.activeSession()!=mixerSessionId_)
                            hotkeys_.activateSession(mixerSessionId_);
                        cleaned.getDynamicObject()->setProperty("sessionId",mixerSessionId_);
                    }
                }
                armix::hotkey::Mapping mapping;
                if(error.isEmpty() && !armix::hotkey::Registry::decode(cleaned,mapping))
                    error="INVALID_HOTKEY_MAPPING";
                if(error.isEmpty()&&mapping.operation.startsWith("SET_FX_PARAM_")&&
                   !fxParameterHotkeyAdmitted(mapping))
                    error="HOTKEY_PARAMETER_ID_OR_INSTANCE_UNAVAILABLE";
                if(error.isEmpty())error=hotkeys_.upsert(std::move(mapping));
            } else error="UNSUPPORTED_HOTKEY_OPERATION";
        }
        sendHotkeyRegistry(requestId,error);
    }

    void fireNativeHotkey(const armix::hotkey::Mapping& mapping) {
        if(mapping.operation=="PANIC_MUTE") {
            engine_.setOutputMuted(true);
            emitApprovedNativeSnapshot();
            return;
        }
        if(mapping.scope!="SESSION" ||
           mapping.sessionId!=mixerSessionId_ || deviceApply_.busy() ||
           nativeShowBusy_ || studioBackupBusy_ || engine_.isGraphSwitchPending())
            return;
        const auto accepted=approvedNativeSnapshot();
        const auto graph=accepted.getProperty("graph",{});
        if(!static_cast<bool>(graph.getProperty("capabilities",{})
              .getProperty("callbackOwnsChannelIds",false)))return;
        const auto channels=graph.getProperty("channels",{});
        if(!channels.isArray())return;
        const juce::var* match=nullptr;
        for(const auto& ch:*channels.getArray())
            if(ch.getProperty("id","").toString()==mapping.channelId &&
               (ch.getProperty("role","").toString()=="INPUT" ||
                ch.getProperty("role","").toString()=="GROUP" ||
                ch.getProperty("role","").toString()=="FX_RETURN")) {
                match=&ch;break;
            }
        if(!match)return;
        if(mapping.operation.startsWith("SET_FX_PARAM_")) {
            (void)applyFxParameterHotkey(mapping);
            return;
        }
        const bool fxRack=mapping.operation=="SET_FX_RACK_BYPASS";
        const bool fxSlot=mapping.operation=="SET_FX_SLOT_BYPASS";
        if(fxRack||fxSlot) {
            juce::var bypass;
            if(fxRack)bypass=match->getProperty("fxRackBypassed",{});
            else {
                const auto inserts=match->getProperty("inserts",{});
                if(!inserts.isArray()||mapping.slot<0||
                   mapping.slot>=inserts.getArray()->size())return;
                const auto insert=inserts.getArray()->getReference(mapping.slot);
                // Fail closed: if a plugin was replaced or reordered, hotkey
                // must never touch the different processor at the same slot.
                if(insert.getProperty("id","").toString()!=mapping.insertId)return;
                bypass=insert.getProperty("bypassed",{});
            }
            if(!bypass.isBool())return;
            auto native=juce::var(new juce::DynamicObject());
            auto* n=native.getDynamicObject();
            n->setProperty("action",fxRack?"routingNativeFxRackBypass":
                                      "routingNativeFxInsertBypass");
            n->setProperty("requestId","hotkey-fx-"+juce::String(
                static_cast<juce::int64>(++firedHotkeySerial_)));
            n->setProperty("channelId",mapping.channelId);
            n->setProperty("deviceEpoch",accepted.getProperty("device",{}).getProperty("epoch","0"));
            n->setProperty("revision",graph.getProperty("revision","0"));
            n->setProperty("value",!static_cast<bool>(bypass));
            if(fxSlot) {
                n->setProperty("slot",mapping.slot);
                n->setProperty("insertId",mapping.insertId);
            }
            // Exact same owner, guardrails and ACK as GUI rack/slot Power.
            onAction(native);
            return;
        }
        if(mapping.operation!="SET_MUTE"&&mapping.operation!="SET_SOLO")return;
        const auto control=mapping.operation=="SET_MUTE"?"mute":"solo";
        const auto existing=match->getProperty("controls",{}).getProperty(control,{});
        if(!existing.isBool())return;
        auto payload=juce::var(new juce::DynamicObject());
        payload.getDynamicObject()->setProperty("value",!static_cast<bool>(existing));
        auto request=juce::var(new juce::DynamicObject());
        auto* o=request.getDynamicObject();
        o->setProperty("requestId","hotkey-"+juce::String(static_cast<juce::int64>(++firedHotkeySerial_)));
        o->setProperty("operation",mapping.operation);
        o->setProperty("targetChannelId",mapping.channelId);
        o->setProperty("deviceEpoch",accepted.getProperty("device",{}).getProperty("epoch","0"));
        o->setProperty("baseGraphRevision",graph.getProperty("revision","0"));
        o->setProperty("baseRosterRevision",graph.getProperty("rosterRevision","0"));
        o->setProperty("payload",payload);
        handleApprovedNativeControl(request);
    }

    void handleLogicalMixerIntent(const juce::var& request) {
        const auto snapshot=approvedNativeSnapshot();
        logicalMixer_.handleIntent(request,{
            deviceApply_.busy() || nativeShowBusy_ || studioBackupBusy_ ||
                engine_.isGraphSwitchPending(),
            snapshot.getProperty("logicalMixer",{}).isObject(),
            logicalVendorEditor_.has_value()
        },[this](const juce::String& key) {
            return armix::builtin_fx::findNativeBuiltInFx(key) ||
                (!catalog_.busy() && catalog_.find(key));
        },[this](const auto& workspace,auto channel,auto slot,auto& insert,auto& processor) {
            return prepareLogicalFx(workspace,channel,slot,insert,processor);
        },[this](juce::var ack) {
            if(browser_.loaded)
                browser_.emitEventIfBrowserIsVisible("arMixLogicalAck",std::move(ack));
        });
    }

    juce::var approvedNativeSnapshot (const juce::var& live)
    {
        auto accepted=armix::native_console_m2::snapshot (
            live, engine_.devices(), engine_.isMonitorEnabled());
        // Native routing's generation advances on STOP as well as START.
        // Keep even disconnected G0 snapshots monotonic to invalidate UI.
        (void) armix::console::synchronizeApprovedRouteDeviceEpoch (
            accepted, live);
        ensureLogicalWorkspace();
        // A silent starter may exist internally to verify the callback. Keep
        // the authored mixer visible until its FX and graph are admitted.
        const bool showCheckpoint=mixerSessionRecoveryPending_ &&
            engine_.hasPristineStartupMixer();
        if(!showCheckpoint) {
            (void) armix::console::publishVerifiedFirstSoundState (accepted, live);
            if (!armix::console::projectInstalledNativeChannelIdGraph (accepted, live))
                (void) armix::console::projectRetainedNativeChannelIdGraph (accepted, live);
        }
        armix::console::publishAuthoritativeMixerSession(accepted);
        const auto caps=accepted.getProperty("graph",{})
            .getProperty("capabilities",{});
        const auto authority=accepted.getProperty("mixerSession",{})
            .getProperty("authority","").toString();
        if(!static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",false)) &&
           authority!="CHANNEL_ID" && authority!="M4" &&
           !engine_.hasRetainedNativeSession()) {
            // Separate field: physical graph remains truthful/unadmitted.
            // Browsers may render this only as an explicitly unbound editor.
            accepted.getDynamicObject()->setProperty("logicalMixer",logicalMixer_.readback());
        }
        if(auto* obj=accepted.getDynamicObject()) {
            obj->setProperty("channelAppearance",channelAppearance_.snapshot(accepted.getProperty("graph",{})));
            obj->setProperty("waitingAsioInputs",live.getProperty("waitingAsioInputs",0));
        }
        return accepted;
    }

    juce::var approvedNativeSnapshot()
    {
        return approvedNativeSnapshot (engine_.status());
    }

    void emitApprovedNativeSnapshot()
    {
        if (consoleSubscribed_ && browser_.loaded)
            browser_.emitEventIfBrowserIsVisible (
                "arMixConsoleSnapshot", approvedNativeSnapshot());
    }

    // UI-only native transaction. No PCM mutation; never arm an output.
    void handleNativeChannelIdentitySet(const juce::var& request)
    {
        const auto v=request.getProperty("requestId",{});
        const auto id=v.isString()?v.toString():juce::String{};
        juce::String error;
        if(id.isEmpty()||id.length()>120)
            error="INVALID_CHANNEL_APPEARANCE_REQUEST_ID";
        else if(acceptedAppearanceRequests_.contains(id))
            error="DUPLICATE_CHANNEL_APPEARANCE_REQUEST_ID";
        else {
            acceptedAppearanceRequests_.add(id);
            if(acceptedAppearanceRequests_.size()>256)
                acceptedAppearanceRequests_.remove(0);
            error=channelAppearance_.apply(request,approvedNativeSnapshot());
        }
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",id);
        ack->setProperty("ok",error.isEmpty());
        ack->setProperty("error",error);
        if(browser_.loaded)
            browser_.emitEventIfBrowserIsVisible("arMixChannelIdentityAck",juce::var(ack.release()));
        if(error.isEmpty()) {
            diagnostics_.record("NATIVE_CHANNEL_APPEARANCE_SAVED",
                request.getProperty("channelId","").toString());
            emitApprovedNativeSnapshot();
        } else diagnostics_.record("NATIVE_CHANNEL_APPEARANCE_REJECTED",error);
    }

    // One native dispatcher; the source of truth is the already installed
    // ENGINE callback. This code executes on the JUCE message thread, not RT.
    void handleApprovedNativeControl(const juce::var& request)
    {
        if(deviceApply_.busy()) {
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request.getProperty("requestId",{}));
            ack->setProperty("status","REJECTED");
            ack->setProperty("errorCode","AUDIO_DEVICE_APPLY_IN_PROGRESS");
            browser_.emitEventIfBrowserIsVisible("arMixConsoleAck",juce::var(ack.release()));
            return;
        }
        const auto beforeLive=engine_.status();
        const auto before=approvedNativeSnapshot(beforeLive);
        const auto route=beforeLive.getProperty("dryRoutingLab",{});
        const auto installedBus=route.getProperty("nativeBus",{});
        const auto idValue=request.getProperty("requestId",{});
        const auto id=idValue.isString()?idValue.toString():juce::String{};
        const bool replay=id.isNotEmpty()&&acceptedControlRequests_.contains(id);
        // All well-formed IDs are consumed once, including failed mutations.
        // Bounded storage avoids unbounded memory growth in a long session.
        if(id.isNotEmpty()&&id.length()<=128&&!replay) {
            acceptedControlRequests_.add(id);
            if(acceptedControlRequests_.size()>512)
                acceptedControlRequests_.remove(0);
        }
        const auto translated=armix::console::translateApprovedNativeControl(
            request,before,installedBus);
        bool applied=false;
        juce::String nativeError;
        if(!replay&&translated.ok()) {
            const auto command=translated.translated;
            if(command.getProperty("operation","").toString()=="NATIVE_LOAD_INSERT") {
                // Exactly one catalog-resolved vendor processor from the
                // installed native library. No browser path, ordinal M4
                // fallback, secondary DSP or user-visible MONITOR ritual.
                const auto args=command.getProperty("payload",{});
                const auto catalogId=args.getProperty("catalogId","").toString();
                const auto* builtIn=armix::builtin_fx::findNativeBuiltInFx(catalogId);
                // Native FX do not depend on third-party catalog scans.
                const bool externalCatalog=builtIn==nullptr;
                const auto* plugin=externalCatalog?catalog_.find(catalogId):nullptr;
                std::uint64_t ep=0,rev=0,roster=0,target=0;
                if((externalCatalog&&(!plugin||catalog_.busy()))||nativeShowBusy_||
                   studioBackupBusy_||engine_.isGraphSwitchPending()) {
                    nativeError="NATIVE_VST3_CATALOG_OR_HOST_BUSY";
                } else if(!armix::console::approvedNativeId(
                            command.getProperty("deviceEpoch",{}),ep) ||
                          !armix::console::approvedNativeId(
                            command.getProperty("baseGraphRevision",{}),rev) ||
                          !armix::console::approvedNativeId(
                            command.getProperty("baseRosterRevision",{}),roster) ||
                          !armix::console::approvedNativeId(
                            command.getProperty("targetChannelId",{}),target)) {
                    nativeError="NATIVE_VST3_INVALID_SESSION_ID";
                } else {
                    const auto result=builtIn?
                        engine_.installNativeBuiltInFx(
                            *builtIn,ep,rev,roster,armix::mixer::ChannelId{target}):
                        engine_.installNativeVst3(
                            ep,rev,roster,armix::mixer::ChannelId{target},
                            plugin->path,plugin->uid,plugin->key(),plugin->name);
                    applied=result.accepted && result.error &&
                            result.error[0]==0;
                    if(!applied)nativeError=result.error;
                }
            } else {
                applied=armix::console::executeApprovedNativeControl(engine_,command,&nativeError);
            }
        }
        // An engine setter ACK is not independent PCM evidence. Re-read the
        // same physical callback authority after mutation before claiming it
        // was accepted; browser then explicitly requests LATER readback.
        const auto after=applied?approvedNativeSnapshot():before;
        const auto device=after.getProperty("device",{});
        const auto graph=after.getProperty("graph",{});
        const auto caps=graph.getProperty("capabilities",{});
        const bool nativeStillAdmitted=applied&&caps.isObject()&&
            static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",false))&&
            device.getProperty("epoch","0").toString()==
                request.getProperty("deviceEpoch","").toString();
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",id);
        ack->setProperty("status",nativeStillAdmitted?"ACCEPTED":"REJECTED");
        ack->setProperty("errorCode",nativeStillAdmitted?"NONE":
            replay?"DUPLICATE_REQUEST_ID":
            translated.error.isNotEmpty()?translated.error:
            nativeError.isNotEmpty()?nativeError:
            applied?"NATIVE_CALLBACK_LOST":"NATIVE_PCM_MUTATION_REJECTED");
        ack->setProperty("deviceEpoch",device.getProperty("epoch","0"));
        ack->setProperty("graphRevision",graph.getProperty("revision","0"));
        ack->setProperty("rosterRevision",graph.getProperty("rosterRevision","0"));
        ack->setProperty("productionCallbackLive",false);
        if(browser_.loaded)
            browser_.emitEventIfBrowserIsVisible("arMixConsoleAck",
                                               juce::var(ack.release()));
        // NEVER emit a same-handler optimistic snapshot. The approved browser
        // sends arMixConsoleSubscribe AFTER ACK and verifies a later producer
        // readback before moving fader/route UI from its accepted state.
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
        const auto live=engine_.status();
        o->setProperty("deviceGeneration",live.getProperty("deviceGeneration",0));
        o->setProperty("mainOutputs",live.getProperty("dryRoutingLab",{}).getProperty("mainOutputs",{}));
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

    // One-shot on a verified setup or startup, including the first input
    // patch. A later device epoch or a user MUTE cancels the pending arm.
    std::uint64_t autoMainEpoch_=0;
    bool autoMainRequiresInput_=true;
    std::optional<std::uint64_t> autoMainCheckedRevision_;
    void serviceAutomaticMain() {
        if(autoMainEpoch_==0)return;
        if(autoMainEpoch_!=engine_.dryRoutingLabEpoch() ||
           engine_.isMonitorEnabled()) {
            autoMainEpoch_=0;
            return;
        }
        if(nativeShowBusy_||studioBackupBusy_||engine_.secondaryArmed()||
           engine_.diagnosticRoute()!=armix::diagnostics::Route::normal)return;
        const auto revision=engine_.outputArmRevision();
        if(autoMainCheckedRevision_==revision)return;
        if(engine_.isGraphSwitchPending()||!engine_.hasBoundMixerRoute())return;
        // A transient native graph/FX ACK must not permanently consume
        // the only automatic MAIN attempt for this control revision.
        // Retry only the cheap CONTROL preflight until it is genuinely safe.
        if(engine_.liveOutputArmFault(autoMainRequiresInput_).isNotEmpty())return;
        autoMainCheckedRevision_=revision;
        engine_.setMonitorEnabled(true);
        autoMainEpoch_=0;
        diagnostics_.record("MAIN_AUTO_ENABLED","Verified device, input and MAIN output");
    }

    void requestAutomaticMain(bool requireInput=true) {
        autoMainRequiresInput_=requireInput;
        autoMainEpoch_=engine_.dryRoutingLabEpoch();
        autoMainCheckedRevision_.reset();
        serviceAutomaticMain();
    }

    using MixerBindingMode=armix::session::MixerDeviceBindingPolicy::Mode;
    armix::device::NativeDeviceApplyCoordinator deviceApply_; // one CONTROL-thread transaction owner

    juce::String openSelectedAudioDevice(const juce::String& backend,
        const juce::AudioDeviceManager::AudioDeviceSetup& setup,bool resetDefaults=false) {
        return armix::device::NativeDeviceApplyCoordinator::openSelected(
            engine_.devices(),backend,setup,resetDefaults,
            [this](const juce::String& phase) {
                diagnostics_.record("CUSTOM_AUDIO_PHASE",phase);
            });
    }

    void applyCustomAudioDevice(const juce::var& args)
    {
        // JUCE device lifecycle runs on the message thread, including ASIO's
        // modal callbacks. Serialize transactions across nested dispatch.
        const auto id=args.getProperty("requestId",{}).toString().substring(0,64);
        if(deviceApply_.busy()) {
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",id);
            ack->setProperty("ok",false);
            ack->setProperty("state","BUSY");
            ack->setProperty("error","Audio I/O is already applying a device change.");
            browser_.emitEventIfBrowserIsVisible("arMixAudioDeviceApplyAck",juce::var(ack.release()));
            return;
        }
        auto applying=deviceApply_.tryBegin(
            armix::device::NativeDeviceApplyCoordinator::Purpose::explicitApply);
        if(!applying)return; // busy ACK already emitted above
        const auto oldGeneration=engine_.status().getProperty("deviceGeneration",0);
        auto send=[&](bool ok,const juce::String& state,const juce::String& error,
                      bool canReusePins=false)
        {
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",id);
            ack->setProperty("ok",ok);
            ack->setProperty("state",state);
            ack->setProperty("error",error);
            ack->setProperty("active",currentDeviceSnapshot());
            ack->setProperty("outputMuted",engine_.isOutputMuted());
            ack->setProperty("mixerBound",engine_.hasBoundMixerRoute());
            ack->setProperty("canReusePins",canReusePins);
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
        if(nativeShowBusy_||studioBackupBusy_||mixerSessionWorker_.busy()||logicalVendorEditor_||
           engine_.isGraphSwitchPending()||
            static_cast<bool>(nativeRoute.getProperty("pending",false))||
            engine_.independentClockActive()||
            engine_.diagnosticRoute()!=armix::diagnostics::Route::normal||
            engine_.secondaryArmed())
            return send(false,"REJECTED",
                "Stop STREAM and finish Show/FX/diagnostic/clock transition first");
        const auto config=args.getProperty("config",{});
        const auto parsed=armix::device::NativeDeviceApplyCoordinator::parse(config);
        if(!parsed)return send(false,"REJECTED",parsed.error);
        const auto& requested=parsed.value;
        const auto& typeName=requested.backend;
        const auto& capture=requested.capture;
        const auto& playback=requested.playback;
        const double rate=requested.rate,block=requested.block;
        auto& manager=engine_.devices();
        juce::AudioIODeviceType* selectedType=nullptr;
        for(auto* type:manager.getAvailableDeviceTypes())
            if(type!=nullptr&&type->getTypeName()==typeName)selectedType=type;
        if(selectedType==nullptr)return send(false,"REJECTED","Audio backend is not installed");
        // Do not let JS submit arbitrary names or combine unmatched ASIO devices.
        const bool separate=selectedType->hasSeparateInputsAndOutputs();
        const bool outputOnly=separate&&capture.isEmpty();
        const auto namesError=armix::device::NativeDeviceApplyCoordinator::validateNames(
            requested,selectedType->getDeviceNames(true),
            selectedType->getDeviceNames(false),separate);
        if(namesError.isNotEmpty())return send(false,"REJECTED",namesError);
        const bool unchangedDevice=manager.getCurrentAudioDeviceType()==typeName;
        juce::AudioDeviceManager::AudioDeviceSetup before;
        manager.getAudioDeviceSetup(before);
        const auto previousType=manager.getCurrentAudioDeviceType();
        const bool sameEndpoint=unchangedDevice&&before.inputDeviceName==capture
                             &&before.outputDeviceName==playback;
        auto* previousDevice=manager.getCurrentAudioDevice();
        const bool previousWasOpen=previousDevice!=nullptr&&previousDevice->isOpen();
        const bool previousMixerBound=engine_.hasBoundMixerRoute();
        const bool previousMonitor=engine_.isMonitorEnabled();
        const bool explicitMain=requested.assignMain;
        if(requested.confirmRetainedPinMapping) {
            // The first Apply must open and verify the selected device.
            // Confirmation binds only that exact, still-open generation;
            // draft rate, buffer and pin changes use the normal Apply path.
            if(explicitMain || !sameEndpoint || !previousWasOpen || rate!=before.sampleRate ||
               block!=before.bufferSize ||
               !engine_.confirmRetainedWasapiPortMapping())
                return send(false,"NEEDS_REMAP",
                    "Cannot reuse mixer pins. Endpoint, pin layout or FX format is incompatible.");
            deviceSettingsDirty_=true;
            directRestore_.cancel();
            diagnostics_.record("CUSTOM_AUDIO_REMAP_ACCEPTED",typeName);
            requestAutomaticMain();
            send(true,engine_.isMonitorEnabled()?"ACCEPTED_LIVE":"ACCEPTED_MUTED",{});
            emitApprovedNativeSnapshot();
            return;
        }
        const auto originalInputNames=previousWasOpen
            ?previousDevice->getInputChannelNames():juce::StringArray{};
        const auto originalOutputNames=previousWasOpen
            ?previousDevice->getOutputChannelNames():juce::StringArray{};
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
        // External ASIO4ALL changes can leave the same endpoint name in JUCE
        // with a closed object. Reopen with safe default masks first; the
        // previous (possibly reordered) physical pin indices are not valid.
        const auto recovery=armix::device::AudioSetupRecoveryPolicy::select(
            sameEndpoint,previousWasOpen,typeName=="ASIO",
            pinInput.isArray()&&pinOutput.isArray());
        if(explicitMain && (!recovery.reuseActivePins ||
                           pinOutput.getArray()->size()!=2))
            return send(false,"REJECTED","Select exactly two inspected playback pins for MAIN.");
        if(recovery.reuseActivePins) {
            juce::BigInteger inMask,outMask;
            if(!armix::device::NativeDeviceApplyCoordinator::parsePinMasks(
                  pinInput,pinOutput,previousDevice->getInputChannelNames().size(),
                  previousDevice->getOutputChannelNames().size(),outputOnly,
                  inMask,outMask))
                return send(false,"REJECTED",
                    "Select real input pins (or none) and >=2 verified playback pins");
            candidate.useDefaultInputChannels=false;
            candidate.useDefaultOutputChannels=false;
            candidate.inputChannels=inMask;
            candidate.outputChannels=outMask;
        }else {
            // The requested 0-IN option must not reopen a microphone merely
            // because JUCE defaults to enabling available capture channels.
            // Output pin selection still requires actual driver readback.
            candidate.useDefaultInputChannels=!outputOnly;
            if(outputOnly)candidate.inputChannels.clear();
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
        autoMainEpoch_=0;
        engine_.beginManualAudioSetup();
        // JUCE setAudioDeviceSetup() can return success without reopening when
        // setup == currentSetup and currentAudioDevice != nullptr, even when
        // that driver object has stopped. closeAudioDevice() deletes it but
        // keeps the setup for an explicit, fresh driver instance on APPLY.
        // For ASIO this also observes changed ASIO4ALL aggregate WDM pins.
        // Only the operator initiates this; never auto-reopen/arm on a timer.
        if(recovery.recreateDevice && manager.getCurrentAudioDeviceType()==typeName) {
            diagnostics_.record("CUSTOM_AUDIO_RECREATE",
                typeName+" | "+(previousWasOpen?"REFRESH_ASIO_PINS":"CLOSED_DEVICE_RECOVERY"));
            manager.closeAudioDevice();
        }
        diagnostics_.record("CUSTOM_AUDIO_PHASE","APPLY | "+typeName);
        auto error=openSelectedAudioDevice(typeName,candidate);
        diagnostics_.record("CUSTOM_AUDIO_PHASE","VERIFY_READBACK | "+typeName);
        juce::AudioDeviceManager::AudioDeviceSetup actual;
        manager.getAudioDeviceSetup(actual);
        auto* opened=manager.getCurrentAudioDevice();
        bool changedAsioPinLayout=error.isEmpty()&&recovery.verifyAsioPinIdentity&&
            opened!=nullptr&&opened->isOpen()&&
            (opened->getInputChannelNames()!=originalInputNames||
             opened->getOutputChannelNames()!=originalOutputNames);
        const bool refreshedAsio4All=typeName=="ASIO" && playback.startsWithIgnoreCase("ASIO4ALL") &&
            recovery.reuseActivePins && (changedAsioPinLayout || error.isNotEmpty());
        bool explicitPairStillIdentified=explicitMain;
        if(refreshedAsio4All) {
            // Never replay an index into a newly enumerated ASIO aggregate.
            // Preserve verified selected names if they still exist, including
            // VB-Cable capture moved past newly enabled input channels.
            using BindingPolicy=armix::session::MixerDeviceBindingPolicy;
            const auto freshInputNames=changedAsioPinLayout?opened->getInputChannelNames():juce::StringArray{};
            const auto freshOutputNames=changedAsioPinLayout?opened->getOutputChannelNames():juce::StringArray{};
            const auto capturePins=changedAsioPinLayout?BindingPolicy::translateNamedPins(
                candidate.inputChannels,originalInputNames,freshInputNames):std::nullopt;
            const auto playbackPins=changedAsioPinLayout?BindingPolicy::translateNamedPins(
                candidate.outputChannels,originalOutputNames,freshOutputNames):std::nullopt;
            candidate.useDefaultInputChannels=!capturePins && !candidate.inputChannels.isZero();
            candidate.useDefaultOutputChannels=!playbackPins;
            if(capturePins)candidate.inputChannels=*capturePins;
            if(playbackPins)candidate.outputChannels=*playbackPins;
            explicitPairStillIdentified=explicitMain && playbackPins.has_value();
            diagnostics_.record("CUSTOM_AUDIO_PHASE","REFRESH_ASIO4ALL_INVENTORY");
            error=openSelectedAudioDevice(typeName,candidate,true);
            manager.getAudioDeviceSetup(actual);
            opened=manager.getCurrentAudioDevice();
            changedAsioPinLayout=error.isEmpty() && opened && opened->isOpen() &&
                ((capturePins && opened->getInputChannelNames()!=freshInputNames) ||
                 (playbackPins && opened->getOutputChannelNames()!=freshOutputNames));
        }
        // Never consume the automatic recovery epoch before a mixer bind.
        // A transient ASIO4ALL pin inventory can settle on the next tick.
        if(changedAsioPinLayout)
            error="ASIO pin layout changed. Output remains muted; review the new playback pins.";
        if(error.isEmpty()) {
            const auto observed=armix::device::NativeDeviceApplyCoordinator::inspectApplied(manager);
            if(!armix::device::NativeDeviceApplyCoordinator::matchesApplied(
                    requested,candidate,observed))
                error="Driver readback mismatch; device setting was not accepted";
        }
        if(error.isEmpty())diagnostics_.record("CUSTOM_AUDIO_PHASE","BIND_MIXER | "+typeName);
        const auto bindingMode=explicitPairStillIdentified?MixerBindingMode::verifiedMainPinApply:
                                            MixerBindingMode::verifiedDeviceApply;
        if(error.isEmpty()&&!finishAudioSetupAndRecoverMixer(bindingMode)) {
            if(!previousMixerBound && engine_.directRouteAuthorized() &&
               (engine_.hasRetainedUnboundMixer() || mixerSessionRecoveryPending_)) {
                const auto needsRemap=mixerSessionError_.isNotEmpty()?mixerSessionError_:
                    juce::String("Device opened, but the mixer could not bind to its playback pins.");
                diagnostics_.record("CUSTOM_AUDIO_NEEDS_REMAP",needsRemap);
                send(false,"NEEDS_REMAP",needsRemap,engine_.stageRetainedWasapiPortMapping());
                emitApprovedNativeSnapshot();
                return;
            }
            error=mixerSessionError_.isNotEmpty()?mixerSessionError_:
                juce::String("Device opened but mixer preparation was rejected");
        }
        if(error.isEmpty()) {
            startupError_.clear();
            deviceSettingsDirty_=true;
            directRestore_.cancel();
            asioRecoveryEpoch_=engine_.dryRoutingLabEpoch();
            diagnostics_.record("CUSTOM_AUDIO_APPLY_OK",
                typeName+" | "+juce::String(static_cast<int>(monotonicMilliseconds()-start))+" ms");
            requestAutomaticMain();
            send(true,engine_.isMonitorEnabled()?"ACCEPTED_LIVE":"ACCEPTED_MUTED",{});
            emitApprovedNativeSnapshot();
            return;
        }
        // A changed ASIO layout cannot be 'restored' by replaying indices
        // that now refer to different WDM pins. Keep the fresh device MUTED,
        // unauthorised, and show its new pin names for deliberate remapping.
        if(changedAsioPinLayout) {
            diagnostics_.record("CUSTOM_AUDIO_ASIO_TOPOLOGY_CHANGED",error);
            return send(false,"RECOVERY_REQUIRED",error);
        }
        // One rollback, never blind default endpoint fallback, no auto-unmute.
        // A previously CLOSED device was never a verified rollback target.
        engine_.beginManualAudioSetup();
        diagnostics_.record("CUSTOM_AUDIO_PHASE","ROLLBACK | "+previousType);
        const auto restored=recovery.priorKnownOpen&&previousType.isNotEmpty()&&
            openSelectedAudioDevice(previousType,before).isEmpty()&&
            manager.getCurrentAudioDeviceType()==previousType;
        const auto rollback=armix::device::NativeDeviceApplyCoordinator::inspectRollback(manager);
        const bool verified=restored&&
            armix::device::NativeDeviceApplyCoordinator::matchesRollback(
                before,previousType,originalInputNames,originalOutputNames,rollback)&&
            finishAudioSetupAndRecoverMixer();
        if(verified)asioRecoveryEpoch_=engine_.dryRoutingLabEpoch();
        diagnostics_.record("CUSTOM_AUDIO_APPLY_FAIL",error+" | rollback="+
            (verified?"RESTORED":"RECOVERY_REQUIRED")+" | "+
            juce::String(static_cast<int>(monotonicMilliseconds()-start))+" ms");
        if(verified&&previousMonitor)requestAutomaticMain();
        send(false,verified?(engine_.isMonitorEnabled()?"RESTORED_LIVE":"RESTORED_MUTED"):
            "RECOVERY_REQUIRED",error);
        emitApprovedNativeSnapshot();
    }

    void showDeviceSetup()
    {
        diagnostics_.record ("SETUP", "Open audio device setup");
        if (setupLaunching_) return;
        // Repeated clicks must not re-suspend output or discard a pending
        // preserve-monitor intention. The selector itself is already active.
        if (audioSetupWindow_ && audioSetupWindow_->isVisible()) {
            audioSetupWindow_->toFront(true);
            return;
        }
        auto modalLease=deviceApply_.tryBegin(
            armix::device::NativeDeviceApplyCoordinator::Purpose::setupDialog);
        if(!modalLease) {
            emitNotice("Finish the active audio device change first.",true);
            return;
        }
        audioSetupLease_.emplace(std::move(modalLease));
        // Hold a lease for the WHOLE modal selection, including ASIO's
        // nested dispatcher and the final accepted mixer rebind.
        autoMainEpoch_=0;
        engine_.beginManualAudioSetup(true);

        // Reuse cached JUCE selector without creating another DeviceManager.
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
                        if (safe == nullptr || !safe->audioSetupLease_) return;
                        // Keep the transaction reserved until the selector
                        // close has completed rebind, native readback and ACK.
                        auto closingLease=std::move(*safe->audioSetupLease_);
                        safe->audioSetupLease_.reset();
                        if (safe->finishAudioSetupAndRecoverMixer())
                        {
                            safe->requestAutomaticMain(!safe->engine_.takeAsioResetMonitorIntent());
                            safe->deviceSettingsDirty_ = true;
                            safe->startupError_.clear();
                            safe->diagnostics_.record ("DIRECT_ROUTE_APPROVED",
                                "User confirmed selected ASIO/WASAPI backend in AUDIO SETUP");
                            // Refresh native physical port inventory after the user
                            // finishes device selection; never require a restart.
                            safe->emitApprovedNativeSnapshot();
                            // The operator already pressed Play AND chose a
                            // physical output. Finish that exact pending Play
                            // once; never autoplay on ordinary device setup.
                            const int pending=safe->playerOutputPendingIndex_;
                            safe->playerOutputPendingIndex_=-1;
                            if(pending>=0 && pending==safe->playerSelected_ &&
                               pending<static_cast<int>(safe->playerLibrary_.size()))
                                safe->playSelectedMusic();
                        }
                        else
                        {
                            safe->playerOutputPendingIndex_=-1;
                            safe->diagnostics_.record ("DIRECT_ROUTE_REJECTED",
                                "No verified stereo playback device; audio remains gated");
                            safe->emitNotice (
                                safe->engine_.hasRetainedUnboundMixer()
                                    ? "Playback unavailable. Mixer and plugin settings are saved."
                                    : "Choose a working stereo output in Audio I/O. Input hardware is optional.", true);
                            safe->emitApprovedNativeSnapshot();
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
        result << "Build: " << juce::JUCEApplication::getInstance()->getApplicationVersion()
               << " | Source HEAD: " << ARMIX_BUILD_SOURCE_SHA << "\n";
        result << "Platform: " << juce::SystemStats::getOperatingSystemName() << "\n";
        result << "CPU: " << juce::SystemStats::getCpuModel() << "\n";
        result << "Logical cores: " << juce::SystemStats::getNumCpus()
               << " | RAM: " << juce::SystemStats::getMemorySizeInMegabytes() << " MiB\n";
        result << "Telemetry: true hardware render underruns UNAVAILABLE; callback time/gap are proxies only.\n";
        const auto memory=readMemoryPressure();
        result << "=== WINDOWS MEMORY PRESSURE (MESSAGE THREAD ONLY) ===\n";
        if(memory.systemAvailable)
            result << "System RAM load: " << static_cast<juce::int64>(memory.systemLoadPercent)
                   << "% | Physically available: "
                   << static_cast<juce::int64>(memory.availablePhysicalMiB)
                   << " MiB\n";
        else result << "System RAM pressure: UNAVAILABLE\n";
        if(memory.processAvailable)
            result << "ArSonMix resident working set: "
                   << static_cast<juce::int64>(memory.processWorkingSetMiB)
                   << " MiB | Private commit: "
                   << static_cast<juce::int64>(memory.processPrivateCommitMiB)
                   << " MiB | All process page faults (SOFT+HARD): "
                   << static_cast<juce::int64>(memory.processPageFaults) << "\n";
        else result << "ArSonMix process counters: UNAVAILABLE\n";
        result << "70% is NOT an OS or ASIO glitch threshold. These are correlation"
                  " indicators, not disk hard-fault evidence or a fix for lost audio.\n";
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

    // One message-thread owner for native EQ commands/readback. The only
    // parameter writer is the existing JUCE callback via ChannelEqProcessor's
    // bounded SPSC mailbox. ACK is emitted only AFTER audio consumed the edit.
    void emitNativeEqSnapshot(const juce::String& request,
        const juce::String& channelId,const juce::String& epoch,
        const juce::String& revision,int slot,const juce::String& instanceId,
        armix::builtin_fx::ChannelEqProcessor* processor) {
        auto reply=std::make_unique<juce::DynamicObject>();
        reply->setProperty("requestId",request);
        reply->setProperty("channelId",channelId);
        reply->setProperty("deviceEpoch",epoch);
        reply->setProperty("graphRevision",revision);
        reply->setProperty("slot",slot);
        reply->setProperty("instanceId",instanceId);
        reply->setProperty("schemaVersion",1);
        reply->setProperty("status",processor?"ACCEPTED":"REJECTED");
        if(processor) {
            reply->setProperty("sampleRate",processor->acceptedSampleRate());
            juce::Array<juce::var> bands;
            for(const auto& band:processor->acceptedBands()){
                auto item=std::make_unique<juce::DynamicObject>();
                item->setProperty("enabled",band.enabled);
                item->setProperty("frequencyHz",static_cast<double>(band.frequencyHz));
                item->setProperty("gainDb",static_cast<double>(band.gainDb));
                item->setProperty("q",static_cast<double>(band.q));
                item->setProperty("slope",static_cast<int>(band.slope));
                bands.add(juce::var(item.release()));
            }
            reply->setProperty("bands",juce::var(bands));
            if(epoch=="0" && instanceId==lastLogicalFxInstance_ && revision==lastLogicalFxRevision_)
                reply->setProperty("appliedRequestId",lastLogicalFxRequest_);
            else if(instanceId==lastNativeEqInstance_ &&
               epoch==lastNativeEqEpoch_ && revision==lastNativeEqRevision_)
                reply->setProperty("appliedRequestId",lastNativeEqRequest_);
        } else reply->setProperty("error","NATIVE_EQ_INSTANCE_OR_REVISION_UNAVAILABLE");
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinEqSnapshot",juce::var(reply.release()));
    }

    void servicePendingNativeEq() {
        if(!pendingNativeEq_.owner)return;
        auto* processor=dynamic_cast<armix::builtin_fx::ChannelEqProcessor*>(
            pendingNativeEq_.owner->processor.get());
        if(!processor){pendingNativeEq_={};return;}
        armix::builtin_fx::ChannelEqProcessor::Completion completion;
        if(!processor->pollCompletion(completion)){
            if(juce::Time::getMillisecondCounter()-pendingNativeEq_.started<3000u)
                return;
        }
        const auto request=pendingNativeEq_.request;
        bool applied=completion.applied && completion.requestId &&
            request=="eq-"+juce::String(std::to_string(completion.requestId));
        // Reject stale graph/device ACKs even if DSP consumed the old command.
        const auto snap=approvedNativeSnapshot();
        const auto device=snap.getProperty("device",{});
        const auto graph=snap.getProperty("graph",{});
        applied=applied && pendingNativeEq_.deviceEpoch==
                device.getProperty("epoch","").toString() &&
                pendingNativeEq_.revision==
                graph.getProperty("revision","").toString();
        if(applied){
            lastNativeEqRequest_=request;
            lastNativeEqInstance_=pendingNativeEq_.instanceId;
            lastNativeEqEpoch_=pendingNativeEq_.deviceEpoch;
            lastNativeEqRevision_=pendingNativeEq_.revision;
        }
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",request);
        ack->setProperty("channelId",pendingNativeEq_.channelId);
        ack->setProperty("deviceEpoch",pendingNativeEq_.deviceEpoch);
        ack->setProperty("graphRevision",pendingNativeEq_.revision);
        ack->setProperty("instanceId",pendingNativeEq_.instanceId);
        ack->setProperty("status",applied?"APPLIED":"REJECTED");
        ack->setProperty("errorCode",applied?"NONE":"DSP_EDIT_NOT_APPLIED_OR_STALE");
        pendingNativeEq_={};
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinEqAck",juce::var(ack.release()));
    }



    // Native compressor readback is CONTROL-thread-only for accepted settings.
    // The gain-reduction meter is one atomic block summary from AUDIO.
    void emitNativeCompSnapshot(const juce::String& request,
            const juce::String& channelId,const juce::String& epoch,
            const juce::String& revision,const juce::String& instanceId,
            armix::builtin_fx::ChannelCompressorProcessor* processor) {
        auto reply=std::make_unique<juce::DynamicObject>();
        reply->setProperty("requestId",request);
        reply->setProperty("channelId",channelId);
        reply->setProperty("deviceEpoch",epoch);
        reply->setProperty("graphRevision",revision);
        reply->setProperty("instanceId",instanceId);
        reply->setProperty("schemaVersion",1);
        reply->setProperty("status",processor?"ACCEPTED":"REJECTED");
        if(processor) {
            auto p=std::make_unique<juce::DynamicObject>();
            const auto s=processor->acceptedSettings();
            p->setProperty("thresholdDb",s.thresholdDb);
            p->setProperty("ratio",s.ratio);
            p->setProperty("attackMs",s.attackMs);
            p->setProperty("releaseMs",s.releaseMs);
            p->setProperty("kneeDb",s.kneeDb);
            p->setProperty("makeupDb",s.makeupDb);
            p->setProperty("mix",s.mix);
            reply->setProperty("settings",juce::var(p.release()));
            reply->setProperty("sampleRate",processor->acceptedSampleRate());
            reply->setProperty("gainReductionDb",processor->observedGainReductionDb());
            // Actual pre/post Compressor PCM block peaks. Serial increments
            // only after a processed audio callback; UI can decay stale
            // meters to silence on device stop without touching realtime.
            reply->setProperty("inputPeakDbfs",processor->observedInputPeakDbfs());
            reply->setProperty("outputPeakDbfs",processor->observedOutputPeakDbfs());
            reply->setProperty("meterBlockSerial",
                static_cast<juce::int64>(processor->observedBlockSerial()));
            reply->setProperty("addedLatencySamples",processor->getLatencySamples());
            if(epoch=="0" && instanceId==lastLogicalFxInstance_ && revision==lastLogicalFxRevision_)
                reply->setProperty("appliedRequestId",lastLogicalFxRequest_);
            else if(instanceId==lastNativeCompInstance_&&
               epoch==lastNativeCompEpoch_&&revision==lastNativeCompRevision_)
                reply->setProperty("appliedRequestId",lastNativeCompRequest_);
        }else reply->setProperty("error","NATIVE_COMP_INSTANCE_OR_REVISION_UNAVAILABLE");
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinCompSnapshot",juce::var(reply.release()));
    }

    void servicePendingNativeComp() {
        if(!pendingNativeComp_.owner)return;
        auto* processor=dynamic_cast<armix::builtin_fx::ChannelCompressorProcessor*>(
            pendingNativeComp_.owner->processor.get());
        if(!processor){pendingNativeComp_={};return;}
        armix::builtin_fx::ChannelCompressorProcessor::Completion completion{};
        if(!processor->pollCompletion(completion) &&
           juce::Time::getMillisecondCounter()-pendingNativeComp_.started<3000u)
            return;
        const auto request=pendingNativeComp_.request;
        bool applied=completion.applied&&completion.requestId!=0 &&
            request=="comp-"+juce::String(std::to_string(completion.requestId));
        const auto snap=approvedNativeSnapshot();
        const auto device=snap.getProperty("device",{});
        const auto graph=snap.getProperty("graph",{});
        applied=applied && pendingNativeComp_.deviceEpoch==
                    device.getProperty("epoch","").toString() &&
                    pendingNativeComp_.revision==
                    graph.getProperty("revision","").toString();
        if(applied) {
            lastNativeCompRequest_=request;
            lastNativeCompInstance_=pendingNativeComp_.instanceId;
            lastNativeCompEpoch_=pendingNativeComp_.deviceEpoch;
            lastNativeCompRevision_=pendingNativeComp_.revision;
        }
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",request);
        ack->setProperty("channelId",pendingNativeComp_.channelId);
        ack->setProperty("deviceEpoch",pendingNativeComp_.deviceEpoch);
        ack->setProperty("graphRevision",pendingNativeComp_.revision);
        ack->setProperty("instanceId",pendingNativeComp_.instanceId);
        ack->setProperty("status",applied?"APPLIED":"REJECTED");
        ack->setProperty("errorCode",applied?"NONE":"DSP_EDIT_NOT_APPLIED_OR_STALE");
        pendingNativeComp_={};
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinCompAck",juce::var(ack.release()));
    }

    // One channel/slot/epoch/revision-bound Harmony state. No browser DSP.
    void emitNativeHarmonySnapshot(const juce::String& request,
            const juce::String& channelId,const juce::String& epoch,
            const juce::String& revision,const juce::String& instanceId,
            armix::builtin_fx::ChannelHarmonyProcessor* processor) {
        auto reply=std::make_unique<juce::DynamicObject>();
        reply->setProperty("requestId",request);
        reply->setProperty("channelId",channelId);
        reply->setProperty("deviceEpoch",epoch);
        reply->setProperty("graphRevision",revision);
        reply->setProperty("instanceId",instanceId);
        reply->setProperty("schemaVersion",3);
        reply->setProperty("status",processor?"ACCEPTED":"REJECTED");
        if(processor) {
            const auto s=processor->acceptedSettings();
            auto settings=std::make_unique<juce::DynamicObject>();
            settings->setProperty("key",s.key);
            settings->setProperty("scale",s.scale);
            settings->setProperty("voices",s.voices);
            settings->setProperty("voiceMode",s.voiceMode);
            settings->setProperty("correction",s.correction);
            settings->setProperty("wet",s.wet);
            settings->setProperty("space",s.space);
            settings->setProperty("color",s.color);
            settings->setProperty("gainDb",s.gainDb);
            settings->setProperty("guideMode",s.guideMode);
            settings->setProperty("guideChannelId",
                juce::String(std::to_string(s.guideChannelId)));
            reply->setProperty("settings",juce::var(settings.release()));
            reply->setProperty("observedNote",processor->observedNote());
            // Audio-published, atomic pair: exactly the key/scale used by DSP,
            // including manual fallback during silence or uncertain music.
            const int effective=processor->effectiveKeyCode();
            reply->setProperty("effectiveKey",effective%12);
            reply->setProperty("effectiveScale",effective/12);
            reply->setProperty("guideDetectedKey",processor->observedMusicKey());
            reply->setProperty("guideDetectedScale",processor->observedMusicScale());
            reply->setProperty("guideConfidence",
                static_cast<double>(processor->observedMusicConfidence()));
            reply->setProperty("sampleRate",processor->acceptedSampleRate());
            reply->setProperty("latencySamples",processor->getLatencySamples());
            if(epoch=="0" && instanceId==lastLogicalFxInstance_ && revision==lastLogicalFxRevision_)
                reply->setProperty("appliedRequestId",lastLogicalFxRequest_);
            else if(instanceId==lastNativeHarmonyInstance_ &&
               epoch==lastNativeHarmonyEpoch_ &&
               revision==lastNativeHarmonyRevision_)
                reply->setProperty("appliedRequestId",lastNativeHarmonyRequest_);
        }else reply->setProperty("errorCode","HARMONY_INSTANCE_OR_REVISION_UNAVAILABLE");
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinHarmonySnapshot",juce::var(reply.release()));
    }

    void servicePendingNativeHarmony() {
        if(!pendingNativeHarmony_.owner)return;
        auto* processor=dynamic_cast<armix::builtin_fx::ChannelHarmonyProcessor*>(
            pendingNativeHarmony_.owner->processor.get());
        if(!processor){pendingNativeHarmony_={};return;}
        armix::builtin_fx::ChannelHarmonyProcessor::Completion completion{};
        if(!processor->pollCompletion(completion) &&
           juce::Time::getMillisecondCounter()-pendingNativeHarmony_.started<3000u)
            return;
        const auto request=pendingNativeHarmony_.request;
        bool applied=completion.applied&&completion.requestId &&
            request=="harm-"+juce::String(std::to_string(completion.requestId));
        const auto snap=approvedNativeSnapshot();
        const auto device=snap.getProperty("device",{});
        const auto graph=snap.getProperty("graph",{});
        applied=applied && pendingNativeHarmony_.deviceEpoch==
                    device.getProperty("epoch","").toString() &&
                    pendingNativeHarmony_.revision==
                    graph.getProperty("revision","").toString();
        if(applied) {
            lastNativeHarmonyRequest_=request;
            lastNativeHarmonyInstance_=pendingNativeHarmony_.instanceId;
            lastNativeHarmonyEpoch_=pendingNativeHarmony_.deviceEpoch;
            lastNativeHarmonyRevision_=pendingNativeHarmony_.revision;
        }
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",request);
        ack->setProperty("channelId",pendingNativeHarmony_.channelId);
        ack->setProperty("deviceEpoch",pendingNativeHarmony_.deviceEpoch);
        ack->setProperty("graphRevision",pendingNativeHarmony_.revision);
        ack->setProperty("instanceId",pendingNativeHarmony_.instanceId);
        ack->setProperty("status",applied?"APPLIED":"REJECTED");
        ack->setProperty("errorCode",applied?"NONE":"DSP_EDIT_NOT_APPLIED_OR_STALE");
        pendingNativeHarmony_={};
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinHarmonyAck",juce::var(ack.release()));
    }

    // CONTROL ONLY: drain at most 512 ten-ms music summary frames per 12Hz
    // host tick. The actual music audio was tapped inside the existing PCM
    // graph; BPM/onset analysis never touches the ASIO callback. All live
    // vocal inserts consume a precomputed atomic interval, not a WebView BPM.
    void serviceNativeMusicTempo() {
        lastMusicTempo_=engine_.pollNativeMusicTempo();
        const bool bpmLocked=lastMusicTempo_.active&&
            lastMusicTempo_.tempo.locked;
        const bool phaseLocked=bpmLocked&&
            lastMusicTempo_.tempo.phaseLocked&&
            lastMusicTempo_.tempo.beatAnchorFrame>0;
        const float bpm=bpmLocked?lastMusicTempo_.tempo.bpm:0.f;
        const std::uint64_t anchor=phaseLocked?
            lastMusicTempo_.tempo.beatAnchorFrame:0;
        const auto epoch=lastMusicTempo_.epoch;
        const auto revision=lastMusicTempo_.revision;
        engine_.forEachNativeProcessor([bpm,anchor,epoch,revision](
            armix::mixer::ChannelId,juce::AudioProcessor* p) {
            if(auto* vocal=dynamic_cast<armix::builtin_fx::VocalThrowProcessor*>(p))
                vocal->publishMusicGrid(bpm,anchor,epoch,revision);
        });
    }

    // Native Vocal Throw settings, manual cue and auto-trigger readback.
    // Settings and triggers are not accepted until the AUDIO-owned SPSC
    // completion is drained on JUCE's message thread.
    void emitNativeThrowSnapshot(const juce::String& request,
            const juce::String& channelId,const juce::String& epoch,
            const juce::String& revision,const juce::String& instanceId,
            armix::builtin_fx::VocalThrowProcessor* processor) {
        auto reply=std::make_unique<juce::DynamicObject>();
        reply->setProperty("requestId",request);
        reply->setProperty("channelId",channelId);
        reply->setProperty("deviceEpoch",epoch);
        reply->setProperty("graphRevision",revision);
        reply->setProperty("instanceId",instanceId);
        reply->setProperty("schemaVersion",1);
        reply->setProperty("status",processor?"ACCEPTED":"REJECTED");
        if(processor) {
            auto settings=std::make_unique<juce::DynamicObject>();
            const auto s=processor->acceptedSettings();
            settings->setProperty("mode",s.mode);
            settings->setProperty("bpm",s.bpm);
            settings->setProperty("division",s.division);
            settings->setProperty("repeats",s.repeats);
            settings->setProperty("captureMs",s.captureMs);
            settings->setProperty("silenceMs",s.silenceMs);
            settings->setProperty("thresholdDb",s.thresholdDb);
            settings->setProperty("wet",s.wet);
            settings->setProperty("decay",s.decay);
            settings->setProperty("ducking",s.ducking);
            reply->setProperty("settings",juce::var(settings.release()));
            reply->setProperty("sampleRate",processor->acceptedSampleRate());
            // MUSIC tempo belongs to Vocal Throw, not to the unrelated EQ.
            // Fence every displayed source/beat to THIS accepted graph.
            // A retired device may leave a last control-timer snapshot; it
            // must never appear as a locked beat on a newly opened device.
            const bool currentMusicGraph=lastMusicTempo_.active&&
                epoch==juce::String(std::to_string(lastMusicTempo_.epoch))&&
                revision==juce::String(std::to_string(lastMusicTempo_.revision));
            reply->setProperty("musicSourceChannelId",
                currentMusicGraph?
                  juce::String(std::to_string(lastMusicTempo_.source.value)):
                  juce::String("0"));
            reply->setProperty("musicSourceActive",currentMusicGraph);
            reply->setProperty("musicLocked",currentMusicGraph&&
                lastMusicTempo_.tempo.locked);
            reply->setProperty("musicBpm",
                currentMusicGraph&&lastMusicTempo_.tempo.locked?
                    lastMusicTempo_.tempo.bpm:0.f);
            reply->setProperty("musicConfidence",
                currentMusicGraph?lastMusicTempo_.tempo.confidence:0.f);
            reply->setProperty("musicGridAvailable",currentMusicGraph&&
                lastMusicTempo_.tempo.locked&&
                lastMusicTempo_.tempo.phaseLocked);
            reply->setProperty("musicPhaseConfidence",
                currentMusicGraph?lastMusicTempo_.tempo.phaseConfidence:0.f);
            reply->setProperty("musicBeatAnchorFrame",juce::String(
                std::to_string(currentMusicGraph?
                    lastMusicTempo_.tempo.beatAnchorFrame:0)));
            reply->setProperty("musicDroppedFrames",currentMusicGraph?
                static_cast<int>(lastMusicTempo_.dropped):0);

            reply->setProperty("throwCount",static_cast<int>(processor->observedThrowCount()));
            reply->setProperty("tailActive",processor->observedTailActive());
            reply->setProperty("addedLatencySamples",processor->getLatencySamples());
            if(epoch=="0" && instanceId==lastLogicalFxInstance_ && revision==lastLogicalFxRevision_)
                reply->setProperty("appliedRequestId",lastLogicalFxRequest_);
            else if(instanceId==lastNativeThrowInstance_&&
               epoch==lastNativeThrowEpoch_&&revision==lastNativeThrowRevision_)
                reply->setProperty("appliedRequestId",lastNativeThrowRequest_);
        } else reply->setProperty("error","NATIVE_THROW_INSTANCE_OR_REVISION_UNAVAILABLE");
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinThrowSnapshot",juce::var(reply.release()));
    }

    void servicePendingNativeThrow() {
        if(!pendingNativeThrow_.owner)return;
        auto* processor=dynamic_cast<armix::builtin_fx::VocalThrowProcessor*>(
            pendingNativeThrow_.owner->processor.get());
        if(!processor){pendingNativeThrow_={};return;}
        const auto request=pendingNativeThrow_.request;
        const auto wire=request.startsWith("throw-")?
            request.substring(6).toStdString():std::string();
        std::uint64_t requestId=0;
        if(!wire.empty()&&wire.size()<=19&&
           wire.find_first_not_of("0123456789")==std::string::npos) {
            try{requestId=std::stoull(wire);}catch(...){requestId=0;}
        }
        // Old completions can arrive after a rejected timeout. Drain at
        // most the bounded SPSC depth and ONLY ACK our exact request.
        armix::builtin_fx::VocalThrowProcessor::Completion completion{},item{};
        bool matched=false;
        for(int i=0;i<32 && processor->pollCompletion(item);++i) {
            if(requestId && item.requestId==requestId) {
                completion=item;matched=true;break;
            }
        }
        if(!matched &&
           juce::Time::getMillisecondCounter()-pendingNativeThrow_.started<3000u)
            return;
        if(!matched)processor->cancelQueuedRequest(requestId);
        bool applied=matched&&completion.applied&&requestId!=0;
        const auto snap=approvedNativeSnapshot();
        const auto device=snap.getProperty("device",{});
        const auto graph=snap.getProperty("graph",{});
        applied=applied&&pendingNativeThrow_.deviceEpoch==
                    device.getProperty("epoch","").toString()&&
                    pendingNativeThrow_.revision==
                    graph.getProperty("revision","").toString();
        if(applied) {
            lastNativeThrowRequest_=request;
            lastNativeThrowInstance_=pendingNativeThrow_.instanceId;
            lastNativeThrowEpoch_=pendingNativeThrow_.deviceEpoch;
            lastNativeThrowRevision_=pendingNativeThrow_.revision;
        }
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",request);
        ack->setProperty("channelId",pendingNativeThrow_.channelId);
        ack->setProperty("deviceEpoch",pendingNativeThrow_.deviceEpoch);
        ack->setProperty("graphRevision",pendingNativeThrow_.revision);
        ack->setProperty("instanceId",pendingNativeThrow_.instanceId);
        ack->setProperty("status",applied?"APPLIED":"REJECTED");
        ack->setProperty("command",pendingNativeThrow_.trigger?
            "TRIGGER":"SETTINGS");
        ack->setProperty("errorCode",applied?"NONE":!matched?
            "DSP_ACK_TIMEOUT_CANCELLED":
            (pendingNativeThrow_.trigger&&completion.bypassed)?
            "FX_BYPASSED_THROW_DISABLED":
            pendingNativeThrow_.trigger?
            "NO_CAPTURE_OR_STALE_GRAPH":"DSP_EDIT_NOT_APPLIED_OR_STALE");
        pendingNativeThrow_={};
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinThrowAck",juce::var(ack.release()));
    }

    // Never confuse durable logical ChannelId state with a physical device
    // authorization. This message-thread report is status only, not PCM.
    void emitMixerSessionAck(const juce::String& operation,
                             const juce::String& error={}) {
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("operation",operation);
        ack->setProperty("ok",error.isEmpty());
        ack->setProperty("error",error);
        ack->setProperty("state",mixerSessionStatus_);
        ack->setProperty("saved",mixerSessionId_.isNotEmpty());
        if(browser_.loaded)
            browser_.emitEventIfBrowserIsVisible("arMixNativeSessionAck",
                juce::var(ack.release()));
    }

    juce::String resolveMixerSessionCatalog(armix::session::MixerSessionDocument& doc,
                                           bool captureIdentity) {
        for(auto& insert:doc.inserts) {
            if(armix::builtin_fx::findNativeBuiltInFx(insert.catalogId)) {
                if(insert.path.isNotEmpty() || insert.uid!=0)
                    return "Saved built-in FX identity was modified";
                continue;
            }
            const auto* registered=catalog_.find(insert.catalogId);
            if(!registered || registered->key()!=insert.catalogId ||
               (!captureIdentity && (registered->uid!=insert.uid || registered->path!=insert.path)))
                return "Saved external VST3 identity unavailable: "+insert.catalogId;
            if(captureIdentity) {
                insert.path=registered->path;
                insert.uid=registered->uid;
                insert.name=registered->name;
            }
        }
        return {};
    }

    juce::String materializeLogicalFxForBinding(armix::session::MixerSessionDocument& doc,
            std::vector<armix::shows::BinaryState>& states) {
        if(!doc.logicalOnly)return {};
        armix::session::NativeLogicalMixerWorkspace workspace;
        if(!workspace.load(doc,states))return "Saved mixer graph is invalid";
        for(const auto& row:doc.roster.channels()) {
            for(std::size_t slot=0;slot<row.insertCatalogIds.size();++slot) {
                if(workspace.storedFx(row.id,slot))continue;
                armix::session::StoredInsert insert;
                std::unique_ptr<juce::AudioProcessor> processor;
                const auto error=prepareLogicalFx(workspace,row.id,slot,insert,processor);
                if(error.isNotEmpty())return error;
                juce::MemoryBlock bytes;
                processor->getStateInformation(bytes);
                if(!workspace.saveFx(std::move(insert),std::move(bytes)))
                    return "Invalid offline FX state";
            }
        }
        doc=workspace.document();
        states=workspace.states();
        return {};
    }

    juce::String readMixerSessionForRecovery(armix::session::MixerSessionDocument& doc,
            std::vector<armix::shows::BinaryState>& blobs) {
        if(engine_.hasRetainedNativeSession()) {
            const auto error=engine_.captureNativeMixerSession(doc,blobs,true);
            if(error.isNotEmpty())return error;
            doc.id=mixerSessionId_;
            return resolveMixerSessionCatalog(doc,true);
        }
        return armix::session::NativeMixerSessionStore(settingsDirectory()).load(doc,blobs);
    }

    juce::String restoreSavedMixerSession(bool startupOnSavedDevice=false,
            armix::session::MixerDeviceBindingPolicy::Mode bindingMode=
                armix::session::MixerDeviceBindingPolicy::Mode::exact) {
        if(!engine_.allOutputsDisarmed() || mixerSessionWorker_.busy() || logicalVendorEditor_ ||
           nativeShowBusy_ || studioBackupBusy_ || engine_.isGraphSwitchPending())
            return "MUTE output and finish Studio IO before restoring mixer";
        armix::session::NativeMixerSessionStore store(settingsDirectory());
        if(!engine_.hasRetainedNativeSession() && !store.exists()) {
            mixerSessionStatus_="NO_SAVED_SESSION";
            return "No saved dynamic mixer session";
        }
        armix::session::MixerSessionDocument doc;
        std::vector<armix::shows::BinaryState> states;
        const bool retained=engine_.hasRetainedNativeSession();
        auto error=readMixerSessionForRecovery(doc,states);
        bool usedPrevious=false;
        if(error.isNotEmpty()&&!retained&&store.recoverPrevious().isEmpty()) {
            error=store.load(doc,states);
            usedPrevious=error.isEmpty();
        }
        if(error.isNotEmpty()) {
            mixerSessionStatus_=retained?"RETAINED_SESSION_UNAVAILABLE":"DAMAGED_SESSION";
            mixerSessionError_=error;
            diagnostics_.record("DYNAMIC_MIXER_READ_REJECTED",error);
            return error;
        }
        mixerSessionId_=doc.id;
        const auto restoredHotkeys=doc.hotkeys;
        if(usedPrevious)
            (void)logicalMixer_.restoreCheckpoint(doc,states);
        const bool firstBinding=doc.logicalOnly;
        if(firstBinding && !engine_.hasPristineStartupMixer()) {
            (void)hotkeys_.activateSession(mixerSessionId_);
            if(hotkeys_.sessionEntries().empty() && !restoredHotkeys.empty()) {
                const auto issue=hotkeys_.activateSession(mixerSessionId_,&restoredHotkeys);
                if(issue.isNotEmpty())diagnostics_.record("HOTKEY_IMPORT_REJECTED",issue);
            }
            mixerSessionRecoveryPending_=true;
            mixerSessionStatus_="LOGICAL_SESSION_SAVED_UNBOUND";
            return "Saved mixer is offline and awaiting verified device binding";
        }
        error=materializeLogicalFxForBinding(doc,states);
        if(error.isNotEmpty())return error;
        // A disk SHA is NOT permission to instantiate executable code.
        // Every external VST3 must still match today's verified catalog
        // identity, exact UID and registered path. A moved or tampered
        // binary needs an explicit catalog remap, never a blind load.
        error=resolveMixerSessionCatalog(doc,false);
        if(error.isNotEmpty()) {
            mixerSessionStatus_="NEEDS_VST3_REMAP";
            mixerSessionError_=error;
            diagnostics_.record("DYNAMIC_MIXER_VST3_REMAP_REQUIRED",error);
            return error;
        }
        if(bindingMode==MixerBindingMode::exact && startupOnSavedDevice &&
           armix::session::MixerDeviceBindingPolicy::isAsio4All(doc.binding))
            bindingMode=MixerBindingMode::verifiedAsioRestart;
        const bool resume=!firstBinding && startupOnSavedDevice &&
            (bindingMode==MixerBindingMode::exact || bindingMode==MixerBindingMode::verifiedAsioRestart) &&
            !usedPrevious && !retained &&
            doc.resumeMain.value_or(true);
        const auto live=engine_.status().getProperty("dryRoutingLab",{});
        const bool nativeAlreadyActive=static_cast<bool>(
            live.getProperty("nativeChannelIdPcmActive",false));
        // A fresh silent ChannelId starter is now admitted before the saved
        // Show is inspected. Only THAT unmodified bootstrap may be replaced
        // by cold-start restore or by verified Apply after a failed startup. User-authored FX,
        // WIRE, BUS and fader edits must never be overwritten by late scan.
        // A verified PREVIOUS checkpoint is also eligible: the separate
        // resume policy keeps MAIN muted after recovery from corrupt HEAD.
        const bool recoveringAfterDeviceApply=bindingMode!=MixerBindingMode::exact &&
            mixerSessionRecoveryPending_;
        const bool replacePristineStartup=nativeAlreadyActive && !retained &&
            (startupOnSavedDevice || recoveringAfterDeviceApply || firstBinding) &&
            engine_.hasPristineStartupMixer();
        if(nativeAlreadyActive && !replacePristineStartup) {
            mixerSessionStatus_="LIVE_MIXER_UNCHANGED";
            return "Live native mixer already edited; refusing to replace it";
        }
        error=engine_.restoreNativeMixerSession(std::move(doc),states,
            replacePristineStartup,nullptr,bindingMode);
        if(error.isNotEmpty()) {
            mixerSessionStatus_=error.contains("NEEDS_REMAP")
                ?"NEEDS_REMAP":"RESTORE_REJECTED";
            mixerSessionError_=error;
            diagnostics_.record("DYNAMIC_MIXER_RESTORE_REJECTED",error);
            return error;
        }
        mixerSessionRecoveryPending_=false;
        (void)hotkeys_.activateSession(mixerSessionId_);
        if(hotkeys_.sessionEntries().empty() && !restoredHotkeys.empty()) {
            const auto issue=hotkeys_.activateSession(mixerSessionId_,&restoredHotkeys);
            if(issue.isNotEmpty())diagnostics_.record("HOTKEY_IMPORT_REJECTED",issue);
        }
        if(resume)requestAutomaticMain(bindingMode!=MixerBindingMode::verifiedAsioRestart);
        mixerSessionStatus_=engine_.isMonitorEnabled()?"RESTORED_MONITORING":"RESTORED_MUTED";
        mixerSessionError_.clear();
        diagnostics_.record("DYNAMIC_MIXER_RESTORED",mixerSessionStatus_);
        if(usedPrevious)emitNotice("Last verified mixer checkpoint restored with MAIN muted.",true);
        emitApprovedNativeSnapshot();
        return {};
    }

    juce::String saveMixerSession(bool resumeMain, const juce::File& exportFile={}) {
        const bool automatic=exportFile==juce::File{};
        ensureLogicalWorkspace();
        if(mixerSessionWorker_.busy()||nativeShowBusy_||studioBackupBusy_||
           catalog_.busy()||engine_.isGraphSwitchPending())
            return "Mixer is busy. Try again after the current operation.";
        if(logicalMixer_.readOnly())
            return "Saved mixer recovery required before writing a new session";
        const auto liveState=engine_.status();
        const auto live=liveState.getProperty("dryRoutingLab",{});
        const auto observed=approvedNativeSnapshot(liveState);
        const auto session=observed.getProperty("mixerSession",{});
        const bool m4Active=session.getProperty("authority","").toString()=="M4";
        if(!static_cast<bool>(live.getProperty("nativeChannelIdPcmActive",false)) &&
           !m4Active && !engine_.secondaryArmed()) {
            if(engine_.hasRetainedNativeSession())
                return "Retained native mixer still owns the session";
            // Logical edits and Open already commit HEAD before ACK.
            if(automatic)return {};
            auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
            if(!logicalMixer_.exportTo(exportFile,[safe](juce::String error){
                if(safe!=nullptr)safe->emitMixerSessionAck("SAVE",error);
            }))return "Mixer session IO worker unavailable";
            return {};
        }
        if(automatic&&mixerSessionRecoveryPending_)
            return "The retained mixer checkpoint is awaiting a verified device";
        const bool monitoring=engine_.isMonitorEnabled();
        if(engine_.secondaryArmed())return "Stop STREAM before saving the mixer";
        const auto epoch=engine_.dryRoutingLabEpoch();
        if(monitoring)engine_.setMonitorEnabled(false);
        armix::session::MixerSessionDocument doc;
        std::vector<armix::shows::BinaryState> states;
        auto error=engine_.captureNativeMixerSession(doc,states);
        if(monitoring && epoch==engine_.dryRoutingLabEpoch() &&
           engine_.liveOutputArmFault(false).isEmpty())
            engine_.setMonitorEnabled(true);
        if(error.isNotEmpty())return error;
        if(automatic)doc.resumeMain=resumeMain;
        doc.id=mixerSessionId_.isNotEmpty()?mixerSessionId_:
            juce::Uuid().toDashedString().toLowerCase();
        // Shortcut entries are additive .smix data, never callback state.
        if(hotkeys_.activeSession()==doc.id)
            doc.hotkeys=hotkeys_.sessionEntries();
        // Use the verified installed vendor identity; never load using a
        // browser-provided plugin name or relocate an arbitrary VST3 path.
        error=resolveMixerSessionCatalog(doc,true);
        if(error.isNotEmpty())return error;
        doc.savedMs=1; // signature excludes wall time; includes FX state hashes
        const auto canonical=armix::session::Codec::encode(doc);
        armix::session::MixerSessionDocument verified;
        if(!armix::session::Codec::decode(canonical,verified))
            return "Mixer session document validation rejected";
        const auto fingerprint=juce::SHA256(canonical.toRawUTF8(),
            canonical.getNumBytesAsUTF8()).toHexString();
        if(automatic&&fingerprint==lastMixerSessionFingerprint_)
            return {};
        doc.savedMs=juce::Time::currentTimeMillis();
        const auto pendingId=doc.id;
        auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
        if(!mixerSessionWorker_.save(settingsDirectory(),std::move(doc),
                  std::move(states),
                  [safe,pendingId,fingerprint,automatic](juce::String failure){
            if(safe==nullptr)return;
            if(!automatic) {
                safe->emitMixerSessionAck("SAVE",failure);
                return;
            }
            if(failure.isEmpty()) {
                safe->mixerSessionId_=pendingId;
                safe->lastMixerSessionFingerprint_=fingerprint;
                safe->mixerSessionStatus_="SAVED_VERIFIED_MUTED";
                safe->mixerSessionError_.clear();
                safe->diagnostics_.record("DYNAMIC_MIXER_SAVED",
                    "AUTO_CHECKPOINT");
            }else{
                safe->mixerSessionStatus_="SAVE_REJECTED";
                safe->mixerSessionError_=failure;
                safe->diagnostics_.record("DYNAMIC_MIXER_SAVE_REJECTED",failure);
            }
        },exportFile))return "Mixer session IO worker unavailable";
        if(automatic)mixerSessionStatus_="SAVING";
        return {};
    }

    void openMixerFile(const juce::File& file) {
        autoMainEpoch_=0;
        const auto chosenEpoch=engine_.dryRoutingLabEpoch();
        auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
        if(!mixerSessionWorker_.load(file,[safe,chosenEpoch,file](juce::String error,
                armix::session::MixerSessionDocument doc,
                std::vector<armix::shows::BinaryState> states) {
            if(safe==nullptr)return;
            auto& engine=safe->engine_;
            if(error.isEmpty() && (safe->nativeShowBusy_||safe->studioBackupBusy_||
                safe->catalog_.busy()||engine.isGraphSwitchPending()||
                chosenEpoch!=engine.dryRoutingLabEpoch()))
                error="Mixer or audio device changed while opening the file";
            if(error.isEmpty()&&engine.secondaryArmed())
                error="Stop STREAM before opening a mixer";
            const auto liveState=engine.status();
            const auto lab=liveState.getProperty("dryRoutingLab",{});
            const auto session=safe->approvedNativeSnapshot(liveState)
                .getProperty("mixerSession",{});
            const bool physical=static_cast<bool>(lab.getProperty(
                "nativeChannelIdPcmActive",false)) ||
                session.getProperty("authority","").toString()=="M4";
            if(error.isEmpty()&&!physical&&engine.hasRetainedNativeSession())
                error="Retained native mixer still owns the session";
            const auto importedHotkeys=doc.hotkeys;
            const auto importedId=doc.id;
            if(error.isEmpty()&&!physical) {
                // Open the real .smix into the same stable-ID logical
                // document. Never demand audio hardware or instantiate VST3
                // just to inspect and edit a saved mixer.
                if(!safe->logicalMixer_.open(std::move(doc),std::move(states),
                        [safe,file,importedHotkeys,importedId](juce::String openError){
                    if(safe==nullptr)return;
                    if(openError.isEmpty()) {
                        safe->mixerSessionId_=importedId;
                        const auto keyError=safe->hotkeys_.activateSession(importedId,&importedHotkeys);
                        if(keyError.isNotEmpty())safe->emitNotice("Hotkey import: "+keyError,true);
                        safe->mixerSessionStatus_="LOGICAL_SESSION_OPENED_UNBOUND";
                        safe->lastMixerFile_=file;
                    }
                    safe->emitMixerSessionAck("OPEN",openError);
                    if(openError.isEmpty())safe->emitApprovedNativeSnapshot();
                }))safe->emitMixerSessionAck("OPEN","Mixer session IO worker unavailable");
                return;
            }
            // Physical Open may construct vendor DLLs inside the logical FX
            // materialization step. Arm the marker BEFORE any preparation,
            // not merely before the final native graph restore.
            std::unique_ptr<armix::startup::Vst3RestoreCrashGuard> explicitRecovery;
            bool hadPreviousCrashEvidence=false;
            if(error.isEmpty()) {
                explicitRecovery=std::make_unique<armix::startup::Vst3RestoreCrashGuard>(
                    settingsDirectory());
                hadPreviousCrashEvidence=explicitRecovery->interrupted();
                if(!explicitRecovery->armExplicitRetry())
                    error="Cannot protect VST3 Open: settings folder is not writable";
                else
                    safe->startupFxGuard_.reset(); // old timer must not clear new marker
            }
            if(error.isEmpty())error=safe->materializeLogicalFxForBinding(doc,states);
            if(error.isEmpty())error=safe->resolveMixerSessionCatalog(doc,false);
            if(error.isEmpty()) {
                const auto id=doc.id;
                bool sameOutputs=false;
                const bool monitoring=engine.isMonitorEnabled();
                error=engine.restoreNativeMixerSession(std::move(doc),states,true,&sameOutputs);
                if(monitoring&&sameOutputs&&error.isEmpty()&&
                   chosenEpoch==engine.dryRoutingLabEpoch()&&engine.liveOutputArmFault(false).isEmpty())
                    engine.setMonitorEnabled(true);
                if(error.isEmpty()) {
                    safe->closePluginEditor();
                    safe->stopOrbit();
                    safe->mixerSessionId_=id;
                    const auto keyError=safe->hotkeys_.activateSession(id,&importedHotkeys);
                    if(keyError.isNotEmpty())safe->emitNotice("Hotkey import: "+keyError,true);
                    safe->mixerSessionRecoveryPending_=false;
                    safe->lastMixerSessionFingerprint_.clear();
                    safe->mixerSessionStatus_=engine.isMonitorEnabled()?"RESTORED_MONITORING":"RESTORED_MUTED";
                    safe->mixerSessionError_.clear();
                    safe->lastMixerFile_=file;
                    safe->startupFxGuard_=std::move(explicitRecovery);
                    safe->startupFxGuardTick_=juce::Time::getMillisecondCounter();
                    safe->diagnostics_.record("STARTUP_FX_EXPLICIT_RECOVERY_ACCEPTED",
                        "Manual FX graph restored; pending 15s stability verification");
                    safe->emitApprovedNativeSnapshot();
                }
            }
            // A controlled rejection is not an abnormal process crash.
            // Never erase an earlier failure marker if safe recovery existed.
            if(error.isNotEmpty()&&explicitRecovery&&!hadPreviousCrashEvidence)
                (void)explicitRecovery->complete();
            safe->emitMixerSessionAck("OPEN",error);
        }))emitMixerSessionAck("OPEN","Mixer file operation already in progress");
    }

    void chooseMixerFile(bool save) {
        if(logicalVendorEditor_)return emitMixerSessionAck(save?"SAVE":"OPEN",
            "Close the vendor FX editor to save its settings first");
        if(mixerFileDialogOpen_)return;
        const auto operation=save?"SAVE":"OPEN";
        if(mixerSessionWorker_.busy()||nativeShowBusy_||studioBackupBusy_)
            return emitMixerSessionAck(operation,"Mixer file operation already in progress");
        auto start=lastMixerFile_;
        if(start==juce::File{}) {
            start=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
            if(save)start=start.getChildFile("Mixer.smix");
        }
        mixerFileDialogOpen_=true;
        mixerFileChooser_=std::make_unique<juce::FileChooser>(
            save?"Save mixer":"Open mixer",start,"*.smix",true);
        using BrowserFlags=juce::FileBrowserComponent;
        mixerFileChooser_->launchAsync(BrowserFlags::canSelectFiles |
            (save?(BrowserFlags::saveMode|BrowserFlags::warnAboutOverwriting):BrowserFlags::openMode),
            [safe=juce::Component::SafePointer<ConsoleWindow>(this),save,operation]
            (const juce::FileChooser& picker) {
                if(safe==nullptr)return;
                safe->mixerFileDialogOpen_=false;
                auto file=picker.getResult();
                if(file==juce::File{})return;
                if(!save)return safe->openMixerFile(file);
                if(!file.hasFileExtension(".smix"))
                    return safe->emitMixerSessionAck(operation,"Use the .smix file extension");
                safe->lastMixerFile_=file;
                const auto error=safe->saveMixerSession(false,file);
                if(error.isNotEmpty())safe->emitMixerSessionAck(operation,error);
            });
    }

    // One native player, one local setlist, one worker decoder, same JUCE
    // callback. The WebView may only select songs or send transport intents.
    void restorePlayerLibrary() {
        const auto file=settingsDirectory().getChildFile("player-library.json");
        if(!file.existsAsFile())return;
        auto json=juce::JSON::parse(file.loadFileAsString());
        const int version=static_cast<int>(json.getProperty("version",0));
        if(!json.isObject() || (version!=1 && version!=2)) {
            playerError_="Saved setlist could not be read; music files were not changed";
            return;
        }
        const auto tracks=json.getProperty("files",{});
        if(const auto* list=tracks.getArray()) {
            for(const auto& item:*list) {
                if(playerLibrary_.size()>=2000)break;
                if((version==1 && !item.isString()) ||
                   (version==2 && !item.isObject()))continue;
                const auto text=version==1?item.toString():
                    item.getProperty("path","").toString();
                if(text.isEmpty() || text.length()>4096)continue;
                const juce::File entry(text);
                if(!juce::File::isAbsolutePath(text) ||
                   !entry.hasFileExtension("wav;mp3;flac;aif;aiff;ogg") ||
                   std::find(playerLibrary_.begin(),playerLibrary_.end(),entry)
                       !=playerLibrary_.end())
                    continue;
                PlayerTrackSettings controls;
                if(version==2) {
                    controls.key=static_cast<int>(item.getProperty("key",0));
                    controls.tempo=static_cast<int>(item.getProperty("tempo",100));
                    if(controls.key< -6 || controls.key>6 ||
                       controls.tempo<80 || controls.tempo>120)
                        controls={}; // corrupt automation cannot alter audio
                }
                playerLibrary_.push_back(entry);
                playerTrackSettings_.push_back(controls);
            }
        }
        playerAutoNext_=json.getProperty("autoNext",false).isBool() &&
            static_cast<bool>(json.getProperty("autoNext",false));
        if(!playerLibrary_.empty())playerSelected_=0;
    }
    void persistPlayerLibrary() {
        auto document=std::make_unique<juce::DynamicObject>();
        document->setProperty("version",2);
        document->setProperty("autoNext",playerAutoNext_);
        juce::Array<juce::var> files;
        for(std::size_t i=0;i<playerLibrary_.size();++i) {
            auto entry=std::make_unique<juce::DynamicObject>();
            entry->setProperty("path",playerLibrary_[i].getFullPathName());
            entry->setProperty("key",playerTrackSettings_[i].key);
            entry->setProperty("tempo",playerTrackSettings_[i].tempo);
            files.add(juce::var(entry.release()));
        }
        document->setProperty("files",juce::var(files));
        const auto target=settingsDirectory().getChildFile("player-library.json");
        juce::TemporaryFile staging(target);
        if(!staging.getFile().replaceWithText(
                juce::JSON::toString(juce::var(document.release()),false)) ||
           !staging.overwriteTargetFileWithTemporary())
            playerError_="Unable to save setlist; check folder permissions";
    }
    void emitPlayerState(bool includeLibrary=false) {
        if(!browser_.loaded)return;
        auto obj=std::make_unique<juce::DynamicObject>();
        obj->setProperty("schema","arsonmix.player.v1");
        obj->setProperty("transport",engine_.playerStatus());
        obj->setProperty("selected",playerSelected_);
        obj->setProperty("current",playerCurrent_);
        obj->setProperty("autoNext",playerAutoNext_);
        obj->setProperty("libraryBusy",playerFolderBusy_);
        obj->setProperty("error",playerError_);
        obj->setProperty("chooseOutput",
            playerError_.containsIgnoreCase("Choose a verified MAIN output") ||
            playerError_.containsIgnoreCase("native mixer is preparing"));
        if(includeLibrary) {
            juce::Array<juce::var> rows;
            for(std::size_t i=0;i<playerLibrary_.size();++i) {
                auto song=std::make_unique<juce::DynamicObject>();
                song->setProperty("id",static_cast<int>(i));
                song->setProperty("title",
                    playerLibrary_[i].getFileNameWithoutExtension());
                song->setProperty("missing",!playerLibrary_[i].existsAsFile());
                song->setProperty("keySemitones",playerTrackSettings_[i].key);
                song->setProperty("tempoPercent",playerTrackSettings_[i].tempo);
                rows.add(juce::var(song.release()));
            }
            obj->setProperty("tracks",juce::var(rows));
        }
        browser_.emitEventIfBrowserIsVisible("arMixPlayerState",
                                               juce::var(obj.release()));
    }
    void choosePlayerFiles() {
        if(playerFileChooser_)return; // one native dialog only
        playerFileChooser_=std::make_unique<juce::FileChooser>(
            "Add Music",juce::File::getSpecialLocation(
                juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg",true);
        const auto flags=juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems;
        playerFileChooser_->launchAsync(flags,
            [safe=juce::Component::SafePointer<ConsoleWindow>(this)]
            (const juce::FileChooser& picker) {
                if(!safe)return;
                const auto files=picker.getResults();
                std::size_t first=static_cast<std::size_t>(-1);
                for(const auto& file:files) {
                    if(!file.existsAsFile() ||
                       !file.hasFileExtension("wav;mp3;flac;aif;aiff;ogg"))
                        continue;
                    const auto found=std::find(safe->playerLibrary_.begin(),
                        safe->playerLibrary_.end(),file);
                    if(found!=safe->playerLibrary_.end()) {
                        if(first==static_cast<std::size_t>(-1))
                            first=static_cast<std::size_t>(
                                found-safe->playerLibrary_.begin());
                        continue;
                    }
                    if(safe->playerLibrary_.size()>=2000)break;
                    if(first==static_cast<std::size_t>(-1))
                        first=safe->playerLibrary_.size();
                    safe->playerLibrary_.push_back(file);
                    safe->playerTrackSettings_.push_back({});
                }
                if(safe->playerSelected_<0 && first!=static_cast<std::size_t>(-1))
                    safe->playerSelected_=static_cast<int>(first);
                safe->playerError_.clear();
                safe->persistPlayerLibrary();
                safe->emitPlayerState(true);
                // Do not destroy FileChooser inside its own callback.
                juce::MessageManager::callAsync([safe] {
                    if(safe)safe->playerFileChooser_.reset();
                });
            });
    }
    void choosePlayerFolder() {
        if(playerFileChooser_ || playerFolderBusy_)return;
        playerFileChooser_=std::make_unique<juce::FileChooser>(
            "Add Music Folder",
            juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*",true);
        playerFileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [safe=juce::Component::SafePointer<ConsoleWindow>(this)]
            (const juce::FileChooser& picker) {
                if(!safe)return;
                const auto folder=picker.getResult();
                // FileChooser must not be destroyed inside its callback.
                juce::MessageManager::callAsync([safe] {
                    if(safe)safe->playerFileChooser_.reset();
                });
                if(!folder.isDirectory())return;
                safe->playerFolderBusy_=true;
                safe->playerError_.clear();
                safe->emitPlayerState();
                // Only filesystem enumeration, bounded in scope and output,
                // is performed on the worker. Never block the JUCE message
                // loop or ASIO callback while importing a large USB folder.
                // Copy immutable IDs/paths on GUI thread; expensive USB
                // existence checks and relink planning belong on the worker.
                const auto initialLibrary=safe->playerLibrary_;
                const int protectedCurrent=safe->playerCurrent_;
                std::thread([safe,folder,initialLibrary,protectedCurrent] {
                    auto scan=armix::player::PlayerLibraryPolicy::scanFolder(folder);
                    auto relinks=armix::player::PlayerLibraryPolicy::planMissingRelinks(
                        initialLibrary,scan,protectedCurrent);
                    juce::MessageManager::callAsync(
                        [safe,scan=std::move(scan),
                         relinks=std::move(relinks),initialLibrary]() mutable {
                            if(!safe)return;
                            safe->playerFolderBusy_=false;
                            int added=0,relinked=0;
                            // Update only the exact rows the worker saw.
                            // Edits made during the scan must remain intact.
                            for(const auto& change:relinks) {
                                if(change.index>=initialLibrary.size() ||
                                   change.index>=safe->playerLibrary_.size() ||
                                   static_cast<int>(change.index)==safe->playerCurrent_ ||
                                   safe->playerLibrary_[change.index]!=
                                       initialLibrary[change.index] ||
                                   std::find(safe->playerLibrary_.begin(),
                                       safe->playerLibrary_.end(),
                                       change.replacement)!=safe->playerLibrary_.end())
                                    continue;
                                safe->playerLibrary_[change.index]=change.replacement;
                                ++relinked; // keep stable IDs + KEY/TEMPO
                            }
                            int first=-1;
                            for(const auto& file:scan.files) {
                                const auto found=std::find(
                                    safe->playerLibrary_.begin(),
                                    safe->playerLibrary_.end(),file);
                                if(found!=safe->playerLibrary_.end())continue;
                                if(safe->playerLibrary_.size()>=
                                   armix::player::PlayerLibraryPolicy::maxSongs)break;
                                if(first<0)
                                    first=static_cast<int>(safe->playerLibrary_.size());
                                safe->playerLibrary_.push_back(file);
                                safe->playerTrackSettings_.push_back({});
                                ++added;
                            }
                            if(safe->playerSelected_<0 && first>=0)
                                safe->playerSelected_=first;
                            safe->playerError_=scan.truncated?
                                "Folder scan reached its safe size limit; only some songs were found":
                                added==0 && relinked==0?
                                    "No new supported songs or safe missing-file matches found":
                                    juce::String{};
                            safe->persistPlayerLibrary();
                            safe->emitPlayerState(true);
                        });
                }).detach();
            });
    }

    void choosePlayerRelink() {
        if(playerFileChooser_ || playerFolderBusy_)return;
        const int index=playerSelected_;
        if(index<0 || index>=static_cast<int>(playerLibrary_.size()))return;
        const auto old=playerLibrary_[static_cast<std::size_t>(index)];
        if(old.existsAsFile())return; // only resolve genuinely missing music
        playerFileChooser_=std::make_unique<juce::FileChooser>(
            "Locate Missing Music — "+old.getFileName(),
            old.getParentDirectory().isDirectory()?
                old.getParentDirectory():
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
            "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg",true);
        playerFileChooser_->launchAsync(
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectFiles,
            [safe=juce::Component::SafePointer<ConsoleWindow>(this),index,old]
            (const juce::FileChooser& picker) {
                if(!safe)return;
                const auto chosen=picker.getResult();
                juce::MessageManager::callAsync([safe] {
                    if(safe)safe->playerFileChooser_.reset();
                });
                if(chosen==juce::File{})return;
                if(index>=static_cast<int>(safe->playerLibrary_.size()) ||
                   safe->playerLibrary_[static_cast<std::size_t>(index)]!=old) {
                    safe->playerError_="Setlist changed; locate the song again";
                } else if(!armix::player::PlayerLibraryPolicy::sameSongName(
                              old,chosen)) {
                    safe->playerError_="Choose the same song filename to avoid relinking the wrong music";
                } else if(std::find(safe->playerLibrary_.begin(),
                                   safe->playerLibrary_.end(),chosen)!=
                          safe->playerLibrary_.end()) {
                    safe->playerError_="This music file is already in your setlist";
                } else {
                    // Preserve same logical song ID, order and per-song
                    // KEY/TEMPO. Never start a song or switch the output
                    // as a side effect of locating a file.
                    if(safe->playerCurrent_==index) {
                        safe->engine_.playerStop();
                        safe->playerCurrent_=-1;
                    }
                    safe->playerLibrary_[static_cast<std::size_t>(index)]=chosen;
                    safe->playerOutputPendingIndex_=-1;
                    safe->playerError_.clear();
                    safe->persistPlayerLibrary();
                }
                safe->emitPlayerState(true);
            });
    }

    void playTransportMusic() {
        const auto phase=engine_.playerStatus().getProperty(
            "state","EMPTY").toString().toStdString();
        // Selecting NEXT must not replace a PAUSED CURRENT. Play resumes
        // the actual deck; double-click / Next explicitly change song.
        if(armix::player::SetlistPolicy::resumeCurrentOnPlay(
                phase,playerCurrent_)) {
            playerError_=engine_.playerPlay();
            emitPlayerState();
            return;
        }
        playSelectedMusic();
    }
    void playSelectedMusic() {
        if(playerLibrary_.empty()) {
            playerError_="Add Music to begin";emitPlayerState();return;
        }
        if(playerSelected_<0)playerSelected_=0;
        if(playerSelected_>=static_cast<int>(playerLibrary_.size()))return;
        const auto phase=engine_.playerStatus().getProperty(
            "state","EMPTY").toString();
        if(playerCurrent_==playerSelected_ && phase!="ERROR" &&
           phase!="EMPTY") {
            playerError_=engine_.playerPlay();
        } else {
            // Song-level KEY/TEMPO is installed before decoder generation
            // starts. Never transpose microphone input or alter ASIO clock.
            const auto controls=playerTrackSettings_[
                static_cast<std::size_t>(playerSelected_)];
            if(!engine_.setPlayerPitchTempo(controls.key,controls.tempo)) {
                playerError_="Invalid KEY or TEMPO settings";
                emitPlayerState();return;
            }
            const auto error=engine_.loadPlayerMusic(
                playerLibrary_[static_cast<std::size_t>(playerSelected_)]);
            playerError_=error;
            if(error.isEmpty()) {
                playerCurrent_=playerSelected_;
                // A newly native-owned MUSIC ChannelId is visible in the
                // authoritative mixer immediately after publisher ACK.
                // The browser never invents a GUI-only strip or meter.
                emitApprovedNativeSnapshot();
            }
        }
        emitPlayerState(true);
    }
    void playNextMusic() {
        const int next=armix::player::SetlistPolicy::nextTrack(
            playerCurrent_,playerSelected_,
            static_cast<int>(playerLibrary_.size()));
        if(next<0) {
            engine_.playerStop();emitPlayerState();return;
        }
        playerSelected_=next;playSelectedMusic();
    }

    void onAction (const juce::var& args)
    {
        auto action = args.getProperty ("action", {}).toString();
        if(deviceApply_.busy() && action!="audioDeviceApply")return;
        const auto value = args.getProperty ("value", {});
        const int slot = static_cast<int> (args.getProperty ("slot", -1));
        if(action=="playerChooseOutput") {
            if(playerSelected_<0 ||
               playerSelected_>=static_cast<int>(playerLibrary_.size()) ||
               !(playerError_.containsIgnoreCase("Choose a verified MAIN output") ||
                 playerError_.containsIgnoreCase("native mixer is preparing")))
                return emitPlayerState();
            // Reuse EXACTLY the existing native AUDIO SETUP dispatcher, with
            // all of its independent-stream/restore/feedback protections.
            // This is an explicit user-selected endpoint, never a fallback.
            playerOutputPendingIndex_=playerSelected_;
            action="audioSetup";
        }
        if(action=="playerSubscribe")return emitPlayerState(true);
        if(action=="playerAdd")return choosePlayerFiles();
        if(action=="playerAddFolder")return choosePlayerFolder();
        if(action=="playerRelink")return choosePlayerRelink();
        if(action=="playerKeyStep" || action=="playerTempoStep" ||
           action=="playerPitchTempoReset") {
            const auto target=armix::player::SetlistPolicy::pitchTempoTarget(
                playerCurrent_,playerSelected_,
                static_cast<int>(playerTrackSettings_.size()));
            const int i=target.index;
            if(i<0) {
                playerError_="Add Music to set a song's KEY or TEMPO";
                emitPlayerState();return;
            }
            const int direction=static_cast<int>(value);
            auto next=playerTrackSettings_[static_cast<std::size_t>(i)];
            if(action=="playerPitchTempoReset")next={};
            else if((direction==1 || direction==-1) &&
                    action=="playerKeyStep")
                next.key=std::clamp(next.key+direction,-6,6);
            else if(direction==1 || direction==-1)
                next.tempo=std::clamp(next.tempo+direction*5,80,120);
            else return; // invalid native control intent cannot mutate DSP
            // A performer must be able to tune NEXT while CURRENT is
            // playing without changing CURRENT audio. Only send live DSP
            // commands for the currently loaded song.
            if(!target.updateLiveDeck ||
               engine_.setPlayerPitchTempo(next.key,next.tempo)) {
                playerTrackSettings_[static_cast<std::size_t>(i)]=next;
                playerError_.clear();
                persistPlayerLibrary();
                emitPlayerState(true);
            }
            return;
        }
        if(action=="playerPlay")return playTransportMusic();
        if(action=="playerPause") {
            engine_.playerPause();emitPlayerState();return;
        }
        if(action=="playerStop") {
            playerOutputPendingIndex_=-1;
            engine_.playerStop();emitPlayerState();return;
        }
        if(action=="playerNext")return playNextMusic();
        if(action=="playerAutoNext") {
            playerAutoNext_=value.isBool() && static_cast<bool>(value);
            persistPlayerLibrary();emitPlayerState(true);return;
        }
        if(action=="playerSelect" || action=="playerPlayNow") {
            const int i=static_cast<int>(value);
            if(i>=0 && i<static_cast<int>(playerLibrary_.size())) {
                if(i!=playerSelected_)playerOutputPendingIndex_=-1;
                playerSelected_=i;
                if(action=="playerPlayNow")playSelectedMusic();
                else emitPlayerState(true);
            }
            return;
        }
        // User may adjust live levels, but cannot alter the persisted VST
        // library/preset while a background snapshot is being copied.
        if ((studioBackupBusy_ || nativeShowBusy_) && (action.startsWith ("preset") ||
            action.startsWith ("import") || action.startsWith ("fx") ||
            action == "addCatalog" || action == "addScanFolder" ||
            action == "scanVst3" || action == "routingDryApply" ||
            action == "routingFxBypass" ||
            action == "routingNativeFxRackBypass" ||
            action == "routingNativeFxInsertBypass" ||
            action == "routingFxRackBypass" ||
            action == "nativeEqSetBand" || action == "nativeCompSet" ||
            action == "nativeThrowSet" || action == "nativeThrowTrigger" ||
            action == "nativeThrowTempoSource"))
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
            if(action=="routingFxRackBypass" && browser_.loaded){
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("requestId",args.getProperty("requestId",{}));
                ack->setProperty("channel",args.getProperty("channel",-1));
                ack->setProperty("ok",false);
                ack->setProperty("error",reason);
                browser_.emitEventIfBrowserIsVisible(
                    "arMixRoutingFxRackAck",juce::var(ack.release()));
            }
            if((action=="routingNativeFxRackBypass" ||
                action=="routingNativeFxInsertBypass") && browser_.loaded) {
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("requestId",args.getProperty("requestId",{}));
                ack->setProperty("ok",false);
                ack->setProperty("error",reason);
                browser_.emitEventIfBrowserIsVisible(
                    "arMixNativeFxPowerAck",juce::var(ack.release()));
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
        // F0: independently acknowledged existing M4 PCM controls during
        // G0, before ChannelId admission. Do not send GUI demo parameters
        // or mutate a second DSP. One control-thread atomic update only.

        // One control-thread portable preset service for ALL built-in FX.
        // Existing Harmonice editor retains the legacy action/result names.
        // Read/list/rename/delete are disk-only; file data NEVER bypasses
        // each DSP's existing typed SPSC -> ACK -> snapshot control path.
        if(action=="nativeHarmonyUserPreset"||
           action=="nativeBuiltinFxUserPreset") {
            const bool harmonyLegacy=action=="nativeHarmonyUserPreset";
            const auto fxRequest=armix::builtin_fx::NativeFxControlRequest::parse(
                args,LiveDryRoutingLab::maxFxPerLane);
            const auto& request=fxRequest.request;
            const auto operation=args.getProperty("operation",{}).toString();
            const auto& rawChannel=fxRequest.rawChannel;
            const auto& rawEpoch=fxRequest.rawEpoch;
            const auto& rawRevision=fxRequest.rawRevision;
            const auto instance=fxRequest.rawInstance.toString();
            const auto channel=fxRequest.channel,epoch=fxRequest.epoch,
                       revision=fxRequest.revision;
            const int slot=fxRequest.slot;
            const juce::var requestedCatalog=args.getProperty("catalogId",{});
            const juce::String catalog=harmonyLegacy?
                armix::builtin_fx::ChannelHarmonyProcessor::catalogIdentity:
                requestedCatalog.toString();
            const bool catalogValid=harmonyLegacy||
                (requestedCatalog.isString()&&
                 armix::storage::BuiltInFxUserPresetStore::effectFolder(catalog)
                    .isNotEmpty());
            const bool identity=fxRequest.valid&&!fxRequest.unbound&&epoch!=0&&
                catalogValid&&request.startsWith(
                    harmonyLegacy?"hpres-":"fxpres-");
            auto lease=identity?engine_.leaseNativeVst3Editor(
                epoch,revision,armix::mixer::ChannelId{channel},
                static_cast<std::size_t>(slot),instance):
                armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* processor=lease?lease->processor.get():nullptr;
            const bool exactReady=armix::storage::BuiltInFxAcceptedSnapshot
                ::matchesReady(processor,catalog);
            auto result=std::make_unique<juce::DynamicObject>();
            result->setProperty("requestId",request);
            result->setProperty("operation",operation);
            result->setProperty("catalogId",catalog);
            result->setProperty("channelId",rawChannel);
            result->setProperty("deviceEpoch",rawEpoch);
            result->setProperty("graphRevision",rawRevision);
            result->setProperty("instanceId",instance);
            const auto presetId=args.getProperty("presetId","").toString();
            juce::String error;
            if(!identity||!exactReady||nativeShowBusy_||studioBackupBusy_||
               engine_.independentClockActive())
                error="STALE_OR_UNAVAILABLE_BUILTIN_FX_INSTANCE";
            else if(operation=="list") {
                juce::var items(juce::Array<juce::var>{});
                for(const auto& item:builtinFxUserPresets_.enumerate(catalog)) {
                    auto data=std::make_unique<juce::DynamicObject>();
                    data->setProperty("id",item.id);
                    data->setProperty("name",item.name);
                    data->setProperty("category",item.category);
                    items.append(juce::var(data.release()));
                }
                result->setProperty("items",items);
            }else if(operation=="read") {
                armix::storage::BuiltInFxUserPreset entry;
                error=builtinFxUserPresets_.read(catalog,presetId,entry);
                if(error.isEmpty()){
                    result->setProperty("presetId",entry.id);
                    result->setProperty("name",entry.name);
                    result->setProperty("settings",entry.settings);
                }
            }else if(operation=="saveAs"||operation=="save") {
                // All four native FX publish a single producer and keep their
                // accepted_ state on CONTROL. Never capture an unapplied edit.
                const bool busy=
                    (catalog==armix::builtin_fx::ChannelEqProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeEq_.owner))||
                    (catalog==armix::builtin_fx::ChannelCompressorProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeComp_.owner))||
                    (catalog==armix::builtin_fx::ChannelHarmonyProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeHarmony_.owner))||
                    (catalog==armix::builtin_fx::VocalThrowProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeThrow_.owner));
                auto* realtime=dynamic_cast<armix::mixer::RealtimeNativeFxControl*>(processor);
                if(busy||!realtime||!realtime->stateSnapshotQuiescent())
                    error="BUILTIN_FX_AUDIO_EDIT_PENDING";
                else {
                    const auto portable=armix::storage::BuiltInFxAcceptedSnapshot
                        ::acceptedSettings(processor,catalog);
                    if(!portable.isObject())
                        error="BUILTIN_FX_ACCEPTED_STATE_INVALID";
                    else {
                        armix::storage::BuiltInFxUserPreset entry;
                        entry.catalogId=catalog;
                        entry.category="My Presets";
                        entry.settings=portable;
                        if(operation=="saveAs"){
                            entry.name=args.getProperty("name","").toString();
                            error=builtinFxUserPresets_.saveNew(entry);
                        }else{
                            error=builtinFxUserPresets_.read(catalog,presetId,entry);
                            if(error.isEmpty()){
                                // Preserve exact existing user ID/name/category.
                                entry.settings=portable;
                                error=builtinFxUserPresets_.overwrite(entry);
                            }
                        }
                        if(error.isEmpty()){
                            result->setProperty("presetId",entry.id);
                            result->setProperty("name",entry.name);
                        }
                    }
                }
            }else if(operation=="rename") {
                error=builtinFxUserPresets_.rename(catalog,presetId,
                          args.getProperty("name","").toString());
                if(error.isEmpty()) {
                    result->setProperty("presetId",presetId);
                    result->setProperty("name",args.getProperty("name","").toString());
                }
            }else if(operation=="delete") {
                error=builtinFxUserPresets_.remove(catalog,presetId);
                if(error.isEmpty())result->setProperty("presetId",presetId);
            }else error="UNSUPPORTED_BUILTIN_FX_PRESET_ACTION";
            result->setProperty("status",error.isEmpty()?"OK":"REJECTED");
            if(error.isNotEmpty())result->setProperty("errorCode",error);
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                harmonyLegacy?"arMixBuiltinHarmonyPresetResult":
                              "arMixBuiltinFxPresetResult",
                juce::var(result.release()));
            return;
        }

        // Exact native instance lease; queued != audio-applied.
        if(action=="nativeHarmonySnapshot"||action=="nativeHarmonySetSettings") {
            const auto fxRequest=armix::builtin_fx::NativeFxControlRequest::parse(
                args,LiveDryRoutingLab::maxFxPerLane);
            const auto& rawChannel=fxRequest.rawChannel;
            const auto& rawEpoch=fxRequest.rawEpoch;
            const auto& rawRevision=fxRequest.rawRevision;
            const auto& rawInstance=fxRequest.rawInstance;
            const auto& request=fxRequest.request;
            const int slot=fxRequest.slot;
            const auto ch=fxRequest.channel,ep=fxRequest.epoch,rev=fxRequest.revision;
            const bool valid=fxRequest.valid,unbound=fxRequest.unbound;
            armix::session::StoredInsert offlineInsert;
            auto offline=unbound?openLogicalBuiltinFx(args,offlineInsert,
                armix::builtin_fx::ChannelHarmonyProcessor::catalogIdentity):nullptr;
            auto owner=valid&&!unbound?engine_.leaseNativeVst3Editor(ep,rev,
                 armix::mixer::ChannelId{ch},static_cast<std::size_t>(slot),
                 rawInstance.toString()):
                 armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* processor=dynamic_cast<armix::builtin_fx::ChannelHarmonyProcessor*>(
                unbound?offline.get():owner?owner->processor.get():nullptr);
            if(action=="nativeHarmonySnapshot"){
                emitNativeHarmonySnapshot(request,rawChannel.toString(),
                    rawEpoch.toString(),rawRevision.toString(),
                    rawInstance.toString(),processor);
                return;
            }
            const auto v=args.getProperty("settings",{});
            const auto k=v.getProperty("key",{}),sc=v.getProperty("scale",{});
            const auto voices=v.getProperty("voices",{}),mode=v.getProperty("voiceMode",{});
            const auto tune=v.getProperty("correction",{});
            auto num=[](const juce::var& value,float& output) {
                if(!value.isDouble()&&!value.isInt())return false;
                const auto x=static_cast<double>(value);
                if(!std::isfinite(x))return false;
                output=static_cast<float>(x);return std::isfinite(output);
            };
            armix::harmony::Settings settings{};
            const bool typed=k.isInt()&&sc.isInt()&&voices.isInt()&&mode.isInt()&&tune.isBool()&&
                num(v.getProperty("wet",{}),settings.wet)&&
                num(v.getProperty("space",{}),settings.space)&&
                num(v.getProperty("color",{}),settings.color)&&
                num(v.getProperty("gainDb",{}),settings.gainDb);
            settings.key=static_cast<int>(k);settings.scale=static_cast<int>(sc);
            settings.voices=static_cast<int>(voices);
            settings.voiceMode=static_cast<int>(mode);
            settings.correction=static_cast<bool>(tune);
            // Exact ChannelId is never parsed from a JS number. Validate
            // against the currently admitted native graph, not a UI track
            // index or a remembered localStorage name.
            const auto modeValue=v.getProperty("guideMode",{});
            const auto sourceValue=v.getProperty("guideChannelId",{});
            bool guideValid=modeValue.isInt()&&sourceValue.isString();
            if(guideValid){
                settings.guideMode=static_cast<int>(modeValue);
                const auto sourceText=sourceValue.toString();
                if(settings.guideMode==0)guideValid=sourceText=="0";
                else if(settings.guideMode==1){
                    std::uint64_t source=0;
                    guideValid=armix::console::approvedNativeId(sourceValue,source)&&
                        source!=ch && source<=static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max());
                    if(guideValid){
                        const auto live=approvedNativeSnapshot();
                        const auto graph=unbound?live.getProperty("logicalMixer",{}).getProperty("graph",{}):
                            live.getProperty("graph",{});
                        const auto channels=graph.getProperty("channels",{});
                        auto* list=channels.getArray();
                        bool found=false;
                        if(list)for(const auto& item:*list){
                            const auto role=item.getProperty("role","").toString();
                            if(item.getProperty("id","").toString()==sourceText &&
                               (role=="INPUT"||role=="GROUP")){found=true;break;}
                        }
                        guideValid=found;
                    }
                    if(guideValid)settings.guideChannelId=source;
                }else guideValid=false;
            }
            bool queued=false;
            if(typed&&guideValid&&processor&&!pendingNativeHarmony_.owner&&!nativeShowBusy_&&
               !studioBackupBusy_&&!engine_.independentClockActive()&&
               armix::harmony::valid(settings)&&request.startsWith("harm-")) {
            const auto numeric=fxRequest.sequence("harm-");
                queued=numeric>0&&processor->queueAccepted(settings,numeric);
                if(queued && !unbound) {
                    pendingNativeHarmony_.owner=owner;
                    pendingNativeHarmony_.request=request;
                    pendingNativeHarmony_.channelId=rawChannel.toString();
                    pendingNativeHarmony_.deviceEpoch=rawEpoch.toString();
                    pendingNativeHarmony_.revision=rawRevision.toString();
                    pendingNativeHarmony_.instanceId=rawInstance.toString();
                    pendingNativeHarmony_.started=juce::Time::getMillisecondCounter();
                }
            }
            if(unbound)
                return completeLogicalFxEdit(args,std::move(offlineInsert),processor,
                    queued,"arMixBuiltinHarmonyAck");
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("channelId",rawChannel);
            ack->setProperty("instanceId",rawInstance);
            ack->setProperty("status",queued?"QUEUED":"REJECTED");
            ack->setProperty("errorCode",queued?"NONE":
                pendingNativeHarmony_.owner?"HARMONY_CONTROL_BUSY":"INVALID_OR_STALE_NATIVE_HARMONY");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixBuiltinHarmonyQueue",juce::var(ack.release()));
            return;
        }

        // Real built-in rack node, not a GUI-only pre-VST channel EQ.
        // All identity fields are validated by the accepted native lease.
        if(action=="nativeEqSnapshot"||action=="nativeEqSetBand") {
            const auto fxRequest=armix::builtin_fx::NativeFxControlRequest::parse(
                args,LiveDryRoutingLab::maxFxPerLane);
            const auto& rawChannel=fxRequest.rawChannel;
            const auto& rawEpoch=fxRequest.rawEpoch;
            const auto& rawRevision=fxRequest.rawRevision;
            const auto& rawInstance=fxRequest.rawInstance;
            const auto& request=fxRequest.request;
            const int eqSlot=fxRequest.slot;
            const auto ch=fxRequest.channel,ep=fxRequest.epoch,rev=fxRequest.revision;
            const bool valid=fxRequest.valid,unbound=fxRequest.unbound;
            armix::session::StoredInsert offlineInsert;
            auto offline=unbound?openLogicalBuiltinFx(args,offlineInsert,
                armix::builtin_fx::ChannelEqProcessor::catalogIdentity):nullptr;
            auto owner=valid&&!unbound?engine_.leaseNativeVst3Editor(ep,rev,
                armix::mixer::ChannelId{ch},static_cast<std::size_t>(eqSlot),
                rawInstance.toString()):
                armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* processor=dynamic_cast<armix::builtin_fx::ChannelEqProcessor*>(
                unbound?offline.get():owner?owner->processor.get():nullptr);
            if(action=="nativeEqSnapshot") {
                emitNativeEqSnapshot(request,rawChannel.toString(),
                    rawEpoch.toString(),rawRevision.toString(),
                    eqSlot,rawInstance.toString(),processor);
                return;
            }
            const auto bandData=args.getProperty("band",{});
            const int index=static_cast<int>(args.getProperty("bandIndex",-1));
            const auto enabled=bandData.getProperty("enabled",{});
            const auto frequency=bandData.getProperty("frequencyHz",{});
            const auto gain=bandData.getProperty("gainDb",{});
            const auto q=bandData.getProperty("q",{});
            const auto slope=bandData.getProperty("slope",{});
            double f=0.,g=0.,quality=0.;
            auto validNumber=[](const juce::var& v,double& d) {
                if(!v.isDouble()&&!v.isInt())return false;
                d=static_cast<double>(v);return std::isfinite(d);
            };
            bool queued=false;
            if(processor&&!pendingNativeEq_.owner&&!nativeShowBusy_&&!studioBackupBusy_ &&
               !engine_.independentClockActive() && index>=0&&index<8 &&
               enabled.isBool()&&slope.isInt() &&
               (static_cast<int>(slope)==12||static_cast<int>(slope)==24) &&
               validNumber(frequency,f)&&validNumber(gain,g)&&
               validNumber(q,quality) && request.startsWith("eq-")) {
                armix::builtin_fx::Band b;
                b.enabled=static_cast<bool>(enabled);
                b.frequencyHz=static_cast<float>(f);
                b.gainDb=static_cast<float>(g);
                b.q=static_cast<float>(quality);
                b.slope=static_cast<int>(slope)==24?
                    armix::builtin_fx::Slope::db24:armix::builtin_fx::Slope::db12;
            const auto numeric=fxRequest.sequence("eq-");
                queued=numeric>0&&processor->queueBand(
                    static_cast<std::size_t>(index),b,numeric);
                if(queued && !unbound) {
                    pendingNativeEq_.owner=owner;
                    pendingNativeEq_.request=request;
                    pendingNativeEq_.channelId=rawChannel.toString();
                    pendingNativeEq_.deviceEpoch=rawEpoch.toString();
                    pendingNativeEq_.revision=rawRevision.toString();
                    pendingNativeEq_.instanceId=rawInstance.toString();
                    pendingNativeEq_.slot=eqSlot;
                    pendingNativeEq_.started=juce::Time::getMillisecondCounter();
                }
            }
            if(unbound)
                return completeLogicalFxEdit(args,std::move(offlineInsert),processor,
                    queued,"arMixBuiltinEqAck");
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("channelId",rawChannel);
            ack->setProperty("instanceId",rawInstance);
            ack->setProperty("status",queued?"QUEUED":"REJECTED");
            ack->setProperty("errorCode",queued?"NONE":
                pendingNativeEq_.owner?"EQ_CONTROL_BUSY":"INVALID_OR_STALE_NATIVE_EQ");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixBuiltinEqQueue",juce::var(ack.release()));
            return;
        }


        // Each accepted compressor edit stays on this ChannelId's ordered
        // native insert. No GUI mutation until callback ACK + graph readback.
        if(action=="nativeCompSnapshot"||action=="nativeCompSet") {
            const auto fxRequest=armix::builtin_fx::NativeFxControlRequest::parse(
                args,LiveDryRoutingLab::maxFxPerLane);
            const auto& rawChannel=fxRequest.rawChannel;
            const auto& rawEpoch=fxRequest.rawEpoch;
            const auto& rawRevision=fxRequest.rawRevision;
            const auto& rawInstance=fxRequest.rawInstance;
            const auto& request=fxRequest.request;
            const int compSlot=fxRequest.slot;
            const auto ch=fxRequest.channel,ep=fxRequest.epoch,rev=fxRequest.revision;
            const bool valid=fxRequest.valid,unbound=fxRequest.unbound;
            armix::session::StoredInsert offlineInsert;
            auto offline=unbound?openLogicalBuiltinFx(args,offlineInsert,
                armix::builtin_fx::ChannelCompressorProcessor::catalogIdentity):nullptr;
            auto owner=valid&&!unbound?engine_.leaseNativeVst3Editor(ep,rev,
                armix::mixer::ChannelId{ch},static_cast<std::size_t>(compSlot),
                rawInstance.toString()):
                armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* processor=dynamic_cast<armix::builtin_fx::ChannelCompressorProcessor*>(
                unbound?offline.get():owner?owner->processor.get():nullptr);
            if(action=="nativeCompSnapshot") {
                emitNativeCompSnapshot(request,rawChannel.toString(),
                    rawEpoch.toString(),rawRevision.toString(),
                    rawInstance.toString(),processor);
                return;
            }
            const auto values=args.getProperty("settings",{});
            armix::builtin_fx::CompressorSettings settings{};
            auto number=[&values](const char* key,float& out) {
                const auto v=values.getProperty(key,{});
                if(!v.isDouble()&&!v.isInt())return false;
                const double n=static_cast<double>(v);
                if(!std::isfinite(n))return false;
                out=static_cast<float>(n);
                return std::isfinite(out);
            };
            const auto numeric=fxRequest.sequence("comp-");
            const bool parameters=values.isObject()&&
                number("thresholdDb",settings.thresholdDb)&&
                number("ratio",settings.ratio)&&
                number("attackMs",settings.attackMs)&&
                number("releaseMs",settings.releaseMs)&&
                number("kneeDb",settings.kneeDb)&&
                number("makeupDb",settings.makeupDb)&&
                number("mix",settings.mix);
            bool queued=false;
            if(processor&&numeric>0&&parameters&&
               !pendingNativeComp_.owner&&!nativeShowBusy_&&!studioBackupBusy_&&
               !engine_.independentClockActive()) {
                queued=processor->queueSettings(settings,numeric);
                if(queued && !unbound) {
                    pendingNativeComp_.owner=owner;
                    pendingNativeComp_.request=request;
                    pendingNativeComp_.channelId=rawChannel.toString();
                    pendingNativeComp_.deviceEpoch=rawEpoch.toString();
                    pendingNativeComp_.revision=rawRevision.toString();
                    pendingNativeComp_.instanceId=rawInstance.toString();
                    pendingNativeComp_.started=juce::Time::getMillisecondCounter();
                }
            }
            if(unbound)
                return completeLogicalFxEdit(args,std::move(offlineInsert),processor,
                    queued,"arMixBuiltinCompAck");
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("status",queued?"QUEUED":"REJECTED");
            ack->setProperty("errorCode",queued?"NONE":
                pendingNativeComp_.owner?"COMP_CONTROL_BUSY":"INVALID_OR_STALE_COMP");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixBuiltinCompQueue",juce::var(ack.release()));
            return;
        }

        // ArSon Vocal Throw: all UI commands reference one exact installed
        // ChannelId + device epoch + graph revision + insert instance.
        if(action=="nativeThrowSnapshot"||action=="nativeThrowSet"||
           action=="nativeThrowTrigger"||action=="nativeThrowTempoSource") {
            const auto fxRequest=armix::builtin_fx::NativeFxControlRequest::parse(
                args,LiveDryRoutingLab::maxFxPerLane);
            const auto& rawChannel=fxRequest.rawChannel;
            const auto& rawEpoch=fxRequest.rawEpoch;
            const auto& rawRevision=fxRequest.rawRevision;
            const auto& rawInstance=fxRequest.rawInstance;
            const auto& request=fxRequest.request;
            const int throwSlot=fxRequest.slot;
            const auto ch=fxRequest.channel,ep=fxRequest.epoch,rev=fxRequest.revision;
            const bool valid=fxRequest.valid,unbound=fxRequest.unbound;
            armix::session::StoredInsert offlineInsert;
            auto offline=unbound?openLogicalBuiltinFx(args,offlineInsert,
                armix::builtin_fx::VocalThrowProcessor::catalogIdentity):nullptr;
            auto owner=valid&&!unbound?engine_.leaseNativeVst3Editor(ep,rev,
                armix::mixer::ChannelId{ch},static_cast<std::size_t>(throwSlot),
                rawInstance.toString()):
                armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* processor=dynamic_cast<armix::builtin_fx::VocalThrowProcessor*>(
                unbound?offline.get():owner?owner->processor.get():nullptr);
            if(action=="nativeThrowSnapshot") {
                emitNativeThrowSnapshot(request,rawChannel.toString(),
                    rawEpoch.toString(),rawRevision.toString(),
                    rawInstance.toString(),processor);
                return;
            }
            if(action=="nativeThrowTempoSource") {
                // A single shared song clock is selected by stable ChannelId;
                // the source cannot be this vocal channel, MAIN or MONITOR.
                const auto rawSource=args.getProperty("sourceChannelId",{});
                const auto sourceText=rawSource.toString();
                std::uint64_t source=0;
                const bool parsed=sourceText=="0" ||
                    armix::console::approvedNativeId(rawSource,source);
                const bool chosen=!unbound && processor && parsed && (!source||source!=ch) &&
                    !nativeShowBusy_&&!studioBackupBusy_ &&
                    engine_.selectNativeMusicTempoSource(
                        ep,rev,armix::mixer::ChannelId{source});
                if(chosen)serviceNativeMusicTempo();
                auto reply=std::make_unique<juce::DynamicObject>();
                reply->setProperty("requestId",request);
                reply->setProperty("status",chosen?"APPLIED":"REJECTED");
                reply->setProperty("sourceChannelId",sourceText);
                reply->setProperty("errorCode",chosen?"NONE":
                    "INVALID_OR_STALE_MUSIC_CHANNEL");
                if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                    "arMixBuiltinThrowTempoAck",juce::var(reply.release()));
                return;
            }
            const auto numeric=fxRequest.sequence("throw-");
            const bool isSettings=action=="nativeThrowSet";
            const auto values=args.getProperty("settings",{});
            armix::builtin_fx::VocalThrowSettings settings{};
            auto read=[&values](const char* key,float& out) {
                const auto value=values.getProperty(key,{});
                if(!value.isInt()&&!value.isDouble())return false;
                const auto v=static_cast<double>(value);
                if(!std::isfinite(v))return false;
                out=static_cast<float>(v);
                return std::isfinite(out);
            };
            auto integer=[&read](const char* key,int& out) {
                float candidate=0.f;
                if(!read(key,candidate)||candidate!=std::floor(candidate)||
                   candidate<0.f||candidate>1000.f)return false;
                out=static_cast<int>(candidate);return true;
            };
            const bool settingsValid=!isSettings || (values.isObject()&&
                integer("mode",settings.mode)&&read("bpm",settings.bpm)&&
                integer("division",settings.division)&&integer("repeats",settings.repeats)&&
                read("captureMs",settings.captureMs)&&read("silenceMs",settings.silenceMs)&&
                read("thresholdDb",settings.thresholdDb)&&read("wet",settings.wet)&&
                read("decay",settings.decay)&&read("ducking",settings.ducking)&&
                armix::builtin_fx::VocalThrow::validSettings(settings));
            bool queued=false;
            if(processor&&numeric>0&&settingsValid&&!pendingNativeThrow_.owner&&
               !nativeShowBusy_&&!studioBackupBusy_&&!engine_.independentClockActive()) {
                queued=isSettings?processor->queueSettings(settings,numeric):
                                  !unbound && processor->queueTrigger(numeric);
                if(queued && !unbound) {
                    pendingNativeThrow_.owner=owner;
                    pendingNativeThrow_.request=request;
                    pendingNativeThrow_.channelId=rawChannel.toString();
                    pendingNativeThrow_.deviceEpoch=rawEpoch.toString();
                    pendingNativeThrow_.revision=rawRevision.toString();
                    pendingNativeThrow_.instanceId=rawInstance.toString();
                    pendingNativeThrow_.started=juce::Time::getMillisecondCounter();
                    pendingNativeThrow_.trigger=!isSettings;
                }
            }
            if(unbound)
                return completeLogicalFxEdit(args,std::move(offlineInsert),processor,
                    queued,"arMixBuiltinThrowAck");
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("status",queued?"QUEUED":"REJECTED");
            ack->setProperty("errorCode",queued?"NONE":
                pendingNativeThrow_.owner?"THROW_CONTROL_BUSY":
                "INVALID_OR_STALE_NATIVE_THROW");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixBuiltinThrowQueue",juce::var(ack.release()));
            return;
        }

        // Route the approved channel FX picker to the SAME prepared M4
        // graph as physical IN/OUT. ID resolves exclusively in the JUCE
        // catalog; browser paths/names are never trusted as plugin identity.
        if(action=="routingFxInsert"){
            const auto requestId=args.getProperty("requestId",{}).toString();
            const int lane=static_cast<int>(args.getProperty("channel",-1));
            const int position=static_cast<int>(args.getProperty("slot",-1));
            const auto id=args.getProperty("id",{}).toString();
            const auto ep=args.getProperty("deviceEpoch",{});
            const auto rv=args.getProperty("revision",{});
            const auto status=engine_.status().getProperty("dryRoutingLab",{});
            const auto actualEp=status.getProperty("epoch",{});
            const auto actualRev=status.getProperty("revision",{});
            const auto* plugin=catalog_.find(id);
            juce::String error="FX INSERT: invalid/stale device, channel, slot or catalog";
            const bool tokens=
                armix::console::nativeSessionTokenMatches(ep,actualEp) &&
                armix::console::nativeSessionTokenMatches(rv,actualRev);
            if(requestId.startsWith("fx-")&&requestId.length()<=48 &&
               tokens && lane>=0&&lane<LiveDryRoutingLab::count &&
               position>=0&&position<LiveDryRoutingLab::maxFxPerLane &&
               plugin && !catalog_.busy() && !nativeShowBusy_ &&
               !studioBackupBusy_ && !engine_.isGraphSwitchPending() &&
               static_cast<bool>(status.getProperty("hasAcceptedRoute",false))) {
                const LiveDryRoutingLab::FxSelection selection{
                    plugin->key(),plugin->path,plugin->name,plugin->uid};
                error=engine_.insertDryRoutingLabFx(lane,position,
                    static_cast<std::uint64_t>(static_cast<juce::int64>(ep)),
                    static_cast<std::uint64_t>(static_cast<juce::int64>(rv)),
                    selection);
            }
            auto reply=std::make_unique<juce::DynamicObject>();
            reply->setProperty("requestId",requestId);
            reply->setProperty("ok",error.isEmpty());
            reply->setProperty("error",error);
            reply->setProperty("channel",lane);
            reply->setProperty("slot",position);
            reply->setProperty("state",engine_.status().getProperty("dryRoutingLab",{}));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixRoutingFxInsertAck",juce::var(reply.release()));
            if(error.isNotEmpty())emitNotice(error,true);
            else diagnostics_.record("NATIVE_ROUTED_FX_INSERT",
                "CH "+juce::String(lane+1)+" slot "+juce::String(position+1));
            return;
        }
        // M4 is internal PCM compatibility, not an independent GUI state.
        // One native rack gate overrides FX wet/dry, preserving slot bypasses.
        if(action=="routingFxRackBypass"){
            const auto requestId=args.getProperty("requestId",{}).toString();
            const int lane=static_cast<int>(args.getProperty("channel",-1));
            const auto ep=args.getProperty("deviceEpoch",juce::var());
            const auto rv=args.getProperty("revision",juce::var());
            const auto route=engine_.status().getProperty("dryRoutingLab",{});
            const auto currentEp=route.getProperty("epoch",juce::var());
            const auto currentRv=route.getProperty("revision",juce::var());
            const bool tokens=
                armix::console::nativeSessionTokenMatches(ep,currentEp) &&
                armix::console::nativeSessionTokenMatches(rv,currentRv);
            const bool replay=acceptedControlRequests_.contains(requestId);
            const bool valid=tokens&&value.isBool()&&
                requestId.startsWith("rack-m4-")&&requestId.length()<=48 &&
                lane>=0&&lane<LiveDryRoutingLab::count&&!replay&&
                !nativeShowBusy_&&!studioBackupBusy_&&
                static_cast<bool>(route.getProperty("hasAcceptedRoute",false)) &&
                !static_cast<bool>(route.getProperty("nativeChannelIdPcmActive",false)) &&
                !static_cast<bool>(route.getProperty("nativeBusStructural",false)) &&
                !static_cast<bool>(route.getProperty("topologyFault",true)) &&
                engine_.directRouteAuthorized()&&!engine_.independentClockActive();
            if(!replay && requestId.startsWith("rack-m4-") && requestId.length()<=48){
                acceptedControlRequests_.add(requestId);
                if(acceptedControlRequests_.size()>512)acceptedControlRequests_.remove(0);
            }
            const bool applied=valid&&engine_.setDryRoutingLabFxRackBypass(
                lane,static_cast<std::uint64_t>(static_cast<juce::int64>(ep)),
                static_cast<std::uint64_t>(static_cast<juce::int64>(rv)),
                static_cast<bool>(value));
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",requestId);
            ack->setProperty("ok",applied);
            ack->setProperty("deviceEpoch",currentEp);
            ack->setProperty("routingRevision",currentRv);
            ack->setProperty("channel",lane);
            ack->setProperty("bypassed",value);
            ack->setProperty("error",applied?juce::String():
                "Native rack bypass rejected: stale epoch, revision or PCM authority");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixRoutingFxRackAck",juce::var(ack.release()));
            return;
        }
        if(action=="routingM4Control"){
            const auto requestId=args.getProperty("requestId",juce::var()).toString();
            const int channel=static_cast<int>(args.getProperty("channel",-1));
            const auto name=args.getProperty("parameter",juce::var()).toString();
            const auto val=args.getProperty("controlValue",juce::var());
            const auto ep=args.getProperty("deviceEpoch",juce::var());
            const auto rv=args.getProperty("revision",juce::var());
            const auto state=engine_.status();
            const auto route=state.getProperty("dryRoutingLab",{});
            const auto currentEp=route.getProperty("epoch",juce::var());
            const auto currentRv=route.getProperty("revision",juce::var());
            const bool tokenOk=
                armix::console::nativeSessionTokenMatches(ep,currentEp) &&
                armix::console::nativeSessionTokenMatches(rv,currentRv);
            const bool ready=tokenOk && requestId.startsWith("m4-") &&
                requestId.length()<=48 &&
                !nativeShowBusy_ && !studioBackupBusy_ &&
                static_cast<bool>(route.getProperty("hasAcceptedRoute",false)) &&
                // Native ChannelId may own CH1..10 while the physical host
                // still owns post-mixer MAIN gain/mute. Admit only MAIN in that case.
                (channel==-1 ||
                 !static_cast<bool>(route.getProperty("nativeBusStructural",false))) &&
                !static_cast<bool>(route.getProperty("topologyFault",true)) &&
                engine_.directRouteAuthorized() && !engine_.independentClockActive();
            const bool boolean=(name=="mute"||name=="solo"||name=="masterMute");
            const double numeric=boolean?0.:static_cast<double>(val);
            const bool typeOk=boolean?val.isBool():(val.isDouble()||val.isInt());
            const bool master=(channel==-1 &&
                (name=="masterGain"||name=="masterMute"));
            const bool lane=(channel>=0 && channel<LiveDryRoutingLab::count &&
                (name=="faderDb"||name=="pan"||name=="mute"||name=="solo"));
            const bool range=boolean||((name=="pan")?
                (numeric>=-100.&&numeric<=100.):
                (numeric>=-60.&&numeric<=10.));
            bool applied=ready && typeOk && (master||lane) &&
                (boolean||std::isfinite(numeric)) && range;
            if(applied && lane)
                applied=engine_.setDryRoutingLabParameter(channel,
                    static_cast<std::uint64_t>(static_cast<juce::int64>(ep)),
                    static_cast<std::uint64_t>(static_cast<juce::int64>(rv)),
                    name,val.isBool()?(bool(val)?1.f:0.f):
                         static_cast<float>(numeric));
            else if(applied && name=="masterGain")
                engine_.setMasterGainDb(static_cast<float>(numeric));
            else if(applied && name=="masterMute")
                engine_.setMasterMuted(bool(val));
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",requestId);
            ack->setProperty("ok",applied);
            ack->setProperty("deviceEpoch",currentEp);
            ack->setProperty("routingRevision",currentRv);
            ack->setProperty("channel",channel);
            ack->setProperty("parameter",name);
            ack->setProperty("error",applied?juce::String():
                juce::String("Stale G0 route, invalid parameter or PCM authority changed"));
            if(browser_.loaded)
                browser_.emitEventIfBrowserIsVisible("arMixM4ControlAck",
                    juce::var(ack.release()));
            // ACK is not PCM proof; the bridge requires a LATER snapshot.
            return;
        }
        // A visible /preview/ channel OUT changes this same installed M4
        // topology. Only actual MAIN / NONE semantics are supported here.
        if(action=="routingChannelPatch"){
            const int requestId=static_cast<int>(args.getProperty("requestId",0));
            const int channel=static_cast<int>(args.getProperty("channel",-1));
            const auto target=args.getProperty("target",juce::var()).toString();
            const auto operation=args.getProperty("operation","SET_PRIMARY_OUT").toString();
            const auto ep=args.getProperty("deviceEpoch",juce::var());
            const auto rv=args.getProperty("revision",juce::var());
            juce::String error="CHANNEL PATCH: stale identity, unsupported format/output or busy Show";
            const bool allowed=(operation=="SET_PRIMARY_OUT" &&
                                (target=="MAIN"||target=="NONE")) ||
                               (operation=="SET_PROCESSING_FORMAT" &&
                                (target=="MONO"||target=="STEREO"));
            if(requestId>0 && channel>=0 && channel<LiveDryRoutingLab::count &&
               allowed && armix::console::nativeSessionTokenValid(ep) &&
               armix::console::nativeSessionTokenValid(rv) &&
               !nativeShowBusy_ && !studioBackupBusy_)
                error=engine_.repatchDryRoutingLabChannel(channel,
                    static_cast<std::uint64_t>(static_cast<juce::int64>(ep)),
                    static_cast<std::uint64_t>(static_cast<juce::int64>(rv)),
                    operation,target);
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",requestId);
            ack->setProperty("channel",channel);
            ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            ack->setProperty("state",engine_.status().getProperty("dryRoutingLab",{}));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixChannelPatchAck",juce::var(ack.release()));
            if(error.isNotEmpty())emitNotice(error,true);
            return;
        }
        if(action=="routingSourceSwitch"){
            const int requestId=static_cast<int>(args.getProperty("requestId",0));
            const int channel=static_cast<int>(args.getProperty("channel",-1));
            const auto mode=args.getProperty("mode",juce::var()).toString();
            const auto ep=args.getProperty("deviceEpoch",juce::var());
            const auto rv=args.getProperty("revision",juce::var());
            const int left=static_cast<int>(args.getProperty("left",-1));
            const int right=static_cast<int>(args.getProperty("right",-1));
            // SOURCE uses the SAME exact native session authority as FX,
            // OUT and M4. A JUCE JSON double may be an exact integer, but
            // fractional/NaN/rounded or stale tokens must never be cast to
            // a valid active epoch/revision before changing physical PCM.
            const auto nativeRoute=engine_.status().getProperty("dryRoutingLab",{});
            const bool tokens=
                armix::console::nativeSessionTokenMatches(
                    ep,nativeRoute.getProperty("epoch",{})) &&
                armix::console::nativeSessionTokenMatches(
                    rv,nativeRoute.getProperty("revision",{}));
            const juce::String error=requestId>0 &&
                channel>=0&&channel<LiveDryRoutingLab::count&&tokens&&
                !nativeShowBusy_&&!studioBackupBusy_?
                engine_.switchDryRoutingLabSource(channel,
                    armix::console::nativeSessionTokenValue(ep),
                    armix::console::nativeSessionTokenValue(rv),
                    mode,left,right):
                juce::String("SOURCE SWITCH: stale native epoch/revision, channel or Show/backup busy");
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
        // Accepted ChannelId FX power. A rack master is an additive gate,
        // not a rewrite of the eight saved individual bypass preferences.
        // Both gestures are fenced to the actual device epoch + PCM revision
        // and reply with an ACK; the WebView re-reads the producer snapshot.
        if(action=="routingNativeFxRackBypass" ||
           action=="routingNativeFxInsertBypass") {
            const auto request=args.getProperty("requestId",{}).toString();
            const auto rawId=args.getProperty("channelId",{});
            const auto rawEpoch=args.getProperty("deviceEpoch",{});
            const auto rawRevision=args.getProperty("revision",{});
            const auto parse=[](const juce::var& wire,std::uint64_t& out) -> bool {
                if(!wire.isString())return false;
                const auto text=wire.toString().toStdString();
                if(text.empty()||text.size()>20||
                   text[0]=='0'||text.find_first_not_of("0123456789")!=std::string::npos)
                    return false;
                try {
                    const auto decoded=std::stoull(text);
                    if(!decoded||std::to_string(decoded)!=text)return false;
                    out=decoded;return true;
                } catch(...) {return false;}
            };
            std::uint64_t channel=0,ep=0,rev=0;
            const bool valid=request.isNotEmpty()&&request.length()<=128 &&
                parse(rawId,channel)&&parse(rawEpoch,ep)&&
                parse(rawRevision,rev)&&value.isBool() &&
                !studioBackupBusy_&&!nativeShowBusy_&&
                !engine_.independentClockActive();
            bool applied=false;
            if(valid && !acceptedControlRequests_.contains(request)) {
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)
                    acceptedControlRequests_.remove(0);
                const armix::mixer::ChannelId id{channel};
                if(action=="routingNativeFxRackBypass")
                    applied=engine_.bypassNativeFxRack(ep,rev,id,static_cast<bool>(value));
                else {
                    const auto token=args.getProperty("insertId",{}).toString();
                    applied=slot>=0&&slot<LiveDryRoutingLab::maxFxPerLane &&
                        token.isNotEmpty()&&
                        engine_.bypassNativeVst3(ep,rev,id,
                            static_cast<std::size_t>(slot),token,static_cast<bool>(value));
                }
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("ok",applied);
            ack->setProperty("channelId",rawId);
            ack->setProperty("bypassed",value);
            ack->setProperty("error",applied?juce::String():
                "Native FX power rejected: stale device/revision, Show busy or instance mismatch");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixNativeFxPowerAck",juce::var(ack.release()));
            if(applied)diagnostics_.record("NATIVE_FX_POWER",
                rawId.toString()+(action=="routingNativeFxRackBypass"?" rack":" insert")+
                (static_cast<bool>(value)?" BYPASS":" ENABLE"));
            return;
        }

        // Real DAW + BUS from the approved Windows GUI. The engine creates
        // and publishes a stereo GROUP directly in its sole ChannelId PCM
        // owner; no browser-only track or legacy M4 shadow is created.
        // Normal GUI physical IN remains available after a native BUS/WIRE
        // publication. The ChannelId producer, not stale M4, owns the patch.
        if(action=="routingNativeSourcePatch"){
            const auto request=args.getProperty("requestId",{}).toString();
            const auto rawId=args.getProperty("channelId",{});
            const auto rawEp=args.getProperty("deviceEpoch",{});
            const auto rawRev=args.getProperty("graphRevision",{});
            const auto rawRoster=args.getProperty("rosterRevision",{});
            const auto mode=args.getProperty("mode",{}).toString();
            const int left=static_cast<int>(args.getProperty("left",-1));
            const int right=static_cast<int>(args.getProperty("right",-1));
            std::uint64_t id=0,ep=0,rev=0,roster=0;
            const auto snap=approvedNativeSnapshot();
            const auto graph=snap.getProperty("graph",{});
            bool input=false;
            const auto channels=graph.getProperty("channels",{});
            if(const auto* rows=channels.getArray())
                for(const auto& row:*rows)
                    if(row.getProperty("id","").toString()==rawId.toString()&&
                       row.getProperty("role","").toString()=="INPUT")
                        input=true;
            const bool valid=request.startsWith("source-native-")&&
                request.length()<=64&&!acceptedControlRequests_.contains(request)&&
                armix::console::approvedNativeId(rawId,id)&&
                armix::console::approvedNativeId(rawEp,ep)&&
                armix::console::approvedNativeId(rawRev,rev)&&
                armix::console::approvedNativeId(rawRoster,roster)&&
                (mode=="NONE"||mode=="MONO"||mode=="STEREO")&&
                (mode!="NONE"||(left==-1&&right==-1))&&
                (mode!="MONO"||right==-1)&&input&&
                snap.getProperty("device",{}).getProperty("epoch","").toString()==rawEp.toString()&&
                graph.getProperty("revision","").toString()==rawRev.toString()&&
                graph.getProperty("rosterRevision","").toString()==rawRoster.toString()&&
                !nativeShowBusy_&&!studioBackupBusy_;
            LiveDryRoutingLab::NativeBusResult result;
            result.error="NATIVE_SOURCE_NOT_ADMITTED_OR_STALE";
            if(valid){
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)
                    acceptedControlRequests_.remove(0);
                result=engine_.patchNativeInputSource(ep,rev,roster,
                    armix::mixer::ChannelId{id},left,right,mode=="STEREO");
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("channelId",rawId);
            ack->setProperty("ok",valid&&result.accepted);
            ack->setProperty("error",result.error?result.error:"NATIVE_SOURCE_UNKNOWN");
            ack->setProperty("graphRevision",
                juce::String(std::to_string(result.pcmRevision)));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixNativeSourceAck",juce::var(ack.release()));
            if(valid&&result.accepted)
                diagnostics_.record("DAW_NATIVE_SOURCE",
                    "ChannelId "+rawId.toString()+" → "+mode);
            else emitNotice("Channel IN rejected: "+
                juce::String(result.error?result.error:"native error"),true);
            return;
        }
        if(action=="routingNativeBusCreate" || action=="routingNativeTrackCreate") {
            const bool track=action=="routingNativeTrackCreate";
            const auto request=args.getProperty("requestId",{}).toString();
            const auto rawEp=args.getProperty("deviceEpoch",{});
            const auto rawRev=args.getProperty("graphRevision",{});
            const auto rawRoster=args.getProperty("rosterRevision",{});
            std::uint64_t ep=0,rev=0,roster=0;
            const auto live=engine_.status();
            const auto lab=live.getProperty("dryRoutingLab",{});
            auto native=lab.getProperty("nativeBus",{});
            const auto before=approvedNativeSnapshot(live);
            const auto caps=before.getProperty("graph",{})
                .getProperty("capabilities",{}).getProperty("supportedOps",{});
            const bool admitted=caps.isArray()&&
                caps.getArray()->contains(juce::String(track?"CREATE_CHANNEL":"CREATE_GROUP"));
            const auto rawBootstrap=args.getProperty("bootstrapRevision",{});
            std::uint64_t bootstrapRevision=0;
            const bool bootstrap=track && !native.isObject() &&
                !lab.getProperty("retainedNativeBus",{}).isObject() &&
                !static_cast<bool>(lab.getProperty("hasAcceptedRoute",false)) &&
                armix::console::approvedNativeId(rawBootstrap,bootstrapRevision) &&
                juce::String(std::to_string(bootstrapRevision))==
                    lab.getProperty("revision",{}).toString();
            const bool currentGraph=admitted &&
                armix::console::approvedNativeId(rawRev,rev)&&
                armix::console::approvedNativeId(rawRoster,roster)&&
                native.getProperty("graphRevision","").toString()==rawRev.toString() &&
                native.getProperty("rosterRevision","").toString()==rawRoster.toString();
            const bool valid=request.startsWith(track?"track-":"bus-")&&
                request.length()<=64 &&
                !acceptedControlRequests_.contains(request)&&
                armix::console::approvedNativeId(rawEp,ep)&&
                ep==engine_.dryRoutingLabEpoch() &&
                (currentGraph||bootstrap) && !nativeShowBusy_ && !studioBackupBusy_;
            LiveDryRoutingLab::NativeBusResult result;
            result.error="NATIVE_CHANNEL_CREATE_NOT_ADMITTED_OR_STALE";
            juce::String bootstrapError;
            if(valid) {
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)
                    acceptedControlRequests_.remove(0);
                if(bootstrap) {
                    // Admit the existing silent starter mixer without choosing
                    // a hardware source, then use the revisions it published.
                    bootstrapError=engine_.switchDryRoutingLabSource(
                        0,ep,bootstrapRevision,"NONE",-1,-1);
                    native=engine_.status().getProperty("dryRoutingLab",{})
                        .getProperty("nativeBus",{});
                    if(bootstrapError.isEmpty() &&
                       (!armix::console::approvedNativeId(native.getProperty("graphRevision",{}),rev) ||
                        !armix::console::approvedNativeId(native.getProperty("rosterRevision",{}),roster)))
                        bootstrapError="NATIVE_STARTER_MIXER_NOT_ADMITTED";
                }
                const auto channels=native.getProperty("channels",{});
                const auto* rows=channels.getArray();
                if(track && bootstrapError.isEmpty()) {
                    juce::StringArray names;
                    int inputs=0;
                    if(rows)for(const auto& row:*rows) {
                        names.add(row.getProperty("name","").toString());
                        if(row.getProperty("role","").toString()=="INPUT")++inputs;
                    }
                    juce::String name;
                    do { name="Track "+juce::String(++inputs); }
                    while(names.contains(name,true));
                    result=engine_.createNativeInputChannel(ep,rev,roster,
                        name.toStdString(),armix::mixer::ProcessingFormat::stereo);
                } else if(!track) {
                    int count=0;
                    if(rows)for(const auto& row:*rows)
                        if(row.getProperty("role","").toString()=="GROUP")++count;
                    result=engine_.editNativeBus(ep,rev,roster,
                        armix::mixer::ChannelRole::group,
                        std::span<const armix::mixer::ChannelId>{},
                        "Bus "+std::to_string(count+1),1.f);
                }
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("ok",valid&&result.accepted);
            const juce::String error=bootstrapError.isNotEmpty()?bootstrapError:
                juce::String(result.error?result.error:"NATIVE_CHANNEL_CREATE_UNKNOWN");
            ack->setProperty("error",error);
            ack->setProperty("createdId",result.accepted?
                juce::String(std::to_string(result.created.value)):juce::String());
            ack->setProperty("graphRevision",
                juce::String(std::to_string(result.pcmRevision)));
            if(browser_.loaded)
                browser_.emitEventIfBrowserIsVisible(
                    "arMixNativeBusAck",juce::var(ack.release()));
            if(valid&&result.accepted)
                diagnostics_.record(track?"NATIVE_DAW_TRACK":"NATIVE_DAW_BUS",
                    track?"Created unpatched stereo input with MAIN output":
                          "Created real ChannelId GROUP with MAIN output");
            else emitNotice(juce::String(track?"Add Audio Track rejected: ":"Add BUS rejected: ")+
                error,true);
            return;
        }

        // Native Delete Audio Track uses exact ChannelId and accepted device,
        // graph and roster revisions, with a single prepared PCM publication.
        if(action=="routingNativeTrackDelete") {
            const auto request=args.getProperty("requestId",{}).toString();
            const auto rawId=args.getProperty("channelId",{});
            const auto rawEp=args.getProperty("deviceEpoch",{});
            const auto rawRev=args.getProperty("graphRevision",{});
            const auto rawRoster=args.getProperty("rosterRevision",{});
            std::uint64_t id=0,ep=0,rev=0,roster=0;
            const auto snap=approvedNativeSnapshot();
            const auto device=snap.getProperty("device",{});
            const auto graph=snap.getProperty("graph",{});
            const auto caps=graph.getProperty("capabilities",{})
                .getProperty("supportedOps",{});
            const auto channels=graph.getProperty("channels",{});
            bool input=false;
            if(const auto* rows=channels.getArray())
                for(const auto& row:*rows)
                    if(row.getProperty("id","").toString()==rawId.toString() &&
                       row.getProperty("role","").toString()=="INPUT")
                        input=true;
            const bool supported=caps.isArray()&&
                caps.getArray()->contains(juce::String("DELETE_CHANNEL"));
            const bool valid=request.startsWith("track-delete-")&&
                request.length()<=64&&!acceptedControlRequests_.contains(request)&&
                armix::console::approvedNativeId(rawId,id)&&
                armix::console::approvedNativeId(rawEp,ep)&&
                armix::console::approvedNativeId(rawRev,rev)&&
                armix::console::approvedNativeId(rawRoster,roster)&&
                input&&supported&&
                device.getProperty("epoch","").toString()==rawEp.toString()&&
                graph.getProperty("revision","").toString()==rawRev.toString()&&
                graph.getProperty("rosterRevision","").toString()==rawRoster.toString()&&
                !nativeShowBusy_&&!studioBackupBusy_;
            LiveDryRoutingLab::NativeBusResult result;
            result.error="NATIVE_TRACK_DELETE_NOT_ADMITTED_OR_STALE";
            if(valid){
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)
                    acceptedControlRequests_.remove(0);
                result=engine_.removeNativeInputChannel(ep,rev,roster,
                    armix::mixer::ChannelId{id});
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("channelId",rawId);
            ack->setProperty("ok",valid&&result.accepted);
            ack->setProperty("error",result.error?result.error:"NATIVE_TRACK_DELETE_UNKNOWN");
            ack->setProperty("graphRevision",
                juce::String(std::to_string(result.pcmRevision)));
            if(browser_.loaded)
                browser_.emitEventIfBrowserIsVisible(
                    "arMixNativeTrackDeleteAck",juce::var(ack.release()));
            if(valid&&result.accepted && orbit_.active() &&
               nativeOrbitChannelId_==rawId.toString())stopOrbit();
            if(valid&&result.accepted)
                diagnostics_.record("NATIVE_DAW_TRACK_DELETE",
                    "Removed one ChannelId with source, wires and FX leases");
            else emitNotice("Delete Audio Track rejected: "+
                juce::String(result.error?result.error:"native error"),true);
            return;
        }


        // AUX/Bus removal is an exact ChannelId native PCM edit. Incoming
        // primary routes are reconnected to MAIN by the roster transaction,
        // parallel sends are detached, and the removed VST3 lease is retired.
        if(action=="routingNativeBusDelete") {
            const auto request=args.getProperty("requestId",{}).toString();
            const auto rawId=args.getProperty("channelId",{});
            const auto rawEp=args.getProperty("deviceEpoch",{});
            const auto rawRev=args.getProperty("graphRevision",{});
            const auto rawRoster=args.getProperty("rosterRevision",{});
            std::uint64_t id=0,ep=0,rev=0,roster=0;
            const auto snap=approvedNativeSnapshot();
            const auto device=snap.getProperty("device",{});
            const auto graph=snap.getProperty("graph",{});
            const auto caps=graph.getProperty("capabilities",{})
                .getProperty("supportedOps",{});
            const bool supported=caps.isArray()&&
                caps.getArray()->contains(juce::String("REMOVE_BUS"));
            bool removable=false;
            auto role=armix::mixer::ChannelRole::group;
            const auto channelRows=graph.getProperty("channels",{});
            if(const auto* rows=channelRows.getArray())
                for(const auto& row:*rows) {
                    if(row.getProperty("id","").toString()!=rawId.toString())
                        continue;
                    const auto kind=row.getProperty("role","").toString();
                    if(kind=="GROUP")removable=true;
                    if(kind=="FX_RETURN"){
                        removable=true;role=armix::mixer::ChannelRole::fxReturn;
                    }
                }
            const bool valid=request.startsWith("bus-delete-")&&
                request.length()<=64&&!acceptedControlRequests_.contains(request)&&
                armix::console::approvedNativeId(rawId,id)&&
                armix::console::approvedNativeId(rawEp,ep)&&
                armix::console::approvedNativeId(rawRev,rev)&&
                armix::console::approvedNativeId(rawRoster,roster)&&
                removable&&supported&&
                device.getProperty("epoch","").toString()==rawEp.toString()&&
                graph.getProperty("revision","").toString()==rawRev.toString()&&
                graph.getProperty("rosterRevision","").toString()==rawRoster.toString()&&
                !nativeShowBusy_&&!studioBackupBusy_;
            LiveDryRoutingLab::NativeBusResult result;
            result.error="NATIVE_BUS_DELETE_NOT_ADMITTED_OR_STALE";
            if(valid) {
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)
                    acceptedControlRequests_.remove(0);
                result=engine_.editNativeBus(ep,rev,roster,role,
                    std::span<const armix::mixer::ChannelId>{},"",1.f,
                    armix::mixer::ChannelId{id});
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("channelId",rawId);
            ack->setProperty("ok",valid&&result.accepted);
            ack->setProperty("error",result.error?result.error:"NATIVE_BUS_DELETE_UNKNOWN");
            ack->setProperty("graphRevision",
                juce::String(std::to_string(result.pcmRevision)));
            if(browser_.loaded)
                browser_.emitEventIfBrowserIsVisible(
                    "arMixNativeTrackDeleteAck",juce::var(ack.release()));
            if(valid&&result.accepted&&orbit_.active()&&
               nativeOrbitChannelId_==rawId.toString())stopOrbit();
            if(valid&&result.accepted)
                diagnostics_.record("NATIVE_DAW_BUS_DELETE",
                    "Removed accepted GROUP/FX_RETURN and native VST3 lease");
            else emitNotice("Delete BUS/AUX rejected: "+
                juce::String(result.error?result.error:"native error"),true);
            return;
        }

        // Native VST3 Orbit is bound to a verified ChannelId and a live
        // instance lease, not the legacy ordinal engine_.pluginAt(slot).
        if(action=="routingNativeOrbitInspect"||
           action=="routingNativeOrbitStart"||
           action=="routingNativeOrbitStop"||
           action=="routingM4OrbitInspect"||
           action=="routingM4OrbitStart"||
           action=="routingM4OrbitStop"){
            const bool m4=action.startsWith("routingM4Orbit");
            const auto rawId=args.getProperty("channelId",{}),
                rawInsert=args.getProperty("instanceId",{}),
                rawEp=args.getProperty("deviceEpoch",{}),
                rawRev=args.getProperty(m4?"revision":"graphRevision",{}),
                rawRoster=args.getProperty("rosterRevision",{});
            const int lane=static_cast<int>(args.getProperty("channel",-1));
            const auto idText=m4?
                "m4-lane-"+juce::String(lane):rawId.toString();
            const auto insertId=rawInsert.toString();
            const int nativeSlot=static_cast<int>(args.getProperty("slot",-1));
            std::uint64_t id=0,ep=0,rev=0,roster=0;
            const auto snap=approvedNativeSnapshot();
            const auto graph=snap.getProperty("graph",{}),
                device=snap.getProperty("device",{});
            bool nativeMatches=false,m4Matches=false;
            const auto channels=graph.getProperty("channels",{});
            if(!m4)if(const auto* rows=channels.getArray())
                for(const auto& row:*rows)if(row.getProperty("id","").toString()==idText){
                    const auto inserts=row.getProperty("inserts",{});
                    if(const auto* slots=inserts.getArray())
                        if(nativeSlot>=0&&nativeSlot<slots->size()&&
                           (*slots)[nativeSlot].getProperty("id","").toString()==insertId)
                            nativeMatches=true;
                    break;
                }
            const auto lab=engine_.status().getProperty("dryRoutingLab",{});
            if(m4&&lane>=0&&lane<LiveDryRoutingLab::fxSlots){
                const auto all=lab.getProperty("fxInstances",{});
                if(const auto* rows=all.getArray())
                    if(lane<rows->size()){
                        const auto list=(*rows)[lane];
                        if(const auto* slots=list.getArray())
                            if(nativeSlot>=0&&nativeSlot<slots->size()&&
                               (*slots)[nativeSlot].getProperty("instanceId","").toString()==insertId)
                                m4Matches=true;
                    }
            }
            const bool counters=armix::console::approvedNativeId(rawEp,ep)&&
                armix::console::approvedNativeId(rawRev,rev);
            const bool ready=!nativeShowBusy_&&!studioBackupBusy_&&
                !engine_.isGraphSwitchPending()&&counters&&
                nativeSlot>=0&&nativeSlot<8&&!insertId.isEmpty();
            const bool accepted=ready&&(m4?
                (m4Matches&&
                 static_cast<bool>(lab.getProperty("active",false))&&
                 static_cast<bool>(lab.getProperty("hasAcceptedRoute",false))&&
                 lab.getProperty("epoch",{}).toString()==rawEp.toString()&&
                 lab.getProperty("revision",{}).toString()==rawRev.toString()):
                (nativeMatches&&armix::console::approvedNativeId(rawId,id)&&
                 armix::console::approvedNativeId(rawRoster,roster)&&
                 device.getProperty("epoch","").toString()==rawEp.toString()&&
                 graph.getProperty("revision","").toString()==rawRev.toString()&&
                 graph.getProperty("rosterRevision","").toString()==rawRoster.toString()));
            if(!accepted){
                emitNotice("Orbit target is stale or not admitted; reopen the FX menu.",true);
                return;
            }
            const auto lease=m4?
                engine_.leaseRoutedFxForEditor(lane,nativeSlot,ep,rev,insertId):
                engine_.leaseNativeVst3Editor(ep,rev,armix::mixer::ChannelId{id},
                    static_cast<std::size_t>(nativeSlot),insertId);
            auto* plugin=lease&&lease->processor?
                dynamic_cast<juce::AudioPluginInstance*>(lease->processor.get()):nullptr;
            if(!plugin||!armix::core::isDearVrPro2(plugin->getName().toStdString())){
                emitNotice("Orbit requires the verified dearVR PRO 2 VST3 instance.",true);
                return;
            }
            if(action=="routingNativeOrbitInspect"||
               action=="routingM4OrbitInspect"){
                const auto shapeArg=args.getProperty("shape",-1);
                const int shape=shapeArg.isInt()?static_cast<int>(shapeArg):-1;
                emitOrbitState(nativeSlot,shape,plugin,idText,rawRev.toString(),insertId);
                return;
            }
            if(action=="routingNativeOrbitStop"||
               action=="routingM4OrbitStop"){
                if(orbit_.active()&&nativeOrbitChannelId_==idText&&
                   nativeOrbitInstanceId_==insertId)stopOrbit();
                else emitOrbitState(nativeSlot,-1,plugin,idText,rawRev.toString(),insertId);
                return;
            }
            const auto shapeArg=args.getProperty("shape",-1),
                azArg=args.getProperty("parameterIndex",-1),
                distArg=args.getProperty("distanceIndex",-1),
                elevArg=args.getProperty("elevationIndex",-1),
                nearArg=args.getProperty("nearDistance",-1.0),
                farArg=args.getProperty("farDistance",-1.0),
                secondsArg=args.getProperty("seconds",20.0),
                reverseArg=args.getProperty("reverse",false);
            if(!shapeArg.isInt()||
               !(azArg.isInt()||azArg.isInt64())||
               !(distArg.isInt()||distArg.isInt64())||
               !(elevArg.isInt()||elevArg.isInt64())||
               !(nearArg.isDouble()||nearArg.isInt())||
               !(farArg.isDouble()||farArg.isInt())||
               !(secondsArg.isDouble()||secondsArg.isInt())||
               !reverseArg.isBool()){
                emitNotice("Orbit settings are invalid.",true);return;
            }
            const int shape=static_cast<int>(shapeArg),
                az=static_cast<int>(azArg),
                distance=static_cast<int>(distArg),
                elevation=static_cast<int>(elevArg);
            if(!armix::core::validOrbitShape(shape)){
                emitNotice("Orbit path is invalid.",true);return;
            }
            const double nearSetpoint=static_cast<double>(nearArg),
                farSetpoint=static_cast<double>(farArg);
            float normalizedNear=0.f,normalizedFar=1.f;
            const auto unit=args.getProperty("distanceUnit",{}).toString();
            if(unit=="m"){
                if(distance<0||distance>=plugin->getParameters().size()||
                   !metresToNormalized(plugin->getParameters()[distance],nearSetpoint,normalizedNear)||
                   !metresToNormalized(plugin->getParameters()[distance],farSetpoint,normalizedFar)){
                    emitNotice("Orbit metre setpoints do not match the vendor parameter.",true);
                    return;
                }
            }else if(unit=="%"){
                if(!std::isfinite(nearSetpoint)||!std::isfinite(farSetpoint)||
                   nearSetpoint<0||farSetpoint>100){
                    emitNotice("Orbit distance must be 0–100%.",true);return;
                }
                normalizedNear=static_cast<float>(nearSetpoint/100.0);
                normalizedFar=static_cast<float>(farSetpoint/100.0);
            }else{
                emitNotice("Orbit distance unit is invalid.",true);return;
            }
            if(normalizedNear>normalizedFar){
                emitNotice("Orbit Near cannot exceed Far.",true);return;
            }
            // Old VST3 instance must stop BEFORE its new lease is installed.
            if(orbit_.active())stopOrbit();
            const auto error=orbit_.start(plugin,nativeSlot,az,
                static_cast<double>(secondsArg),
                static_cast<bool>(reverseArg),shape,distance,elevation,
                normalizedNear,normalizedFar);
            if(error.isNotEmpty()){
                emitNotice("Orbit: "+error,true);return;
            }
            nativeOrbitLease_=lease;
            nativeOrbitChannelId_=idText;
            nativeOrbitInstanceId_=insertId;
            nativeOrbitEpoch_=ep;
            emitOrbitState(nativeSlot,-1,plugin,idText,rawRev.toString(),insertId);
            diagnostics_.record("NATIVE_DAW_ORBIT",
                "ChannelId="+idText+" slot="+juce::String(nativeSlot)+
                " vendor instance="+insertId);
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
            ack->setProperty("requestId",args.getProperty("requestId",juce::var()));
            ack->setProperty("channel",lane);
            ack->setProperty("slot",slot);
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
        if(action=="logicalVendorFxEditor")return openLogicalVendorEditor(args);
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
                armix::mixer::LiveChannelFxGraph::InstanceLease owner;
                std::uint64_t retainedEpoch=epoch;
                const auto nativeId=args.getProperty("channelId",{});
                if(!nativeId.isVoid()) {
                    std::uint64_t id=0,ep=0,rev=0;
                    if(armix::console::approvedNativeId(nativeId,id)&&
                       armix::console::approvedNativeId(args.getProperty("deviceEpoch",{}),ep)&&
                       armix::console::approvedNativeId(args.getProperty("revision",{}),rev)&&
                       slot>=0&&slot<LiveDryRoutingLab::maxFxPerLane) {
                        owner=engine_.leaseNativeVst3Editor(ep,rev,
                            armix::mixer::ChannelId{id},static_cast<std::size_t>(slot),token);
                        retainedEpoch=ep;
                    }
                } else owner=engine_.leaseRoutedFxForEditor(lane,slot,epoch,revision,token);
                if(owner && owner->processor) {
                    try {
                        closePluginEditor();
                        if(auto* editor=owner->processor->createEditorIfNeeded()) {
                            routedEditorOwner_=owner;
                            routedEditorEpoch_=retainedEpoch;
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

        if(action=="nativeMixerFileSave"||action=="nativeMixerFileOpen") {
            chooseMixerFile(action=="nativeMixerFileSave");
            return;
        }
        if (action == "routingDryShowSave")
        {
            const auto updateShowId=value.isString()?value.toString():juce::String();
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
            if(updateShowId.isNotEmpty()&&!armix::shows::detail::uuid(updateShowId))
                return reject("Invalid Show identity for version update.");
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
                },updateShowId);
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
                        row->setProperty("needsRecovery",item.needsRecovery);
                        row->setProperty("hasPrevious",item.hasPrevious);
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
        // Explicit recovery repairs only a damaged disk HEAD, never auto-
        // recalls/arms audio and never overwrites a verified current Show.
        if (action == "routingDryShowRecover")
        {
            const auto id=value.toString();
            const auto reject=[this](const juce::String& error) {
                if(browser_.loaded) {
                    auto ack=std::make_unique<juce::DynamicObject>();
                    ack->setProperty("ok",false);
                    ack->setProperty("error",error);
                    browser_.emitEventIfBrowserIsVisible(
                        "arMixNativeShowRecoverAck",juce::var(ack.release()));
                }
                emitNotice("Show recovery unchanged: "+error,true);
            };
            if(!armix::shows::detail::uuid(id))
                return reject("Select a valid saved Show identity.");
            if(nativeShowBusy_||studioBackupBusy_||catalog_.busy()||
               engine_.isGraphSwitchPending()||!engine_.allOutputsDisarmed())
                return reject("MUTE both outputs and finish all Studio tasks before recovery.");
            auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
            const bool started=nativeShowWorker_.recover(settingsDirectory(),id,
                [safe](armix::shows::NativeShowSaveWorker::Result result) {
                    if(safe==nullptr)return;
                    safe->nativeShowBusy_=false;
                    if(result.ok())
                        safe->diagnostics_.record("NATIVE_SHOW_LKG_RECOVERED",result.showId);
                    else safe->diagnostics_.record("NATIVE_SHOW_LKG_REJECTED",result.error);
                    if(safe->browser_.loaded) {
                        auto ack=std::make_unique<juce::DynamicObject>();
                        ack->setProperty("ok",result.ok());
                        ack->setProperty("error",result.error);
                        ack->setProperty("showId",result.showId);
                        safe->browser_.emitEventIfBrowserIsVisible(
                            "arMixNativeShowRecoverAck",juce::var(ack.release()));
                    }
                    if(result.ok())
                        safe->emitNotice("Last verified Show restored on disk. OUTPUT stays MUTED; review and RECALL explicitly.");
                    else
                        safe->emitNotice("Last-good Show recovery rejected: "+result.error,true);
                });
            if(!started)return reject("Cannot start Show recovery IO worker.");
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
            autoMainEpoch_=0;
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
            if (engine_.hasTemporaryDiagnosticDevice() ||
                engine_.diagnosticRoute()==armix::diagnostics::Route::mainOutputTone)
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
            auto selected = value.toString();
            if(selected=="TOGGLE_MAIN_OUTPUT_TONE")
                selected=engine_.diagnosticRoute()==Route::mainOutputTone?"NORMAL":"MAIN_OUTPUT_TONE";
            auto route = Route::normal;
            if (selected == "OUTPUT_ONLY_TONE") route = Route::outputOnlyTone;
            else if (selected == "MAIN_OUTPUT_TONE") route = Route::mainOutputTone;
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
            if(route==Route::mainOutputTone)
                emitNotice("MAIN test tone active. Use low speaker volume. Press Test MAIN output again to stop.");
            else
                emitNotice ("Audio diagnostic: " + selected +
                    ". Use low headphone volume and MARK CRACKLE on audible faults.");
        }
        else if (action == "diagnosticMark")
        {
            const auto status = engine_.status();
            const auto window=crackleEvidence_.mark(makeEvidenceSample(status));
            diagnostics_.record ("AUDIO_GLITCH_MARK", juce::JSON::toString (status, false));
            // The recent/important history intentionally caps each entry
            // at 950 characters. Keep MEMORY as a separate compact event
            // so a long XRun/cadence report never truncates RAM evidence.
            diagnostics_.record ("AUDIO_GLITCH_MEMORY",
                compactMemoryWindowReport(window));
            diagnostics_.record ("AUDIO_GLITCH_WINDOW",
                crackleWindowReport(window));
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
                    error = engine_.addVst3 (plugin->path, plugin->uid, plugin->key(),plugin->name);
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
                    !=engine_.outputArmRevision())
                fault="Native route revision changed; review hardware patch.";
            // Isolated output-tone testing remains supported in SIGNAL CHECK.
            if (arm && fault.isEmpty() &&
                engine_.diagnosticRoute()==armix::diagnostics::Route::normal)
                fault=engine_.liveOutputArmFault();
            if (fault.isEmpty()) {
                autoMainEpoch_=0;
                if(!arm)engine_.cancelAsioResetMonitorIntent();
                engine_.setMonitorEnabled(arm);
                diagnostics_.record("OUTPUT_MUTE",arm?"OFF":"ON");
            }
            if (browser_.loaded) {
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("ok",fault.isEmpty());
                ack->setProperty("armed",engine_.isMonitorEnabled());
                // Optional request identity for safe, ordered device changes.
                // No new audio-thread state and backward-compatible with MUTE.
                ack->setProperty("requestId",
                    args.getProperty("requestId",juce::var()));
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

    bool finishAudioSetupAndRecoverMixer(MixerBindingMode bindingMode=MixerBindingMode::exact) {
        if(logicalVendorEditor_ || mixerSessionWorker_.busy())return false;
        const bool bound=engine_.finishManualAudioSetup(bindingMode!=MixerBindingMode::verifiedMainPinApply);
        const auto lab=engine_.status().getProperty("dryRoutingLab",{});
        const bool checkpoint=armix::session::NativeMixerSessionStore(settingsDirectory()).exists();
        using Coordinator=armix::device::NativeDeviceApplyCoordinator;
        const auto decision=Coordinator::decideMixerRebind({
            engine_.directRouteAuthorized(),
            static_cast<bool>(lab.getProperty("nativeChannelIdPcmActive",false)),
            checkpoint,mixerSessionRecoveryPending_,engine_.hasPristineStartupMixer(),
            engine_.hasRetainedNativeSession(),bound
        });
        if(decision==Coordinator::MixerRebindAction::retainInstalled ||
           decision==Coordinator::MixerRebindAction::acceptFreshBound)return true;
        if(decision!=Coordinator::MixerRebindAction::restoreRetained)return false;
        // Only a verified retained identity may upgrade exact -> ASIO restart.
        // The .smix document remains authoritative and is never reset here.
        if(bindingMode==MixerBindingMode::exact && engine_.sameRetainedAsioDriver())
            bindingMode=MixerBindingMode::verifiedAsioRestart;
        const auto error=restoreSavedMixerSession(false,bindingMode);
        if(error.isNotEmpty()) {
            mixerSessionError_=error;
            emitNotice(error.startsWith("NEEDS_REMAP:")?error.fromFirstOccurrenceOf(": ",false,false):error,true);
            return false;
        }
        return true;
    }

    std::uint64_t asioRecoveryEpoch_=0; // last successfully bound generation
    armix::device::AsioRestartRecoveryGate asioRecoveryGate_;

    void serviceAsioMixerRestart() {
        const auto epoch=engine_.dryRoutingLabEpoch();
        const bool retained=engine_.hasRetainedNativeSession();
        const bool silent=!mixerSessionRecoveryPending_ &&
            engine_.hasPristineSilentMixerAwaitingAsio();
        if(epoch==asioRecoveryEpoch_ || (!retained && !silent) ||
           deviceApply_.busy() || (audioSetupWindow_ && audioSetupWindow_->isVisible()) ||
           nativeShowBusy_ || studioBackupBusy_ || mixerSessionWorker_.busy() || catalog_.busy() ||
           engine_.isGraphSwitchPending() || engine_.independentClockActive() ||
           engine_.diagnosticRoute()!=armix::diagnostics::Route::normal)return;
        auto* device=engine_.devices().getCurrentAudioDevice();
        if(device) {
            if(device->getTypeName()!="ASIO" ||
               !engine_.sameRetainedAsioDriver())return;
        } else if(!engine_.selectedRetainedAsioDriverForReopen()) {
            // The device pointer disappeared: never open an unapproved or
            // guessed driver name, even if ASIO4ALL happens to be installed.
            return;
        }
        const auto now=monotonicMilliseconds();
        asioRecoveryGate_.observeEpoch(epoch,now);
        if(!asioRecoveryGate_.due(now))return; // let ASIO4ALL WDM pin inventory settle
        auto applying=deviceApply_.tryBegin(
            armix::device::NativeDeviceApplyCoordinator::Purpose::asioRecovery);
        if(!applying)return;
        if(!device || !device->isOpen() ||
           device->getActiveOutputChannels().countNumberOfSetBits()<2) {
            // A missing, closed or output-less ASIO device requires a vendor call.
            // Never reopen it repeatedly on each UI timer tick.
            if(!asioRecoveryGate_.mayChangeDriver()) {
                asioRecoveryGate_.waitForReadback(now);
                return;
            }
            juce::AudioDeviceManager::AudioDeviceSetup setup;
            engine_.devices().getAudioDeviceSetup(setup);
            // ASIO4ALL can rediscover WDM pins. Other drivers must reopen
            // their selected masks; a restart is not a new device selection.
            const bool refreshWdm=setup.outputDeviceName.startsWithIgnoreCase("ASIO4ALL");
            setup.useDefaultInputChannels=refreshWdm && !setup.inputChannels.isZero();
            setup.useDefaultOutputChannels=refreshWdm;
            asioRecoveryGate_.driverTransaction(now);
            if(!device)diagnostics_.record("ASIO_DEVICE_MISSING",
                "Reopening previously approved registered ASIO driver; no fallback");
            const auto error=openSelectedAudioDevice("ASIO",setup,true);
            asioRecoveryGate_.carryInternalEpoch(engine_.dryRoutingLabEpoch());
            device=engine_.devices().getCurrentAudioDevice();
            if(error.isNotEmpty() || !device || !device->isOpen()) {
                diagnostics_.record("ASIO_REOPEN_FAILED",error);
                emitApprovedNativeSnapshot();
                return;
            }
        }
        if(silent) {
            // Recreate the original unedited SOURCE NONE PCM publisher only
            // after proving the SAME ASIO driver and playback pin identity.
            // No saved mixer, FX or WIRE may be replaced by this path.
            if(!engine_.rebuildPristineSilentAsioMixer()) {
                asioRecoveryGate_.bindFailed(now);
                diagnostics_.record("ASIO_SILENT_MIXER_REBUILD_REJECTED",
                    "Registered ASIO driver/pins or native graph not ready");
                emitApprovedNativeSnapshot();
                return;
            }
            asioRecoveryEpoch_=engine_.dryRoutingLabEpoch();
            asioRecoveryGate_.carryInternalEpoch(asioRecoveryEpoch_);
            asioRecoveryGate_.recovered();
            mixerSessionStatus_="PRISTINE_SOURCE_NONE_REATTACHED";
            mixerSessionError_.clear();
            diagnostics_.record("ASIO_SILENT_MIXER_REATTACHED",
                "Same physical ASIO output; no mic source and no auto-arm");
            emitApprovedNativeSnapshot();
            return;
        }
        if(!engine_.authorizeRetainedAsioRestart()) {
            // Admission can lag driver restart without any driver call.
            // It MUST NOT exhaust the bounded reopen/correction allowance.
            asioRecoveryGate_.waitForReadback(now);
            diagnostics_.record("ASIO_BIND_NOT_READY","Waiting for verified callback/readback");
            return;
        }
        if(const auto corrected=engine_.asioRestartPinCorrection()) {
            if(!asioRecoveryGate_.mayChangeDriver()) {
                asioRecoveryGate_.waitForReadback(now);
                diagnostics_.record("ASIO_PIN_CORRECTION_DEFERRED",
                    "Driver transaction budget exhausted for this physical epoch");
                return;
            }
            const auto inputNames=device->getInputChannelNames();
            const auto outputNames=device->getOutputChannelNames();
            asioRecoveryGate_.driverTransaction(now);
            const auto error=openSelectedAudioDevice("ASIO",*corrected);
            asioRecoveryGate_.carryInternalEpoch(engine_.dryRoutingLabEpoch());
            auto* reopened=engine_.devices().getCurrentAudioDevice();
            if(error.isNotEmpty() || !reopened || !reopened->isOpen() ||
               reopened->getInputChannelNames()!=inputNames ||
               reopened->getOutputChannelNames()!=outputNames ||
               reopened->getActiveInputChannels()!=corrected->inputChannels ||
               reopened->getActiveOutputChannels()!=corrected->outputChannels ||
               !engine_.authorizeRetainedAsioRestart()) {
                diagnostics_.record("ASIO_PIN_CORRECTION_FAILED",
                    error.isNotEmpty()?error:juce::String("Readback changed during correction"));
                emitApprovedNativeSnapshot();
                return;
            }
        }
        const auto error=engine_.reattachRetainedAsioMixer()?juce::String{}:
            restoreSavedMixerSession(false,MixerBindingMode::verifiedAsioRestart);
        if(error.isNotEmpty()) {
            // A native graph/FX lease may become ready after the driver's
            // current epoch; retry without reopening hardware at an
            // exponentially bounded CONTROL cadence.
            asioRecoveryGate_.bindFailed(now);
            diagnostics_.record("ASIO_REATTACH_FAILED",error);
            emitApprovedNativeSnapshot();
            return;
        }
        asioRecoveryEpoch_=engine_.dryRoutingLabEpoch();
        asioRecoveryGate_.carryInternalEpoch(asioRecoveryEpoch_);
        asioRecoveryGate_.recovered();
        mixerSessionStatus_="RESTORED_MUTED";
        mixerSessionError_.clear();
        if(engine_.takeAsioResetMonitorIntent())requestAutomaticMain(false);
        if(engine_.isMonitorEnabled())mixerSessionStatus_="RESTORED_MONITORING";
        diagnostics_.record("ASIO_REATTACHED","Verified driver and current MAIN pair");
        emitApprovedNativeSnapshot();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if(deviceApply_.busy())return; // synchronous transaction owns recovery
        // Save device state only after a real change, not on every 3-second tick.
        deviceSettingsDirty_ = true;
        // The retention gate closes only on an actual epoch/owner change.
        diagnostics_.record ("DEVICE_CHANGE", "Device manager event");
        if((audioSetupWindow_==nullptr || !audioSetupWindow_->isVisible()) &&
           engine_.recoverSameWasapiMixer()) {
            mixerSessionStatus_="RESTORED_MUTED";
            mixerSessionError_.clear();
            diagnostics_.record("DYNAMIC_MIXER_REATTACHED","Verified WASAPI callback epoch; outputs DISARMED");
            emitApprovedNativeSnapshot();
        }
    }

    void timerCallback() override
    {
#if defined(_WIN32)
        // A startup splash can delay HWND creation; attach once the real JUCE
        // peer exists. Never install a low-level keyboard hook.
        if(auto* peer=getPeer())
            hotkeys_.attach(peer->getNativeHandle());
#endif
        if(deviceApply_.busy())return; // a vendor modal loop may re-enter JUCE
        // Always disabled unless the user explicitly starts a bounded trace.
        const bool timing = guiPerf_.active;
        const double tickStart = timing ? juce::Time::getMillisecondCounterHiRes() : 0.0;
        if (timing)
        {
            if (guiPerf_.prevTickMs > 0.0)
                guiPerf_.maxTickGapMs = std::max(guiPerf_.maxTickGapMs,
                                                 tickStart - guiPerf_.prevTickMs);
            guiPerf_.prevTickMs = tickStart;
        }
        ++timerTicks_;
        serviceNativeMusicTempo();
        servicePendingNativeEq();
        servicePendingNativeComp();
        servicePendingNativeHarmony();
        servicePendingNativeThrow();
        saveClosedLogicalVendorEditor();
        // A hidden JUCE/WebView2 peer may defer first navigation. Expose the
        // compositor behind the opaque startup splash after the first tick;
        // the user sees the completed mixer only after the ready handshake.
        if (!webViewPeerWarmed_)
        {
            webViewPeerWarmed_ = true;
            setVisible (true);
            toBack();
            browser_.resized(); // first visible HWND, not its pre-splash size
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
            if (!browser_.loaded || mixerSessionWorker_.busy() || logicalVendorEditor_ ||
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
        // Factory entries are published only after asynchronous manifest
        // verification. Restore once they are visible, never reject a saved
        // factory insert simply because the startup catalog is still busy.
        if(!mixerSessionStartupInspected_ && !catalog_.busy()) {
            mixerSessionStartupInspected_=true;
            if(armix::session::NativeMixerSessionStore(settingsDirectory()).exists()){
                mixerSessionRecoveryPending_=true;
                auto guard=std::make_unique<armix::startup::Vst3RestoreCrashGuard>(settingsDirectory());
                // A crash in vendor DLL code bypasses C++ exception handling.
                // Preserve the user's .smix without replaying its FX at every
                // launch. Manual Open remains available after diagnosis.
                if(guard->interrupted()) {
                    mixerSessionStatus_="STARTUP_FX_SAFE_RECOVERY";
                    mixerSessionError_="Previous startup ended during FX restore. "
                        "Session preserved; reopen it manually after checking VST3 plugins.";
                    diagnostics_.record("STARTUP_FX_RESTORE_CRASH_GUARD",mixerSessionError_);
                    emitNotice(mixerSessionError_,true);
                } else if(!guard->arm()) {
                    mixerSessionStatus_="STARTUP_FX_RESTORE_UNAVAILABLE";
                    mixerSessionError_="Unable to protect saved FX startup. "
                        "Session preserved; check the settings folder permissions.";
                    diagnostics_.record("STARTUP_FX_RESTORE_GUARD_IO",mixerSessionError_);
                    emitNotice(mixerSessionError_,true);
                } else {
                    startupFxGuard_=std::move(guard);
                    const auto error=restoreSavedMixerSession(startupError_.isEmpty());
                    if(error.isNotEmpty()) {
                        (void)startupFxGuard_->complete();
                        startupFxGuard_.reset();
                        emitNotice(error.startsWith("NEEDS_REMAP:")?
                            error.fromFirstOccurrenceOf(": ",false,false):error,true);
                    } else {
                        // Count stability only after vendor construction.
                        startupFxGuardTick_=juce::Time::getMillisecondCounter();
                        deviceSettingsDirty_=true;
                    }
                }
            }else if(startupError_.isEmpty())requestAutomaticMain();
        }
        // Do not clear the marker as soon as constructors return: Waves and
        // other VST3 vendors may crash in their first audio callbacks.
        if(startupFxGuard_ && browser_.loaded &&
           juce::Time::getMillisecondCounter()-startupFxGuardTick_>=15000u) {
            if(startupFxGuard_->complete())startupFxGuard_.reset();
        }
        serviceAsioMixerRestart();
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
            if(!engine_.sameRetainedAsioDriver())
                emitNotice ("Audio device changed unexpectedly. Live output muted; confirm your ASIO/WASAPI device in AUDIO SETUP.", true);
        }
        if (!blocked) unexpectedRouteNoticeSent_ = false;
        // Audio thread publishes only fault flags. A bounded message-thread
        // restore transaction handles device failures without hot-plug spin.
        serviceDirectRestore();
        serviceAutomaticMain();
        // Autosave only while ALL outputs are muted. Vendor-state reads
        // happen on CONTROL; disk commits run on ONE serialized IO worker.
        // A performance with armed speakers/stream is never interrupted for
        // snapshot capture. One bounded attempt per 60 seconds of idle UI.
        if(audioEngineStarted_ && timerTicks_%720==0 &&
           engine_.allOutputsDisarmed() && !mixerSessionWorker_.busy() &&
           !nativeShowBusy_ && !studioBackupBusy_ && !catalog_.busy() &&
           !engine_.isGraphSwitchPending() && !engine_.hasTemporaryDiagnosticDevice())
            (void)saveMixerSession(false);
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
        // A disconnected WebView still needs the 1 Hz safety/health trace.
        // Once the browser is ready, reuse the exact status read below:
        // no duplicate engine_.status() and NO second 1 Hz GUI snapshot.
        if (timerTicks_ % 12 == 0 && !browser_.loaded)
        {
            const auto health = engine_.status();
            crackleEvidence_.push (makeEvidenceSample (health));
            diagnostics_.record ("AUDIO_HEALTH", juce::JSON::toString (health, false));
        }

        if (deviceSettingsDirty_)
        {
            deviceSettingsDirty_ = false;
            // DeviceManager signals even when a temporary capture-only /
            // output-only route is selected. Never persist that test route.
            if (!engine_.hasTemporaryDiagnosticDevice() && !engine_.independentClockActive() &&
                engine_.directRouteAuthorized() && !engine_.hasRetainedUnboundMixer() &&
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
            emitPlayerState(true);
        }
        if (catalog_.busy() && timerTicks_ % 3 == 0)
            emitScanProgress();
        if(timerTicks_%3==0) {
            const auto transport=engine_.playerStatus();
            if(playerAutoNext_ &&
               transport.getProperty("state","").toString()=="ENDED") {
                const auto generation=static_cast<std::uint64_t>(
                    transport.getProperty("generation","0").toString().getLargeIntValue());
                if(generation!=lastAutoAdvanceGeneration_) {
                    lastAutoAdvanceGeneration_=generation;
                    playNextMusic();
                }
            }
            emitPlayerState();
        }
        const double statusStart = timing ? juce::Time::getMillisecondCounterHiRes() : 0.0;
        auto state = engine_.status();
        if (timing)
        {
            const double elapsed = juce::Time::getMillisecondCounterHiRes() - statusStart;
            ++guiPerf_.statusReads;
            guiPerf_.totalStatusMs += elapsed;
            guiPerf_.maxStatusMs = std::max(guiPerf_.maxStatusMs, elapsed);
        }
        // Share the 12 Hz control-thread status with the 1 Hz health log.
        // Preserve real device/hotplug/First Sound evidence and the async writer.
        if (timerTicks_ % 12 == 0)
        {
            crackleEvidence_.push (makeEvidenceSample (state));
            diagnostics_.record ("AUDIO_HEALTH", juce::JSON::toString (state, false));
        }
        // Guard borrowed VST parameter pointers when a native graph edit,
        // device epoch change or Show recall retires the instance.
        if(nativeOrbitLease_ && orbit_.active() &&
           (!engine_.retainsRoutedFxEditor(nativeOrbitEpoch_,nativeOrbitLease_)))
            stopOrbit();
        if(routedEditorOwner_ &&
           !engine_.retainsRoutedFxEditor(routedEditorEpoch_,routedEditorOwner_))
            closePluginEditor();
        annotateRecovery (state);
        state.getDynamicObject()->setProperty("mixerSessionStatus",mixerSessionStatus_);
        if(mixerSessionError_.isNotEmpty())
            state.getDynamicObject()->setProperty("mixerSessionError",mixerSessionError_);
        state.getDynamicObject()->setProperty ("diagnosticsDropped",
            static_cast<double> (diagnostics_.lostRecords()));
        state.getDynamicObject()->setProperty ("diagnosticsTruncated",
            static_cast<double> (diagnostics_.truncatedRecords()));
        if (startupError_.isNotEmpty() && !bool (state.getProperty ("connected", false)))
            state.getDynamicObject()->setProperty ("error", startupError_);
        browser_.emitEventIfBrowserIsVisible ("arMixStatus", state);
        // One authoritative 12 Hz console snapshot, NOT a second 1 Hz burst.
        // Keep continuous real meters, safe native ACK/readback, PATCH/ARM,
        // hot-plug recovery and future subscribed channels observable.
        // All work stays on JUCE's message thread; RT PCM remains untouched.
        if (consoleSubscribed_)
        {
            const double snapshotStart = timing ? juce::Time::getMillisecondCounterHiRes() : 0.0;
            browser_.emitEventIfBrowserIsVisible (
                "arMixConsoleSnapshot", approvedNativeSnapshot(state));
            if (timing)
            {
                const double elapsed = juce::Time::getMillisecondCounterHiRes() - snapshotStart;
                ++guiPerf_.snapshotEmits;
                guiPerf_.totalSnapshotMs += elapsed;
                guiPerf_.maxSnapshotMs = std::max(guiPerf_.maxSnapshotMs, elapsed);
            }
        }
        if (timing)
        {
            const double elapsed = juce::Time::getMillisecondCounterHiRes() - tickStart;
            ++guiPerf_.ticks;
            guiPerf_.totalTickMs += elapsed;
            guiPerf_.maxTickMs = std::max(guiPerf_.maxTickMs, elapsed);
        }
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
#if defined(_WIN32)
        // Per-user-session installer handshake. An Inno update requests the
        // ordinary JUCE quit path on the MESSAGE thread: the native mixer
        // persists accepted .smix state before the installer replaces the PE.
        // Never signal from the audio callback or terminate the process.
        updateRunningMutex_ = ::CreateMutexW (nullptr, FALSE,
            L"Local\\ArSonMix.MixerRunning.v1");
        installerShutdownEvent_ = ::CreateEventW (nullptr, TRUE, FALSE,
            L"Local\\ArSonMix.InstallShutdown.v1");
#endif
        // Paint the approved dark native splash BEFORE browser cache I/O,
        // WebView2 initialization, device scan or native console construction.
        splash_ = std::make_unique<ArSonMixStartupSplash>();
        startTimer (90); // one-shot handoff after Windows first paint
    }
    void timerCallback() override
    {
#if defined(_WIN32)
        if (installerShutdownEvent_ != nullptr &&
            ::WaitForSingleObject (installerShutdownEvent_, 0) == WAIT_OBJECT_0)
        {
            stopTimer();
            // This reaches shutdown() -> window_.reset() ->
            // ConsoleWindow::~ConsoleWindow() -> saveMixerSession().
            systemRequestedQuit();
            return;
        }
#endif
        if (startupAttempted_)
            return;
        startupAttempted_ = true;
        // Remain active only as a lightweight update handshake (4 Hz).
        // Do not touch audio-device selection, callback, or GUI geometry.
        startTimer (250);
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
            showWebViewStartupError ("The native browser backend could not be initialized on this machine.");
            return;
        }
        window_ = std::make_unique<ConsoleWindow> ([this] (int stage)
        {
            if (splash_) splash_->setStage (stage);
            if (stage >= 2)
            {
                splash_.reset(); // no overlay over live mixer after engine init
#if defined(_WIN32)
                // A real JUCE/WebView ready acknowledgement, not simply
                // "EXE launched". The outside supervisor uses it to decide
                // whether an update may be retained or requires rollback.
                const auto probe = juce::SystemStats::getEnvironmentVariable
                    ("ARSONMIX_UPDATE_PROBE", {});
                const auto localAppData = juce::SystemStats::getEnvironmentVariable
                    ("LOCALAPPDATA", {});
                if (stage == 2 && probe.length() == 36
                    && juce::Uuid (probe).toDashedString() == probe
                    && localAppData.isNotEmpty())
                {
                    const auto healthDir = juce::File (localAppData)
                        .getChildFile ("ArSonMix").getChildFile ("Updates")
                        .getChildFile ("health");
                    if (healthDir.createDirectory().wasOk())
                        healthDir.getChildFile (probe + ".ready")
                            .replaceWithText ("GUI_READY");
                }
#endif


                if (window_) window_->toFront (true);
            }
        });
#if defined(_WIN32)
        launchSmartUpdater (false);
#endif
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
#if defined(_WIN32)
        // All session persistence has completed. Only a normal user exit can
        // apply an already downloaded update. Inno-initiated shutdown must
        // not launch a competing installer.
        const bool innoIsClosing = installerShutdownEvent_ != nullptr
            && ::WaitForSingleObject (installerShutdownEvent_, 0) == WAIT_OBJECT_0;
        // Avoid starting setup while Windows is signing out/shutting down:
        // Windows may terminate installer midway through file replacement.
        // A staged transaction remains available on the next normal exit.
        if (!innoIsClosing && ::GetSystemMetrics (SM_SHUTTINGDOWN) == 0)
            launchSmartUpdater (true);
        if (installerShutdownEvent_ != nullptr)
        {
            ::CloseHandle (installerShutdownEvent_);
            installerShutdownEvent_ = nullptr;
        }
        if (updateRunningMutex_ != nullptr)
        {
            ::CloseHandle (updateRunningMutex_);
            updateRunningMutex_ = nullptr;
        }
#endif
    }
private:
#if defined(_WIN32)
    void launchSmartUpdater (bool applyStaged)
    {
        // Windows per-user installation only: independent updater process.
        // On normal exit, apply only a previously prepared transaction.
        // No audio callback, router or plugin operation.
        const auto installedExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const auto installDir = installedExe.getParentDirectory();
        const auto updaterScript = installDir.getChildFile ("ArSonMixAutoUpdate.ps1");
        const auto provenance = installDir.getChildFile ("BUILD_PROVENANCE.json");
        const auto systemFolder = juce::File::getSpecialLocation (juce::File::windowsSystemDirectory);
        const auto powerShell = systemFolder.getChildFile ("WindowsPowerShell")
                                           .getChildFile ("v1.0")
                                           .getChildFile ("powershell.exe");
        if (installedExe.getFileName().equalsIgnoreCase ("ArSonMix.exe")
            && updaterScript.existsAsFile() && provenance.existsAsFile()
            && powerShell.existsAsFile()
            && !installDir.getChildFile ("portable_mode.txt").exists())
        {
            juce::StringArray command { powerShell.getFullPathName(),
                "-NoLogo", "-NoProfile", "-NonInteractive",
                "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
                "-File", updaterScript.getFullPathName(),
                "-OwnerProcessId", juce::String (static_cast<int> (::GetCurrentProcessId())) };
            if (applyStaged)
                command.add ("-ApplyStaged");
            juce::ChildProcess supervisor;
            // The supervisor is detached from the mixing lifecycle. If it
            // cannot start, the GUI still operates normally; updates are
            // optional and never a reason to lose the live session.
            supervisor.start (command, 0);
        }
    }
#endif
    bool startupAttempted_ = false;
#if defined(_WIN32)
    HANDLE updateRunningMutex_ = nullptr;
    HANDLE installerShutdownEvent_ = nullptr;
#endif
    std::unique_ptr<ArSonMixStartupSplash> splash_;
    std::unique_ptr<ConsoleWindow> window_;
};
}
START_JUCE_APPLICATION (ConsoleApp)
