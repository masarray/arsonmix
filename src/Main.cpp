// SPDX-License-Identifier: AGPL-3.0-only
// Native VST3 FX catalog, plugin rack and audio-device bridge.
// Audio runs exclusively in JUCE AudioIODeviceCallback; WebView is control only.
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <BinaryData.h>
#include "ApprovedGuiResources.h"
#include "EmbeddedLegalResources.h"
#include "ApprovedConsoleNativeTransportV6.h"
#include "../integration/ApprovedNativeRuntimeSnapshot.h"
#include "../integration/ApprovedNativeControlIntent.h"
#include "LiveAudioEngine.h"
#include "../engine/builtin_fx/GuitarRigEditor.h"
#include "NativeSessionToken.h"
#include "ArSonMixStartupSplash.h"
#include "ArSonWindowChrome.h"
#include "NativeOwnedFloatingWindow.h"
#include "VstOrbitController.h"
#include "../engine/core/OrbitDistanceMapping.h"
#include "../engine/core/DearVrPolarPolicy.h"
#include "DiagnosticsRecorder.h"
#include "../engine/device/DirectRestoreBackoff.h"
#include "NativeDirectRestoreCoordinator.h"
#include "../engine/device/AsioRestartRecoveryGate.h"
#include "../engine/device/PeriodCapabilityEvidence.h"
#include "../engine/diagnostics/CrackleEvidenceWindow.h"
#if defined(_WIN32)
#include "../engine/device/WasapiCapabilityProbe.h"
#include <windows.h>
#include <commctrl.h> // SetWindowSubclass: observe REAL system resume without touching JUCE audio
#include <psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Comctl32.lib")
#endif
#endif
#include "PluginCatalog.h"
#include "StartupVst3RecoveryGuard.h"
#include "RuntimeVendorCrashJournal.h"
#include "PortableWorkspace.h"
#include "BuiltInFxUserPresetStore.h"
#include "BuiltInFxAcceptedSnapshot.h"
#include "StudioBackup.h"
#include "NativeLiveShowCapture.h"
#include "NativeDryShowRecall.h"
#include "NativeShowSaveWorker.h"
#include "NativeChannelAppearanceStore.h"
#include "NativeNamedPatchStore.h"
#include "NativeMixerSessionSaveWorker.h"
#include "NativeMixerSessionRecovery.h"
#include "NativeLogicalMixerControl.h"
#include "PlayerSetlistArchive.h"
#include "PlayerSetlistRepository.h"
#include "PlayerSetlistEditTransaction.h"
#include "PlayerLibraryEditPlan.h"
#include "PlayerStateProjection.h"
#include "NativeHotkeyRegistry.h"
#include "NativeHotkeyCommandPlan.h"
#include "NativeFxParameterHotkey.h"
#include "NativeFxControlRequest.h"
#include "AntiNoizNativeBinding.h"
#include "SonKuNativeBridgeContract.h"
#include "DeEzzerNativeBridgeContract.h"
#include "../engine/builtin_fx/DeEzzerProcessor.h"
#include "NativeDeviceApplyCoordinator.h"
#if defined(_WIN32)
#include "../engine/device/WindowsFirstRunEndpointDiscovery.h"
#endif
#include <algorithm>
#include <map>
#include <set>
#include <cstring>
#include <optional>
#include <memory>
#include <chrono>
#include <cmath>
#include <functional>
#include <vector>
#include "../engine/player/PlayerSetlistPolicy.h"
#include "../engine/player/PlayerShowEditPolicy.h"
#include "../engine/player/PlayerShowTransitionPolicy.h"
#include "../engine/player/PlayerCueEditPolicy.h"
#include "../engine/player/PlayerLiveMetadataCommitPolicy.h"
#include "../engine/player/PlayerLibraryPolicy.h"
#include <juce_core/juce_core.h>
#include <thread>
#include <future>
#include <utility>
#include <juce_cryptography/juce_cryptography.h>

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
    if (! resolveEmbeddedLegalResource (path, data, size, mime)
        && ! resolveApprovedGuiResource (path, data, size, mime))
        return std::nullopt;

    juce::WebBrowserComponent::Resource resource;
    resource.data.resize (static_cast<size_t> (size));
    std::memcpy (resource.data.data(), data, static_cast<size_t> (size));
    resource.mimeType = mime;
    return resource;
}

juce::File webViewProfile()
{
    const auto folder=armix::storage::PortableWorkspace::browserDataRoot();
#if defined(_WIN32)
    if (juce::JUCEApplication::getCommandLineParameterArray().contains("--safe-webview-profile"))
        return folder.getSiblingFile("WebView2Recovery");
#endif
    return folder;
}

// Materialize browser cache BEFORE WebView2 construction (USB portable mode).
// This single options factory also drives the startup preflight below.
juce::WebBrowserComponent::Options webViewOptions()
{
    const auto folder = webViewProfile();
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

void writeStartupDiagnostic(const juce::String& phase,const juce::String& detail={})
{
#if defined(_WIN32)
    const auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
    if (local.isNotEmpty())
    {
        const auto report = juce::File (local).getChildFile ("ArSonMix")
            .getChildFile ("diagnostics").getChildFile ("startup-failure.txt");
        if (report.getParentDirectory().createDirectory().wasOk())
            (void)report.replaceWithText(
                "ArSonMix "+juce::String(ARMIX_BUILD_SOURCE_SHA)+"\nTime: "
                + juce::Time::getCurrentTime().toISO8601 (true)
                + "\nOS: " + juce::SystemStats::getOperatingSystemName()
                + "\nStage: "+phase+"\n"+detail.substring(0,512)+"\n");
    }
#else
    juce::ignoreUnused(phase,detail);
#endif
}

// Emergency-only presentation: WebView is absent, so the native peer must
// explain recovery without loading another renderer or opening an audio device.
// Reuse the approved caption and low-glare palette; no nested modal loop.
class ArSonNoticeButton final : public juce::TextButton
{
public:
    ArSonNoticeButton(const juce::String& text,bool primary)
        : juce::TextButton(text),primary_(primary) {}
    void paintButton(juce::Graphics& g,bool over,bool down) override {
        const auto bounds=getLocalBounds().toFloat().reduced(1.f);
        g.setColour(juce::Colour(primary_?(down?0xff236077:over?0xff225066:0xff193c4c):
            (down?0xff344752:over?0xff283c48:0xff1a2932)));
        g.fillRoundedRectangle(bounds,4.f);
        g.setColour(juce::Colour(hasKeyboardFocus(true)?0xff68d9f2:primary_?0xff4593ad:0xff405662));
        g.drawRoundedRectangle(bounds,4.f,1.f);
        g.setColour(juce::Colour(0xffe5edf7));
        g.setFont(juce::FontOptions(13.f));
        g.drawFittedText(getButtonText(),getLocalBounds().reduced(7,0),
            juce::Justification::centred,1);
    }
private:
    bool primary_;
};

class ArSonRecoveryNotice final : public juce::DocumentWindow
{
public:
    ArSonRecoveryNotice(const juce::String& title,const juce::String& message,
            const juce::String& primary,const juce::String& cancel,std::function<void(bool)> onChoice)
        : DocumentWindow("ArSonMix recovery",juce::Colour(0xff101921),closeButton),
          panel_(title,message,primary,cancel),onChoice_(std::move(onChoice))
    {
        setLookAndFeel(&caption_);
        setUsingNativeTitleBar(false);
        setTitleBarButtonsRequired(closeButton,false);
        setTitleBarHeight(31);
        setIcon(approvedAppIcon());
        setResizable(false,false);
        setContentNonOwned(&panel_,false);
        panel_.primary.onClick=[this]{finish(true);};
        panel_.cancel.onClick=[this]{finish(false);};
        centreWithSize(560,330);
        setVisible(true);
        toFront(true);
    }
    ~ArSonRecoveryNotice() override {
        clearContentComponent();
        setLookAndFeel(nullptr);
    }
    void closeButtonPressed() override {finish(false);}
private:
    struct Panel final : juce::Component {
        juce::String title,message;
        ArSonNoticeButton primary,cancel;
        Panel(const juce::String& t,const juce::String& m,
              const juce::String& p,const juce::String& c)
            : title(t),message(m),primary(p,true),cancel(c,false) {
            addAndMakeVisible(primary);
            if(c.isNotEmpty())addAndMakeVisible(cancel);
        }
        void resized() override {
            auto buttons=getLocalBounds().reduced(20).removeFromBottom(32);
            primary.setBounds(buttons.removeFromRight(208));
            buttons.removeFromRight(8);
            cancel.setBounds(buttons.removeFromRight(90));
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xff101921));
            auto area=getLocalBounds().reduced(20);
            g.setColour(juce::Colour(0xff68d9f2));
            g.setFont(juce::FontOptions(18.f));
            g.drawFittedText(title,area.removeFromTop(30),juce::Justification::centredLeft,1);
            area.removeFromTop(8);area.removeFromBottom(48);
            g.setColour(juce::Colour(0xffd0dce3));
            g.setFont(juce::FontOptions(13.f));
            g.drawFittedText(message,area,juce::Justification::topLeft,14);
        }
    };
    void finish(bool accepted) {
        if(!onChoice_)return;
        auto callback=std::move(onChoice_);
        setVisible(false);
        // The callback may replace this window (archive result) or quit.
        // Defer until the originating Button/Window event has unwound.
        juce::MessageManager::callAsync([callback=std::move(callback),accepted]{
            callback(accepted);
        });
    }
    armix::ui::MixerCaptionLookAndFeel caption_;
    Panel panel_;
    std::function<void(bool)> onChoice_;
};
std::unique_ptr<ArSonRecoveryNotice> recoveryNotice;
void showRecoveryNotice(const juce::String& title,const juce::String& message,
        const juce::String& primary,const juce::String& cancel,std::function<void(bool)> callback) {
    recoveryNotice.reset();
    recoveryNotice=std::make_unique<ArSonRecoveryNotice>(title,message,primary,cancel,std::move(callback));
}

void showWebViewStartupError (const juce::String& detail)
{
    writeStartupDiagnostic("WEBVIEW_STARTUP_FAILED",detail);
#if defined(_WIN32)
    showRecoveryNotice("Mixer interface unavailable",
        "The offline mixer needs Microsoft Edge WebView2 Runtime.\n\n"+detail+
        "\n\nChoose Repair WebView2 to open the official Microsoft download. "
        "You can also use Start ArSonMix with Recovery Profile or the --safe-webview-profile option.\n"
        "Diagnostic: %LOCALAPPDATA%\\ArSonMix\\diagnostics\\startup-failure.txt",
        "Repair WebView2","Close",[](bool accepted){
            if(accepted)juce::URL("https://developer.microsoft.com/en-us/microsoft-edge/webview2/")
                .launchInDefaultBrowser();
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
#else
    showRecoveryNotice("Mixer interface unavailable",
        "ArSonMix requires the system WebKit browser backend "
        "(WKWebView on macOS, WebKitGTK on Linux).\n\n"+detail+
        "\n\nNo Show, plugin or preset data were deleted.",
        "Close",{},[](bool){juce::JUCEApplication::getInstance()->systemRequestedQuit();});
#endif
}

class Browser final : public juce::WebBrowserComponent
{
public:
    explicit Browser (Options options) : WebBrowserComponent (std::move (options)) {}
    bool loaded = false;
    // Fan out ONLY native editor ACK/readback to owned FX WebViews; the
    // canonical mixer remains the single control publisher. A method-level
    // relay avoids duplicating 20+ existing DSP event sources.
    std::function<void(const juce::String&,const juce::var&)> onNativeEvent;
    void emitEventIfBrowserIsVisible(const juce::String& event,
                                     const juce::var& payload) {
        juce::WebBrowserComponent::emitEventIfBrowserIsVisible(event,payload);
        if(onNativeEvent)onNativeEvent(event,payload);
    }
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
    PluginEditorWindow (const juce::String& title, armix::ui::HostedAudioEditor editorOwner,
                        juce::Component& mixer)
        : DocumentWindow (title, juce::Colour (0xff14212a), allButtons),
          mixer_ (mixer), ownedEditor_(std::move(editorOwner))
    {
        auto* editor=ownedEditor_.get();
        // Reverb-Sis retains its existing native editor/lease but owns
        // its own compact faceplate chrome, like other built-in FX.
        const bool reverbFaceplate=dynamic_cast<armix::builtin_fx::VocalReverbEditor*>(editor)!=nullptr;
        if(reverbFaceplate) {
            setUsingNativeTitleBar(false);
            setTitleBarButtonsRequired(0,false);
            setTitleBarHeight(0);
        } else setUsingNativeTitleBar (true);
        setIcon (approvedAppIcon());
        // The RAII owner notifies the processor before deletion, including
        // exceptions during construction. JUCE window is presentation only.
        setContentNonOwned (editor, true);
        editor->addMouseListener(this,true); // descendant JUCE controls, not a new editor owner
        // Do not offer false host resizing for fixed-geometry built-in FX.
        setResizable (editor->isResizable(), false);
        if(reverbFaceplate)centreWithSize(getWidth(),getHeight());
        else centreWithSize (juce::jmax (380, editor->getWidth()), juce::jmax (300, editor->getHeight()));
        // A Win32-owned floating editor stays above THIS mixer, not every app.
        // Attach after bounds are set (JUCE 8 native owned-popup coordinates).
        (void) armix::ui::attachOwnedFloatingWindow (*this, mixer);
        setVisible (true);
        (void) armix::ui::attachOwnedFloatingWindow (*this, mixer);
        present(true); // centre first presentation near the mixer, only once
    }
    void present (bool centreOnMixer=false)
    {
        if (isMinimised()) setMinimised (false);
        // Repeated rack clicks raise the same HWND without moving it; only
        // an off-monitor window is recovered by the common placement policy.
        armix::ui::placeFloatingEditor (*this, mixer_,
                                        centreOnMixer && !everPresented_);
        everPresented_=true;
        if (!isVisible()) setVisible (true);
        toFront (true);
    }
    void mouseDown(const juce::MouseEvent& event) override {
        if(event.eventComponent==this)juce::DocumentWindow::mouseDown(event);
        if(event.mods.isLeftButtonDown())
            (void)armix::ui::raiseOwnedFloatingWindow(*this,mixer_);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        // Unhandled Escape only; vendor text fields/menus keep first refusal.
        if(key==juce::KeyPress::escapeKey && isActiveWindow()){
            closeButtonPressed();return true;
        }
        return juce::DocumentWindow::keyPressed(key);
    }
    ~PluginEditorWindow() override {
        if(auto* editor=ownedEditor_.get())editor->removeMouseListener(this);
        clearContentComponent();
        ownedEditor_.reset(); // unregister active editor before the lease is released
    }
    bool presentationConfirmed() {
        return armix::ui::verifyOwnedFloatingPresentation(*this,mixer_);
    }
    void closeButtonPressed() override { setVisible (false); }
private:
    juce::Component& mixer_; // ConsoleWindow outlives all editor windows.
    armix::ui::HostedAudioEditor ownedEditor_;
    bool everPresented_=false;
};

// A real Windows/macOS owned desktop window. The sole /preview/ asset pack
// supplies the built-in FX drawing and JS controls; ONLY this tiny WebView
// draws the editor, not another mixer or any second DSP/PCM callback.
// FUX-1 UX correction: the floating FX is ONE real OS-owned top-level HWND,
// but there is NO JUCE DocumentWindow titlebar, frame or extra Close key.
// The WebView2 content IS the complete approved /preview/ hardware editor.
// No duplicate mixer UI/PCM and no CSS fake window clipped to the mixer.
class BuiltinFxDesktopWindow final : public juce::TopLevelWindow, private juce::Timer {
public:
    using OnIntent=std::function<void(juce::var)>;
    BuiltinFxDesktopWindow(juce::Component& mixer,const juce::String& label,
                           const juce::String& catalog,
                           OnIntent onAction,OnIntent onPower,
                           std::function<void()> onReady)
        : TopLevelWindow(label,false),
          mixer_(mixer),
          browser_(webViewOptions().withNativeIntegrationEnabled()
              .withEventListener("arMixFloatingReady",
                  [this,ready=std::move(onReady)](juce::var message){
                      if(message.getProperty("gui","").toString()!=
                         "APPROVED_PAGES_NATIVE_FX_WINDOW_V1")return;
                      browser_.loaded=true;
                      if(ready)ready();
                  })
              .withEventListener("arMixAction",
                  [intent=std::move(onAction)](juce::var request){
                      if(intent)intent(std::move(request));
                  })
              .withEventListener("arMixFloatingPower",
                  [power=std::move(onPower)](juce::var request){
                      if(power)power(std::move(request));
                  })
              .withEventListener("arMixFloatingHide",[this](juce::var){
                  stopTimer();
                  setVisible(false); // ONLY the approved editor's Close button
              })
              .withEventListener("arMixFloatingActivate",[this](juce::var){
                  // Child WebView pointer/focus does not always raise its top-level HWND.
                  // No recenter, recreation, global topmost or keyboard focus steal.
                  if(isVisible()&&hasMeasuredPanel_)
                      (void)armix::ui::raiseOwnedFloatingWindow(*this,mixer_);
              })
              .withEventListener("arMixFloatingDrag",[this](juce::var){
                  startDesktopDrag(); // native OS drag, not a DOM card move
              })
              .withEventListener("arMixFloatingSize",[this](juce::var size){
                  resizeToPanel(size);
              })
              .withResourceProvider(assetFor)) {
        setUsingNativeTitleBar(false); // no Windows caption, no double Close
        setOpaque(true);
        addAndMakeVisible(browser_);   // no DocumentWindow content border
        // Hardware editor's real dimensions are known BEFORE its first
        // WebView navigation: never reveal a 1500x520 placeholder that
        // subsequently collapses during WebView2 first paint.
        const juce::Point<int> nominal=
            catalog=="armix.builtin.graphic-eq31.v1"?juce::Point<int>(1430,280):
            catalog=="armix.builtin.channel-eq.v1"?juce::Point<int>(850,500):
            catalog=="armix.builtin.channel-compressor.v1"?juce::Point<int>(760,530):
            catalog=="armix.builtin.auto-ducker.v1"?juce::Point<int>(720,450):
            catalog=="armix.builtin.arson-harmony.v1"?juce::Point<int>(1080,610):
            catalog=="armix.builtin.vocal-throw.v1"?juce::Point<int>(1020,450):
            juce::Point<int>(1080,640);
        const auto* display=juce::Desktop::getInstance().getDisplays()
                                 .getDisplayForRect(mixer.getScreenBounds());
        const auto area=display?display->userArea:
                         juce::Rectangle<int>(0,0,1550,930);
        centreWithSize(juce::jlimit(200,juce::jmax(200,area.getWidth()-12),nominal.x),
                       juce::jlimit(100,juce::jmax(100,area.getHeight()-12),nominal.y));
        // One peer, allocated AFTER the frameless styling and final nominal
        // bounds. Avoid JUCE 8's first-show peer reconstruction/flash.
        addToDesktop();
        // Public /preview/ remains the single canonical editor renderer.
        // WebView navigation never opens audio devices or creates PCM.
        browser_.goToURL(juce::WebBrowserComponent::getResourceProviderRoot()+
                         "fx-window.html");
    }
    void resized() override {
        browser_.setBounds(getLocalBounds()); // exact edge-to-edge WebView2
    }
    void present(bool recenter=false) {
        recenterAfterInitialPanelSize_=recenter && !hasMeasuredPanel_;
        (void)armix::ui::attachOwnedFloatingWindow(*this,mixer_);
        if(!hasMeasuredPanel_) {
            // Let WebView2 mount at its real S/M/L scale off-screen before
            // any top-level rectangle is painted in front of the operator.
            setTopLeftPosition(-20000,-20000);
            if(!isVisible())setVisible(true);
            startTimer(1800); // fail-open if asset/JS fails to mount
            return;
        }
        // A measured built-in GEQ31 window belongs at the user's position;
        // reopen only raises it, or recovers an off-monitor window.
        armix::ui::placeFloatingEditor(*this,mixer_,false);
        if(!isVisible())setVisible(true);
        toFront(true);
    }
    void emit(const juce::String& event,const juce::var& payload) {
        if(browser_.loaded && isVisible())
            browser_.emitEventIfBrowserIsVisible(event,payload);
    }
    void resizeToPanel(const juce::var& size) {
        const auto w=size.getProperty("width",{}),h=size.getProperty("height",{});
        if(!w.isInt()||!h.isInt())return;
        const int requestedW=static_cast<int>(w),requestedH=static_cast<int>(h);
        if(requestedW<200||requestedW>4000||requestedH<100||requestedH>2600)return;
        const auto* display=juce::Desktop::getInstance().getDisplays()
                                 .getDisplayForRect(mixer_.getScreenBounds());
        const auto area=display?display->userArea:
                         juce::Rectangle<int>(0,0,1550,930);
        // No +16px body gutter, +50px fake titlebar or JUCE content border.
        // Deduplication keeps WebView2 ResizeObserver from jittering a peer
        // already at its accepted pixel geometry (including GEQ S/M/L).
        const int width=juce::jlimit(200,juce::jmax(200,area.getWidth()-12),requestedW);
        const int height=juce::jlimit(100,juce::jmax(100,area.getHeight()-12),requestedH);
        const bool firstPlacement=recenterAfterInitialPanelSize_||!hasMeasuredPanel_;
        recenterAfterInitialPanelSize_=false;
        hasMeasuredPanel_=true;
        stopTimer();
        if(std::abs(getWidth()-width)>2 || std::abs(getHeight()-height)>2) {
            setSize(width,height);
            armix::ui::placeFloatingEditor(*this,mixer_,firstPlacement);
        } else if(firstPlacement) {
            armix::ui::placeFloatingEditor(*this,mixer_,true);
        }
        if(firstPlacement)toFront(true);
    }
    bool ready()const noexcept{return browser_.loaded;}
protected:
    // JUCE 8 has no associatedComponent; keep FX off the Windows taskbar.
    int getDesktopWindowStyleFlags() const override {
        return juce::TopLevelWindow::getDesktopWindowStyleFlags() &
            ~juce::ComponentPeer::windowAppearsOnTaskbar;
    }
private:
    void timerCallback() override {
        stopTimer();
        if(!hasMeasuredPanel_&&isVisible()) {
            // JS load failure must not strand an inaccessible HWND.
            recenterAfterInitialPanelSize_=false;
            armix::ui::placeFloatingEditor(*this,mixer_,true);
            toFront(true);
        }
    }
    void startDesktopDrag() {
#if defined(_WIN32)
        // Real Windows system-move supports snap and crossing monitor edges.
        // WebView2's original header gesture is consumed in the native-only
        // bridge before its demo DOM-card drag handler can run.
        if(auto* peer=getPeer()) {
            const auto hwnd=static_cast<HWND>(peer->getNativeHandle());
            if(hwnd && ::IsWindow(hwnd)) {
                POINT cursor{};
                if(::GetCursorPos(&cursor)) {
                    ::ReleaseCapture();
                    ::PostMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,
                        MAKELPARAM(cursor.x,cursor.y));
                }
            }
        }
#endif
    }
    juce::Component& mixer_;
    Browser browser_;
    bool recenterAfterInitialPanelSize_=false;
    bool hasMeasuredPanel_=false;
};



// A post-install GUI probe must not open the owner's last audio interface,
// restore or overwrite the last .smix, or automatically resume live output.
// Only the supervisor creates the canonical 36-character GUID env token.
static bool isAutoUpdateHealthProbe()
{
#if defined(_WIN32)
    const auto token = juce::SystemStats::getEnvironmentVariable ("ARSONMIX_UPDATE_PROBE", {});
    return token.length() == 36 && juce::Uuid (token).toDashedString() == token;
#else
    return false;
#endif
}

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
                  cancelPhysicalLoopback();
                  handleLogicalMixerIntent(args);
              })
              .withEventListener ("arMixHotkeyIntent", [this] (juce::var args) {
                  handleNativeHotkeyIntent(args);
              })
              .withEventListener ("arMixWindowFullscreen", [this] (juce::var args) {
                  // WebView2 forwards plain F11; JUCE owns native geometry.
                  if(args.getProperty("operation","").toString()=="TOGGLE")
                      toggleOperatorFullscreen();
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
                  cancelPhysicalLoopback();
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
              .withEventListener ("arMixOpenLegalSource", [] (juce::var)
              {
                  // Exact source for this *binary*; open only on user request,
                  // never from a mixer callback, startup, or a remote URL.
                  const juce::String sha (ARMIX_BUILD_SOURCE_SHA);
                  const auto source = sha.length() == 40
                                   && sha.containsOnly ("0123456789abcdef")
                      ? "https://github.com/masarray/arsonmix/tree/" + sha
                      : "https://github.com/masarray/arsonmix";
                  juce::URL (source).launchInDefaultBrowser();
              })
              .withResourceProvider (assetFor))
    {
        settingsDirectory().createDirectory();
        // Original native event publisher remains UNIQUE. Broadcast only
        // built-in FX receipts plus bounded native readbacks to child HWNDs.
        // A vendor plugin never receives a second processing callback.
        browser_.onNativeEvent=[this](const juce::String& event,
                                      const juce::var& payload) {
            const bool response=event.startsWith("arMixBuiltin") ||
                event=="arMixNativeFxPowerAck" || event=="arMixGlobalFxAck" ||
                event=="arMixLogicalAck";
            const bool snapshot=event=="arMixConsoleSnapshot";
            if(!response&&!snapshot)return;
            if(snapshot && ++floatingFxRelayTick_%4!=0)return; // <=3 Hz
            for(const auto& owned:floatingBuiltinFx_)
                if(owned && owned->window && owned->window->isVisible())
                    owned->window->emit(event,payload);
        };
        hotkeys_.setHandler([this](const armix::hotkey::Mapping& mapping) {
            fireNativeHotkey(mapping);
        });
        restorePlayerLibrary(); // portable, local song paths only; NEVER autoplay on launch
        catalog_.onChanged = [this] { emitCatalog(); emitScanProgress(); };
        if (!updaterProbeMode_)
            catalog_.scanFactoryIfPresent(); // Do not scan even FactoryFX during GUI-only probe.
#if defined(_WIN32)
        // JUCE owns the real window commands; unlike WebView2 JS, these are
        // synchronous native controls with JUCE's existing resize constrainer.
        setLookAndFeel (&mixerCaption_);
        setUsingNativeTitleBar (false);
        // DocumentWindow creates its caption buttons before ConsoleWindow
        // installs its own LookAndFeel. Recreate them explicitly from the
        // approved native SVG factory; never leave stock JUCE buttons here.
        setTitleBarButtonsRequired (allButtons, false);
        setTitleBarHeight (31);
#else
        setUsingNativeTitleBar (true);
#endif
        setIcon (approvedAppIcon());
        setResizable (true, false);
        setResizeLimits (1100, 650, 3840, 2160);
        setWantsKeyboardFocus(true);
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

    bool keyPressed(const juce::KeyPress& key) override {
#if JUCE_WINDOWS
        if(key.getKeyCode()==juce::KeyPress::F11Key && !key.getModifiers().isAnyModifierKeyDown()) {
            toggleOperatorFullscreen();return true;
        }
#endif
        return juce::DocumentWindow::keyPressed(key);
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

    // Reversible native F11. Restore the exact pre-entry maximize/normal
    // mode, without touching audio, plugins or the approved WebView2 GUI.
    void toggleOperatorFullscreen()
    {
#if defined(_WIN32)
        const bool entering=!operatorFullscreen_;
        if(entering) {
            maximisedBeforeOperatorFullscreen_=isFullScreen();
            topmostBeforeOperatorFullscreen_=isAlwaysOnTop();
            setTitleBarHeight(0);
            if(!maximisedBeforeOperatorFullscreen_)setFullScreen(true);
            // DocumentWindow fullscreen on Windows may use the taskbar's
            // workArea. JUCE kiosk instead takes the display totalArea and
            // suppresses taskbar/dock without a second topmost HWND.
            juce::Desktop::getInstance().setKioskModeComponent(this,false);
            // Fullscreen only: retain shell coverage during owned FX focus.
            setAlwaysOnTop(true);
        } else {
            juce::Desktop::getInstance().setKioskModeComponent(nullptr,false);
            setAlwaysOnTop(topmostBeforeOperatorFullscreen_);
            if(!maximisedBeforeOperatorFullscreen_)setFullScreen(false);
            else setFullScreen(true);
            setTitleBarHeight(31);
        }
        operatorFullscreen_=entering;
        if(browser_.isShowing())browser_.resized();
        auto reply=std::make_unique<juce::DynamicObject>();
        reply->setProperty("active",operatorFullscreen_);
        browser_.emitEventIfBrowserIsVisible("arMixWindowFullscreenState",
                                             juce::var(reply.release()));
#endif
    }

    void activeWindowStatusChanged() override
    {
        juce::DocumentWindow::activeWindowStatusChanged();
#if defined(_WIN32)
        // JUCE disables DocumentWindow caption buttons when isActiveWindow()
        // is false. WebView2 has its own child HWND/focus lifetime; this can
        // temporarily report the mixer as inactive even while its native
        // caption is visible and the user is clicking its controls.
        // Windows caption commands must remain interactable on an inactive
        // window: the first pointer click can activate it and perform the
        // requested minimise/maximise/close action.
        // No timers, synthetic clicks, forced foreground or WebView roundtrip.
        for (auto* control : { getMinimiseButton(), getMaximiseButton(), getCloseButton() })
            if (control != nullptr)
                control->setEnabled (true);
#endif
    }

    ~ConsoleWindow() override
    {
#if defined(_WIN32)
        if(operatorFullscreen_&&juce::Desktop::getInstance().getKioskModeComponent()==this)
            juce::Desktop::getInstance().setKioskModeComponent(nullptr,false);
#endif
        sessionLifecycleShuttingDown_=true;
        hotkeys_.detach(); // release global keys before the JUCE HWND disappears
        stopTimer();
#if defined(_WIN32)
        if (powerNoticeHwnd_) {
            RemoveWindowSubclass(powerNoticeHwnd_, &ConsoleWindow::audioPowerWndProc, 0xA50);
            powerNoticeHwnd_ = nullptr;
        }
#endif
        orbit_.stop();
        catalog_.onChanged = nullptr;
        closeAllPluginEditors();
        // Shutdown is the ONE place where the mixer event relay is detached.
        // Ordinary .smix Open, FX replace, preset recall and device changes
        // also call closeAllPluginEditors(): nulling the relay there made
        // every subsequently reopened floating built-in FX window permanently
        // lose DSP ACK/readback. Do not unregister it until final teardown.
        browser_.onNativeEvent=nullptr;
        engine_.devices().removeChangeListener (this);
        pendingRetainedFxCapture_.reset(); // cancel incomplete CONTROL capture before teardown
        // Drain any earlier file work before the final muted checkpoint.
        // Remember MAIN before the shutdown mute changes the hardware state.
        // A driver STOP may have gated hardware already. Preserve the
        // operator's intended MAIN state in the crash-safe .smix checkpoint.
        const bool resumeMain=engine_.wantsMainContinuation() && !operatorOutputMuted_;
        engine_.setOutputMuted(true);
        engine_.disconnectSecondaryOutput();
        mixerSessionWorker_.finishForShutdown(logicalVendorEditor_.has_value());
        saveClosedLogicalVendorEditor(true);
        mixerSessionWorker_.finishForShutdown();
        logicalVendorEditor_.reset();
        const auto lab=engine_.status().getProperty("dryRoutingLab",{});
        if (!updaterProbeMode_ &&
            (static_cast<bool>(lab.getProperty("nativeChannelIdPcmActive",false)) ||
             logicalMixer_.loaded())) {
            const auto error=saveMixerSession(resumeMain,{},true); // shutdown checkpoint, not timer autosave
            if(error.isNotEmpty())
                diagnostics_.record("DYNAMIC_MIXER_EXIT_SAVE_REJECTED",error);
        }
        engine_.stop();
        // No UI/engine capture in the read-only worker. Join after audio
        // teardown so a slow endpoint enumeration cannot keep sound alive.
        if(secondaryInventoryFlight_.valid())secondaryInventoryFlight_.wait();
        // Normal shutdown is not an interrupted startup. Leave marker intact
        // on abnormal vendor termination (destructors never run).
        if(startupFxGuard_)(void)startupFxGuard_->complete();
        browser_.setVisible (false);
        setLookAndFeel (nullptr); // before the owned LookAndFeel is destroyed
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    armix::ui::MixerCaptionLookAndFeel mixerCaption_;
    bool operatorFullscreen_=false;
    bool maximisedBeforeOperatorFullscreen_=false;
    bool topmostBeforeOperatorFullscreen_=false;
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
    armix::io::NamedPatchStore namedPatches_{machineSettingsDirectory()};
    armix::session::NativeIoDefaults ioDefaults_{machineSettingsDirectory()};
    std::unique_ptr<juce::FileChooser> namedIoFileChooser_;
    std::unique_ptr<juce::FileChooser> fxRackFileChooser_;
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
            if(retainedOfflineFxActive_) {
                mixerSessionStatus_="RETAINED_OFFLINE_FX_REAPPLY_REQUIRED";
                retainedOfflineLastActivityMs_=monotonicMilliseconds();
            }
            mixerSessionError_.clear();
        }};
    struct LogicalVendorEditor {
        armix::session::StoredInsert insert;
        juce::String workspaceToken,instanceId;
        std::unique_ptr<juce::AudioProcessor> processor;
        std::unique_ptr<PluginEditorWindow> window;
    };
    std::optional<LogicalVendorEditor> logicalVendorEditor_;
    juce::String lastLogicalFxRequest_,lastLogicalFxInstance_,lastLogicalFxRevision_;
    juce::String mixerSessionId_,mixerSessionStatus_="NO_SAVED_SESSION";
    std::uint64_t workspaceGeneration_=0; // message-thread committed workspace identity
    juce::String mixerSessionError_,lastMixerSessionFingerprint_;
    bool mixerSessionStartupInspected_=false;
    bool sessionLifecycleShuttingDown_=false;
    bool mixerSessionRecoveryPending_=false;
    bool runtimeFxFaultNoticeSent_=false;
    juce::uint32 runtimeFxFaultPollTick_=0;
    bool startupFxSafeMode_=armix::startup::Vst3RestoreCrashGuard(settingsDirectory()).interrupted();
    bool vendorSafeRecoveryActive() const {
        return startupFxSafeMode_ || armix::startup::RuntimeVendorCrashJournal::process().recoveryRequired();
    }
    // Isolated read/write FX workspace captured explicitly from a disconnected
    // retained graph. It edits persisted .smix data, NEVER the original lease.
    bool retainedOfflineFxActive_=false;
    std::uint64_t retainedOfflineEpoch_=0,retainedOfflineRevision_=0;
    std::uint64_t retainedOfflineOwnerEpoch_=0;
    struct PendingRetainedFxCapture final {
        LiveDryRoutingLab::RetainedMixerCapture capture;
        juce::var requestId;
        std::uint64_t epoch=0,revision=0,ownerEpoch=0,startedMs=0;
        bool resumeMain=false;
    };
    std::optional<PendingRetainedFxCapture> pendingRetainedFxCapture_;
    std::uint64_t retainedOfflineLastActivityMs_=0;
    std::uint64_t offlineReturnEpoch_=0,offlineReturnSinceMs_=0;
    std::uint64_t offlineReturnCallback_=0,offlineReturnProgressMs_=0;
    std::uint64_t offlineAutoAttemptEpoch_=0,offlineAutoAttemptRevision_=0,offlineAutoNextVerifyMs_=0;
    juce::String offlineAutoAttemptWorkspace_;
    std::unique_ptr<armix::startup::Vst3RestoreCrashGuard> startupFxGuard_;
    juce::uint32 startupFxGuardTick_=0;
    std::unique_ptr<juce::FileChooser> mixerFileChooser_;
    bool mixerFileDialogOpen_=false;
    juce::File lastMixerFile_;
    bool nativeShowBusy_ = false;
    bool studioBackupBusy_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<juce::FileChooser> playerFileChooser_;
    // PlayerShow v3: one active setlist projects into the existing native
    // library/controls so decoder bindings and FX routing do not change.
    // Save/restore swaps complete sets, not two independently mutated arrays.
    std::vector<juce::File> playerLibrary_;
    using PlayerTrackSettings=armix::player::PlayerTrackSettings;
    using PlayerSavedSet=armix::player::PlayerSavedSet;
    std::vector<PlayerTrackSettings> playerTrackSettings_;
    std::vector<PlayerSavedSet> playerSets_;
    int playerActiveSet_=0;
    std::vector<juce::File> playerQueue_; // transient ordered PLAY NEXT queue
    armix::player::PlayerQueueEditEpoch playerQueueEpoch_;
    struct PlayerUndoRemove {
        int set=-1,index=-1;
        juce::File file;
        PlayerTrackSettings controls{};
    };
    PlayerUndoRemove playerUndoRemove_{};
    int playerSelected_=-1,playerCurrent_=-1;
    int playerPendingIndex_=-1; // becomes CURRENT only after native fade ACK
    juce::File playerPendingQueueFile_; // commit queue ONLY after real PCM ACK
    int playerFadeMilliseconds_=180; // compact optional transition setting
    int playerOutputPendingIndex_=-1; // only after explicit Choose output gesture
    bool playerAutoNext_=false;
    bool playerLoopCurrent_=false; // explicit rehearsal toggle, never persisted
    bool playerFolderBusy_=false; // GUI only; background scanner never touches this
    armix::player::PlayerAutoAdvanceGate playerAutoAdvanceGate_;
    juce::String playerError_;
    juce::File lastPluginFolder_ = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    // Destruction order: editorWindow_ releases its JUCE GUI BEFORE the
    // retained vendor processor lease. No vendor destructor runs on RT.
    armix::mixer::LiveChannelFxGraph::InstanceLease routedEditorOwner_;
    std::uint64_t routedEditorEpoch_=0;
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
    // A separate native concert HWND: third-party VST3 editors must not
    // close or silently replace the active ImmerSIB processor UI.
    armix::mixer::LiveChannelFxGraph::InstanceLease immersibEditorOwner_;
    std::uint64_t immersibEditorEpoch_=0;
    juce::AudioProcessor* immersibEditorProcessor_=nullptr; // non-owning identity
    std::unique_ptr<PluginEditorWindow> immersibEditorWindow_;
#endif
    struct PendingNativeEq {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        int slot=-1;
        juce::uint32 started=0;
    } pendingNativeEq_;
    juce::String lastNativeEqRequest_,lastNativeEqInstance_;
    juce::String lastNativeEqEpoch_,lastNativeEqRevision_;
    // GEQ31 uses the same validated ChannelId/instance/epoch lease as PEQ.
    // The UI lives in the one approved browser; audio ACK is from the same
    // 31-band processor callback, never from a second JS mixer or PCM graph.
    struct PendingNativeGeq {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        juce::uint32 started=0;
    } pendingNativeGeq_;
    juce::String lastNativeGeqRequest_,lastNativeGeqInstance_;
    juce::String lastNativeGeqEpoch_,lastNativeGeqRevision_;
    struct PendingNativeComp {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        juce::uint32 started=0;
    } pendingNativeComp_;
    struct PendingNativeAntiNoiz {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::var identity; // CONTROL only, exact original request envelope
        std::uint64_t id=0,epoch=0,revision=0,channel=0;
        int slot=-1;
        juce::uint32 started=0;
        bool timeoutReported=false;
    } pendingNativeAntiNoiz_;
    struct PendingNativeDucker {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::var identity;
        std::uint64_t sequence=0;
        juce::uint32 started=0;
        bool timeoutNotified=false;
    };
    std::vector<PendingNativeDucker> pendingNativeDuckers_;
    juce::String lastNativeCompRequest_,lastNativeCompInstance_;
    juce::String lastNativeCompEpoch_,lastNativeCompRevision_;
    struct PendingNativeDeEzzer {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        armix::builtin_fx::NativeFxControlRequest request;
        armix::builtin_fx::de_ezzer::Identity id{};
        std::uint64_t sequence=0;
        juce::uint32 started=0;
    } pendingNativeDeEzzer_;
    struct PendingNativeHarmony {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        juce::uint32 started=0;
    } pendingNativeHarmony_;
    juce::String lastNativeHarmonyRequest_,lastNativeHarmonyInstance_;
    juce::String lastNativeHarmonyEpoch_,lastNativeHarmonyRevision_;
    struct PendingNativeSonKu {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        juce::String request,channelId,deviceEpoch,revision,instanceId;
        juce::uint32 started=0;
    } pendingNativeSonKu_;
    juce::String lastNativeSonKuRequest_,lastNativeSonKuInstance_;
    juce::String lastNativeSonKuEpoch_,lastNativeSonKuRevision_;
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
    juce::AudioProcessor* editorProcessor_=nullptr; // identity only, never owns PCM
    // UI-thread-only additional vendor HWNDs; the peer is destroyed BEFORE
    // its exact native processor lease. No duplicate DSP or JUCE editor.
    struct AdditionalFxEditor final {
        armix::mixer::LiveChannelFxGraph::InstanceLease owner;
        std::uint64_t epoch=0,channel=0;
        int lane=-1,slot=-1;
        juce::String instanceId;
        juce::AudioProcessor* processor=nullptr; // non-owning
        std::unique_ptr<PluginEditorWindow> window;
    };
    // Heap-stable entries avoid std::vector erase/move-assignment releasing
    // a processor lease before its vendor HWND. Each pointee destroys its
    // LAST-declared window before its FIRST-declared owner.
    std::vector<std::unique_ptr<AdditionalFxEditor>> additionalFxEditors_;
    static constexpr std::size_t maxSimultaneousVendorEditors=16;
    struct FloatingBuiltinOwner final {
        juce::String channelId,instanceId,catalogId,workspaceToken;
        // Native ACK-authorized one-step logical revision migration.
        // Never follow an arbitrary replacement of the same FX slot.
        juce::String rekeyFrom;
        int slot=-1;
        std::unique_ptr<BuiltinFxDesktopWindow> window; // last, destroyed first
    };
    std::vector<std::unique_ptr<FloatingBuiltinOwner>> floatingBuiltinFx_;
    std::uint64_t floatingFxRelayTick_=0;
    static constexpr std::size_t maxSimultaneousBuiltinWindows=16;
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
    const bool updaterProbeMode_ = isAutoUpdateHealthProbe();
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
    // P0: last verified DIRECT WASAPI profile survives missing hardware;
    // never replace it with JUCE's incidental default after hotplug.
    std::optional<juce::AudioDeviceManager::AudioDeviceSetup> verifiedWasapiSetup_;
    armix::device::AudioContinuityRetry wasapiRecovery_;
    std::uint64_t callbackWatchTickMs_=0, callbackProgressMs_=0;
    std::uint64_t callbackWatchCount_=0;
    bool directCallbackStalled_=false, feedbackMasterTripNotified_=false;
#if defined(_WIN32)
    HWND powerNoticeHwnd_=nullptr;
    bool actualSystemResumePending_=false;
    bool actualSystemSuspendObserved_=false;

    static LRESULT CALLBACK audioPowerWndProc(HWND hwnd,UINT message,WPARAM wp,
                                               LPARAM lp,UINT_PTR id,DWORD_PTR ref) {
        auto* self=reinterpret_cast<ConsoleWindow*>(ref);
        if(message==WM_POWERBROADCAST && self) {
            // Screen/monitor power events are NOT a system suspend.
            // Never gate PCM, mutate MAIN intent, or reopen a healthy device.
            if(wp==PBT_APMSUSPEND)
                self->actualSystemSuspendObserved_=true;
            else if(wp==PBT_APMRESUMEAUTOMATIC &&
                    armix::device::AudioPowerPolicy::inspectDevice(
                        armix::device::NativePowerEvent::systemResume))
                self->actualSystemResumePending_=true;
        }
        if(message==WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd,&ConsoleWindow::audioPowerWndProc,id);
            if(self && self->powerNoticeHwnd_==hwnd)self->powerNoticeHwnd_=nullptr;
        }
        return DefSubclassProc(hwnd,message,wp,lp);
    }
    void attachAudioPowerNotifications(void* nativeHandle) {
        auto* hwnd=static_cast<HWND>(nativeHandle);
        if(!hwnd || hwnd==powerNoticeHwnd_)return;
        if(powerNoticeHwnd_)
            RemoveWindowSubclass(powerNoticeHwnd_,&ConsoleWindow::audioPowerWndProc,0xA50);
        powerNoticeHwnd_=nullptr;
        if(SetWindowSubclass(hwnd,&ConsoleWindow::audioPowerWndProc,0xA50,
                              reinterpret_cast<DWORD_PTR>(this)))
            powerNoticeHwnd_=hwnd;
    }
#endif
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
    // Windows process CPU over one monotonic 1 Hz interval, as a percentage
    // of ALL logical CPUs. This is application CPU (UI/VST/engine), NOT an
    // RT callback usage estimate and NOT total system CPU. The first read,
    // long suspend intervals and unavailable OS counters are UNKNOWN.
    std::uint64_t lastFooterCpuTickMs_ = 0;
    std::uint64_t lastFooterProcessCpu100ns_ = 0;
    juce::var sampleFooterHostCpuPercent() noexcept
    {
#if defined(_WIN32)
        FILETIME created {}, exited {}, kernel {}, user {};
        if (::GetProcessTimes (::GetCurrentProcess(), &created, &exited,
                               &kernel, &user) == FALSE)
            return {};
        const auto count = [] (const FILETIME& time) noexcept -> std::uint64_t
        {
            return (static_cast<std::uint64_t> (time.dwHighDateTime) << 32)
                | static_cast<std::uint64_t> (time.dwLowDateTime);
        };
        const std::uint64_t now = ::GetTickCount64();
        const std::uint64_t consumed = count (kernel) + count (user);
        const auto priorWall = lastFooterCpuTickMs_;
        const auto priorCpu = lastFooterProcessCpu100ns_;
        lastFooterCpuTickMs_ = now;
        lastFooterProcessCpu100ns_ = consumed;
        if (priorWall == 0 || now <= priorWall ||
            now - priorWall < 250 || now - priorWall > 5000 ||
            consumed < priorCpu)
            return {};
        static const double logicalCpus = []
        {
            SYSTEM_INFO info {};
            ::GetSystemInfo (&info);
            return static_cast<double> (
                juce::jmax (1u, static_cast<unsigned int> (info.dwNumberOfProcessors)));
        }();
        const double elapsed100ns = static_cast<double> (now - priorWall) * 10000.0;
        const double value = 100.0 * static_cast<double> (consumed - priorCpu)
                             / (elapsed100ns * logicalCpus);
        return std::isfinite (value)
            ? juce::var (juce::jlimit (0.0, 100.0, value))
            : juce::var();
#else
        return {};
#endif
    }

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

    // INT-1B: preserve a parseable, bounded 1 Hz native health timeline.
    // The entire UI status contains large device/FX arrays and can exceed the
    // diagnostic worker's 4096-byte record; silently truncated JSON makes
    // offline p95/p99 profiling impossible. Reuse the EXISTING status read,
    // only on the message thread, with no new timer or audio callback work.
    static juce::var makeAudioHealthRecord (const juce::var& state,
        const armix::diagnostics::CrackleEvidenceWindow::MemoryPressure& memory)
    {
        auto compact = std::make_unique<juce::DynamicObject>();
        compact->setProperty ("schema", "arsonmix.audio-health.int1b.v1");
        for (const char* key : {
                 "connected", "liveBackendClass", "device", "driver",
                 "inputDevice", "outputDevice", "deviceInputChannels",
                 "deviceOutputChannels", "activeInputMaskHex",
                 "activeOutputMaskHex", "rate", "buffer", "periodUs",
                 "deviceGeneration",
                 "deviceStartCount", "deviceStopCount", "diagnosticRoute",
                 "productionRoutingState", "directRouteAuthorized",
                 "dryTest", "nativeGlobalFxBypassed",
                 "nativeFxInstalledCount", "nativeFxProcessingCount",
                 "nativeFxCountProvenance", "nativeFxGraphRevision",
                 "nativeExternalCh1Leased", "nativeExternalCh1InstalledCount",
                 "nativeExternalCh1ProcessingCount",
                 "nativeFxActivityComplete",
                 "callbackCount", "callbackTimingScope", "callbackBudgetBuckets",
                 "footerProcessCpuPercent", "footerCpuProvenance",
                 "callbackBudgetBucketProvenance", "callbackBudgetSamples",
                 "callbackP95BudgetUpperPct", "callbackP99BudgetUpperPct",
                 "slowCallbackCount", "overBudgetCount", "callbackGapCount",
                 "lateCallbackIntervals", "callbackCadenceClockRegressions",
                 "callbackCadenceComparedIntervals", "worstCallbackUs",
                 "renderPressureWindowValid", "renderPressureWithinThenOnTime", "renderPressureWithinThenLate",
                 "renderPressureOverThenOnTime", "renderPressureOverThenLate", "renderPressureProvenance",
                 "worstPluginUs", "slowPluginCount",
                 "nativeFxWorstUs", "nativeFxWorstName", "nativeFxTimingLeases",
                 "nativeFxWorkloadMeasuredLeases", "nativeFxWorkloadOverBudgetLeases",
                 "nativeFxWorkloadWorstPermille", "nativeFxWorkloadWorstName", "nativeFxWorkloadWorstInstance",
                 "nativeFxWorkloadLegacySampledLeases", "nativeFxWorkloadReviewCount",
                 "nativeFxWorkloadWorstSampledOnly",
                 "nativeFxWorkloadWindowLeases", "nativeFxWorkloadWindowOverBudgetLeases",
                 "nativeExactIdleCount", "nativeLatentInsertCount", "nativeLatentBypassRequestedCount",
                 "nativeMaxInsertLatencySamples", "nativeMaxInsertLatencyName",
                 "nativeLatencyPlanValid", "nativeLatencyPlanProvenance",
                 "nativeLatencyPlanAlignedSamples", "nativeLatencyPlanMainBeforeAlignment",
                 "nativeLatencyPlanMainAlignmentPad", "nativeLatencyPlanHasMonitor",
                 "nativeLatencyPlanMonitorBeforeAlignment", "nativeLatencyPlanMonitorAlignmentPad",
                 "nativeLatencyPlanMonitorPolicy", "nativeLatencyPlanMonitorOutputSamples",
                 "nativeLatencyPlanMonitorIndependentSaving",
                 "nativeLatencyPlanPotentialDirectMax", "nativeLatencyPlanPotentialDirectChannel",
                 "nativeLatencyPlanPotentialDirectTies", "nativeLatencyPlanPotentialDirectRaisesTarget",
                 "callbackModulo4Observed", "callbackModulo4OverBudget",
                 "masterAnalysisState", "masterAnalysisDroppedFramesLifetime", "masterAnalysisSegmentRestartsLifetime",
                 "backendXrunCount", "backendXrunMeaning",
                 "renderUnderrunSource", "secondaryOutputUnderflow",
                 "secondaryOutputOverflow", "secondaryOutputFrames"})
        {
            const auto value = state.getProperty (key, {});
            if (!value.isVoid())
                compact->setProperty (juce::Identifier (key), value);
        }
        if (memory.processAvailable) {
            compact->setProperty ("processWorkingSetMiB",static_cast<juce::int64>(memory.processWorkingSetMiB));
            compact->setProperty ("processPrivateCommitMiB",static_cast<juce::int64>(memory.processPrivateCommitMiB));
        }
        return juce::var (compact.release());
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

    void annotateRecovery (juce::var& state) const {
        armix::device::NativeDirectRestoreCoordinator::annotate(
            state,directRestore_,lastDirectRestoreError_,
            engine_.hasPendingDirectRestore(),monotonicMilliseconds());
    }

    // One control owner; recovery policy, telemetry and bounded retries live
    // in the dedicated Native coordinator. No change to ASIO callback path.
    void serviceDirectRestore() {
        if(pendingRetainedFxCapture_ || retainedOfflineFxActive_)return;
        armix::device::NativeDirectRestoreCoordinator::service(
            directRestore_,lastDirectRestoreError_,deviceSettingsDirty_,
            monotonicMilliseconds(),engine_,
            [this](const juce::String& code,const juce::String& detail) {
                diagnostics_.record(code,detail);
            },
            [this](const juce::String& message,bool error) {
                emitNotice(message,error);
            });
    }

    void emitNotice (const juce::String& message, bool error = false)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("text", message);
        o->setProperty ("error", error);
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible ("arMixNotice", juce::var (o.release()));
    }

    void emitRoutingFxEditorReceipt(const juce::var& args,bool opened,
                                    bool retryable,const juce::String& error) {
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("ok",opened);
        ack->setProperty("retryable",!opened&&retryable);
        ack->setProperty("error",error);
        ack->setProperty("instanceId",args.getProperty("instanceId",{}));
        ack->setProperty("requestId",args.getProperty("requestId",juce::var()));
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixRoutingFxEditorAck",juce::var(ack.release()));
        if(!opened&&args.getProperty("requestId",juce::var()).isVoid())
            emitNotice(error,true);
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

    void synchronizeOpenImmerSIBPower() {
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
        // The editor must reflect accepted global/rack/slot PCM state even
        // when the gesture originated in the WebView header or FX Rack.
        // All these dispatches run on JUCE's UI/message thread.
        if(immersibEditorWindow_)
            if(auto* ui=dynamic_cast<armix::builtin_fx::ImerSipP92Editor*>(
                  immersibEditorWindow_->getContentComponent()))
                ui->synchronizeHostPower();
#endif
    }
    void closePluginEditor() noexcept {
        // Dispose the native UI BEFORE dropping the graph/processor lease.
        editorWindow_.reset();
        editorProcessor_=nullptr;
        routedEditorOwner_.reset();
        routedEditorEpoch_=0;
    }

    void closeImmerSIBEditor() noexcept {
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
        immersibEditorWindow_.reset(); // JUCE editor disposed before PCM owner
        immersibEditorProcessor_=nullptr;
        immersibEditorOwner_.reset();
        immersibEditorEpoch_=0;
#endif
    }
    void closeAllPluginEditors() noexcept {
        closePluginEditor();
        closeImmerSIBEditor();
        floatingBuiltinFx_.clear(); // owned WebViews before PCM engine cleanup
        // DO NOT disable browser_.onNativeEvent here: this is a session-reset
        // helper used throughout the application's lifetime, not a destructor.
        // Each additional window's own destructor releases JUCE UI before
        // its vendor lease, all on the native message thread.
        additionalFxEditors_.clear();
    }

    // Close affected floating editors BEFORE a validated rack topology edit.
    // Slot-relative WebView leases become stale even for shifted neighbours.
    // Vendor editors are matched by their immutable instance token, not slot.
    void closeEditorsForNativeRackEdit(const juce::var& command) {
        const auto op=command.getProperty("operation",{}).toString();
        if(op!="NATIVE_MOVE_INSERT" && op!="NATIVE_REMOVE_INSERT")return;
        const auto payload=command.getProperty("payload",{});
        const auto token=payload.getProperty("instanceId",{}).toString();
        const auto from=command.getProperty("targetChannelId",{}).toString();
        const auto to=payload.getProperty("toChannelId",{}).toString();
        if(token.isEmpty()||from.isEmpty())return;
        std::erase_if(floatingBuiltinFx_,[&](const auto& item){
            return item && (item->channelId==from ||
                (to.isNotEmpty()&&item->channelId==to));
        });
        if(routedEditorOwner_ && routedEditorOwner_->instanceId==token)
            closePluginEditor();
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
        if(immersibEditorOwner_ && immersibEditorOwner_->instanceId==token)
            closeImmerSIBEditor();
#endif
        std::erase_if(additionalFxEditors_,[&](const auto& item){
            return item && item->owner && item->owner->instanceId==token;
        });
    }

    // FUX-1: only exact native published ChannelId/slot/instance can create
    // a separate WEBVIEW editor HWND. The six FX asset modules are the same
    // original /preview/ modules, not a second mixer or a JS DSP implementation.
    bool floatingBuiltinPresent(const FloatingBuiltinOwner* raw) const noexcept {
        return std::any_of(floatingBuiltinFx_.begin(),floatingBuiltinFx_.end(),
            [raw](const auto& x){return x.get()==raw;});
    }
    void emitFloatingBuiltinOpen(const FloatingBuiltinOwner& owner) {
        if(!owner.window || !owner.window->ready())return;
        auto args=std::make_unique<juce::DynamicObject>();
        args->setProperty("snapshot",approvedNativeSnapshot());
        args->setProperty("channelId",owner.channelId);
        args->setProperty("instanceId",owner.instanceId);
        args->setProperty("catalogId",owner.catalogId);
        args->setProperty("slot",owner.slot);
        args->setProperty("workspaceToken",owner.workspaceToken);
        args->setProperty("rekeyFrom",owner.rekeyFrom);
        owner.window->emit("arMixFloatingOpen",juce::var(args.release()));
    }
    void openFloatingBuiltin(const juce::var& args) {
        const auto channel=args.getProperty("channelId",{}).toString();
        const auto instance=args.getProperty("instanceId",{}).toString();
        const auto catalog=args.getProperty("catalogId",{}).toString();
        const auto rawSlot=args.getProperty("slot",{});
        const int slot=rawSlot.isInt()?static_cast<int>(rawSlot):-1;
        const auto workspace=args.getProperty("workspaceToken","").toString();
        // Explicitly allowlist the six existing original DOM editors; other
        // built-ins providing JUCE editors use PluginEditorWindow instead.
        const juce::StringArray editors{
            "armix.builtin.channel-eq.v1",
            "armix.builtin.channel-compressor.v1",
            "armix.builtin.auto-ducker.v1",
            "armix.builtin.graphic-eq31.v1",
            "armix.builtin.arson-harmony.v1",
            "armix.builtin.vocal-throw.v1",
            "armix.builtin.8d-sonku.v1"
        };
        const auto current=approvedNativeSnapshot();
        const auto physical=current.getProperty("graph",{});
        const auto saved=current.getProperty("logicalMixer",{});
        const auto session=current.getProperty("mixerSession",{});
        const auto device=current.getProperty("device",{});
        const bool offline=workspace.isNotEmpty() &&
            saved.getProperty("schema","").toString()=="arsonmix.native.logical-mixer.v1" &&
            saved.getProperty("authority","").toString()=="LOGICAL_ONLY" &&
            saved.getProperty("physicalOutputAdmitted",true)==juce::var(false) &&
            saved.getProperty("outputArmed",true)==juce::var(false) &&
            saved.getProperty("workspaceToken","").toString()==workspace &&
            saved.getProperty("revision",{}).toString()==
                args.getProperty("graphRevision",{}).toString();
        const bool live=workspace.isEmpty() &&
            session.getProperty("authority","").toString()=="CHANNEL_ID" &&
            physical.getProperty("capabilities",{})
                .getProperty("callbackOwnsChannelIds",false)==juce::var(true) &&
            physical.getProperty("bindingState","").toString()!="NEEDS_REMAP" &&
            device.getProperty("epoch",{}).toString()==
                args.getProperty("deviceEpoch",{}).toString() &&
            physical.getProperty("revision",{}).toString()==
                args.getProperty("graphRevision",{}).toString();
        const auto graph=offline?saved.getProperty("graph",{}):physical;
        bool matched=false;
        if(slot>=0&&slot<LiveDryRoutingLab::maxFxPerLane &&
           channel.isNotEmpty()&&instance.isNotEmpty()&&
           editors.contains(catalog)&&(offline||live)) {
            const auto rows=graph.getProperty("channels",{});
            if(const auto* list=rows.getArray())
                for(const auto& row:*list) {
                    if(row.getProperty("id",{}).toString()!=channel)continue;
                    const auto inserts=row.getProperty("inserts",{});
                    if(const auto* fx=inserts.getArray();fx&&slot<fx->size()) {
                        const auto insert=(*fx)[slot];
                        matched=insert.getProperty("id",{}).toString()==instance &&
                            insert.getProperty("catalogId","").toString()==catalog;
                    }
                    break;
                }
        }
        if(!matched) {
            emitNotice("FX editor needs a current saved/live insert · refresh FX Rack",true);
            return;
        }
        // Native top-level owned HWNDs are independent and physically free to
        // leave mixer bounds, unlike any CSS fixed/absolute element in WebView.
        for(const auto& held:floatingBuiltinFx_)
            if(held && held->channelId==channel && held->slot==slot &&
               held->instanceId==instance && held->catalogId==catalog &&
               held->workspaceToken==workspace) {
                held->window->present(static_cast<bool>(
                    args.getProperty("recenter",false)));
                emitFloatingBuiltinOpen(*held);
                return; // no duplicate WebView or processor for repeated clicks
            }
        if(floatingBuiltinFx_.size()>=maxSimultaneousBuiltinWindows) {
            // Closed built-in editor HWNDs have no processor ownership.
            // Recycle the first hidden WebView before rejecting a new FX
            // rack click; preserve all OPEN user editors and any DSP state.
            const auto hidden=std::find_if(floatingBuiltinFx_.begin(),
                floatingBuiltinFx_.end(),[](const auto& item) {
                    return item && item->window && !item->window->isVisible();
                });
            if(hidden!=floatingBuiltinFx_.end())
                floatingBuiltinFx_.erase(hidden);
            else {
                emitNotice("Close an unused built-in FX editor before opening another",true);
                return;
            }
        }
        auto owned=std::make_unique<FloatingBuiltinOwner>();
        owned->channelId=channel;
        owned->instanceId=instance;
        owned->catalogId=catalog;
        owned->workspaceToken=workspace;
        owned->slot=slot;
        const auto* ref=owned.get();
        const auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
        const auto onAction=[safe,ref](juce::var request) {
            if(!safe || !safe->floatingBuiltinPresent(ref))return;
            // Popup may only issue CONTROL intents for its exact open insert.
            if(request.getProperty("channelId",{}).toString()!=ref->channelId ||
               request.getProperty("instanceId",{}).toString()!=ref->instanceId ||
               request.getProperty("slot",{})!=juce::var(ref->slot))return;
            const auto action=request.getProperty("action","").toString();
            // A popup never receives a GENERAL native control capability.
            // Each catalog can request only its own original approved FX
            // snapshot/edit/preset commands, never Audio Setup, routing,
            // WIRE, mixer session, device or another insert's processor.
            juce::StringArray permitted;
            if(ref->catalogId=="armix.builtin.channel-eq.v1")
                permitted={"nativeEqSnapshot","nativeEqSetBand","nativeBuiltinFxUserPreset"};
            else if(ref->catalogId=="armix.builtin.graphic-eq31.v1")
                permitted={"nativeGeqSnapshot","nativeGeqSet","nativeBuiltinFxUserPreset"};
            else if(ref->catalogId=="armix.builtin.channel-compressor.v1")
                permitted={"nativeCompSnapshot","nativeCompSet","nativeBuiltinFxUserPreset"};
            else if(ref->catalogId=="armix.builtin.auto-ducker.v1")
                permitted={"nativeDuckerSnapshot","nativeDuckerSet","nativeDuckerSources"};
            else if(ref->catalogId=="armix.builtin.arson-harmony.v1")
                permitted={"nativeHarmonySnapshot","nativeHarmonySetSettings",
                           "nativeHarmonyUserPreset"};
            else if(ref->catalogId=="armix.builtin.vocal-throw.v1")
                permitted={"nativeThrowSnapshot","nativeThrowSet","nativeThrowTrigger",
                           "nativeThrowTempoSource","nativeBuiltinFxUserPreset"};
            else if(ref->catalogId=="armix.builtin.8d-sonku.v1")
                permitted={"nativeSonKuSnapshot","nativeSonKuSet"};
            if(!permitted.contains(action))return;
            if(action=="nativeBuiltinFxUserPreset" &&
               request.getProperty("catalogId","").toString()!=ref->catalogId)
                return;
            safe->onAction(request); // existing single native DSP request fence
        };
        const auto onPower=[safe,ref](juce::var request) {
            if(!safe||!safe->floatingBuiltinPresent(ref))return;
            if(request.getProperty("channelId",{}).toString()!=ref->channelId ||
               request.getProperty("instanceId",{}).toString()!=ref->instanceId ||
               request.getProperty("catalogId",{}).toString()!=ref->catalogId ||
               request.getProperty("slot",{})!=juce::var(ref->slot))return;
            const auto source=safe->approvedNativeSnapshot();
            const auto logical=request.getProperty("logical",false)==juce::var(true);
            const auto actualGraph=logical?
                source.getProperty("logicalMixer",{}).getProperty("graph",{}):
                source.getProperty("graph",{});
            const auto expectedRevision=logical?
                source.getProperty("logicalMixer",{}).getProperty("revision",{}):
                actualGraph.getProperty("revision",{});
            if(expectedRevision.toString()!=
                   request.getProperty("graphRevision",{}).toString() ||
               source.getProperty("device",{}).getProperty("epoch",{}).toString()!=
                   request.getProperty("deviceEpoch",{}).toString() ||
               !request.getProperty("value",{}).isBool())return;
            auto outgoing=std::make_unique<juce::DynamicObject>();
            const auto id=request.getProperty("requestId","").toString();
            if(!id.startsWith("fx-window-")||id.length()>128)return;
            outgoing->setProperty("requestId",logical?"logical-"+id:id);
            outgoing->setProperty("channelId",ref->channelId);
            outgoing->setProperty("slot",ref->slot);
            outgoing->setProperty("value",request.getProperty("value",{}));
            const auto gate=request.getProperty("gate","slot").toString();
            if(gate=="rack"||gate=="global") {
                // Explicit GEQ popup controls may clear a verified upstream
                // bypass, never silently toggle global/rack along with slot.
                // Existing native CONTROL handlers still enforce epoch/revision.
                if(logical||request.getProperty("value",{})!=juce::var(false)||
                   ref->catalogId!="armix.builtin.graphic-eq31.v1")return;
                outgoing->setProperty("requestId",
                    gate=="global"?"global-fx-"+id:id);
                outgoing->setProperty("action",gate=="global"?
                    "routingGlobalFxBypass":"routingNativeFxRackBypass");
                outgoing->setProperty("deviceEpoch",
                    request.getProperty("deviceEpoch",{}));
                outgoing->setProperty("revision",expectedRevision);
                safe->onAction(juce::var(outgoing.release()));
                return;
            }
            if(gate!="slot")return;
            if(logical) {
                if(request.getProperty("workspaceToken","").toString()!=ref->workspaceToken ||
                   ref->workspaceToken.isEmpty())return;
                // Logical FX policy consumes payload.slot + payload.enabled,
                // not an untyped top-level value. The verified popup is the
                // *only* source of this insert identity and target bypass.
                auto payload=std::make_unique<juce::DynamicObject>();
                payload->setProperty("slot",ref->slot);
                payload->setProperty("enabled",request.getProperty("value",{}));
                outgoing->setProperty("payload",juce::var(payload.release()));
                outgoing->setProperty("insertId",ref->instanceId);
                outgoing->setProperty("operation","SET_FX_SLOT_BYPASS");
                outgoing->setProperty("workspaceToken",ref->workspaceToken);
                outgoing->setProperty("baseRevision",expectedRevision);
                safe->handleLogicalMixerIntent(juce::var(outgoing.release()));
            } else {
                if(ref->workspaceToken.isNotEmpty())return;
                outgoing->setProperty("action","routingNativeFxInsertBypass");
                outgoing->setProperty("deviceEpoch",request.getProperty("deviceEpoch",{}));
                outgoing->setProperty("revision",expectedRevision);
                outgoing->setProperty("insertId",ref->instanceId);
                safe->onAction(juce::var(outgoing.release()));
            }
        };
        const auto ready=[safe,ref] {
            if(safe && safe->floatingBuiltinPresent(ref))
                safe->emitFloatingBuiltinOpen(*ref);
        };
        owned->window=std::make_unique<BuiltinFxDesktopWindow>(
            *this,"ArSonMix FX · "+catalog,catalog,std::move(onAction),
            std::move(onPower),std::move(ready));
        floatingBuiltinFx_.push_back(std::move(owned));
        floatingBuiltinFx_.back()->window->present(true); // first open centred
        emitFloatingBuiltinOpen(*floatingBuiltinFx_.back());
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

    // Stable *stored FX* identity is the lifetime authority; a logical
    // graph revision may advance for an unrelated fader without replacing
    // the processor. The incoming click's exact instance token is verified
    // separately by logicalFxTarget() against the current workspace.
    bool logicalVendorSourceCurrent(const LogicalVendorEditor& fx) const {
        const auto& workspace=logicalMixer_.workspace();
        const auto* row=workspace.document().roster.find(fx.insert.channel);
        const auto* current=workspace.storedFx(fx.insert.channel,fx.insert.slot);
        return row && fx.insert.slot<row->insertCatalogIds.size() &&
            row->insertCatalogIds[fx.insert.slot]==fx.insert.catalogId.toStdString() &&
            ((current==nullptr && fx.insert.sha256.isEmpty()) ||
             (current!=nullptr && current->catalogId==fx.insert.catalogId &&
              current->sha256==fx.insert.sha256 &&
              current->uid==fx.insert.uid && current->path==fx.insert.path));
    }

    // Shared host-owned library, for both exact retained live inserts and
    // CONTROL-owned logical editors. No disk/JSON inside audio callbacks.
    void bindReverbUserPresets(armix::builtin_fx::VocalReverbEditor& editor,
        std::function<armix::builtin_fx::VocalReverbProcessor*()> current,
        bool detached) {
        using R=armix::builtin_fx::VocalReverbProcessor;
        using S=armix::builtin_fx::VocalReverbSettings;
        using Store=armix::storage::BuiltInFxUserPresetStore;
        constexpr auto catalog=R::catalogIdentity;
        editor.bindUserPresetService(
            [this,current,catalog] {
                std::vector<armix::builtin_fx::VocalReverbEditor::UserPresetEntry> result;
                if(current())for(const auto& item:builtinFxUserPresets_.enumerate(catalog))
                    result.push_back({item.id,item.name});
                return result;
            },
            [this,current,detached,catalog](const juce::String& name) {
                auto* fx=current();
                if(!fx||!fx->isReady()||!Store::safeText(name,80)) {
                    emitNotice("Reverb-Sis preset name or FX lease invalid",true);
                    return juce::String{};
                }
                juce::MemoryBlock bytes;
                if(detached) {
                    if(!fx->captureDetachedState(bytes)) {
                        emitNotice("Reverb-Sis settings not ready to save",true);
                        return juce::String{};
                    }
                } else fx->getStateInformation(bytes);
                S snapshot;
                if(!R::decodeState(bytes.getData(),static_cast<int>(bytes.getSize()),snapshot)) {
                    emitNotice("Reverb-Sis: changes awaiting audio confirmation; try Save again",true);
                    return juce::String{};
                }
                juce::var settings(new juce::DynamicObject());
                juce::var values(juce::Array<juce::var>{});
                for(float x:snapshot.values)values.append(static_cast<double>(x));
                settings.getDynamicObject()->setProperty("algorithm",snapshot.algorithm);
                if(snapshot.voicing)settings.getDynamicObject()->setProperty("voicing",snapshot.voicing);
                settings.getDynamicObject()->setProperty("values",values);
                armix::storage::BuiltInFxUserPreset entry;
                entry.catalogId=catalog;entry.category="My Presets";entry.name=name;
                entry.settings=std::move(settings);
                const auto error=builtinFxUserPresets_.saveNew(entry);
                if(error.isNotEmpty()) {
                    emitNotice("Reverb-Sis preset not saved: "+error,true);
                    return juce::String{};
                }
                emitNotice("Reverb-Sis preset saved: "+name);
                return entry.id;
            },
            [this,current,detached,catalog](const juce::String& id) {
                auto* fx=current();
                if(!fx||!fx->isReady()||!Store::safeId(id))return false;
                armix::storage::BuiltInFxUserPreset entry;
                const auto error=builtinFxUserPresets_.read(catalog,id,entry);
                if(error.isNotEmpty()||!Store::validateSettings(catalog,entry.settings)) {
                    emitNotice("Reverb-Sis preset rejected: "+error,true);
                    return false;
                }
                S next;next.preset=-1;
                next.algorithm=static_cast<int>(entry.settings.getProperty("algorithm",{}));
                next.voicing=static_cast<int>(entry.settings.getProperty("voicing",0));
                const auto values=entry.settings.getProperty("values",{});
                for(std::size_t i=0;i<next.values.size();++i)
                    next.values[i]=static_cast<float>(static_cast<double>((*values.getArray())[static_cast<int>(i)]));
                if(!armix::builtin_fx::validVocalReverbSettings(next)||!fx->queueSettings(next))return false;
                if(detached) {
                    juce::MemoryBlock accepted;
                    return fx->captureDetachedState(accepted);
                }
                // Pending intent only: the editor's existing tooltip reports
                // actual callback ACK, never a synthetic success notification.
                return true;
            });
    }

    // Resolve current channel by the actual accepted processor pointer, not
    // stale window slot coordinates. The popup captures source epoch/revision
    // and revalidates them before changing the one shared monitor.
    std::uint64_t currentDelayChannel(juce::AudioProcessor* target) const {
        std::uint64_t id=0;
        engine_.forEachNativeProcessor([&](armix::mixer::ChannelId ch,juce::AudioProcessor* p){
            if(p==target)id=ch.value;
        });
        return id;
    }
    void bindDelayMusicSources(armix::builtin_fx::vocal_delay::SingerEditor& editor,
        std::function<bool()> access,juce::AudioProcessor* target) {
        using Source=armix::builtin_fx::vocal_delay::SingerEditor::MusicSource;
        editor.bindMusicSources([this,access,target]{
            std::vector<Source> result;
            if(!access()||deviceApply_.busy())return result;
            const auto own=currentDelayChannel(target);
            if(!own)return result;
            const auto snap=approvedNativeSnapshot();
            const auto graph=snap.getProperty("graph",{});
            std::uint64_t ep=0,rev=0;
            if(!armix::console::approvedNativeId(snap.getProperty("device",{}).getProperty("epoch",{}),ep)||
               !armix::console::approvedNativeId(graph.getProperty("revision",{}),rev))return result;
            const auto rows=graph.getProperty("channels",{});
            if(const auto* items=rows.getArray())for(const auto& row:*items){
                std::uint64_t id=0;const auto role=row.getProperty("role","").toString();
                if(!armix::console::approvedNativeId(row.getProperty("id",{}),id)||id==own||
                   (role!="INPUT"&&role!="GROUP"&&role!="FX_RETURN"))continue;
                auto name=row.getProperty("name","").toString();
                if(name.isEmpty())name="Channel "+juce::String(std::to_string(id));
                result.push_back({id,ep,rev,name+" (shared)"});
            }
            return result;
        },[this,access,target](const Source& source){
            if(!access()||deviceApply_.busy())return false;
            const auto own=currentDelayChannel(target);
            if(!own||!source.id||source.id==own)return false;
            // Host revalidates scheduled channel, epoch and revision.
            const bool ok=engine_.selectNativeMusicTempoSource(source.epoch,source.revision,
                armix::mixer::ChannelId{source.id});
            if(ok)serviceNativeMusicTempo();
            else emitNotice("Music source changed or unavailable; reopen the source menu",true);
            return ok;
        });
    }

    void bindDelayUserPresets(armix::builtin_fx::vocal_delay::SingerEditor& editor,
        std::function<armix::builtin_fx::vocal_delay::JuceVocalDelayProcessor*()> current) {
        using namespace armix::builtin_fx::vocal_delay;
        using Store=armix::storage::BuiltInFxUserPresetStore;
        constexpr auto catalog=JuceVocalDelayProcessor::catalogIdentity;
        editor.bindUserPresetService(
            [this,current,catalog]{
                std::vector<SingerEditor::UserPresetEntry> result;
                if(current())for(const auto& item:builtinFxUserPresets_.enumerate(catalog))
                    result.push_back({item.id,item.name});
                return result;
            },
            [this,current,catalog](const juce::String& name){
                auto* fx=current();
                if(!fx||!fx->stateSnapshotQuiescent()||!Store::safeText(name,80)){
                    emitNotice("Vocal Delay: wait for settings confirmation or enter a valid name",true);
                    return juce::String{};
                }
                armix::storage::BuiltInFxUserPreset entry;
                entry.catalogId=catalog;entry.category="My Presets";entry.name=name;
                entry.settings=PortablePreset::encode(fx->acceptedSnapshot().accepted);
                const auto error=builtinFxUserPresets_.saveNew(entry);
                if(error.isNotEmpty()){emitNotice("Vocal Delay preset not saved: "+error,true);return juce::String{};}
                emitNotice("Vocal Delay preset saved: "+name);return entry.id;
            },
            [this,current,catalog](const juce::String& id)->std::optional<Parameters>{
                auto* fx=current();if(!fx||!Store::safeId(id))return {};
                armix::storage::BuiltInFxUserPreset entry;
                const auto error=builtinFxUserPresets_.read(catalog,id,entry);
                auto p=fx->singerDisplay();
                if(error.isNotEmpty()||!PortablePreset::decode(entry.settings,p)){
                    emitNotice("Vocal Delay preset rejected: "+error,true);return {};
                }
                return p;
            });
    }

    void openLogicalVendorEditor(const juce::var& request) {
        std::uint64_t channel=0;
        std::size_t slot=0;
        if(!logicalFxTarget(request,channel,slot)) {
            emitNotice("FX editor unavailable: the selected FX slot changed",true);
            return;
        }
        const auto requestedInstance=request.getProperty("instanceId",{}).toString();
        if(logicalVendorEditor_) {
            auto& existing=*logicalVendorEditor_;
            if(existing.workspaceToken==logicalMixer_.workspace().workspaceToken() &&
               existing.insert.channel==armix::mixer::ChannelId{channel} &&
               existing.insert.slot==slot && logicalVendorSourceCurrent(existing)) {
                // Revisions from unrelated fader changes produce a new
                // logical click token but NOT a new underlying FX instance.
                existing.instanceId=requestedInstance;
                if(existing.window) {
                    if(static_cast<bool>(request.getProperty("recenter",false)))
                        existing.window->present(true);
                    else existing.window->present();
                    if(!existing.window->presentationConfirmed())
                        emitNotice("FX editor window could not be presented",true);
                }
                return; // Exact same logical FX: refocus without losing state.
            }
            emitNotice("Finish the currently open FX editor first",true);
            return;
        }
        if(mixerSessionWorker_.busy() || logicalMixer_.readOnly()) {
            emitNotice("Finish the current mixer save before opening FX",true);
            return;
        }
        LogicalVendorEditor prepared;
        auto error=prepareLogicalFx(logicalMixer_.workspace(),{channel},slot,prepared.insert,prepared.processor);
        if(error.isEmpty()) {
            try {
                if(auto editor=armix::ui::createHostedAudioEditor(*prepared.processor)) {
                    const juce::Component::SafePointer<armix::builtin_fx::vocal_delay::SingerEditor> preparedDelay(
                        dynamic_cast<armix::builtin_fx::vocal_delay::SingerEditor*>(editor.get()));
                    const juce::Component::SafePointer<armix::builtin_fx::VocalReverbEditor> preparedReverb(
                        dynamic_cast<armix::builtin_fx::VocalReverbEditor*>(editor.get()));
                    prepared.window=std::make_unique<PluginEditorWindow>(
                        prepared.processor->getName(),std::move(editor),*this);
                    if(static_cast<bool>(request.getProperty("recenter",false)))
                        prepared.window->present(true); // first-open must also centre on mixer
                    prepared.workspaceToken=logicalMixer_.workspace().workspaceToken();
                    prepared.instanceId=requestedInstance;
                    const bool visible=prepared.window->presentationConfirmed();
                    logicalVendorEditor_=std::move(prepared);
                    if(auto* delay=preparedDelay.getComponent()) {
                        auto* identity=logicalVendorEditor_->processor.get();
                        const auto access=[this,identity]{return logicalVendorEditor_&&
                            logicalVendorEditor_->processor.get()==identity&&
                            logicalVendorEditor_->workspaceToken==logicalMixer_.workspace().workspaceToken()&&
                            !mixerSessionWorker_.busy()&&!logicalMixer_.readOnly()&&
                            !studioBackupBusy_&&!nativeShowBusy_&&logicalVendorSourceCurrent(*logicalVendorEditor_);};
                        delay->bindHostAccess(access);
                        bindDelayUserPresets(*delay,[identity,access]{
                            return access()?dynamic_cast<armix::builtin_fx::vocal_delay::JuceVocalDelayProcessor*>(identity):nullptr;
                        });
                    }
                    if(auto* reverb=preparedReverb.getComponent()) {
                        auto* identity=logicalVendorEditor_->processor.get();
                        bindReverbUserPresets(*reverb,[this,identity]() -> armix::builtin_fx::VocalReverbProcessor* {
                            if(!logicalVendorEditor_||logicalVendorEditor_->processor.get()!=identity||
                               mixerSessionWorker_.busy()||logicalMixer_.readOnly()||
                               studioBackupBusy_||nativeShowBusy_||
                               !logicalVendorSourceCurrent(*logicalVendorEditor_))return nullptr;
                            return dynamic_cast<armix::builtin_fx::VocalReverbProcessor*>(identity);
                        },true);
                    }
                    if(!visible)emitNotice("FX editor window could not be presented",true);
                    return;
                }
                error="This FX does not provide a vendor editor";
            } catch(...) {
                error="Vendor editor could not be opened";
            }
        }
        emitNotice("FX editor unavailable: "+error,true);
    }

    void saveClosedLogicalVendorEditor(bool shutdown=false) {
        if(!logicalVendorEditor_ || mixerSessionWorker_.busy() ||
           (!shutdown && logicalVendorEditor_->window->isVisible()))return;
        auto& fx=*logicalVendorEditor_;
        if(fx.workspaceToken!=logicalMixer_.workspace().workspaceToken()) {
            logicalVendorEditor_.reset();
            return;
        }
        // Snapshot-and-compare the original logical FX identity before
        // persisting vendor state. Unrelated fader edits are harmless, but
        // replacing an FX or rewriting its saved blob must reject stale writes.
        if(!logicalVendorSourceCurrent(fx)) {
            emitNotice("FX slot or stored state changed: close/reopen its editor before saving",true);
            if(!shutdown)fx.window->setVisible(true);
            return;
        }
        juce::MemoryBlock bytes;
        try {
            if(auto* reverb=dynamic_cast<armix::builtin_fx::VocalReverbProcessor*>(
                    fx.processor.get())) {
                // Logical-only processor is not a PCM owner. Never persist
                // an empty Reverb-Sis blob; restore requires a valid packet.
                if(!reverb->captureDetachedState(bytes)) {
                    emitNotice("Reverb-Sis settings not captured; previous saved state retained",true);
                    if(!shutdown)fx.window->setVisible(true);
                    return;
                }
            } else if(auto* delay=dynamic_cast<armix::builtin_fx::vocal_delay::JuceVocalDelayProcessor*>(fx.processor.get())) {
                delay->getStateInformation(bytes);
                if(!delay->isDetached()||bytes.getSize()!=48) {
                    emitNotice("Vocal Delay settings not captured; previous saved state retained",true);
                    if(!shutdown)fx.window->setVisible(true);
                    return;
                }
            } else fx.processor->getStateInformation(bytes);
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
            for(const auto* key:{"requestId","channelId","deviceEpoch","graphRevision","instanceId","slot","workspaceToken"})
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
                const bool session=supplied.getProperty("scope","").toString()=="SESSION";
                const auto sessionId=session && mixerSessionId_.isEmpty()
                    ? juce::Uuid().toDashedString().toLowerCase() : mixerSessionId_;
                auto plan=armix::hotkey::NativeHotkeyCommandPlan::planRegistration(
                    supplied,session?approvedNativeSnapshot():juce::var{},sessionId);
                error=plan.error;
                if(error.isEmpty() && session) {
                    if(mixerSessionId_.isEmpty())mixerSessionId_=sessionId;
                    if(hotkeys_.activeSession()!=mixerSessionId_)
                        error=hotkeys_.activateSession(mixerSessionId_);
                }
                // VST3 ParamID ownership stays in its specialized service.
                if(error.isEmpty()&&plan.mapping.operation.startsWith("SET_FX_PARAM_")&&
                   !fxParameterHotkeyAdmitted(plan.mapping))
                    error="HOTKEY_PARAMETER_ID_OR_INSTANCE_UNAVAILABLE";
                if(error.isEmpty())error=hotkeys_.upsert(std::move(plan.mapping));
            } else error="UNSUPPORTED_HOTKEY_OPERATION";
        }
        sendHotkeyRegistry(requestId,error);
    }

    void fireNativeHotkey(const armix::hotkey::Mapping& mapping) {
        if(mapping.operation=="PANIC_MUTE") {
            // PANIC is explicit user intent, unlike device stop/restart.
            operatorOutputMuted_=true;
            engine_.setMonitorEnabled(false);
            engine_.cancelAsioResetMonitorIntent();
            autoMainEpoch_=0;
            emitApprovedNativeSnapshot();
            return;
        }
        // Existing control-thread guards, single native dispatcher and FX
        // ParamID service remain authoritative. The planner has no side effects.
        if(mapping.scope!="SESSION" || mapping.sessionId!=mixerSessionId_ ||
           deviceApply_.busy() || nativeShowBusy_ || studioBackupBusy_ ||
           engine_.isGraphSwitchPending())return;
        const auto plan=armix::hotkey::NativeHotkeyCommandPlan::dispatch(
            mapping,approvedNativeSnapshot(),mixerSessionId_,true,
            firedHotkeySerial_+1);
        using Kind=armix::hotkey::NativeHotkeyCommandPlan::Kind;
        switch(plan.kind) {
            case Kind::parameter:
                (void)applyFxParameterHotkey(mapping);
                break;
            case Kind::action:
                ++firedHotkeySerial_;
                onAction(plan.request); // same FX dispatcher as GUI
                break;
            case Kind::intent:
                ++firedHotkeySerial_;
                handleApprovedNativeControl(plan.request); // one PCM intent owner
                break;
            case Kind::ignore: break;
        }
    }

    // Explicit retained-session FX editor handoff. A silent device STOP does
    // not give an old HWND authority to mutate the accepted PCM processor.
    // Capture its exact quiescent binary state into the existing logical
    // document, then edit an isolated control-side processor. The user must
    // explicitly re-apply the saved .smix when hardware returns; no auto ARM.
    void emitRetainedFxOfflineAck(const juce::var& requestId,const juce::String& error) {
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("ok",error.isEmpty());
        ack->setProperty("error",error);
        ack->setProperty("requestId",requestId);
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixRetainedFxOfflineAck",juce::var(ack.release()));
        if(error.isEmpty())emitApprovedNativeSnapshot();
        else emitNotice("Offline FX unavailable: "+error,true);
    }
    void scheduleRetainedFxCaptureStep() {
        juce::Timer::callAfterDelay(1,[safe=juce::Component::SafePointer<ConsoleWindow>(this)] {
            if(safe!=nullptr)safe->serviceRetainedFxCapture();
        });
    }
    void serviceRetainedFxCapture() {
        if(!pendingRetainedFxCapture_)return;
        auto lease=deviceApply_.tryBegin(
            armix::device::NativeDeviceApplyCoordinator::Purpose::offlineFxCapture);
        if(!lease){scheduleRetainedFxCaptureStep();return;}
        auto& pending=*pendingRetainedFxCapture_;
        bool done=false;
        juce::String error;
        if(monotonicMilliseconds()-pending.startedMs>=8000)
            error="Offline FX capture timed out; retry the rack";
        else if(mixerSessionWorker_.busy() || nativeShowBusy_ || studioBackupBusy_)
            error="Mixer transaction changed; retry offline FX";
        else error=engine_.stepRetainedMixerCapture(pending.capture,done);
        if(error.isEmpty()&&!done){scheduleRetainedFxCaptureStep();return;}
        auto requestId=pending.requestId;
        const auto elapsed=monotonicMilliseconds()-pending.startedMs;
        if(error.isEmpty() && elapsed>=8000)
            error="Offline FX capture timed out; retry the rack";
        if(error.isEmpty()) {
            auto& doc=pending.capture.document;
            doc.id=mixerSessionId_.isNotEmpty()?mixerSessionId_:
                logicalMixer_.workspace().document().id;
            doc.resumeMain=pending.resumeMain && !operatorOutputMuted_;
            doc.savedMs=juce::Time::currentTimeMillis();
            if(hotkeys_.activeSession()==doc.id)doc.hotkeys=hotkeys_.sessionEntries();
            error=resolveMixerSessionCatalog(doc,true);
            if(error.isEmpty() &&
               !logicalMixer_.restoreCheckpoint(std::move(doc),std::move(pending.capture.states)))
                error="Retained FX document could not be validated";
            if(error.isEmpty()) {
                retainedOfflineFxActive_=true;
                retainedOfflineEpoch_=pending.epoch;
                retainedOfflineRevision_=pending.revision;
                retainedOfflineOwnerEpoch_=pending.ownerEpoch;
                retainedOfflineLastActivityMs_=monotonicMilliseconds();
                offlineAutoAttemptEpoch_=offlineAutoAttemptRevision_=0;
                offlineAutoAttemptWorkspace_.clear();
                mixerSessionRecoveryPending_=true;
                mixerSessionStatus_="RETAINED_OFFLINE_FX_REAPPLY_REQUIRED";
            }
        }
        pendingRetainedFxCapture_.reset();
        diagnostics_.record("OFFLINE_FX_CAPTURE_MS",juce::String(static_cast<juce::int64>(elapsed)));
        emitRetainedFxOfflineAck(requestId,error);
    }

    void openRetainedFxOffline(const juce::var& request) {
        if(pendingRetainedFxCapture_)
            return emitRetainedFxOfflineAck(request.getProperty("requestId",{}),
                "Offline FX workspace is already preparing");
        juce::String error;
        const auto source=approvedNativeSnapshot();
        const auto graph=source.getProperty("graph",{});
        const auto device=source.getProperty("device",{});
        const auto session=source.getProperty("mixerSession",{});
        const auto rawSlot=request.getProperty("slot",{});
        const int slot=rawSlot.isInt()?static_cast<int>(rawSlot):-1;
        std::uint64_t channel=0,epoch=0,revision=0;
        const bool identity=armix::console::approvedNativeId(
                request.getProperty("channelId",{}),channel)&&
            armix::console::approvedNativeId(request.getProperty("deviceEpoch",{}),epoch)&&
            armix::console::approvedNativeId(request.getProperty("graphRevision",{}),revision)&&
            slot>=0&&slot<LiveDryRoutingLab::maxFxPerLane&&
            engine_.hasRetainedNativeSession()&&
            !engine_.isGraphSwitchPending()&&!deviceApply_.busy()&&
            !nativeShowBusy_&&!studioBackupBusy_&&!mixerSessionWorker_.busy()&&
            !logicalVendorEditor_&&
            graph.getProperty("bindingState","").toString()=="NEEDS_REMAP"&&
            session.getProperty("authority","").toString()=="CHANNEL_ID"&&
            session.getProperty("deviceEpoch",{}).toString()==juce::String(epoch)&&
            device.getProperty("epoch",{}).toString()==juce::String(epoch)&&
            graph.getProperty("revision",{}).toString()==juce::String(revision)&&
            engine_.retainedNativeEpoch()!=0;
        bool sameInsert=false;
        if(identity) {
            const auto rows=graph.getProperty("channels",{});
            if(const auto* list=rows.getArray())for(const auto& row:*list) {
                if(row.getProperty("id",{}).toString()!=juce::String(channel))continue;
                const auto inserts=row.getProperty("inserts",{});
                if(const auto* fx=inserts.getArray();fx && slot==fx->size() &&
                   request.getProperty("instanceId",{}).toString().isEmpty()) {
                    sameInsert=true; // exact next empty slot, opening editable workspace only
                } else if(fx && slot<fx->size()) {
                    const auto effect=(*fx)[slot];
                    sameInsert=effect.isObject() &&
                        effect.getProperty("id",{}).toString()==
                            request.getProperty("instanceId",{}).toString() &&
                        effect.getProperty("catalogId","").toString().isNotEmpty();
                }
            }
        }
        if(!identity||!sameInsert)error="Retained FX instance changed; refresh the rack";
        else if(retainedOfflineFxActive_ &&
                (retainedOfflineOwnerEpoch_!=engine_.retainedNativeEpoch() ||
                 retainedOfflineEpoch_!=epoch || retainedOfflineRevision_!=revision)) {
            error="A different retained session is pending offline FX recovery";
        } else if(!retainedOfflineFxActive_) {
            const bool resumeMain=engine_.wantsMainContinuation() && !operatorOutputMuted_;
            engine_.setOutputMuted(true);
            if(!engine_.allOutputsDisarmed())
                error="Disarm secondary output before editing retained FX";
            else {
                auto lease=deviceApply_.tryBegin(
                    armix::device::NativeDeviceApplyCoordinator::Purpose::offlineFxCapture);
                if(!lease)return emitRetainedFxOfflineAck(request.getProperty("requestId",{}),
                    "Audio device transaction in progress");
                closeAllPluginEditors();
                PendingRetainedFxCapture pending;
                pending.requestId=request.getProperty("requestId",{});
                pending.epoch=epoch;pending.revision=revision;
                pending.ownerEpoch=engine_.retainedNativeEpoch();
                pending.resumeMain=resumeMain;
                pending.startedMs=monotonicMilliseconds();
                error=engine_.beginRetainedMixerCapture(pending.capture);
                if(error.isEmpty() &&
                   (pending.capture.observedEpoch!=epoch ||
                    pending.capture.ownerEpoch!=pending.ownerEpoch))
                    error="Audio connection changed; retry offline FX";
                if(error.isEmpty()) {
                    pendingRetainedFxCapture_.emplace(std::move(pending));
                    scheduleRetainedFxCaptureStep();
                    return; // ACK only after complete validated workspace
                }
            }
        }
        emitRetainedFxOfflineAck(request.getProperty("requestId",{}),error);
    }

    // Once the logical .smix store has COMMITTED an exact popup Power intent,
    // the revision-scoped logical instance ID changes. Rebind ONLY that same
    // owner from a successful correlated ACK; a same-catalog replacement,
    // browser guess, stale revision or unrelated mixer edit cannot steal it.
    void rekeyFloatingLogicalPower(const juce::var& request,const juce::var& ack) {
        const auto requestId=request.getProperty("requestId","").toString();
        if(request.getProperty("operation","").toString()!="SET_FX_SLOT_BYPASS" ||
           !requestId.startsWith("logical-fx-window-") ||
           ack.getProperty("requestId","").toString()!=requestId ||
           ack.getProperty("status","").toString()!="ACCEPTED")
            return;
        const auto channel=request.getProperty("channelId","").toString();
        const auto oldId=request.getProperty("insertId","").toString();
        const auto workspace=request.getProperty("workspaceToken","").toString();
        const auto rawSlot=request.getProperty("slot",{});
        const int slot=rawSlot.isInt()?static_cast<int>(rawSlot):-1;
        const auto acceptedRevision=ack.getProperty("revision","").toString();
        const auto saved=logicalMixer_.readback();
        if(channel.isEmpty()||oldId.isEmpty()||workspace.isEmpty()||
           slot<0||slot>=LiveDryRoutingLab::maxFxPerLane ||
           saved.getProperty("workspaceToken","").toString()!=workspace ||
           saved.getProperty("revision","").toString()!=acceptedRevision ||
           saved.getProperty("authority","").toString()!="LOGICAL_ONLY")
            return;
        const auto rows=saved.getProperty("graph",{}).getProperty("channels",{});
        // Hold a COPY of the accepted readback insert. A pointer into the
        // temporary row.getProperty("inserts") juce::var would dangle after
        // leaving this loop and could rekey the wrong processor.
        juce::var replacement;
        if(const auto* channels=rows.getArray())
            for(const auto& row:*channels) {
                if(row.getProperty("id","").toString()!=channel)continue;
                const auto inserts=row.getProperty("inserts",{});
                if(const auto* fx=inserts.getArray();fx&&slot<fx->size())
                    replacement=(*fx)[slot];
                break;
            }
        if(!replacement.isObject())return;
        const auto newId=replacement.getProperty("id","").toString();
        if(newId.isEmpty()||newId==oldId)return;
        for(auto& owner:floatingBuiltinFx_) {
            if(!owner||!owner->window || owner->channelId!=channel ||
               owner->slot!=slot || owner->workspaceToken!=workspace ||
               owner->instanceId!=oldId ||
               owner->catalogId!=replacement.getProperty("catalogId","").toString())
                continue;
            owner->rekeyFrom=oldId;
            owner->instanceId=newId;
            emitFloatingBuiltinOpen(*owner); // original /preview/ FX, no new HWND
            break;
        }
    }

    void handleLogicalMixerIntent(const juce::var& request) {
        if(retainedOfflineFxActive_)retainedOfflineLastActivityMs_=monotonicMilliseconds();
        // Durable logical workspace edits never grant PCM admission.
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
        },[safe=juce::Component::SafePointer<ConsoleWindow>(this),
           origin=request](juce::var ack) {
            if(safe==nullptr)return;
            // Commit callback runs on CONTROL after durable .smix replacement.
            // ACK is required before changing any owned popup identity.
            safe->rekeyFloatingLogicalPower(origin,ack);
            if(safe->browser_.loaded)
                safe->browser_.emitEventIfBrowserIsVisible("arMixLogicalAck",std::move(ack));
        });
    }

    bool canCreateOfflineSession() {
        auto* audio=engine_.devices().getCurrentAudioDevice();
        return (!audio || !audio->isOpen()) && engine_.allOutputsDisarmed() &&
            !engine_.wantsMainContinuation() && !engine_.hasRetainedNativeSession() &&
            !engine_.hasBoundMixerRoute() && !engine_.isGraphSwitchPending() &&
            !engine_.independentClockActive() && !engine_.hasTemporaryDiagnosticDevice() &&
            !engine_.hasPendingDirectRestore() && !sessionLifecycleShuttingDown_ &&
            !pendingRetainedFxCapture_ && !retainedOfflineFxActive_ &&
            !logicalVendorEditor_ && !logicalMixer_.readOnly() &&
            !mixerSessionWorker_.busy() && !deviceApply_.busy() && !nativeShowBusy_ &&
            !studioBackupBusy_ && !catalog_.busy() && !mixerFileDialogOpen_ && !namedIoFileChooser_;
    }

    // New Project with the currently admitted device still open. The exact
    // current mixer is archived and read back before replacing any graph.
    // No audio device is reopened and no output is automatically armed.
    bool canCreateLiveSession() {
        auto* audio=engine_.devices().getCurrentAudioDevice();
        return audio && audio->isOpen() && engine_.directRouteAuthorized() &&
            engine_.hasBoundMixerRoute() && !engine_.hasRetainedUnboundMixer() &&
            !engine_.isGraphSwitchPending() && !engine_.independentClockActive() &&
            !engine_.hasTemporaryDiagnosticDevice() && !engine_.hasPendingDirectRestore() &&
            !sessionLifecycleShuttingDown_ && !mixerSessionRecoveryPending_ &&
            !pendingRetainedFxCapture_ && !retainedOfflineFxActive_ &&
            !logicalVendorEditor_ && !logicalMixer_.readOnly() &&
            !mixerSessionWorker_.busy() && !deviceApply_.busy() && !nativeShowBusy_ &&
            !studioBackupBusy_ && !catalog_.busy() && !mixerFileDialogOpen_ && !namedIoFileChooser_ && !fxRackFileChooser_;
    }
    bool liveNewSessionLeaseMatches(const juce::var& args) {
        const auto snapshot=approvedNativeSnapshot();
        const auto device=snapshot.getProperty("device",{});
        const auto graph=snapshot.getProperty("graph",{});
        return graph.getProperty("capabilities",{}).getProperty("callbackOwnsChannelIds",false)==juce::var(true) &&
            args.getProperty("deviceEpoch","").toString().isNotEmpty() &&
            args.getProperty("deviceEpoch","").toString()==device.getProperty("epoch","").toString() &&
            args.getProperty("graphRevision","").toString()==graph.getProperty("revision","").toString() &&
            args.getProperty("rosterRevision","").toString()==graph.getProperty("rosterRevision","").toString();
    }
    void createLiveSession(const juce::var& args) {
        bool confirmed=false;
        if(!armix::session::strictBool(args.getProperty("confirmed",{}),confirmed)||!confirmed||
           !canCreateLiveSession()||!liveNewSessionLeaseMatches(args))
            return emitMixerSessionAck("NEW_LIVE","Finish current operations and refresh the admitted mixer");
        // Explicit New Project is an operator-authorized stop, not a temporary
        // capture mute. Never resume the old output intent on failure.
        autoMainEpoch_=0;operatorOutputMuted_=true;
        engine_.setMonitorEnabled(false);
        engine_.cancelAsioResetMonitorIntent();
        engine_.disconnectSecondaryOutput();
        closeAllPluginEditors();stopOrbit();
        engine_.playerStop();
        playerCurrent_=-1;playerPendingIndex_=-1;playerOutputPendingIndex_=-1;
        playerPendingQueueFile_={};playerLoopCurrent_=false;playerAutoAdvanceGate_.rearm();
        emitPlayerState();
        armix::session::MixerSessionDocument previous;
        std::vector<armix::shows::BinaryState> states;
        auto error=engine_.captureNativeMixerSession(previous,states);
        if(error.isNotEmpty())return emitMixerSessionAck("NEW_LIVE",error+" · mixer retained, output muted");
        previous.id=mixerSessionId_.isNotEmpty()?mixerSessionId_:juce::Uuid().toDashedString().toLowerCase();
        previous.resumeMain.reset();
        if(hotkeys_.activeSession()==previous.id)previous.hotkeys=hotkeys_.sessionEntries();
        error=resolveMixerSessionCatalog(previous,true);
        if(error.isNotEmpty())return emitMixerSessionAck("NEW_LIVE",error);
        previous.savedMs=1;
        const auto expected=armix::session::Codec::encode(previous);
        const auto archive=armix::session::NativeMixerSessionStore(settingsDirectory()).archiveDirectory()
            .getChildFile(previous.id+"-"+juce::Uuid().toDashedString()+".smix");
        const auto oldId=previous.id;
        auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
        nativeShowBusy_=true;
        if(!mixerSessionWorker_.save(settingsDirectory(),std::move(previous),std::move(states),
            [safe,args,expected,oldId](juce::String failure){
                if(safe==nullptr||safe->sessionLifecycleShuttingDown_)return;
                auto& self=*safe;
                auto reject=[&](const juce::String& reason){
                    self.nativeShowBusy_=false;
                    self.emitApprovedNativeSnapshot();
                    self.emitMixerSessionAck("NEW_LIVE",reason);
                };
                if(failure.isNotEmpty())return reject(failure+" · previous mixer retained");
                if(!self.liveNewSessionLeaseMatches(args)||!self.engine_.allOutputsDisarmed()||
                   self.engine_.isGraphSwitchPending()||self.deviceApply_.busy())
                    return reject("Mixer changed during archive; retry New Project");
                // Even scalar edits that leave the graph revision unchanged
                // must not be discarded. Re-capture and compare exact state.
                armix::session::MixerSessionDocument current;
                std::vector<armix::shows::BinaryState> currentStates;
                auto error=self.engine_.captureNativeMixerSession(current,currentStates);
                if(error.isNotEmpty())return reject(error);
                current.id=oldId;current.savedMs=1;current.resumeMain.reset();
                if(self.hotkeys_.activeSession()==oldId)current.hotkeys=self.hotkeys_.sessionEntries();
                error=self.resolveMixerSessionCatalog(current,true);
                if(error.isNotEmpty())return reject(error);
                if(armix::session::Codec::encode(current)!=expected)
                    return reject("Mixer or plugin state changed during archive; retry New Project");
                armix::session::NativeLogicalMixerWorkspace fresh;
                auto doc=fresh.document();
                doc.resumeMain=false;
                const auto newId=doc.id;
                error=self.engine_.restoreNativeMixerSession(std::move(doc),{},true);
                if(error.isNotEmpty())return reject(error+" · previous project archived");
                ++self.workspaceGeneration_; // graph committed even if checkpoint fails
                self.mixerSessionId_=newId;
                self.lastMixerFile_=juce::File{};
                self.lastMixerSessionFingerprint_.clear();
                self.mixerSessionRecoveryPending_=false;
                const auto keyError=self.hotkeys_.activateSession(newId);
                if(keyError.isNotEmpty())self.emitNotice("Hotkey activation: "+keyError,true);
                self.mixerSessionStatus_="NEW_LIVE_SESSION_MUTED";
                self.mixerSessionError_.clear();
                self.nativeShowBusy_=false;
                self.emitApprovedNativeSnapshot();
                // Reuse existing checkpoint worker/admission. The prior exact
                // project remains in SessionArchive even if autosave fails.
                error=self.saveMixerSession(false);
                self.emitMixerSessionAck("NEW_LIVE",error.isEmpty()?juce::String{}:
                    "New mixer active and muted; checkpoint could not start: "+error);
            },archive,armix::session::NativeMixerSessionStore::ArchiveMode::none,true)){
            nativeShowBusy_=false;
            emitMixerSessionAck("NEW_LIVE","Session archive worker unavailable; previous mixer retained");
        }
        emitApprovedNativeSnapshot();
    }

    void createOfflineSession(const juce::var& args) {
        ensureLogicalWorkspace();
        bool confirmed=false;
        std::uint64_t revision=0;
        const auto mode=args.getProperty("mode","").toString();
        const bool reset=mode=="RESET_IO";
        const juce::String operation=reset?"RESET_IO":"NEW";
        const auto token=args.getProperty("workspaceToken","").toString();
        if(!armix::session::strictBool(args.getProperty("confirmed",{}),confirmed)||!confirmed||
           !armix::session::parseDecimal(args.getProperty("baseRevision",{}),revision,true)||
           (mode!="EMPTY"&&mode!="DEFAULT"&&!reset)||!canCreateOfflineSession())
            return emitMixerSessionAck(operation,"Disconnect audio and finish current mixer operations first");
        if(mode!="EMPTY"&&(ioDefaults_.readOnly()||ioDefaults_.layout().empty()))
            return emitMixerSessionAck(operation,"No valid saved I/O defaults");
        auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
        auto complete=[safe,reset,operation](juce::String error){
            if(safe==nullptr || safe->sessionLifecycleShuttingDown_)return;
            if(error.isEmpty()){
                safe->autoMainEpoch_=0;
                safe->operatorOutputMuted_=true;
                safe->closeAllPluginEditors();
                safe->stopOrbit();
                if(!reset){
                    safe->lastMixerFile_=juce::File{};
                    const auto keyError=safe->hotkeys_.activateSession(safe->logicalMixer_.workspace().document().id);
                    if(keyError.isNotEmpty())safe->emitNotice("Hotkey activation: "+keyError,true);
                }
                ++safe->workspaceGeneration_; // committed new logical workspace or I/O reset
                safe->mixerSessionStatus_=reset?"OFFLINE_IO_RESET_MUTED":"NEW_LOGICAL_SESSION_MUTED";
                safe->emitApprovedNativeSnapshot();
            }
            safe->emitMixerSessionAck(operation,error);
        };
        const bool queued=reset?
            logicalMixer_.resetIo(token,revision,ioDefaults_.layout(),std::move(complete)):
            logicalMixer_.newSession(token,revision,mode=="DEFAULT"?&ioDefaults_.layout():nullptr,std::move(complete));
        if(!queued)emitMixerSessionAck(operation,"Mixer session IO worker unavailable");
    }

    bool canInstallIoDefaults() const {
        const auto session=settingsDirectory().getChildFile("MixerSession");
        // Never replace a checkpoint, including a damaged HEAD or orphaned
        // PREVIOUS. No default application during Open/recovery/device reset.
        return !session.getChildFile("HEAD").exists()&&!session.getChildFile("PREVIOUS").exists()&&
            !mixerSessionRecoveryPending_&&!mixerSessionWorker_.busy()&&!nativeShowBusy_&&!studioBackupBusy_&&
            !retainedOfflineFxActive_&&!deviceApply_.busy()&&engine_.hasPristineStartupMixer()&&engine_.allOutputsDisarmed();
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
        if(retainedOfflineFxActive_ && engine_.hasRetainedNativeSession() &&
           engine_.retainedNativeEpoch()==retainedOfflineOwnerEpoch_ &&
           engine_.allOutputsDisarmed() && authority=="CHANNEL_ID" &&
           accepted.getProperty("graph",{}).getProperty("bindingState",{}).toString()=="NEEDS_REMAP") {
            // UI projection ONLY. The physical NEEDS_REMAP graph remains
            // retained and output-disarmed, not a second PCM authority.
            auto offline=logicalMixer_.readback();
            offline.getDynamicObject()->setProperty("retainedOffline",true);
            // Current callback envelope is distinct from the retained owner.
            offline.getDynamicObject()->setProperty("retainedDeviceEpoch",
                accepted.getProperty("device",{}).getProperty("epoch",{}));
            offline.getDynamicObject()->setProperty("retainedGraphRevision",
                accepted.getProperty("graph",{}).getProperty("revision",{}));
            offline.getDynamicObject()->setProperty("retainedCaptureDeviceEpoch",
                juce::String(retainedOfflineEpoch_));
            offline.getDynamicObject()->setProperty("retainedCaptureGraphRevision",
                juce::String(retainedOfflineRevision_));
            // Preserve supported offline edits; physical output stays disarmed.
            accepted.getDynamicObject()->setProperty("logicalMixer",std::move(offline));
        } else if(!static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",false)) &&
                  authority!="CHANNEL_ID" && authority!="M4" &&
                  !engine_.hasRetainedNativeSession()) {
            // Separate field: physical graph remains truthful/unadmitted.
            accepted.getDynamicObject()->setProperty("logicalMixer",logicalMixer_.readback());
        }
        if(auto* obj=accepted.getDynamicObject()) {
            // PCM-authoritative global A/B readback; not a mirrored button state.
            obj->setProperty("globalFxBypassed",engine_.globalFxBypassed());
            obj->setProperty("workspaceGeneration",juce::String(std::to_string(workspaceGeneration_)));
            obj->setProperty("retainedOfflineFxPending",retainedOfflineFxActive_);
            obj->setProperty("operatorOutputMuted",operatorOutputMuted_);
            obj->setProperty("channelAppearance",channelAppearance_.snapshot(accepted.getProperty("graph",{})));
            obj->setProperty("waitingAsioInputs",live.getProperty("waitingAsioInputs",0));
            obj->setProperty("externalOutputs",
                live.getProperty("dryRoutingLab",{}).getProperty("externalOutputs",{}));
            obj->setProperty("externalRevision",
                live.getProperty("dryRoutingLab",{}).getProperty("nativeBus",{})
                    .getProperty("graphRevision",{}));
            obj->setProperty("secondaryMainOnly",live.getProperty("dryRoutingLab",{})
                .getProperty("secondaryMainOnly",false));
            obj->setProperty("physicalMainOutputs",
                live.getProperty("dryRoutingLab",{}).getProperty("mainOutputs",{}));
            obj->setProperty("physicalMonitorOutputs",
                live.getProperty("dryRoutingLab",{}).getProperty("monitorOutputs",{}));
            obj->setProperty("logicalInputRegistry",
                live.getProperty("dryRoutingLab",{}).getProperty("nativeBus",{})
                    .getProperty("logicalInputRegistry",{}));
            obj->setProperty("logicalOutputRegistry",
                live.getProperty("dryRoutingLab",{}).getProperty("nativeBus",{})
                    .getProperty("logicalOutputRegistry",{}));
            obj->setProperty("ioDefaults",ioDefaults_.snapshot(canInstallIoDefaults()));
            const bool offlineSessionAvailable=canCreateOfflineSession();
            obj->setProperty("offlineNewSessionAvailable",offlineSessionAvailable);
            obj->setProperty("liveNewSessionAvailable",canCreateLiveSession());
            obj->setProperty("offlineResetIoAvailable",offlineSessionAvailable&&logicalMixer_.canResetIo()&&
                !ioDefaults_.readOnly()&&!ioDefaults_.layout().empty());
            obj->setProperty("physicalBusPatchSupported",true);
            obj->setProperty("physicalBusOutputObserved",
                live.getProperty("dryRoutingLab",{}).getProperty("busOutputObserved",false));
            obj->setProperty("physicalBusSecondaryArmed",engine_.secondaryArmed());
            const bool streamReady=bool(live.getProperty("secondaryOutputReady",false))&&
                !bool(live.getProperty("secondaryOutputFault",false));
            obj->setProperty("secondaryOutputFault",live.getProperty("secondaryOutputFault",false));
            obj->setProperty("secondaryOutputName",live.getProperty("secondaryOutputName",""));
            obj->setProperty("secondaryOutputRecovering",secondaryRecoveryPending());
            obj->setProperty("secondaryOutputRecoveryName",secondaryRecoveryName());
            obj->setProperty("secondaryMainReady",streamReady);
            obj->setProperty("secondaryMainEndpoint",streamReady?
                live.getProperty("secondaryOutputName",{}):juce::var(""));
            // Accepted plan, not requested browser state. Graph revision
            // callback readback is a separate condition for UI completion.
            if(bool(live.getProperty("nativeLatencyPlanValid",false))&&
               bool(live.getProperty("nativeLatencyPlanHasMonitor",false))){
                auto plan=std::make_unique<juce::DynamicObject>();
                plan->setProperty("policy",live.getProperty("nativeLatencyPlanMonitorPolicy",{}));
                plan->setProperty("mainSamples",live.getProperty("nativeLatencyPlanAlignedSamples",{}));
                plan->setProperty("monitorSamples",live.getProperty("nativeLatencyPlanMonitorOutputSamples",{}));
                plan->setProperty("potentialSaving",live.getProperty("nativeLatencyPlanMonitorIndependentSaving",{}));
                obj->setProperty("monitorTiming",juce::var(plan.release()));
            }
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
            } else if(command.getProperty("operation","").toString()=="NATIVE_CONVERT_REVERB_RETURN"){
                std::uint64_t ep=0,rev=0,roster=0,target=0;
                const auto args=command.getProperty("payload",{});
                const int slot=static_cast<int>(args.getProperty("slot",-1));
                if(nativeShowBusy_||studioBackupBusy_||slot<0||slot>=8||
                   !armix::console::approvedNativeId(command.getProperty("deviceEpoch",{}),ep)||
                   !armix::console::approvedNativeId(command.getProperty("baseGraphRevision",{}),rev)||
                   !armix::console::approvedNativeId(command.getProperty("baseRosterRevision",{}),roster)||
                   !armix::console::approvedNativeId(command.getProperty("targetChannelId",{}),target))
                    nativeError="DMX_CONVERSION_INVALID_OR_BUSY";
                else{
                    const auto result=engine_.convertNativeReverbReturn(ep,rev,roster,
                        armix::mixer::ChannelId{target},static_cast<std::size_t>(slot),
                        args.getProperty("instanceId",{}).toString());
                    applied=result.accepted;
                    if(applied)closeAllPluginEditors();
                    else nativeError=result.error;
                }
            } else {
                closeEditorsForNativeRackEdit(command);
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
        else if (name == "masterMute" && value.isBool()) {
            engine_.setMasterMuted (bool (value));
            if (!bool(value)) feedbackMasterTripNotified_=false;
        }
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
        const auto active=currentDeviceSnapshot();
        inventory->setProperty("active",active);
        inventory->setProperty("namedPatches",namedPatches_.snapshot(active));
        inventory->setProperty("scanState",scanState);
        inventory->setProperty("scanDurationMs",static_cast<double>(elapsed));
        if(explicitRescan)
            diagnostics_.record("DEVICE_REFRESH",
                scanState+" | "+juce::String(static_cast<juce::int64>(elapsed))+" ms");
        if(browser_.loaded)
            browser_.emitEventIfBrowserIsVisible("arMixAudioDeviceInventory",
                juce::var(inventory.release()));
    }


    // One-shot OS + JUCE native first-run preflight. This is observation,
    // not a device replacement or output-arm authority. The decision is
    // sourced from both Core Audio IDs and JUCE exact selectable names;
    // it never guesses "speakers" from a JavaScript label. No vendor scans.
    bool firstRunInventoryInspected_ = false;
    void inspectFirstRunAudio()
    {
#if defined(_WIN32)
        if (firstRunInventoryInspected_) return;
        firstRunInventoryInspected_ = true;
        auto& manager = engine_.devices();
        std::vector<std::wstring> captures, renders;
        for (auto* type : manager.getAvailableDeviceTypes())
        {
            if (type == nullptr || type->getTypeName() != "Windows Audio") continue;
            for (const auto& label : type->getDeviceNames(true))
                captures.emplace_back(label.toWideCharPointer());
            for (const auto& label : type->getDeviceNames(false))
                renders.emplace_back(label.toWideCharPointer());
            break;
        }
        const auto result = armix::device::WindowsFirstRunEndpointDiscovery::inspect(
            captures, renders);
        const auto& d = result.decision;
        auto proposal = std::make_unique<juce::DynamicObject>();
        proposal->setProperty("inspected", result.inspected);
        proposal->setProperty("state", static_cast<int>(d.outcome));
        // A proposed candidate is not an open stream or an audible route.
        proposal->setProperty("captureName", juce::String(d.captureName.c_str()));
        proposal->setProperty("renderName", juce::String(d.renderName.c_str()));
        proposal->setProperty("mayAttemptApply", result.inspected && d.mayAttemptApply());
        proposal->setProperty("message", juce::String(result.diagnostic.c_str()));
        diagnostics_.record("FIRST_RUN_MMDEVICE_PROPOSAL",
            juce::String(result.diagnostic.c_str()) + " | state=" +
            juce::String(static_cast<int>(d.outcome)) + " | apply=" +
            (d.mayAttemptApply() ? "candidate-only" : "unsafe"));
        if (browser_.loaded)
            browser_.emitEventIfBrowserIsVisible(
                "arMixFirstRunAudioProposal", juce::var(proposal.release()));

        // Auto-select only for an untouched, no-profile, SOURCE NONE startup.
        // Hardware must have a provably unique MMDevice identity AND an exact
        // selectable JUCE label; the existing native device coordinator is
        // the sole backend-switch owner. No OS default changes or WebAudio.
        if (!result.inspected || !d.mayAttemptApply() ||
            !engine_.allOutputsDisarmed() || !engine_.hasPristineStartupMixer() ||
            operatorOutputMuted_ || deviceApply_.busy() ||
            nativeShowBusy_ || mixerSessionWorker_.busy()) return;
        const juce::String input (d.captureName.c_str());
        const juce::String output (d.renderName.c_str());
        auto binding = armix::device::WasapiEndpointIdentityGuard::Selection {
            d.captureName, d.renderName, d.captureId, d.renderId
        };
        auto renderBinding = armix::device::WasapiEndpointIdentityGuard::RenderSelection {
            d.renderName, d.renderId
        };
        juce::AudioDeviceManager::AudioDeviceSetup before;
        manager.getAudioDeviceSetup (before);
        auto* current = manager.getCurrentAudioDevice();
        const bool existing = manager.getCurrentAudioDeviceType() == "Windows Audio"
            && before.inputDeviceName == input && before.outputDeviceName == output
            && current != nullptr && current->isOpen();
        if (!existing)
        {
            auto cfg = std::make_unique<juce::DynamicObject>();
            cfg->setProperty ("backend", "Windows Audio");
            cfg->setProperty ("captureDevice", input);
            cfg->setProperty ("playbackDevice", output);
            cfg->setProperty ("sampleRate", 0.0);
            cfg->setProperty ("bufferSize", 0.0);
            cfg->setProperty ("assignSelectedPlaybackPinsToMain", false);
            cfg->setProperty ("confirmRetainedPinMapping", false);
            auto args = std::make_unique<juce::DynamicObject>();
            args->setProperty ("requestId", "AUTO_FIRST_RUN_MMDEVICE");
            args->setProperty ("expectedDeviceGeneration",
                engine_.status().getProperty ("deviceGeneration", 0));
            args->setProperty ("config", juce::var (cfg.release()));
            applyCustomAudioDevice (juce::var (args.release()));
        }
        juce::AudioDeviceManager::AudioDeviceSetup actual;
        manager.getAudioDeviceSetup (actual);
        auto* opened = manager.getCurrentAudioDevice();
        const bool active = manager.getCurrentAudioDeviceType() == "Windows Audio"
            && opened != nullptr && opened->isOpen()
            && actual.inputDeviceName == input && actual.outputDeviceName == output
            && opened->getActiveOutputChannels().countNumberOfSetBits() >= 2
            && engine_.directRouteAuthorized();
        if (!active)
        {
            diagnostics_.record ("FIRST_RUN_AUTO_DEVICE_REJECTED",
                "Native device transaction or full-duplex readback did not accept the proposed endpoint");
            return;
        }
        const auto idFault = d.hasCapture()
            ? armix::device::WasapiEndpointIdentityGuard::verify (
                binding, actual.inputDeviceName, actual.outputDeviceName)
            : armix::device::WasapiEndpointIdentityGuard::verifyRender (
                renderBinding, actual.outputDeviceName);
        if (idFault.isNotEmpty())
        {
            engine_.setOutputMuted (true);
            diagnostics_.record ("FIRST_RUN_ENDPOINT_ID_CHANGED", idFault);
            emitNotice ("Audio endpoint changed during setup; please choose speakers in AUDIO I/O.", true);
            return;
        }
        diagnostics_.record ("FIRST_RUN_AUDIO_DEVICE_ACCEPTED",
            input + " -> " + output + " | readback=VERIFIED");
        if (d.hasCapture() && engine_.hasPristineStartupMixer())
        {
            // One unedited CH01 can receive the actual two reported VB-CABLE
            // input pins. All other channels remain SOURCE NONE, and any
            // existing user-authored SOURCE ONLY/MAIN WIRE is untouched.
            const auto pins = opened->getActiveInputChannels();
            const int left = pins.findNextSetBit (0);
            const int right = left < 0 ? -1 : pins.findNextSetBit (left + 1);
            if (right < 0)
            {
                diagnostics_.record ("FIRST_RUN_SOURCE_DEFERRED",
                    "Expected 2 verified CABLE capture pins; existing graph retained silent");
                return;
            }
            const auto sourceError = engine_.switchDryRoutingLabSource (
                0, engine_.dryRoutingLabEpoch(), engine_.dryRoutingLabRevision(),
                "STEREO", left, right);
            if (sourceError.isNotEmpty())
            {
                diagnostics_.record ("FIRST_RUN_SOURCE_DEFERRED", sourceError);
                return;
            }
            requestAutomaticMain (true); // native real graph, output gate checks
            diagnostics_.record ("FIRST_RUN_SOURCE_CONNECTED",
                "Verified CABLE Output capture -> CH01 -> original MAIN, no test tone");
            emitApprovedNativeSnapshot();
        }
#endif
    }

    struct LoopbackJob {
        juce::String id, identity;
        int input = -1, output = -1, rate = 0;
        std::uint64_t epoch = 0;
        juce::uint32 started = 0;
        bool cancelled = false;
    };
    std::optional<LoopbackJob> loopbackJob_;
    std::future<LiveAudioEngine::Loopback::Result> loopbackAnalysis_;

    static juce::File loopbackCalibrationFile(const juce::String& identity)
    {
        const juce::SHA256 digest(identity.toRawUTF8(),identity.getNumBytesAsUTF8());
        return settingsDirectory().getChildFile("latency-calibration")
            .getChildFile(digest.toHexString()+".json");
    }

    void emitLoopback(const juce::String& id,const juce::String& state,
                      const juce::String& message={},const juce::var& result={})
    {
        if (!browser_.loaded) return;
        auto response=std::make_unique<juce::DynamicObject>();
        response->setProperty("requestId",id);
        response->setProperty("state",state);
        response->setProperty("message",message);
        response->setProperty("result",result);
        browser_.emitEventIfBrowserIsVisible("arMixPhysicalLoopback",juce::var(response.release()));
    }

    void cancelPhysicalLoopback()
    {
        if (!loopbackJob_) return;
        loopbackJob_->cancelled=true;
        engine_.cancelLoopback();
    }

    void requestPhysicalLoopback(const juce::var& args,bool measure)
    {
        const auto id=args.getProperty("requestId",{}).toString();
        if (id.isEmpty() || id.length()>64) return;
        if (loopbackJob_)
            return emitLoopback(id,"UNKNOWN","A round-trip measurement is already in progress.");
        const auto inputValue=args.getProperty("inputPin",-1);
        const auto outputValue=args.getProperty("outputPin",-1);
        const int input=static_cast<int>(inputValue), output=static_cast<int>(outputValue);
        if (input<0 || input>=64 || output<0 || output>=64 ||
            static_cast<double>(inputValue)!=input || static_cast<double>(outputValue)!=output)
            return emitLoopback(id,"UNKNOWN","Select an enabled line input and MAIN output.");
        const auto pins=engine_.inspectLoopback(input,output);
        const auto epoch=armix::console::nativeSessionTokenValue(args.getProperty("deviceEpoch",{}));
        if (!pins || pins->identity.isEmpty() || pins->epoch!=epoch)
            return emitLoopback(id,"UNKNOWN","The accepted ASIO device or pins changed. Reopen Audio I/O.");
        if (!measure) {
            const auto file=loopbackCalibrationFile(pins->identity);
            if (!file.existsAsFile() || file.getSize()>4096)
                return emitLoopback(id,"UNKNOWN");
            const auto saved=juce::JSON::parse(file);
            const int frames=static_cast<int>(saved.getProperty("frames",0));
            const double confidence=static_cast<double>(saved.getProperty("confidence",0.0));
            if (saved.getProperty("identity",{}).toString()!=pins->identity ||
                frames<=0 || frames>=pins->rate/2 ||
                !(confidence>=0.70 && confidence<=1.0))
                return emitLoopback(id,"UNKNOWN");
            return emitLoopback(id,"MEASURED_LOOPBACK","Saved wired calibration",saved);
        }
        const auto confirmed=args.getProperty("wiredLineLevel",false);
        bool started=false;
        try {
            if (confirmed.isBool() && static_cast<bool>(confirmed) && !nativeShowBusy_ && !studioBackupBusy_)
                started=engine_.beginLoopback(*pins);
        } catch (const std::bad_alloc&) {
            return emitLoopback(id,"UNKNOWN","Could not allocate the bounded capture buffer.");
        }
        if (!started)
            return emitLoopback(id,"UNKNOWN",
                "Use verified, already armed ASIO MAIN, an unused line input, and disarm secondary output.");
        loopbackJob_=LoopbackJob{id,pins->identity,input,output,pins->rate,pins->epoch,
                                 juce::Time::getMillisecondCounter(),false};
        emitLoopback(id,"CAPTURING","Measuring wired round-trip…");
    }

    void servicePhysicalLoopback()
    {
        using Probe=LiveAudioEngine::Loopback;
        if (!loopbackJob_) return;
        auto& job=*loopbackJob_;
        if (engine_.loopbackState()==Probe::State::capturing &&
            juce::Time::getMillisecondCounter()-job.started>3000u) {
            const bool reported=job.cancelled;
            cancelPhysicalLoopback();
            if (!reported)
                emitLoopback(job.id,"UNKNOWN","No stable ASIO callback. Measurement cancelled.");
            return;
        }
        const auto finish=[this,&job](const juce::String& state,const juce::String& message,
                                      const juce::var& value={}) {
            emitLoopback(job.id,state,message,value);
            loopbackJob_.reset();
        };
        if (engine_.loopbackState()==Probe::State::cancelled) {
            (void)engine_.takeLoopback();
            return finish("UNKNOWN","Measurement cancelled. Check output arm, stream and routing.");
        }
        try {
            if (engine_.loopbackState()==Probe::State::complete) {
                auto pcm=engine_.takeLoopback();
                if (job.cancelled) return finish("UNKNOWN","Measurement cancelled.");
                loopbackAnalysis_=std::async(std::launch::async,[pcm=std::move(pcm)] {
                    return Probe::analyse(pcm);
                });
            }
            if (!loopbackAnalysis_.valid() ||
                loopbackAnalysis_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)
                return;
            const auto result=loopbackAnalysis_.get();
            const auto current=engine_.inspectLoopback(job.input,job.output);
            if (job.cancelled || !current || current->epoch!=job.epoch ||
                current->identity!=job.identity)
                return finish("UNKNOWN","Device or pin configuration changed. Measure again.");
            if (result.error)
                return finish("UNKNOWN",result.error);
            auto record=std::make_unique<juce::DynamicObject>();
            record->setProperty("identity",job.identity);
            record->setProperty("provenance","MEASURED_LOOPBACK");
            record->setProperty("frames",result.frames);
            record->setProperty("rate",job.rate);
            record->setProperty("confidence",result.confidence);
            const juce::var saved(record.release());
            const auto file=loopbackCalibrationFile(job.identity);
            const bool written=file.getParentDirectory().createDirectory().wasOk() &&
                file.replaceWithText(juce::JSON::toString(saved,true));
            finish("MEASURED_LOOPBACK",written ?
                "Saved for these ASIO pins · FX excluded" : "Measured · calibration file could not be saved",saved);
        } catch (const std::exception&) {
            finish("UNKNOWN","Could not analyse the wired return.");
        }
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
        o->setProperty("audioPathCode",audioPathCode(live));
        o->setProperty("mainOutputs",live.getProperty("dryRoutingLab",{}).getProperty("mainOutputs",{}));
        o->setProperty("monitorOutputs",live.getProperty("dryRoutingLab",{}).getProperty("monitorOutputs",{}));
        // Read-only snapshot on explicit Audio I/O request. These counters
        // already exist in the engine; do not add RT work or a polling driver.
        for(const char* key: {"callbackCount","overBudgetCount","callbackGapCount",
             "backendXrunCount","backendXrunMeaning","callbackBudgetSamples",
             "callbackP99BudgetUpperPct","callbackP99BeyondDeadline"})
            o->setProperty(juce::Identifier(key),live.getProperty(juce::Identifier(key),{}));
        o->setProperty("bitDepth",device&&device->isOpen()?device->getCurrentBitDepth():0);
        // DAW-style AUDIO SETUP telemetry: JUCE's driver-provided input/output
        // latency hints in SAMPLES, plus the host's current admitted DSP delay.
        // The WebView must never present an untrusted draft as measured audio.
        const bool latencyReady = device != nullptr && device->isOpen() &&
            engine_.directRouteAuthorized();
        o->setProperty("driverInputLatencyFrames",latencyReady
            ? live.getProperty("driverReportedInputLatencyFrames",{}) : juce::var());
        o->setProperty("driverOutputLatencyFrames",latencyReady
            ? live.getProperty("driverReportedOutputLatencyFrames",{}) : juce::var());
        o->setProperty("hostFxLatencyFrames",latencyReady
            ? live.getProperty("latencySamples",{}) : juce::var());
        o->setProperty("latencyClockScope",
            bool (live.getProperty("adaptiveClockActive",false))
                ? "INDEPENDENT_CLOCKS_NO_DIRECT_ROUNDTRIP_ESTIMATE"
                : "SINGLE_CLOCK_DRIVER_HINTS");
        o->setProperty("latencyProvenance",
            "JUCE_DRIVER_REPORTED_NOT_MEASURED_LOOPBACK_OR_ASIO_RTL");
        juce::Array<juce::var> inputNames,outputNames,inputPins,outputPins,rates,buffers;
        // Driver capability is distinct from the enabled callback mask. Keep
        // total counts even beyond the current 64-pin device Apply contract.
        int reportedInputs=0,reportedOutputs=0;
        if(device!=nullptr&&device->isOpen()) {
            const auto inputs=device->getInputChannelNames();
            const auto outputs=device->getOutputChannelNames();
            const auto inputMask=device->getActiveInputChannels();
            const auto outputMask=device->getActiveOutputChannels();
            reportedInputs=inputs.size();
            reportedOutputs=outputs.size();
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
        o->setProperty("reportedInputCount",reportedInputs);
        o->setProperty("reportedOutputCount",reportedOutputs);
        o->setProperty("inputNames",juce::var(inputNames));
        o->setProperty("outputNames",juce::var(outputNames));
        o->setProperty("enabledInputs",juce::var(inputPins));
        o->setProperty("enabledOutputs",juce::var(outputPins));
        if (const auto loopback=engine_.inspectLoopback()) {
            juce::Array<juce::var> capture,playback;
            for (const auto pin : loopback->inputs) capture.add(pin);
            for (const auto pin : loopback->outputs) playback.add(pin);
            auto probe=std::make_unique<juce::DynamicObject>();
            probe->setProperty("inputs",juce::var(capture));
            probe->setProperty("outputs",juce::var(playback));
            probe->setProperty("deviceEpoch",static_cast<juce::int64>(loopback->epoch));
            o->setProperty("physicalLoopback",juce::var(probe.release()));
        }
        o->setProperty("rates",juce::var(rates));
        o->setProperty("buffers",juce::var(buffers));
        return juce::var(o.release());
    }

    // One-shot on a verified setup or startup, including the first input
    // patch. A later device epoch or a user MUTE cancels the pending arm.
    std::uint64_t autoMainEpoch_=0;
    bool autoMainRequiresInput_=true;
    // Operator's explicit OUTPUT MUTE must survive later input/FX/EXT edits.
    // This control-thread intent is NOT a new callback gate or device lock.
    bool operatorOutputMuted_=false;
    std::optional<std::uint64_t> autoMainCheckedRevision_;
    void serviceAutomaticMain() {
        if(vendorSafeRecoveryActive()){autoMainEpoch_=0;return;}
        if(engine_.hasRuntimeFxFault()){autoMainEpoch_=0;return;}
        if(pendingRetainedFxCapture_ || retainedOfflineFxActive_)return; // never arm stale retained PCM
        if(operatorOutputMuted_){autoMainEpoch_=0;return;} // explicit emergency stop
        // Only an explicit, verified CONTROL transaction may schedule MAIN
        // recovery. A generic desired flag cannot prove output-pair continuity.
        if(deviceApply_.busy() ||
           nativeShowBusy_ || studioBackupBusy_ || mixerSessionWorker_.busy() ||
           directCallbackStalled_ || engine_.hasPendingDirectRestore() ||
           engine_.diagnosticRoute()!=armix::diagnostics::Route::normal)return;
        if(autoMainEpoch_==0)return; // no unscheduled rearm
        if(!engine_.directRouteAuthorized() || engine_.hasRetainedUnboundMixer())return;
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
        // SOURCE ONLY/PLAYER -> confirmed EXTERNAL OUT is a complete hardware
        // route. Requiring a microphone as well would strand it at MONITOR OFF.
        // Only same-epoch, driver-verified pins qualify (not stale .smix text).
        const bool requireInput=autoMainRequiresInput_ &&
            !engine_.hasVerifiedExternalOutputIntent();
        const auto fault=engine_.liveOutputArmFault(requireInput);
        if(fault.isNotEmpty()) {
            protectKnownFeedbackLoop(fault);
            return;
        }
        autoMainCheckedRevision_=revision;
        engine_.setMonitorEnabled(true);
        autoMainEpoch_=0;
        diagnostics_.record("MAIN_AUTO_ENABLED","Verified device, input and MAIN output");
    }

    void requestAutomaticMain(bool requireInput=true) {
        if(vendorSafeRecoveryActive()){autoMainEpoch_=0;return;}
        if(engine_.hasRuntimeFxFault()){autoMainEpoch_=0;return;}
        if(operatorOutputMuted_){autoMainEpoch_=0;return;}
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
        if(pendingRetainedFxCapture_||nativeShowBusy_||studioBackupBusy_||mixerSessionWorker_.busy()||logicalVendorEditor_||
           engine_.isGraphSwitchPending()||
            static_cast<bool>(nativeRoute.getProperty("pending",false))||
            engine_.independentClockActive()||
            engine_.diagnosticRoute()!=armix::diagnostics::Route::normal)
            return send(false,"REJECTED",
                "Finish active Show/FX/diagnostic/clock transition first");
        using Apply=armix::device::NativeDeviceApplyCoordinator;
        const auto prepared=Apply::prepare(engine_.devices(),args.getProperty("config",{}));
        if(prepared.error.isNotEmpty())return send(false,"REJECTED",prepared.error);

        const auto& typeName=prepared.requested.backend;
        if(prepared.requested.confirmRetainedPinMapping) {
            if(engine_.secondaryArmed())return send(false,"NEEDS_REMAP",
                "Disconnect additional output before confirming a changed physical pin mapping.");
            if(retainedOfflineFxActive_)return send(false,"NEEDS_REMAP",
                "Offline mixer edits pending. Apply the original verified audio device first.");
            if(!prepared.canConfirmRetainedPins || !engine_.confirmRetainedWasapiPortMapping())
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

        const bool previousMixerBound=engine_.hasBoundMixerRoute();
        const bool previousMonitor=engine_.isMonitorEnabled();
        const auto streamIntent=engine_.captureSecondaryResumeIntent();
        const auto streamGeneration=secondaryRetry_.generation();
        // Apply is itself consent to the requested device transaction. Pause
        // the secondary sink inside the existing suspend hook, rather than
        // demanding a manual Stop/Apply/Enable sequence.
        const auto resumeStream=[&](bool rollback) {
            if(!streamIntent||streamGeneration!=secondaryRetry_.generation()||pendingSecondaryStop_)return;
            const auto& requested=prepared.requested;
            const bool sameAsio=prepared.previousBackend=="ASIO"&&
                (rollback||(requested.backend=="ASIO"&&
                    requested.capture==prepared.before.inputDeviceName&&
                    requested.playback==prepared.before.outputDeviceName));
            if(!sameAsio) {
                engine_.disconnectSecondaryOutput();
                emitNotice("Additional output paused after device change · choose Enable when ready",false);
                return;
            }
            if(!engine_.secondarySourceMatches(*streamIntent))return;
            pendingVendorStream_=PendingVendorStream{*streamIntent,engine_.dryRoutingLabEpoch(),
                engine_.outputArmRevision(),static_cast<std::uint64_t>(monotonicMilliseconds()+5000),
                previousMonitor&&!operatorOutputMuted_};
        };
        const auto start=monotonicMilliseconds();
        const auto result=deviceApply_.execute(engine_.devices(),prepared,{
            [this] {
                if(engine_.hasPendingDirectRestore() &&
                   !engine_.abandonPendingDirectRestoreForSetup())return false;
                autoMainEpoch_=0;
                return true;
            },
            [this] {engine_.beginManualAudioSetup();},
            [this,previousMixerBound](MixerBindingMode mode) -> Apply::BindResult {
                if(finishAudioSetupAndRecoverMixer(mode))return {};
                if(mode!=MixerBindingMode::exact && !previousMixerBound &&
                   engine_.directRouteAuthorized() &&
                   (engine_.hasRetainedUnboundMixer() || mixerSessionRecoveryPending_)) {
                    const auto error=mixerSessionError_.isNotEmpty()?mixerSessionError_:
                        juce::String("Device opened, but the mixer could not bind to its playback pins.");
                    return {error,true,engine_.stageRetainedWasapiPortMapping()};
                }
                return {mixerSessionError_.isNotEmpty()?mixerSessionError_:
                    juce::String("Device opened but mixer preparation was rejected")};
            },
            [this](const juce::String& key,const juce::String& value) {
                diagnostics_.record(key,value);
            }
        });
        switch(result.state) {
        case Apply::Outcome::rejected:
            return send(false,"REJECTED",result.error);
        case Apply::Outcome::needsRemap:
            diagnostics_.record("CUSTOM_AUDIO_NEEDS_REMAP",result.error);
            send(false,"NEEDS_REMAP",result.error,result.canReusePins);
            break;
        case Apply::Outcome::accepted:
            startupError_.clear();
            deviceSettingsDirty_=true;
            directRestore_.cancel();
            asioRecoveryEpoch_=engine_.dryRoutingLabEpoch();
            // New explicit selection supersedes any old automatic reopen.
            verifiedWasapiSetup_.reset();
            wasapiRecovery_.cancel();
            diagnostics_.record("CUSTOM_AUDIO_APPLY_OK",
                typeName+" | "+juce::String(static_cast<int>(monotonicMilliseconds()-start))+" ms");
            requestAutomaticMain();
            resumeStream(false);
            send(true,engine_.isMonitorEnabled()?"ACCEPTED_LIVE":"ACCEPTED_MUTED",{});
            break;
        case Apply::Outcome::topologyChanged:
            diagnostics_.record("CUSTOM_AUDIO_ASIO_TOPOLOGY_CHANGED",result.error);
            return send(false,"RECOVERY_REQUIRED",result.error);
        case Apply::Outcome::restored:
        case Apply::Outcome::recoveryRequired: {
            const bool restored=result.state==Apply::Outcome::restored;
            if(restored)asioRecoveryEpoch_=engine_.dryRoutingLabEpoch();
            diagnostics_.record("CUSTOM_AUDIO_APPLY_FAIL",result.error+" | rollback="+
                (restored?"RESTORED":"RECOVERY_REQUIRED")+" | "+
                juce::String(static_cast<int>(monotonicMilliseconds()-start))+" ms");
            if(restored&&previousMonitor)requestAutomaticMain();
            if(restored)resumeStream(true);
            send(false,restored?(engine_.isMonitorEnabled()?"RESTORED_LIVE":"RESTORED_MUTED"):
                "RECOVERY_REQUIRED",result.error);
            break;
        }
        }
        emitApprovedNativeSnapshot();
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
    // Direct driver settings, not the generic JUCE AudioDeviceSelector.
    // Reuse the same device transaction lease and verified mixer recovery.
    struct PendingVendorStream {
        LiveAudioEngine::SecondaryResumeIntent intent;
        std::uint64_t epoch,revision,deadline;
        bool waitForMain;
    };
    std::optional<PendingVendorStream> pendingVendorStream_;
    // Machine-local preference is NOT ARM authority. Endpoint ID never leaves
    // native control. A portable show cannot silently select a different PC's sink.
    struct SecondaryPreference { juce::String name; std::wstring id; };
    std::optional<SecondaryPreference> secondaryPreference_;
    bool secondaryPreferenceLoaded_=false;
    juce::File secondaryPreferenceFile() const {
        return machineSettingsDirectory().getChildFile("secondary-output.json");
    }
    void loadSecondaryPreference() {
        if(secondaryPreferenceLoaded_)return;
        secondaryPreferenceLoaded_=true;
        const auto file=secondaryPreferenceFile();
        if(!file.existsAsFile()||file.getSize()<=0||file.getSize()>16384)return;
        const auto data=juce::JSON::parse(file.loadFileAsString());
        const auto name=data.getProperty("name",{}),id=data.getProperty("endpointId",{});
        if(data.getProperty("version",{})!=juce::var(1)||!name.isString()||!id.isString())return;
        const auto label=name.toString(),identity=id.toString();
        if(label.isEmpty()||label.length()>240||identity.isEmpty()||identity.length()>2048)return;
        secondaryPreference_=SecondaryPreference{label,std::wstring(identity.toWideCharPointer())};
    }
    void rememberSecondaryPreference(const LiveAudioEngine::SecondaryResumeIntent& intent) {
        loadSecondaryPreference();
        if(secondaryPreference_&&secondaryPreference_->name==intent.name&&
           secondaryPreference_->id==intent.endpointId)return;
        auto data=std::make_unique<juce::DynamicObject>();
        data->setProperty("version",1);
        data->setProperty("name",intent.name);
        data->setProperty("endpointId",juce::String(intent.endpointId.c_str()));
        if(machineSettingsDirectory().createDirectory().wasOk()&&
           secondaryPreferenceFile().replaceWithText(juce::JSON::toString(juce::var(data.release()),true)))
            secondaryPreference_=SecondaryPreference{intent.name,intent.endpointId};
        else diagnostics_.record("SECONDARY_PREFERENCE_SAVE_FAIL","Output active; machine-local preference not saved");
    }
    // One bounded discovery flight for both output surfaces. Worker owns
    // only read-only MMDevice enumeration, never engine, GUI or audio objects.
    using SecondaryInventory=armix::device::WasapiEndpointIdentityGuard::RenderInventory;
    std::future<SecondaryInventory> secondaryInventoryFlight_;
    std::optional<SecondaryInventory> secondaryInventoryCache_;
    juce::String secondaryInventoryRequest_,secondaryPublishedInventory_;
    std::map<juce::String,std::wstring> secondaryPublishedIds_;
    std::optional<juce::var> pendingSecondaryStop_;
    std::uint64_t secondaryInventoryAt_=0;
    void emitSecondaryInventory() {
        if(secondaryInventoryRequest_.isEmpty()||!secondaryInventoryCache_)return;
        const auto request=std::exchange(secondaryInventoryRequest_,juce::String{});
        const auto& inventory=*secondaryInventoryCache_;
        secondaryPublishedInventory_=request;
        secondaryPublishedIds_.clear();
        juce::Array<juce::var> outputs;
        juce::String preferred;
        loadSecondaryPreference();
        // Only unique, selectable names. Full endpoint identity remains native.
        std::map<std::wstring,unsigned> counts;
        for(const auto& ep:inventory.endpoints)++counts[ep.name];
        auto* direct=engine_.devices().getCurrentAudioDevice();
        const auto directName=direct?direct->getName():juce::String{};
        for(const auto& ep:inventory.endpoints) {
            const juce::String label(ep.name.c_str());
            if(counts[ep.name]!=1||ep.id.empty()||label.isEmpty()||
               label.length()>240||label==directName)continue;
            outputs.add(label);
            secondaryPublishedIds_.emplace(label,ep.id);
            if(secondaryPreference_&&label==secondaryPreference_->name&&ep.id==secondaryPreference_->id)
                preferred=label;
        }
        auto result=std::make_unique<juce::DynamicObject>();
        result->setProperty("requestId",request);
        result->setProperty("outputs",juce::var(outputs));
        result->setProperty("preferredOutput",preferred);
        result->setProperty("hasSavedOutput",secondaryPreference_.has_value());
        result->setProperty("error",inventory.error);
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixSecondaryOutputInventory",juce::var(result.release()));
    }
    void requestSecondaryInventory(const juce::var& args) {
        const auto id=args.getProperty("requestId",{}).toString();
        if(id.isEmpty()||id.length()>64)return;
        secondaryInventoryRequest_=id; // newest correlated request wins
        if(secondaryInventoryFlight_.valid())return;
        const auto now=static_cast<std::uint64_t>(monotonicMilliseconds());
        if(!bool(args.getProperty("force",false))&&secondaryInventoryCache_&&
           secondaryInventoryCache_->error.isEmpty()&&now>=secondaryInventoryAt_&&
           now-secondaryInventoryAt_<2000) {
            emitSecondaryInventory();
            return;
        }
        try {
            secondaryInventoryFlight_=std::async(std::launch::async,[] {
                return armix::device::WasapiEndpointIdentityGuard::renderInventory();
            });
        } catch(...) {
            secondaryInventoryCache_=SecondaryInventory{{},"Cannot start playback discovery"};
            emitSecondaryInventory();
        }
    }
    void serviceSecondaryInventory() {
        if(!secondaryInventoryFlight_.valid()||
           secondaryInventoryFlight_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        try {secondaryInventoryCache_=secondaryInventoryFlight_.get();}
        catch(...) {secondaryInventoryCache_=SecondaryInventory{{},"Cannot read playback devices"};}
        secondaryInventoryAt_=static_cast<std::uint64_t>(monotonicMilliseconds());
        emitSecondaryInventory();
    }
    // Same-session, same-graph recovery only. Never carries ARM permission
    // across process restart, explicit disconnect, driver or route change.
    struct SecondaryContinuity {
        LiveAudioEngine::SecondaryResumeIntent intent;
        std::uint64_t epoch,revision;
        bool waitForMain=false;
    };
    std::optional<SecondaryContinuity> secondaryContinuity_;
    armix::device::AudioContinuityRetry secondaryRetry_;
    void cancelSecondaryContinuity() {
        secondaryContinuity_.reset();
        secondaryRetry_.cancel();
    }
    bool secondaryRecoveryPending() const noexcept {
        return pendingVendorStream_.has_value()||secondaryRetry_.pending();
    }
    juce::String secondaryRecoveryName() const {
        if(pendingVendorStream_)return pendingVendorStream_->intent.name;
        return secondaryRetry_.pending()&&secondaryContinuity_?secondaryContinuity_->intent.name:juce::String{};
    }
    // CONTROL thread: one continuity owner for ordinary native mixer
    // topology edits. The existing engine still owns late publication
    // gates, native graph validation and retirement. Do not restart ASIO.
    template<class Edit>
    LiveDryRoutingLab::NativeBusResult withNativeTopologyContinuity(
            std::uint64_t ep,std::uint64_t rev,bool startOnSuccess,Edit&& edit) {
        LiveDryRoutingLab::NativeBusResult result;
        result.error="Audio configuration is finishing";
        auto lease=deviceApply_.tryBegin(armix::device::NativeDeviceApplyCoordinator::Purpose::secondaryOutput);
        if(!lease||mixerSessionWorker_.busy()||pendingRetainedFxCapture_||
           retainedOfflineFxActive_||pendingSecondaryStop_||engine_.hasRuntimeFxFault()||
           nativeShowBusy_||studioBackupBusy_||ep!=engine_.dryRoutingLabEpoch()||
           !engine_.nativeTopologyMatches(ep,rev))return result;
        const auto beforeControlRevision=engine_.outputArmRevision();
        auto stream=engine_.captureSecondaryResumeIntent();
        if(!stream&&pendingVendorStream_&&pendingVendorStream_->epoch==ep&&
           pendingVendorStream_->revision==beforeControlRevision)stream=pendingVendorStream_->intent;
        if(!stream&&secondaryContinuity_&&secondaryContinuity_->epoch==ep&&
           secondaryContinuity_->revision==beforeControlRevision&&secondaryRetry_.pending())
            stream=secondaryContinuity_->intent;
        if(engine_.secondaryArmed()&&(!stream||!engine_.secondarySourceMatches(*stream))) {
            result.error="Disconnect additional output before editing this non-ASIO route";
            return result;
        }
        pendingVendorStream_.reset();
        cancelSecondaryContinuity();
        const auto generation=secondaryRetry_.generation();
        const bool hardwareWanted=!operatorOutputMuted_&&
            (engine_.isMonitorEnabled()||engine_.wantsMainContinuation());
        if(stream)engine_.disarmSecondaryOutput();
        result=edit();
        const auto acceptedRevision=engine_.outputArmRevision();
        if(generation==secondaryRetry_.generation()&&!pendingSecondaryStop_&&
           engine_.dryRoutingLabEpoch()==ep&&(result.accepted||
                (acceptedRevision==beforeControlRevision&&engine_.nativeTopologyMatches(ep,rev)))) {
            if(hardwareWanted||(result.accepted&&startOnSuccess&&!operatorOutputMuted_))
                requestAutomaticMain(!hardwareWanted);
            if(stream&&engine_.secondarySourceMatches(*stream))
                pendingVendorStream_=PendingVendorStream{*stream,ep,acceptedRevision,
                    static_cast<std::uint64_t>(monotonicMilliseconds()+5000),hardwareWanted};
        }
        return result;
    }
    void serviceSecondaryContinuity() {
        if(pendingVendorStream_||deviceApply_.busy())return; // existing transaction owns recovery
        const auto epoch=engine_.dryRoutingLabEpoch(),revision=engine_.outputArmRevision();
        if(engine_.secondaryArmed()) {
            if(!secondaryContinuity_||secondaryContinuity_->epoch!=epoch||
               secondaryContinuity_->revision!=revision) {
                cancelSecondaryContinuity();
                if(engine_.devices().getCurrentAudioDeviceType()=="ASIO")
                    if(auto intent=engine_.captureSecondaryResumeIntent())
                        secondaryContinuity_=SecondaryContinuity{std::move(*intent),epoch,revision};
            }
            if(secondaryRetry_.pending())secondaryRetry_.recovered();
            return;
        }
        if(!secondaryContinuity_)return;
        if(secondaryContinuity_->epoch!=epoch||secondaryContinuity_->revision!=revision||
           nativeShowBusy_||studioBackupBusy_||retainedOfflineFxActive_||
           pendingRetainedFxCapture_||engine_.hasRuntimeFxFault()||
           (!engine_.secondaryFaulted()&&!secondaryRetry_.pending())) {
            cancelSecondaryContinuity();
            return;
        }
        const auto now=static_cast<std::uint64_t>(monotonicMilliseconds());
        secondaryRetry_.fault(now);
        if(!secondaryRetry_.due(now)||deviceApply_.busy()||mixerSessionWorker_.busy()||
           !engine_.secondaryResumeReady()||
           (secondaryContinuity_->waitForMain&&!engine_.isMonitorEnabled()))return;
        auto lease=deviceApply_.tryBegin(armix::device::NativeDeviceApplyCoordinator::Purpose::asioRecovery);
        if(!lease)return;
        const auto intent=secondaryContinuity_->intent;
        const auto generation=secondaryRetry_.generation();
        // Identity preflight avoids opening a default or a replacement device.
        const auto current=armix::device::WasapiEndpointIdentityGuard::resolveRender(intent.name);
        if(current.ok()&&current.selection.id!=intent.endpointId) {
            cancelSecondaryContinuity();
            emitNotice("Additional output identity changed · choose your destination again",true);
            return;
        }
        if(generation!=secondaryRetry_.generation())return;
        const auto error=current.ok()?engine_.restoreSecondaryOutput(intent):current.error;
        // A nested native device event or Stop may revoke this request while
        // the vendor open runs. Never publish success for a superseded intent.
        if(generation!=secondaryRetry_.generation()||
           engine_.dryRoutingLabEpoch()!=epoch||engine_.outputArmRevision()!=revision) {
            engine_.disconnectSecondaryOutput();
            cancelSecondaryContinuity();
            return;
        }
        if(error.isEmpty()) {
            secondaryRetry_.recovered();
            diagnostics_.record("SECONDARY_HOTPLUG_RESTORED",intent.name);
            emitNotice("Additional output restored",false);
        } else {
            secondaryRetry_.failed(now);
            // A missing cable may return after minutes or hours. Keep the
            // verified session intent with a capped 30s cadence, no toast spam.
            if(secondaryRetry_.attempts()==1)
                diagnostics_.record("SECONDARY_HOTPLUG_WAITING",error);
        }
    }
    void serviceVendorStreamRecovery() {
        if(!pendingVendorStream_)return;
        const auto& p=*pendingVendorStream_;
        if(engine_.dryRoutingLabEpoch()!=p.epoch||
           engine_.outputArmRevision()!=p.revision||nativeShowBusy_||studioBackupBusy_||
           retainedOfflineFxActive_||pendingRetainedFxCapture_||engine_.hasRuntimeFxFault()){
            pendingVendorStream_.reset();
            emitNotice("Additional output paused · enable it again when ready",true);
            return;
        }
        if(monotonicMilliseconds()>=p.deadline) {
            // A slow callback or vendor readiness is not an operator Stop.
            // Retain only the same epoch/control identity; bounded recovery
            // will revalidate the native source and full endpoint before ARM.
            secondaryContinuity_=SecondaryContinuity{p.intent,p.epoch,p.revision,p.waitForMain};
            pendingVendorStream_.reset();
            secondaryRetry_.fault(static_cast<std::uint64_t>(monotonicMilliseconds()));
            return;
        }
        if(deviceApply_.busy()||mixerSessionWorker_.busy()||!engine_.secondaryResumeReady()||
           (p.waitForMain&&!engine_.isMonitorEnabled()))return;
        auto lease=deviceApply_.tryBegin(armix::device::NativeDeviceApplyCoordinator::Purpose::asioRecovery);
        if(!lease)return;
        auto intent=p.intent;
        const bool waitForMain=p.waitForMain;
        const auto epoch=p.epoch,revision=p.revision,generation=secondaryRetry_.generation();
        pendingVendorStream_.reset();
        const auto error=engine_.restoreSecondaryOutput(intent);
        if(generation!=secondaryRetry_.generation()||
           engine_.dryRoutingLabEpoch()!=epoch||engine_.outputArmRevision()!=revision) {
            engine_.disconnectSecondaryOutput();
            cancelSecondaryContinuity();
            return;
        }
        if(error.isNotEmpty()) {
            // The route is verified but the sink might still be temporarily
            // absent. Hand off to the same bounded continuity scheduler.
            secondaryContinuity_=SecondaryContinuity{intent,epoch,revision,waitForMain};
            secondaryRetry_.fault(static_cast<std::uint64_t>(monotonicMilliseconds()));
            diagnostics_.record("SECONDARY_AUTO_RESTORE_WAITING",error);
            return;
        }
        diagnostics_.record("SECONDARY_AUTO_RESTORED",intent.name);
        emitNotice("Additional output restored",false);
    }
    void showAsioControlPanel(const juce::var& args)
    {
        const auto active=currentDeviceSnapshot();
        auto& manager=engine_.devices();
        auto* device=manager.getCurrentAudioDevice();
        if(nativeShowBusy_ || studioBackupBusy_ ||
           mixerSessionWorker_.busy() || pendingRetainedFxCapture_ || retainedOfflineFxActive_ ||
           engine_.independentClockActive() || engine_.hasTemporaryDiagnosticDevice() ||
           engine_.isGraphSwitchPending() || !device || !device->isOpen() ||
           manager.getCurrentAudioDeviceType()!="ASIO" || !device->hasControlPanel() ||
           args.getProperty("driver",{}).toString()!=device->getName() ||
           args.getProperty("deviceGeneration",{})!=active.getProperty("deviceGeneration",{}))
            return emitNotice("ASIO control panel unavailable · refresh Audio I/O and try again",true);
        auto lease=deviceApply_.tryBegin(armix::device::NativeDeviceApplyCoordinator::Purpose::setupDialog);
        if(!lease)return emitNotice("Audio device is busy · try again",true);
        const bool resume=engine_.wantsMainContinuation()&&!operatorOutputMuted_;
        pendingVendorStream_.reset();
        const auto streamIntent=engine_.captureSecondaryResumeIntent();
        const auto streamGeneration=secondaryRetry_.generation();
        autoMainEpoch_=0;
        engine_.beginManualAudioSetup(true);
        bool panelError=false;
        try {
            // JUCE's own selector uses this driver API and restarts when it
            // requests a refresh. Never instantiate a second ASIO driver.
            const bool refresh=device->showControlPanel();
            if(refresh && manager.getCurrentAudioDevice()==device){
                manager.closeAudioDevice();
                manager.restartLastAudioDevice();
            }
        } catch(...) {panelError=true;}
        const bool recovered=finishAudioSetupAndRecoverMixer();
        if(recovered){
            deviceSettingsDirty_=true;startupError_.clear();
            if(resume&&!operatorOutputMuted_)requestAutomaticMain(false);
            if(streamIntent&&!panelError&&streamGeneration==secondaryRetry_.generation()&&!pendingSecondaryStop_)
                pendingVendorStream_=PendingVendorStream{*streamIntent,engine_.dryRoutingLabEpoch(),
                    engine_.outputArmRevision(),static_cast<std::uint64_t>(monotonicMilliseconds()+5000),resume};
        }
        diagnostics_.record(recovered&&!panelError?"ASIO_PANEL_RETURN":"ASIO_PANEL_RECOVERY_REQUIRED",
            recovered?"Vendor panel returned; verified device and mixer binding":"Vendor panel returned; mixer retained for recovery");
        emitApprovedNativeSnapshot();
        emitDeviceInventory(false);
        if(!recovered||panelError)
            emitNotice("ASIO settings returned · check Audio I/O; mixer settings retained",true);
        else if(streamIntent)
            emitNotice("ASIO settings updated · restoring Additional Output",false);
    }

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
    juce::String lastAudioPathSignature_;
    juce::String audioPathCode(const juce::var& live) const
    {
        if(!bool(live.getProperty("connected",false)))return "NO_DEVICE";
        if(!bool(live.getProperty("directRouteAuthorized",false)))return "ROUTE_FENCE";
        if(live.getProperty("inputDevice",{}).toString().isNotEmpty()&&
           int(live.getProperty("deviceInputChannels",0))==0)return "CAPTURE_SELECTED_ZERO_ACTIVE_PINS";
        if(int(live.getProperty("deviceOutputChannels",0))<2)return "OUTPUT_PINS_MISSING";
        if(live.getProperty("productionRoutingState",{}).toString()!="NATIVE_ROUTE_ACCEPTED")
            return "NATIVE_ROUTE_UNBOUND";
        if(bool(live.getProperty("masterMute",false)))return "MASTER_MUTED";
        if(!bool(live.getProperty("monitor",false)))
            return operatorOutputMuted_?"OPERATOR_OUTPUT_MUTED":"OUTPUT_NOT_ARMED";
        if(int(live.getProperty("deviceInputChannels",0))>0&&
           int(live.getProperty("callbackInputChannels",0))==0)return "CAPTURE_CALLBACK_NO_INPUT";
        return "ROUTE_OPEN_CHECK_PCM";
    }
    void recordAudioPathTransition(const juce::var& live)
    {
        const auto signature=audioPathCode(live)+" | generation="+live.getProperty("deviceGeneration",{}).toString()
            +" | in="+live.getProperty("activeInputMaskHex",{}).toString()
            +" | out="+live.getProperty("activeOutputMaskHex",{}).toString()
            +" | callbackIN="+live.getProperty("callbackInputChannels",{}).toString()
            +" | monitor="+live.getProperty("monitor",{}).toString()
            +" | native="+live.getProperty("productionRoutingState",{}).toString();
        if(signature==lastAudioPathSignature_)return;
        lastAudioPathSignature_=signature;
        diagnostics_.record("AUDIO_PATH_TRANSITION",signature);
    }
    juce::String makeAudioReport()
    {
        auto status = engine_.status();
        annotateRecovery (status);
        auto* device = engine_.diagnosticRenderDevice();
        auto* capture = engine_.diagnosticCaptureDevice();
        juce::String result;
        result.preallocateBytes (64 * 1024);
        result << "ARSONMIX / SMART AUDIO DIAGNOSTIC REPORT v3 (SIGNAL PATH)\n";
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
        result << "=== SIGNAL PATH / NO-SOUND TRIAGE ===\n";
        result << "Observed stage: " << audioPathCode(status) << "\n";
        result << "Operator mute intent: " << (operatorOutputMuted_?"YES":"NO") << "\n";
        result << "Mixer session: " << mixerSessionStatus_ << " | " << mixerSessionError_ << "\n";
        juce::AudioDeviceManager::AudioDeviceSetup configured;
        engine_.devices().getAudioDeviceSetup(configured);
        result << "Configured capture: " << configured.inputDeviceName
               << " | Default pins: " << (configured.useDefaultInputChannels?"YES":"NO")
               << " | Configured input mask(hex): " << configured.inputChannels.toString(16) << "\n";
        result << "Configured playback: " << configured.outputDeviceName
               << " | Configured output mask(hex): " << configured.outputChannels.toString(16) << "\n";
        result << "No automatic unmute, route rewrite or test tone is triggered by this report.\n\n";
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
            // Explicit report: full driver names, not just active callback
            // buffers or the bounded GUI Apply list. Never open another driver.
            const auto describePins=[&](juce::AudioIODevice* endpoint,bool input) {
                if(!endpoint || !endpoint->isOpen())return;
                const auto names=input?endpoint->getInputChannelNames():endpoint->getOutputChannelNames();
                const auto mask=input?endpoint->getActiveInputChannels():endpoint->getActiveOutputChannels();
                result << (input?"Capture":"Playback") << " pins: "
                       << mask.countNumberOfSetBits() << " active / " << names.size()
                       << " driver-reported\n";
                for(int i=0;i<names.size();++i)
                    result << "  " << (input?"IN ":"OUT ") << (i+1) << " | "
                           << names[i] << " | " << (mask[i]?"ENABLED":"DISABLED") << "\n";
            };
            describePins(capture,true);
            describePins(device,false);
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
        result << "\n=== NATIVE INSERT TIMING (SAMPLED WALL TIME, NOT HARDWARE XRUN) ===\n";
        result << "NativeBus insert fields processTiming*: one actual processor invocation every 24-40 calls; "
                  "lifetime of the current FX lease, approximate atomic snapshot. Includes wrapper/processor region "
                  "and OS preemption; excludes skipped bypass, dry PDC and music guide observation. "
                  "Max is sampled only, mean is not normalized to callback size, -1 means no sample. "
                  "Contention/exception counts are lifetime host events, not confirmed hardware underruns.\n";
        result << "workload*: vendor-independent lifetime invocation wall time versus frames/rate; "
                  "1000 permille means the whole invocation quantum, not a hardware xrun. "
                  "Published counts may lag up to 31 timed calls; external CH1 uses sparse observations "
                  "and a graph/physical-slot identity, not all-call peaks. Sampled silent PCM is not "
                  "proof of drained state. Reviews are advisory; no automatic bypass or suspend.\n";
        result << "workloadWindow*: last complete 32 processor observations, frame-weighted mean "
                  "and observed peak; original CH1 is sparse. Not a time window or current load; "
                  "may be stale after idle/bypass. Lifetime peak remains separate.\n";
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



    // Exactly one native Graphic EQ 31 snapshot source: real APVTS targets
    // and one PCM-callback-ACKed request ID. No UI/demo state is ever accepted.
    void emitNativeGeqSnapshot(const juce::String& request,
            const juce::String& channelId,const juce::String& epoch,
            const juce::String& revision,const juce::String& instanceId,
            armix::builtin_fx::GraphicEq31Processor* processor) {
        auto reply=std::make_unique<juce::DynamicObject>();
        reply->setProperty("requestId",request);
        reply->setProperty("channelId",channelId);
        reply->setProperty("deviceEpoch",epoch);
        reply->setProperty("graphRevision",revision);
        reply->setProperty("instanceId",instanceId);
        reply->setProperty("status",processor?"ACCEPTED":"REJECTED");
        reply->setProperty("schemaVersion",1);
        if(processor) {
            juce::var bands(juce::Array<juce::var>{});
            for(const float db:processor->acceptedBands())
                bands.append(juce::var(static_cast<double>(db)));
            reply->setProperty("bands",bands);
            reply->setProperty("levelDb",processor->acceptedLevelDb());
            reply->setProperty("addedLatencySamples",processor->getLatencySamples());
            if(epoch=="0"&&instanceId==lastLogicalFxInstance_&&
               revision==lastLogicalFxRevision_)
                reply->setProperty("appliedRequestId",lastLogicalFxRequest_);
            else if(instanceId==lastNativeGeqInstance_&&
                    epoch==lastNativeGeqEpoch_&&
                    revision==lastNativeGeqRevision_)
                reply->setProperty("appliedRequestId",lastNativeGeqRequest_);
        }else reply->setProperty("error","NATIVE_GEQ_INSTANCE_OR_REVISION_UNAVAILABLE");
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinGeqSnapshot",juce::var(reply.release()));
    }

    void servicePendingNativeGeq() {
        if(!pendingNativeGeq_.owner)return;
        auto* processor=dynamic_cast<armix::builtin_fx::GraphicEq31Processor*>(
            pendingNativeGeq_.owner->processor.get());
        if(!processor){pendingNativeGeq_={};return;}
        armix::builtin_fx::GraphicEq31Processor::Completion completion{};
        const bool completed=processor->pollCompletion(completion);
        if(!completed&&juce::Time::getMillisecondCounter()-
                       pendingNativeGeq_.started<3000u)return;
        const auto request=pendingNativeGeq_.request;
        bool applied=completed&&completion.applied&&completion.requestId>0&&
            request=="geq-"+juce::String(std::to_string(completion.requestId));
        const auto snapshot=approvedNativeSnapshot();
        const auto device=snapshot.getProperty("device",{});
        const auto graph=snapshot.getProperty("graph",{});
        applied=applied&&pendingNativeGeq_.deviceEpoch==
            device.getProperty("epoch","").toString()&&
            pendingNativeGeq_.revision==
            graph.getProperty("revision","").toString();
        if(applied) {
            lastNativeGeqRequest_=request;
            lastNativeGeqInstance_=pendingNativeGeq_.instanceId;
            lastNativeGeqEpoch_=pendingNativeGeq_.deviceEpoch;
            lastNativeGeqRevision_=pendingNativeGeq_.revision;
        }
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",request);
        ack->setProperty("channelId",pendingNativeGeq_.channelId);
        ack->setProperty("deviceEpoch",pendingNativeGeq_.deviceEpoch);
        ack->setProperty("graphRevision",pendingNativeGeq_.revision);
        ack->setProperty("instanceId",pendingNativeGeq_.instanceId);
        ack->setProperty("status",applied?"APPLIED":"REJECTED");
        ack->setProperty("errorCode",applied?"NONE":"GEQ_PCM_ACK_TIMEOUT_OR_STALE");
        pendingNativeGeq_={};
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinGeqAck",juce::var(ack.release()));
    }

    // de-Ezzer uses the existing exact graph/slot/instance lease and PCM owner.
    auto deEzzerLease(const armix::builtin_fx::NativeFxControlRequest& r) {
        return r.valid&&!r.unbound?engine_.leaseNativeVst3Editor(r.epoch,r.revision,
            armix::mixer::ChannelId{r.channel},static_cast<std::size_t>(r.slot),
            r.rawInstance.toString()):armix::mixer::LiveChannelFxGraph::InstanceLease{};
    }
    void emitDeEzzerReply(const char* event,const armix::builtin_fx::NativeFxControlRequest& r,
                         bool ok,const char* key=nullptr,juce::var payload={}) {
        auto o=std::make_unique<juce::DynamicObject>();
        o->setProperty("requestId",r.request);o->setProperty("channelId",r.rawChannel.toString());
        o->setProperty("deviceEpoch",r.rawEpoch.toString());o->setProperty("graphRevision",r.rawRevision.toString());
        o->setProperty("instanceId",r.rawInstance.toString());o->setProperty("slot",r.slot);
        o->setProperty("ok",ok);if(key)o->setProperty(key,payload);
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(event,juce::var(o.release()));
    }
    // Ephemeral editor session. Not saved to .smix, presets or automation.
    juce::String deEzzerEditorToken_;
    armix::mixer::FxCueKey deEzzerEditorKey_{};
    static const char* deEzzerCueName(armix::mixer::FxCueMode mode) noexcept {
        using M=armix::mixer::FxCueMode;
        switch(mode){case M::Ess:return "Ess";case M::Sidechain:return "Sidechain";case M::Delta:return "Delta";case M::Mix:break;}
        return "Mix";
    }
    static bool validDeEzzerEditorToken(const juce::var& value) {
        if(!value.isString())return false;
        const auto t=value.toString();
        return t.startsWith("dez-")&&t.length()==40&&t.substring(4).containsOnly("0123456789abcdefABCDEF-");
    }
    armix::mixer::FxCueKey deEzzerCueKey(const armix::builtin_fx::NativeFxControlRequest& r,
                                         armix::builtin_fx::DeEzzerProcessor* proc) const noexcept {
        if(!proc||!r.valid||r.unbound)return {};
        const auto id=proc->identity();
        return {r.epoch,r.revision,engine_.deEzzerCueRoute(r.epoch,r.revision,armix::mixer::ChannelId{r.channel}),id.instance,id.epoch};
    }
    juce::var deEzzerCueReadback(armix::mixer::FxCueKey key,bool enabled) {
        const auto st=engine_.deEzzerCueControl().state(key);
        auto o=std::make_unique<juce::DynamicObject>();
        o->setProperty("available",key.valid()&&enabled);
        o->setProperty("requested",deEzzerCueName(st.requested));
        o->setProperty("applied",deEzzerCueName(st.applied));
        return juce::var(o.release());
    }
    void servicePendingNativeDeEzzer() {
        auto& cue=engine_.deEzzerCueControl();
        if(!engine_.privateDeEzzerCueAllowed())cue.cancel(false);
        else cue.expire(juce::Time::getMillisecondCounter());
        auto& p=pendingNativeDeEzzer_;
        if(!p.owner)return;
        auto* proc=dynamic_cast<armix::builtin_fx::DeEzzerProcessor*>(p.owner->processor.get());
        armix::builtin_fx::de_ezzer::Ack ack{};
        const bool completed=proc&&proc->pollAck(ack);
        if(!completed&&proc&&proc->identity()==p.id) {
            // Retain a live outstanding lease rather than pretending a timeout
            // cancelled audio. A removed graph may abandon the old lease.
            if(juce::Time::getMillisecondCounter()-p.started<3000u||deEzzerLease(p.request)==p.owner)return;
        }
        const bool matched=completed&&ack.requested==p.id&&ack.sequence==p.sequence&&
            deEzzerLease(p.request)==p.owner;
        emitDeEzzerReply("arMixBuiltinDeEzzerAck",p.request,matched,
            matched?"ack":nullptr,matched?armix::builtin_fx::de_ezzer::bridge::encode(ack):juce::var{});
        p={};
    }
    // Anti Noiz uses the same exact native lease and one CONTROL producer.
    // No snapshot/ACK is constructed by the browser; telemetry is drained
    // only here on CONTROL, never from audio-owned fields.
    void emitAntiNoizReply(const char* event,const juce::var& identity,
            const char* status,const char* error,
            armix::builtin_fx::AntiNoizProcessor* p=nullptr,bool observe=false) {
        auto reply=std::make_unique<juce::DynamicObject>();
        for(const auto* key:{"requestId","channelId","deviceEpoch","graphRevision","instanceId","slot","workspaceToken"})
            reply->setProperty(key,identity.getProperty(key,{}));
        reply->setProperty("status",status);reply->setProperty("errorCode",error);
        if(p){
            using Binding=armix::builtin_fx::AntiNoizNativeBinding;
            reply->setProperty("settings",Binding::settings(p->acceptedSettings()));
            reply->setProperty("sampleRate",p->getSampleRate());
            reply->setProperty("addedLatencySamples",p->getLatencySamples());
            if(observe)reply->setProperty("observation",Binding::observation(p->readObservation()));
        }
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(event,juce::var(reply.release()));
    }
    void servicePendingNativeAntiNoiz() {
        auto& job=pendingNativeAntiNoiz_;
        if(!job.owner)return;
        auto* p=dynamic_cast<armix::builtin_fx::AntiNoizProcessor*>(job.owner->processor.get());
        if(!p){job={};return;}
        armix::builtin_fx::AntiNoizProcessor::Completion ack;
        if(p->pollCompletion(ack)){
            const auto current=engine_.leaseNativeVst3Editor(job.epoch,job.revision,
                armix::mixer::ChannelId{job.channel},static_cast<std::size_t>(job.slot),
                job.identity.getProperty("instanceId",{}).toString());
            const bool applied=ack.id==job.id&&ack.applied&&current==job.owner;
            if(!job.timeoutReported)
                emitAntiNoizReply("arMixBuiltinAntiNoizAck",job.identity,
                    applied?"APPLIED":"REJECTED",applied?"NONE":"DSP_EDIT_NOT_APPLIED_OR_STALE",
                    current==job.owner?p:nullptr);
            job={};return;
        }
        if(juce::Time::getMillisecondCounter()-job.started<3000u)return;
        // Timeout is not cancellation. Keep draining the original mailbox
        // until ACK or actual epoch/instance retirement; never strand it
        // by dropping the lease while its queued edit can still be applied.
        if(!job.timeoutReported){
            job.timeoutReported=true;
            emitAntiNoizReply("arMixBuiltinAntiNoizAck",job.identity,
                "REJECTED","DSP_ACK_TIMEOUT_OUTCOME_UNKNOWN");
        }
        const auto now=approvedNativeSnapshot();
        const auto device=now.getProperty("device",{}),graph=now.getProperty("graph",{});
        if(device.getProperty("epoch",{}).toString()!=
           job.identity.getProperty("deviceEpoch",{}).toString()){job={};return;}
        if(engine_.isGraphSwitchPending())return;
        const auto rows=graph.getProperty("channels",{});
        if(const auto* channels=rows.getArray()){
            bool found=false;
            for(const auto& row:*channels){
                const auto inserts=row.getProperty("inserts",{});
                if(const auto* list=inserts.getArray())for(const auto& item:*list)
                    if(item.getProperty("id",{}).toString()==
                        job.identity.getProperty("instanceId",{}).toString())found=true;
            }
            if(!found)job={}; // removed/retired, no future same-instance writes
        }
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

    void emitDuckerReply(const juce::var& identity,const char* event,
                         const juce::String& status,const juce::String& error={}) {
        auto reply=std::make_unique<juce::DynamicObject>();
        for(const auto* key:{"requestId","channelId","deviceEpoch","graphRevision","instanceId"})
            reply->setProperty(key,identity.getProperty(key,{}));
        reply->setProperty("status",status);reply->setProperty("errorCode",error);
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(event,juce::var(reply.release()));
    }
    void servicePendingNativeDucker() {
        using D=armix::builtin_fx::AutoDuckerProcessor;
        for(auto i=pendingNativeDuckers_.begin();i!=pendingNativeDuckers_.end();){
            auto* d=dynamic_cast<D*>(i->owner->processor.get());
            D::Completion c{};
            if(d&&d->pollCompletion(c)){
                const bool accepted=c.applied&&c.requestId==i->sequence;
                emitDuckerReply(i->identity,"arMixBuiltinDuckerAck",
                    accepted?"APPLIED":"REJECTED",accepted?"":"DSP_EDIT_REJECTED");
                i=pendingNativeDuckers_.erase(i);continue;
            }
            if(!d||!engine_.retainsRoutedFxEditor(
                    static_cast<std::uint64_t>(i->identity.getProperty("deviceEpoch",{}).toString().getLargeIntValue()),i->owner)){
                emitDuckerReply(i->identity,"arMixBuiltinDuckerAck","REJECTED","INSTANCE_RETIRED");
                i=pendingNativeDuckers_.erase(i);continue;
            }
            if(!i->timeoutNotified&&juce::Time::getMillisecondCounter()-i->started>3000u){
                i->timeoutNotified=true; // not a false rollback; still drain late completion
                emitDuckerReply(i->identity,"arMixBuiltinDuckerAck","PENDING","WAITING_FOR_AUDIO");
            }
            ++i;
        }
    }

    void emitNativeDuckerSnapshot(const juce::var& identity,
            armix::builtin_fx::AutoDuckerProcessor* d,bool offline) {
        auto reply=std::make_unique<juce::DynamicObject>();
        for(const auto* key:{"requestId","channelId","deviceEpoch","graphRevision","instanceId"})
            reply->setProperty(key,identity.getProperty(key,{}));
        reply->setProperty("status",d?"ACCEPTED":"REJECTED");
        reply->setProperty("offline",offline);
        if(d){
            const auto s=d->acceptedSettings();const auto& v=s.dynamics;
            auto settings=std::make_unique<juce::DynamicObject>();
            settings->setProperty("thresholdDb",v.thresholdDb);settings->setProperty("depthDb",v.depthDb);
            settings->setProperty("attackMs",v.attackMs);settings->setProperty("holdMs",v.holdMs);
            settings->setProperty("releaseMs",v.releaseMs);settings->setProperty("hysteresisDb",v.hysteresisDb);
            settings->setProperty("highPassHz",v.highPassHz);settings->setProperty("lowPassHz",v.lowPassHz);
            reply->setProperty("settings",juce::var(settings.release()));
            juce::Array<juce::var> sources;
            for(std::size_t k=0;k<s.count;++k)sources.add(juce::String(std::to_string(s.sources[k])));
            reply->setProperty("sources",juce::var(sources));
            reply->setProperty("pending",!d->stateSnapshotQuiescent());
            armix::builtin_fx::AutoDuckerProcessor::Meter frame;
            if(!offline&&d->readMeter(frame)){
                reply->setProperty("meterSerial",juce::String(std::to_string(frame.serial)));
                reply->setProperty("gainReductionDb",std::max(0.f,-20.f*std::log10(std::max(1.e-8f,frame.gain))));
                reply->setProperty("phase",frame.phase);
                juce::Array<juce::var> meters;
                for(std::size_t k=0;k<frame.ids.size()&&frame.ids[k];++k){
                    auto entry=std::make_unique<juce::DynamicObject>();
                    entry->setProperty("id",juce::String(std::to_string(frame.ids[k])));
                    entry->setProperty("available",frame.available[k]);
                    entry->setProperty("dbfs",std::max(-100.f,10.f*std::log10(std::max(1.e-10f,frame.power[k]))));
                    meters.add(juce::var(entry.release()));
                }
                reply->setProperty("meters",juce::var(meters));
            }
        }
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixBuiltinDuckerSnapshot",juce::var(reply.release()));
    }

    // SonKu Native FX: one editor target = exact ChannelId/slot/instance/epoch.
    // Snapshot reads accepted CONTROL settings only; the single atomically
    // published angle is observation, never another audio motion clock.
    void emitNativeSonKuSnapshot(const juce::String& request,
            const juce::String& channelId,const juce::String& epoch,
            const juce::String& revision,const juce::String& instanceId,
            armix::builtin_fx::SonKuSpatialProcessor* processor) {
        auto reply=std::make_unique<juce::DynamicObject>();
        reply->setProperty("requestId",request);
        reply->setProperty("channelId",channelId);
        reply->setProperty("deviceEpoch",epoch);
        reply->setProperty("graphRevision",revision);
        reply->setProperty("instanceId",instanceId);
        reply->setProperty("schemaVersion",
            armix::builtin_fx::sonku_bridge::schemaVersion());
        reply->setProperty("status",processor?"ACCEPTED":"REJECTED");
#if defined(SONKU_P75_MUSIC_SAFE_RESEARCH) && SONKU_P75_MUSIC_SAFE_RESEARCH
        // Read-only experiment capability is authoritative only for a real
        // leased native SonKu instance. No public UI or saved state change.
        if(processor && processor->preparedReflectionProfile()==
                armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch)
            reply->setProperty("musicSafeStereo",true);
#endif
#if defined(SONKU_P80_TRANSPARENT_BASS_RESEARCH) && SONKU_P80_TRANSPARENT_BASS_RESEARCH
        if(processor && processor->preparedReflectionProfile()==
                armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch)
            reply->setProperty("p8TransparentBass",true);
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT
        if(processor && processor->preparedReflectionProfile()==
               armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch) {
            reply->setProperty("p84MusicFusion",true);
            reply->setProperty("p85AcousticCoherence",true);
            reply->setProperty("p86HorizontalFocus",true);
            reply->setProperty("p87MusicalAirBody",true);
            reply->setProperty("p88PerceptualAlignment",true);
            reply->setProperty("p89StereoRoomDepth",true);
            reply->setProperty("p90TrueExternalSpeaker",true);
            // This capability is emitted only by the new reconciled DSP build.
            // Older v11 P9.0 binaries must NOT be advertised as P9.1+P9.2.
            reply->setProperty("p91p92MusicBassFoundation",true);
#if defined(ARMIX_SONKU_EXPERIMENTAL_P95_MUSICAL_PRESENCE) && ARMIX_SONKU_EXPERIMENTAL_P95_MUSICAL_PRESENCE
            reply->setProperty("p95MusicalPresence",true);
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P96_DISTANCE_REINFORCEMENT) && ARMIX_SONKU_EXPERIMENTAL_P96_DISTANCE_REINFORCEMENT
            reply->setProperty("p96DistanceReinforcement",true);
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P97_STAGE_IMPACT) && ARMIX_SONKU_EXPERIMENTAL_P97_STAGE_IMPACT
            reply->setProperty("p97StageImpact",true);
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING) && ARMIX_SONKU_EXPERIMENTAL_P98_SCENE_VOICING
            reply->setProperty("p98SceneVoicing",true);
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH) && ARMIX_SONKU_EXPERIMENTAL_P99_VOCAL_DEPTH
            // Truthful native opt-in: P9.8 hosts must never claim P9.9.
            // No new JSON scene ID or state bytes; v12 reads back normally.
            reply->setProperty("p99MusicalVocalDepth",true);
#endif
#if defined(ARMIX_SONKU_EXPERIMENTAL_P94_FRONT_BACK_DEPTH) && ARMIX_SONKU_EXPERIMENTAL_P94_FRONT_BACK_DEPTH
            // Branch-only measured-engine A/B. A previous v11 binary cannot
            // advertise this front/rear perceptual depth research capability.
            reply->setProperty("p94FrontBackDepthCue",true);
#endif
        }
#elif defined(ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P89_NATIVE_RT
        if(processor && processor->preparedReflectionProfile()==
               armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch) {
            reply->setProperty("p84MusicFusion",true);
            reply->setProperty("p85AcousticCoherence",true);
            reply->setProperty("p86HorizontalFocus",true);
            reply->setProperty("p87MusicalAirBody",true);
            reply->setProperty("p88PerceptualAlignment",true);
            reply->setProperty("p89StereoRoomDepth",true);
        }
#elif defined(ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P88_NATIVE_RT
        if(processor && processor->preparedReflectionProfile()==
               armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch) {
            reply->setProperty("p84MusicFusion",true);
            reply->setProperty("p85AcousticCoherence",true);
            reply->setProperty("p86HorizontalFocus",true);
            reply->setProperty("p87MusicalAirBody",true);
            reply->setProperty("p88PerceptualAlignment",true);
        }
#elif defined(ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P87_NATIVE_RT
        if(processor && processor->preparedReflectionProfile()==
               armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch) {
            reply->setProperty("p84MusicFusion",true);
            reply->setProperty("p85AcousticCoherence",true);
            reply->setProperty("p86HorizontalFocus",true);
            reply->setProperty("p87MusicalAirBody",true);
        }
#elif defined(ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P86_NATIVE_RT
        if(processor && processor->preparedReflectionProfile()==
               armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch) {
            reply->setProperty("p84MusicFusion",true);
            reply->setProperty("p85AcousticCoherence",true);
            reply->setProperty("p86HorizontalFocus",true);
        }
#elif defined(ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P85_NATIVE_RT
        if(processor && processor->preparedReflectionProfile()==
               armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch) {
            reply->setProperty("p84MusicFusion",true);
            reply->setProperty("p85AcousticCoherence",true);
        }
#elif defined(ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P84_NATIVE_RT
        if(processor && processor->preparedReflectionProfile()==
               armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch)
            reply->setProperty("p84MusicFusion",true);
#elif defined(ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT) && ARMIX_SONKU_EXPERIMENTAL_P83_NATIVE_RT
        if(processor && processor->preparedReflectionProfile()==
               armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch)
            reply->setProperty("p83SpeakerModes",true);
#endif
#if defined(SONKU_P82_MULTIBAND_ROOM_RESEARCH) && SONKU_P82_MULTIBAND_ROOM_RESEARCH
        // Read-only P8.2 room capability; only a real admitted measured
        // SonKu FX instance can claim it in the identity-bound snapshot.
        if(processor && processor->preparedReflectionProfile()==
                armix::sonku::SonKuReflectionProfile::SixSurfaceKemarResearch)
            reply->setProperty("p82MultibandRoom",true);
#endif
        if(processor) {
            reply->setProperty("settings",
                armix::builtin_fx::sonku_bridge::acceptedSettings(
                    processor->acceptedSettings()));
            reply->setProperty("observedAzimuthDegrees",
                processor->observedAzimuthDegrees());
            if(epoch=="0" && instanceId==lastLogicalFxInstance_ &&
               revision==lastLogicalFxRevision_)
                reply->setProperty("appliedRequestId",lastLogicalFxRequest_);
            else if(instanceId==lastNativeSonKuInstance_ &&
               epoch==lastNativeSonKuEpoch_ &&
               revision==lastNativeSonKuRevision_)
                reply->setProperty("appliedRequestId",lastNativeSonKuRequest_);
        } else reply->setProperty("error","SONKU_INSTANCE_OR_REVISION_UNAVAILABLE");
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinSonKuSnapshot",juce::var(reply.release()));
    }

    void servicePendingNativeSonKu() {
        if(!pendingNativeSonKu_.owner)return;
        auto* processor=dynamic_cast<armix::builtin_fx::SonKuSpatialProcessor*>(
            pendingNativeSonKu_.owner->processor.get());
        if(!processor){pendingNativeSonKu_={};return;}
        armix::builtin_fx::SonKuSpatialProcessor::Completion result{};
        const bool complete=processor->pollCompletion(result);
        if(!complete && juce::Time::getMillisecondCounter()-
               pendingNativeSonKu_.started<3000u)return;
        const auto request=pendingNativeSonKu_.request;
        bool applied=complete&&result.applied&&result.requestId!=0 &&
            request=="sonku-"+juce::String(std::to_string(result.requestId));
        const auto snap=approvedNativeSnapshot();
        const auto device=snap.getProperty("device",{});
        const auto graph=snap.getProperty("graph",{});
        applied=applied&&pendingNativeSonKu_.deviceEpoch==
                    device.getProperty("epoch","").toString()&&
                    pendingNativeSonKu_.revision==
                    graph.getProperty("revision","").toString();
        if(applied) {
            lastNativeSonKuRequest_=request;
            lastNativeSonKuInstance_=pendingNativeSonKu_.instanceId;
            lastNativeSonKuEpoch_=pendingNativeSonKu_.deviceEpoch;
            lastNativeSonKuRevision_=pendingNativeSonKu_.revision;
        }
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",request);
        ack->setProperty("channelId",pendingNativeSonKu_.channelId);
        ack->setProperty("deviceEpoch",pendingNativeSonKu_.deviceEpoch);
        ack->setProperty("graphRevision",pendingNativeSonKu_.revision);
        ack->setProperty("instanceId",pendingNativeSonKu_.instanceId);
        ack->setProperty("status",applied?"APPLIED":"REJECTED");
        ack->setProperty("errorCode",applied?"NONE":"SONKU_EDIT_NOT_APPLIED_OR_STALE");
        pendingNativeSonKu_={};
        if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
            "arMixBuiltinSonKuAck",juce::var(ack.release()));
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
        const bool delaySafe=!nativeShowBusy_&&!studioBackupBusy_&&!deviceApply_.busy()&&
            !engine_.isGraphSwitchPending()&&!engine_.independentClockActive();
        const armix::builtin_fx::vocal_delay::MusicObservation observation{
            lastMusicTempo_.source.value,epoch,revision,lastMusicTempo_.tempo.observationFrame,
            bpm,lastMusicTempo_.tempo.confidence,bpmLocked&&delaySafe};
        const double now=juce::Time::getMillisecondCounterHiRes();
        engine_.forEachNativeProcessor([bpm,anchor,epoch,revision,observation,now](
            armix::mixer::ChannelId ch,juce::AudioProcessor* p) {
            if(auto* vocal=dynamic_cast<armix::builtin_fx::VocalThrowProcessor*>(p))
                vocal->publishMusicGrid(bpm,anchor,epoch,revision);
            if(auto* delay=dynamic_cast<armix::builtin_fx::vocal_delay::JuceVocalDelayProcessor*>(p))
                delay->publishMusicObservation(observation,ch.value,now);
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

    juce::String restoreSavedMixerSession(bool startupOnSavedDevice=false,
            armix::session::MixerDeviceBindingPolicy::Mode bindingMode=
                armix::session::MixerDeviceBindingPolicy::Mode::exact) {
        if(vendorSafeRecoveryActive()||engine_.hasRuntimeFxFault()) {
            mixerSessionRecoveryPending_=true;
            mixerSessionStatus_="STARTUP_FX_SAFE_RECOVERY";
            return "Previous vendor lifetime ended unexpectedly. Open a verified .smix manually; MAIN remains disarmed automatically.";
        }
        // Device auto-restore must NEVER overwrite newer offline VST/built-in
        // state with the older retained DSP graph. Only the operator's
        // explicit .smix Open may replace this pending durable document.
        if(pendingRetainedFxCapture_)return "Offline mixer workspace is preparing";
        if(retainedOfflineFxActive_) {
            mixerSessionRecoveryPending_=true;
            mixerSessionStatus_="RETAINED_OFFLINE_FX_REAPPLY_REQUIRED";
            return "Offline FX edits are pending: Apply Audio I/O";
        }
        using Recovery=armix::session::NativeMixerSessionRecovery;
        Recovery::RestoreContext restore;
        restore.outputsDisarmed=engine_.allOutputsDisarmed();
        restore.workerBusy=mixerSessionWorker_.busy();
        restore.vendorEditorOpen=logicalVendorEditor_.has_value();
        restore.showBusy=nativeShowBusy_;
        restore.backupBusy=studioBackupBusy_;
        restore.graphPending=engine_.isGraphSwitchPending();
        const auto blocked=Recovery::restoreBlocker(restore);
        if(blocked)return blocked;
        const armix::session::NativeMixerSessionStore store(settingsDirectory());
        armix::session::MixerSessionDocument doc;
        std::vector<armix::shows::BinaryState> states;
        const auto checkpoint=Recovery::read(store,engine_.hasRetainedNativeSession(),
            doc,states,[this](auto& retainedDoc,auto& retainedStates) {
                const auto error=engine_.captureNativeMixerSession(retainedDoc,retainedStates,true);
                if(error.isNotEmpty())return error;
                retainedDoc.id=mixerSessionId_;
                return resolveMixerSessionCatalog(retainedDoc,true);
            });
        if(checkpoint.source==Recovery::Source::missing) {
            mixerSessionStatus_="NO_SAVED_SESSION";
            return checkpoint.error;
        }
        if(checkpoint.error.isNotEmpty()) {
            mixerSessionStatus_=checkpoint.source==Recovery::Source::retained
                ?"RETAINED_SESSION_UNAVAILABLE":"DAMAGED_SESSION";
            mixerSessionError_=checkpoint.error;
            diagnostics_.record("DYNAMIC_MIXER_READ_REJECTED",checkpoint.error);
            return checkpoint.error;
        }
        const bool usedPrevious=checkpoint.source==Recovery::Source::previous;
        const bool firstBinding=doc.logicalOnly;
        const auto isNativeMixerActive=[this] {
            const auto live=engine_.status().getProperty("dryRoutingLab",{});
            return static_cast<bool>(live.getProperty("nativeChannelIdPcmActive",false));
        };
        if(usedPrevious)
            (void)logicalMixer_.restoreCheckpoint(doc,states);
        const auto restoredId=doc.id;
        const auto restoredHotkeys=doc.hotkeys;
        const auto acceptSessionIdentity=[this,&restoredId,&restoredHotkeys] {
            mixerSessionId_=restoredId;
            (void)hotkeys_.activateSession(mixerSessionId_);
            if(hotkeys_.sessionEntries().empty() && !restoredHotkeys.empty()) {
                const auto issue=hotkeys_.activateSession(mixerSessionId_,&restoredHotkeys);
                if(issue.isNotEmpty())diagnostics_.record("HOTKEY_IMPORT_REJECTED",issue);
            }
        };
        if(firstBinding && !engine_.hasPristineStartupMixer()) {
            // A deferred offline binding cannot take ownership of a live
            // mixer's session ID or activate its own session hotkeys.
            if(!isNativeMixerActive())acceptSessionIdentity();
            mixerSessionRecoveryPending_=true;
            mixerSessionStatus_="LOGICAL_SESSION_SAVED_UNBOUND";
            return "Saved mixer is offline and awaiting verified device binding";
        }
        auto error=materializeLogicalFxForBinding(doc,states);
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
        // Query current admission after FX preparation, immediately before
        // the engine is asked to publish a physical graph.
        const bool nativeAlreadyActive=isNativeMixerActive();
        const auto plan=Recovery::plan(doc,{bindingMode,checkpoint.source,
            startupOnSavedDevice,mixerSessionRecoveryPending_,nativeAlreadyActive,
            engine_.hasPristineStartupMixer(),firstBinding});
        if(!plan.restoreAllowed) {
            mixerSessionStatus_="LIVE_MIXER_UNCHANGED";
            return "Live native mixer already edited; refusing to replace it";
        }
        error=engine_.restoreNativeMixerSession(std::move(doc),states,
            plan.replacePristineStartup,nullptr,plan.bindingMode);
        if(error.isNotEmpty()) {
            mixerSessionStatus_=error.contains("NEEDS_REMAP")
                ?"NEEDS_REMAP":"RESTORE_REJECTED";
            mixerSessionError_=error;
            diagnostics_.record("DYNAMIC_MIXER_RESTORE_REJECTED",error);
            return error;
        }
        mixerSessionRecoveryPending_=false;
        acceptSessionIdentity();
        if(plan.resumeMain)
            requestAutomaticMain(plan.bindingMode!=MixerBindingMode::verifiedAsioRestart);
        mixerSessionStatus_=engine_.isMonitorEnabled()?"RESTORED_MONITORING":"RESTORED_MUTED";
        mixerSessionError_.clear();
        diagnostics_.record("DYNAMIC_MIXER_RESTORED",mixerSessionStatus_);
        if(usedPrevious)emitNotice("Last verified mixer checkpoint restored with MAIN muted.",true);
        emitApprovedNativeSnapshot();
        return {};
    }

    juce::String saveMixerSession(bool resumeMain, const juce::File& exportFile={},
                                    bool shutdownCheckpoint=false) {
        const bool automatic=exportFile==juce::File{};
        if(automatic && vendorSafeRecoveryActive())return "Safe recovery: saved mixer checkpoint preserved";
        if(automatic && engine_.hasRuntimeFxFault())return "Faulted VST3: saved mixer checkpoint preserved";
        // The committed logical FX save is newer than the restored live
        // processor. Auto/shutdown capture MUST NOT replace its binary
        // VST state until an explicit verified session Open adopts it.
        if(pendingRetainedFxCapture_)return "Offline mixer workspace is preparing";
        if(retainedOfflineFxActive_ && automatic)return {};
        ensureLogicalWorkspace();
        using Recovery=armix::session::NativeMixerSessionRecovery;
        const auto liveState=engine_.status();
        const auto live=liveState.getProperty("dryRoutingLab",{});
        const auto session=approvedNativeSnapshot(liveState)
            .getProperty("mixerSession",{});
        // JUCE AudioIODevice::isOpen() is deliberately non-const.
        auto* audio=engine_.devices().getCurrentAudioDevice();
        Recovery::SaveContext save;
        save.automatic=automatic;
        save.workerBusy=mixerSessionWorker_.busy();
        save.showBusy=nativeShowBusy_;
        save.backupBusy=studioBackupBusy_;
        save.catalogBusy=catalog_.busy();
        save.graphPending=engine_.isGraphSwitchPending();
        save.deviceBusy=deviceApply_.busy();
        save.vendorEditorOpen=logicalVendorEditor_.has_value();
        save.readOnly=logicalMixer_.readOnly();
        save.channelIdInstalled=static_cast<bool>(
            live.getProperty("nativeChannelIdPcmActive",false));
        save.m4Installed=session.getProperty("authority","").toString()=="M4";
        save.secondaryArmed=engine_.secondaryArmed();
        save.retainedNative=engine_.hasRetainedNativeSession();
        // Only a successfully captured, committed offline FX workspace
        // can override retained native Save rejection. The logical .smix
        // store stays the source even if a device reconnects before Open.
        save.retainedOfflinePending=retainedOfflineFxActive_ &&
            logicalMixer_.loaded() && !logicalMixer_.readOnly();
        save.recoveryPending=mixerSessionRecoveryPending_;
        save.shutdownCheckpoint=shutdownCheckpoint;
        save.physicalOpen=audio!=nullptr && audio->isOpen();
        save.directAuthorized=engine_.directRouteAuthorized();
        save.mainInterrupted=engine_.mainInterruptedByDevice();
        const auto decision=Recovery::planSave(save);
        if(decision.action==Recovery::SaveAction::reject)
            return juce::String(decision.error);
        if(decision.action==Recovery::SaveAction::unchanged)
            return {}; // Offline autosave: prior logical transaction already committed HEAD.
        if(decision.action==Recovery::SaveAction::exportLogical) {
            auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
            if(!logicalMixer_.exportTo(exportFile,[safe](juce::String error){
                if(safe!=nullptr)safe->emitMixerSessionAck("SAVE",error);
            }))return "Mixer session IO worker unavailable";
            return {};
        }
        // Only a CONTROL-approved installed session reaches the temporary
        // output capture fence; never move plugin capture to the audio callback.
        const bool monitoring=engine_.isMonitorEnabled();
        const auto epoch=engine_.dryRoutingLabEpoch();
        // Temporary capture fence is NOT an operator MUTE. Keep the
        // persisted MAIN desire intact across device loss during Save.
        if(monitoring)engine_.setOutputMuted(true);
        armix::session::MixerSessionDocument doc;
        std::vector<armix::shows::BinaryState> states;
        auto error=engine_.captureNativeMixerSession(doc,states);
        if(monitoring && epoch==engine_.dryRoutingLabEpoch() &&
           engine_.liveOutputArmFault(false).isEmpty())
            engine_.setOutputMuted(false); // restore the existing operator intent
        if(error.isNotEmpty())return error;
        // The periodic autosave caller passes false because speakers must
        // be gated for snapshot capture. That does NOT mean the operator
        // disabled MAIN. Capture desired intent independently of the gate.
        if(automatic)doc.resumeMain=resumeMain ||
            (engine_.wantsMainContinuation() && !operatorOutputMuted_);
        doc.id=mixerSessionId_.isNotEmpty()?mixerSessionId_:
            juce::Uuid().toDashedString().toLowerCase();
        // Shortcut entries are additive .smix data, never callback state.
        if(hotkeys_.activeSession()==doc.id)
            doc.hotkeys=hotkeys_.sessionEntries();
        // Use the verified installed vendor identity; never load using a
        // browser-provided plugin name or relocate an arbitrary VST3 path.
        error=resolveMixerSessionCatalog(doc,true);
        if(error.isNotEmpty())return error;
        const auto staged=Recovery::stageSave(doc,automatic,
            lastMixerSessionFingerprint_,juce::Time::currentTimeMillis());
        if(staged.error.isNotEmpty())return staged.error;
        if(staged.unchanged)return {};
        const auto fingerprint=staged.fingerprint;
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

    void openMixerFile(const juce::File& file,bool archived=false,
                       juce::String archiveToken={},std::uint64_t archiveRevision=0) {
        if(archived && (!canCreateOfflineSession() ||
            logicalMixer_.workspace().workspaceToken()!=archiveToken ||
            logicalMixer_.workspace().revision()!=archiveRevision))
            return emitMixerSessionAck("ARCHIVE","Offline session changed; select the archive again");
        if(pendingRetainedFxCapture_)return emitMixerSessionAck("OPEN","Offline mixer workspace is preparing");
        autoMainEpoch_=0;
        const auto chosenEpoch=engine_.dryRoutingLabEpoch();
        auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
        if(!mixerSessionWorker_.load(file,[safe,chosenEpoch,file,archived,archiveToken,archiveRevision](juce::String error,
                armix::session::MixerSessionDocument doc,
                std::vector<armix::shows::BinaryState> states) {
            if(safe==nullptr)return;
            auto& engine=safe->engine_;
            using Recovery=armix::session::NativeMixerSessionRecovery;
            const auto liveState=engine.status();
            const auto lab=liveState.getProperty("dryRoutingLab",{});
            const auto session=safe->approvedNativeSnapshot(liveState)
                .getProperty("mixerSession",{});
            Recovery::OpenContext opening;
            opening.operationBusy=safe->nativeShowBusy_ || safe->studioBackupBusy_ ||
                safe->catalog_.busy() || safe->deviceApply_.busy() ||
                engine.isGraphSwitchPending();
            opening.vendorEditorOpen=safe->logicalVendorEditor_.has_value();
            opening.epochChanged=chosenEpoch!=engine.dryRoutingLabEpoch();
            opening.secondaryArmed=engine.secondaryArmed();
            opening.channelIdInstalled=static_cast<bool>(
                lab.getProperty("nativeChannelIdPcmActive",false));
            opening.m4Installed=session.getProperty("authority","").toString()=="M4";
            opening.retainedNative=engine.hasRetainedNativeSession();
            const auto decision=Recovery::planOpen(opening);
            if(error.isEmpty() && decision.action==Recovery::OpenAction::reject)
                error=decision.error;
            if(archived && (!safe->canCreateOfflineSession() ||
                safe->logicalMixer_.workspace().workspaceToken()!=archiveToken ||
                safe->logicalMixer_.workspace().revision()!=archiveRevision ||
                decision.action!=Recovery::OpenAction::openLogical))
                error="Offline session changed; select the archive again";
            if(archived && error.isNotEmpty())return safe->emitMixerSessionAck("ARCHIVE",error);
            const auto importedHotkeys=doc.hotkeys;
            const auto importedId=doc.id;
            if(error.isEmpty() && decision.action==Recovery::OpenAction::openLogical) {
                // Open the real .smix into the same stable-ID logical
                // document. Never demand audio hardware or instantiate VST3
                // just to inspect and edit a saved mixer.
                if(!safe->logicalMixer_.open(std::move(doc),std::move(states),
                        [safe,file,importedHotkeys,importedId,archived](juce::String openError){
                    if(safe==nullptr || safe->sessionLifecycleShuttingDown_)return;
                    if(openError.isEmpty()) {
                        // Explicit saved-session Open supersedes the previous
                        // offline FX document only after durable commit.
                        safe->retainedOfflineFxActive_=false;
                        safe->retainedOfflineEpoch_=0;
                        safe->retainedOfflineOwnerEpoch_=0;
                        safe->retainedOfflineRevision_=0;
                        ++safe->workspaceGeneration_; // durable logical Open/Archive commit
                        safe->mixerSessionId_=importedId;
                        const auto keyError=safe->hotkeys_.activateSession(importedId,&importedHotkeys);
                        if(keyError.isNotEmpty())safe->emitNotice("Hotkey import: "+keyError,true);
                        safe->mixerSessionStatus_="LOGICAL_SESSION_OPENED_UNBOUND";
                        safe->lastMixerFile_=archived?juce::File{}:file;
                        if(archived){
                            safe->operatorOutputMuted_=true;
                            safe->engine_.setMonitorEnabled(false);
                            safe->closeAllPluginEditors();
                            safe->stopOrbit();
                        }
                    }
                    safe->emitMixerSessionAck(archived?"ARCHIVE":"OPEN",openError);
                    if(openError.isEmpty())safe->emitApprovedNativeSnapshot();
                },archived))safe->emitMixerSessionAck(archived?"ARCHIVE":"OPEN","Mixer session IO worker unavailable");
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
                const bool monitoring=engine.isMonitorEnabled()&&!safe->vendorSafeRecoveryActive();
                error=engine.restoreNativeMixerSession(std::move(doc),states,true,&sameOutputs);
                if(error.isEmpty()&&!sameOutputs)
                    engine.setMonitorEnabled(false); // revoke stale physical authorization
                if(monitoring&&sameOutputs&&error.isEmpty()&&
                   chosenEpoch==engine.dryRoutingLabEpoch()&&engine.liveOutputArmFault(false).isEmpty())
                    engine.setMonitorEnabled(true);
                if(error.isEmpty()) {
                    // Explicit verified physical .smix Open supersedes pending
                    // offline FX authority; a device reconnect cannot do so.
                    safe->retainedOfflineFxActive_=false;
                    safe->retainedOfflineEpoch_=0;
                    safe->retainedOfflineOwnerEpoch_=0;
                    safe->retainedOfflineRevision_=0;
                    safe->closeAllPluginEditors();
                    safe->stopOrbit();
                    ++safe->workspaceGeneration_; // admitted physical Open commit
                    safe->mixerSessionId_=id;
                    const auto keyError=safe->hotkeys_.activateSession(id,&importedHotkeys);
                    if(keyError.isNotEmpty())safe->emitNotice("Hotkey import: "+keyError,true);
                    safe->mixerSessionRecoveryPending_=false;
                    safe->lastMixerSessionFingerprint_.clear();
                    safe->mixerSessionStatus_=engine.isMonitorEnabled()?"RESTORED_MONITORING":"RESTORED_MUTED";
                    safe->mixerSessionError_.clear();
                    safe->lastMixerFile_=file;
                    safe->startupFxSafeMode_=false;
                    armix::startup::RuntimeVendorCrashJournal::process().acknowledgeExplicitRecovery();
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
        }))emitMixerSessionAck(archived?"ARCHIVE":"OPEN","Mixer file operation already in progress");
    }

    // File paths never authorize loading executable code. Match the exact
    // registered native catalog before passing anything to the FX factory.
    juce::String resolveRackFileCatalog(armix::storage::FxRackFile& rack,bool capture) {
        for(auto& fx:rack.effects){
            if(armix::builtin_fx::findNativeBuiltInFx(fx.catalogId)){
                if(fx.path.isNotEmpty()||fx.uid!=0)return "Invalid built-in FX identity";
                continue;
            }
            const auto* registered=catalog_.find(fx.catalogId);
            if(!registered||registered->key()!=fx.catalogId||
               (!capture&&(registered->path!=fx.path||registered->uid!=fx.uid)))
                return "Registered VST3 unavailable: "+fx.catalogId;
            fx.path=registered->path;fx.uid=registered->uid;fx.name=registered->name;
        }
        return {};
    }
    void chooseFxRackFile(const juce::var& args) {
        const auto op=args.getProperty("operation",{}).toString();
        const bool deactivate=op=="DEACTIVATE",save=op=="SAVE"||deactivate,append=op=="INSERT";
        const auto token=args.getProperty("instanceId",{}).toString();
        const auto slotValue=args.getProperty("slot",{});
        const int slot=deactivate&&slotValue.isInt()?static_cast<int>(slotValue):-1;
        std::uint64_t ep=0,rev=0,roster=0,id=0;
        if((!save&&!append&&op!="OPEN")||(deactivate&&(slot<0||slot>=8||token.isEmpty()))||
           !armix::console::approvedNativeId(args.getProperty("deviceEpoch",{}),ep)||
           !armix::console::approvedNativeId(args.getProperty("graphRevision",{}),rev)||
           !armix::console::approvedNativeId(args.getProperty("rosterRevision",{}),roster)||
           !armix::console::approvedNativeId(args.getProperty("channelId",{}),id))
            return emitNotice("FX rack request invalid; refresh rack",true);
        const armix::mixer::ChannelId channel{id};
        if(fxRackFileChooser_||namedIoFileChooser_||mixerFileDialogOpen_||
           deviceApply_.busy()||nativeShowBusy_||studioBackupBusy_||mixerSessionWorker_.busy()||
           catalog_.busy()||logicalVendorEditor_||!engine_.rackFileAllowed(ep,rev,roster,channel))
            return emitNotice("FX rack file: disarm MAIN and STREAM; use a current native rack",true);
        auto start=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        if(save)start=start.getChildFile(deactivate?"Saved-FX.arfx":"Vocal-Rack.arfx");
        fxRackFileChooser_=std::make_unique<juce::FileChooser>(
            deactivate?"Deactivate and save FX":save?"Save FX Rack":append?
                "Insert saved FX":"Open FX Rack (replace)",start,"*.arfx",true);
        using Flags=juce::FileBrowserComponent;
        fxRackFileChooser_->launchAsync(Flags::canSelectFiles|
            (save?(Flags::saveMode|Flags::warnAboutOverwriting):Flags::openMode),
            [safe=juce::Component::SafePointer<ConsoleWindow>(this),ep,rev,roster,channel,
             token,slot,save,deactivate,append](const juce::FileChooser& picker){
                if(!safe)return;
                const auto file=picker.getResult();
                juce::String error;
                bool archived=false;
                if(file!=juce::File{}){
                    try{
                        if(safe->deviceApply_.busy()||safe->nativeShowBusy_||safe->studioBackupBusy_||
                           safe->mixerSessionWorker_.busy()||safe->catalog_.busy()||safe->logicalVendorEditor_||
                           !safe->engine_.rackFileAllowed(ep,rev,roster,channel))
                            error="Device or rack changed; retry";
                        else{
                            armix::storage::FxRackFile rack;
                            if(save){
                                error=safe->engine_.captureRackFile(ep,rev,roster,channel,slot,token,rack);
                                if(error.isEmpty())error=safe->resolveRackFileCatalog(rack,true);
                                if(error.isEmpty())error=armix::storage::NativeFxRackArchive::save(file,rack);
                                archived=error.isEmpty();
                                if(archived&&deactivate)
                                    error=safe->engine_.applyRackFile(ep,rev,roster,channel,rack,false,slot,token);
                            }else{
                                error=armix::storage::NativeFxRackArchive::load(file,rack);
                                if(error.isEmpty())error=safe->resolveRackFileCatalog(rack,false);
                                if(error.isEmpty())error=safe->engine_.applyRackFile(ep,rev,roster,channel,rack,append);
                            }
                        }
                    }catch(...){error="FX rack file operation failed";}
                    if(error.isEmpty()){
                        if(!save||deactivate){
                            safe->stopOrbit();
                            safe->closeAllPluginEditors();
                            safe->emitApprovedNativeSnapshot();
                        }
                        safe->emitNotice(deactivate?"FX saved and deactivated; latency plan rebuilt. MAIN/STREAM remain disarmed.":
                            save?"FX Rack saved":append?"Saved FX inserted; output remains disarmed":
                            "FX Rack opened; output remains disarmed");
                    }else safe->emitNotice((archived?juce::String("FX saved; deactivation rejected: "):
                        juce::String("FX rack unchanged: "))+error,true);
                }
                juce::MessageManager::callAsync([safe]{if(safe)safe->fxRackFileChooser_.reset();});
            });
    }

    void chooseNamedIoProfile(const juce::var& args) {
        const auto request=args.getProperty("requestId",{}).toString();
        const auto op=args.getProperty("operation",{}).toString();
        if(!request.startsWith("io-file-")||request.length()>64)return;
        const bool save=op=="EXPORT";
        const auto emit=[this,&request,&op](const juce::String& error){
            auto result=std::make_unique<juce::DynamicObject>();
            result->setProperty("requestId",request);result->setProperty("operation",op);
            result->setProperty("ok",false);result->setProperty("error",error);
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixNamedProfileFileResult",juce::var(result.release()));
        };
        if((!save&&op!="OPEN")||namedIoFileChooser_||mixerFileDialogOpen_||
           nativeShowBusy_||studioBackupBusy_||mixerSessionWorker_.busy()||
           engine_.independentClockActive()||engine_.hasTemporaryDiagnosticDevice()||
           !namedPatches_.fileLease(args,currentDeviceSnapshot())){
            emit("I/O profile file operation unavailable or stale");return;
        }
        auto start=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        if(save)start=start.getChildFile(args.getProperty("side",{}).toString()+"-io.ario");
        namedIoFileChooser_=std::make_unique<juce::FileChooser>(
            save?"Export I/O presets":"Open I/O preset profile",start,"*.ario",true);
        using Flags=juce::FileBrowserComponent;
        namedIoFileChooser_->launchAsync(Flags::canSelectFiles |
            (save?(Flags::saveMode|Flags::warnAboutOverwriting):Flags::openMode),
            [safe=juce::Component::SafePointer<ConsoleWindow>(this),args,request,op,save]
            (const juce::FileChooser& picker){
                if(!safe)return;
                const auto file=picker.getResult();
                auto result=std::make_unique<juce::DynamicObject>();
                result->setProperty("requestId",request);result->setProperty("operation",op);
                result->setProperty("cancelled",file==juce::File{});
                juce::String error;
                if(file!=juce::File{}){
                    const auto active=safe->currentDeviceSnapshot();
                    if(safe->nativeShowBusy_||safe->studioBackupBusy_||safe->mixerSessionWorker_.busy()||
                       safe->engine_.independentClockActive()||safe->engine_.hasTemporaryDiagnosticDevice()||
                       !safe->namedPatches_.fileLease(args,active))
                        error="I/O profile device or revision changed";
                    else if(save){
                        const auto profile=safe->namedPatches_.exportProfile(args,active);
                        error=profile.isObject()?armix::io::NamedPatchStore::writeProfileFile(file,profile):
                            juce::String("No accepted presets to export");
                    }else{
                        juce::var profile;
                        error=armix::io::NamedPatchStore::readProfileFile(file,profile);
                        if(error.isEmpty()){
                            for(const char* key:{"backend","captureDevice","playbackDevice","side"})
                                if(profile.getProperty(key,{})!=args.getProperty(key,{}))
                                    error="I/O profile belongs to a different device or side";
                            if(error.isEmpty())result->setProperty("profile",profile);
                        }
                    }
                }
                result->setProperty("ok",error.isEmpty());result->setProperty("error",error);
                if(safe->browser_.loaded)safe->browser_.emitEventIfBrowserIsVisible(
                    "arMixNamedProfileFileResult",juce::var(result.release()));
                // Do not destroy the native chooser from its own callback.
                juce::MessageManager::callAsync([safe]{if(safe)safe->namedIoFileChooser_.reset();});
            });
    }

    void chooseMixerFile(bool save,bool archived=false) {
        if(archived && (!canCreateOfflineSession() ||
            !armix::session::NativeMixerSessionStore(settingsDirectory()).archiveDirectory().isDirectory()))
            return emitMixerSessionAck("ARCHIVE","No offline session archive available");
        const auto archiveToken=logicalMixer_.workspace().workspaceToken();
        const auto archiveRevision=logicalMixer_.workspace().revision();
        if(logicalVendorEditor_)return emitMixerSessionAck(save?"SAVE":"OPEN",
            "Close the vendor FX editor to save its settings first");
        if(mixerFileDialogOpen_)return;
        const auto operation=archived?"ARCHIVE":save?"SAVE":"OPEN";
        if(mixerSessionWorker_.busy()||nativeShowBusy_||studioBackupBusy_)
            return emitMixerSessionAck(operation,"Mixer file operation already in progress");
        auto start=archived?armix::session::NativeMixerSessionStore(settingsDirectory()).archiveDirectory():lastMixerFile_;
        if(start==juce::File{}) {
            start=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
            if(save)start=start.getChildFile("Mixer.smix");
        }
        mixerFileDialogOpen_=true;
        mixerFileChooser_=std::make_unique<juce::FileChooser>(
            archived?"Open archived mixer":save?"Save mixer":"Open mixer",start,"*.smix",true);
        using BrowserFlags=juce::FileBrowserComponent;
        mixerFileChooser_->launchAsync(BrowserFlags::canSelectFiles |
            (save?(BrowserFlags::saveMode|BrowserFlags::warnAboutOverwriting):BrowserFlags::openMode),
            [safe=juce::Component::SafePointer<ConsoleWindow>(this),save,operation,archived,archiveToken,archiveRevision]
            (const juce::FileChooser& picker) {
                if(safe==nullptr)return;
                safe->mixerFileDialogOpen_=false;
                auto file=picker.getResult();
                if(file==juce::File{})return;
                if(!save)return safe->openMixerFile(file,archived,archiveToken,archiveRevision);
                if(!file.hasFileExtension(".smix"))
                    return safe->emitMixerSessionAck(operation,"Use the .smix file extension");
                safe->lastMixerFile_=file;
                const auto error=safe->saveMixerSession(false,file);
                if(error.isNotEmpty())safe->emitMixerSessionAck(operation,error);
            });
    }

    // Versioned setlist v3. Song paths remain external and are NEVER moved,
    // deleted or copied by editing a show. v1/v2 migrate non-destructively.
    void activatePlayerSet(int index) {
        if(index<0 || index>=static_cast<int>(playerSets_.size()))return;
        playerActiveSet_=index;
        const auto& chosen=playerSets_[static_cast<std::size_t>(index)];
        playerLibrary_=chosen.files;
        playerTrackSettings_=chosen.controls;
        playerCurrent_=-1;
        playerAutoAdvanceGate_.rearm();
        playerLoopCurrent_=false;
        playerPendingIndex_=-1;
        playerPendingQueueFile_={};
        playerSelected_=playerLibrary_.empty()?-1:0;
        playerQueue_.clear();
        playerQueueEpoch_.changed(); // setlist switch invalidates drag token
        playerOutputPendingIndex_=-1;
        playerUndoRemove_={};
    }
    // The repository owns only durable Player JSON I/O. ConsoleWindow
    // retains the single deck, live selection and accepted native control.
    void restorePlayerLibrary() {
        playerSets_.clear();
        auto result=armix::player::PlayerSetlistRepository::load(
            settingsDirectory().getChildFile("player-library.json"));
        if(result.status==armix::player::PlayerSetlistRepository::LoadStatus::missing) {
            playerSets_.push_back({"My Setlist",{},{}});
            return;
        }
        if(!result.document) {
            playerSets_.push_back({"My Setlist",{},{}});
            playerError_="Setlists could not be read; no music files were changed";
            return;
        }
        auto& parsed=*result.document;
        playerSets_=std::move(parsed.sets);
        activatePlayerSet(parsed.active);
        playerAutoNext_=parsed.autoNext;
        playerFadeMilliseconds_=parsed.fadeMs;
    }
    bool commitPlayerMetadata(std::vector<juce::File> nextFiles,
                              std::vector<PlayerTrackSettings> nextControls,
                              bool nextAutoNext,int nextFadeMs) {
        using Edit=armix::player::PlayerSetlistEditTransaction;
        auto outcome=Edit::replaceActiveTracks(
            settingsDirectory().getChildFile("player-library.json"),
            playerSets_,playerActiveSet_,nextFiles,nextControls,
            nextAutoNext,nextFadeMs);
        if(!outcome.committed()) {
            playerError_=outcome.error;
            return false;
        }
        // File acceptance comes before EVERY durable in-memory mutation.
        playerSets_=std::move(outcome.sets);
        playerLibrary_=std::move(nextFiles);
        playerTrackSettings_=std::move(nextControls);
        playerAutoNext_=nextAutoNext;
        playerFadeMilliseconds_=nextFadeMs;
        return true;
    }
    bool commitPlayerTrackEdit(std::vector<juce::File> nextFiles,
                               std::vector<PlayerTrackSettings> nextControls) {
        return commitPlayerMetadata(std::move(nextFiles),
            std::move(nextControls),playerAutoNext_,playerFadeMilliseconds_);
    }
    bool commitPlayerPreferences(bool nextAutoNext,int nextFadeMs) {
        return commitPlayerMetadata(playerLibrary_,playerTrackSettings_,
                                    nextAutoNext,nextFadeMs);
    }
    void stopPlayerAfterUnrecoveredMetadata() {
        // Persist rejected AND native restore rejected. Never leave an
        // unrecorded live deck applying controls different from the setlist.
        if(playerPendingIndex_>=0)playerQueueEpoch_.changed();
        engine_.playerStop();
        playerCurrent_=-1;
        playerPendingIndex_=-1;
        playerPendingQueueFile_={};
        playerOutputPendingIndex_=-1;
        playerLoopCurrent_=false;
        playerAutoAdvanceGate_.rearm();
        playerError_="Could not save song settings or restore native playback; "
                     "Player stopped to keep setlist safe";
    }
    bool playerShowMaySwitchSet() {
        const auto state=engine_.playerStatus().getProperty("state","").toString();
        if(state=="PLAYING" || state=="PREPARING" || state=="PAUSED") {
            playerError_="Stop the current song before switching setlists";
            emitPlayerState();return false;
        }
        return !playerFolderBusy_;
    }
    void playerEditSetlist(const juce::String& action,const juce::var& value) {
        // Setlist metadata is durable BEFORE a deck stop or live state
        // mutation. A failed disk write cannot switch/delete a live set.
        if(action=="playerNewSetlist" || action=="playerSelectSetlist" ||
           action=="playerDeleteSetlist" || action=="playerRenameSetlist") {
            const bool rename=action=="playerRenameSetlist";
            if(!rename && !playerShowMaySwitchSet())return;
            using Edit=armix::player::PlayerSetlistEditTransaction;
            const auto command=action=="playerNewSetlist"?Edit::Command::create:
                action=="playerSelectSetlist"?Edit::Command::select:
                action=="playerDeleteSetlist"?Edit::Command::remove:
                Edit::Command::rename;
            const int index=rename || command==Edit::Command::create?
                -1:static_cast<int>(value);
            // Preserve the historical silent no-op for stale row IDs.
            if(!rename && command!=Edit::Command::create &&
               (index<0 || index>=static_cast<int>(playerSets_.size())))return;
            auto outcome=Edit::apply(
                settingsDirectory().getChildFile("player-library.json"),
                playerSets_,playerActiveSet_,playerLibrary_,playerTrackSettings_,
                playerAutoNext_,playerFadeMilliseconds_,command,index,
                rename?value.toString():juce::String{});
            if(!outcome.committed()) {
                playerError_=outcome.error;
                emitPlayerState(true);return;
            }
            playerSets_=std::move(outcome.sets);
            if(!rename) {
                engine_.playerStop();
                activatePlayerSet(outcome.active);
            }
            playerError_.clear();
            emitPlayerState(true);
            return;
        }
        if(action=="playerMoveSong") {
            const int from=static_cast<int>(value.getProperty("from",-1));
            const int to=static_cast<int>(value.getProperty("to",-1));
            const int count=static_cast<int>(playerLibrary_.size());
            if(from<0 || to<0 || from>=count || to>=count || from==to)return;
            auto plan=armix::player::PlayerLibraryEditPlan::move(
                playerLibrary_,playerTrackSettings_,from,to);
            if(!plan)return;
            if(!commitPlayerTrackEdit(
                std::move(plan->files),std::move(plan->controls))) {
                emitPlayerState(true);return;
            }
            const auto remap=[&](int index) {
                return armix::player::PlayerShowEditPolicy::afterMove(
                    index,from,to,count);
            };
            playerSelected_=remap(playerSelected_);
            playerCurrent_=remap(playerCurrent_);
            playerPendingIndex_=remap(playerPendingIndex_);
            playerError_.clear();emitPlayerState(true);
            return;
        }
        if(action=="playerRemoveSong") {
            const int index=static_cast<int>(value);
            if(index<0 || index>=static_cast<int>(playerLibrary_.size()))return;
            if(index==playerPendingIndex_) {
                playerError_="NEXT is preparing; wait before removing this song";
                emitPlayerState();return;
            }
            if(index==playerCurrent_ &&
               engine_.playerStatus().getProperty("state","").toString()!="ENDED" &&
               engine_.playerStatus().getProperty("state","").toString()!="STOPPED") {
                playerError_="Stop the playing song before removing it";
                emitPlayerState();return;
            }
            const auto removedFile=playerLibrary_[static_cast<std::size_t>(index)];
            const auto removedControls=playerTrackSettings_[static_cast<std::size_t>(index)];
            auto plan=armix::player::PlayerLibraryEditPlan::remove(
                playerLibrary_,playerTrackSettings_,index);
            if(!plan)return;
            if(!commitPlayerTrackEdit(
                std::move(plan->files),std::move(plan->controls))) {
                emitPlayerState(true);return;
            }
            playerUndoRemove_={playerActiveSet_,index,removedFile,removedControls};
            const auto queueCountBefore=playerQueue_.size();
            playerQueue_.erase(
                std::remove(playerQueue_.begin(),playerQueue_.end(),
                            playerUndoRemove_.file),playerQueue_.end());
            if(queueCountBefore!=playerQueue_.size())
                playerQueueEpoch_.changed();
            const auto remap=[&](int old) {
                return armix::player::PlayerShowEditPolicy::afterRemove(
                    old,index);
            };
            playerCurrent_=remap(playerCurrent_);
            playerSelected_=remap(playerSelected_);
            playerPendingIndex_=remap(playerPendingIndex_);
            if(playerSelected_<0 && !playerLibrary_.empty())
                playerSelected_=std::min(index,
                    static_cast<int>(playerLibrary_.size())-1);
            playerError_.clear();emitPlayerState(true);
            return;
        }
        if(action=="playerUndoRemove") {
            const auto removed=playerUndoRemove_;
            if(removed.set!=playerActiveSet_ || removed.index<0)return;
            if(!armix::player::PlayerSetlistArchive::canUndoRemove(
                    playerActiveSet_,removed.set,removed.index,
                    playerLibrary_.size(),playerTrackSettings_.size())) {
                // The removed slot may have been filled by an import. Do not
                // create 2,001 songs or block all subsequent session saves.
                playerError_="Setlist is full; remove a song before undoing";
                emitPlayerState(true);return;
            }
            if(std::find(playerLibrary_.begin(),playerLibrary_.end(),
                         removed.file)!=playerLibrary_.end())return;
            auto plan=armix::player::PlayerLibraryEditPlan::undo(
                playerLibrary_,playerTrackSettings_,removed.index,
                removed.file,removed.controls);
            if(!plan)return;
            const int index=plan->first;
            if(!commitPlayerTrackEdit(
                std::move(plan->files),std::move(plan->controls))) {
                emitPlayerState(true);return;
            }
            playerCurrent_=armix::player::PlayerShowEditPolicy::afterInsert(
                playerCurrent_,index);
            playerSelected_=armix::player::PlayerShowEditPolicy::afterInsert(
                playerSelected_,index);
            playerPendingIndex_=armix::player::PlayerShowEditPolicy::afterInsert(
                playerPendingIndex_,index);
            playerUndoRemove_={};
            playerError_.clear();emitPlayerState(true);
            return;
        }
        if(action=="playerQueueNext" || action=="playerQueueAppend") {
            const int index=static_cast<int>(value);
            if(index<0 || index>=static_cast<int>(playerLibrary_.size()) ||
               playerQueue_.size()>=128)return;
            const auto file=playerLibrary_[static_cast<std::size_t>(index)];
            if(action=="playerQueueNext" && playerPendingIndex_>=0) {
                playerError_="NEXT is transitioning; queue changes after ACK";
                emitPlayerState();return;
            }
            if(action=="playerQueueNext")
                playerQueue_.insert(playerQueue_.begin(),file);
            else playerQueue_.push_back(file);
            playerQueueEpoch_.changed();
            // Queue edits are explicit operator decisions. If a previous
            // automatic attempt failed, allow the revised queue to retry.
            if(playerPendingIndex_<0)playerAutoAdvanceGate_.rearm();
            // A manually queued NEXT deserves a warm decoder even with
            // Auto Next OFF. Only CONTROL does file/preload work.
            if(playerPendingIndex_<0 && !playerQueue_.empty() &&
               playerCurrent_>=0) {
                const auto found=std::find(playerLibrary_.begin(),
                    playerLibrary_.end(),playerQueue_.front());
                if(found!=playerLibrary_.end()) {
                    const auto n=static_cast<std::size_t>(
                        found-playerLibrary_.begin());
                    const auto preset=playerTrackSettings_[n];
                    (void)engine_.preloadPlayerMusic(playerQueue_.front(),
                        preset.key,preset.tempo,preset.cueIn,preset.cueOut);
                }
            }
            playerError_.clear();emitPlayerState(true,false);return;
        }
        if(action=="playerQueueMove" || action=="playerQueueRemove") {
            const int from=static_cast<int>(value.getProperty("from",-1));
            const int requestedRevision=static_cast<int>(
                value.getProperty("revision",-1));
            // The native queue is authoritative. Do not trust a row index
            // from stale WebView state following a successful native ACK.
            if(!playerQueueEpoch_.accepts(
                    requestedRevision,playerPendingIndex_>=0)) {
                playerError_=playerPendingIndex_>=0?
                    "NEXT is transitioning; queue changes after ACK":
                    "Queue changed; retry on the refreshed queue";
                emitPlayerState(true,false);return;
            }
            if(from<0 || from>=static_cast<int>(playerQueue_.size()))return;
            if(action=="playerQueueMove") {
                const int to=static_cast<int>(value.getProperty("to",-1));
                if(!armix::player::PlayerShowTransitionPolicy::moveQueued(
                    playerQueue_,from,to))return;
            } else playerQueue_.erase(playerQueue_.begin()+from);
            playerQueueEpoch_.changed();
            playerAutoAdvanceGate_.rearm();
            playerError_.clear();
            emitPlayerState(true,false);return;
        }
    }
    void emitPlayerState(bool includeLibrary=false,
                         bool includeTracks=true) {
        if(!browser_.loaded)return;
        using Projection=armix::player::PlayerStateProjection;
        const auto snapshot=Projection::build({
            playerLibrary_,playerTrackSettings_,playerSets_,playerQueue_,
            engine_.playerStatus(),playerError_,
            playerSelected_,playerCurrent_,playerActiveSet_,playerPendingIndex_,
            playerQueueEpoch_.revision(),playerFadeMilliseconds_,
            playerUndoRemove_.set,playerUndoRemove_.index,
            playerAutoNext_,playerLoopCurrent_,playerFolderBusy_
        },includeLibrary,includeTracks);
        if(!snapshot.isObject()) {
            playerError_="Player library state is inconsistent; no snapshot published";
            return;
        }
        browser_.emitEventIfBrowserIsVisible("arMixPlayerState",snapshot);
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
                const auto plan=armix::player::PlayerLibraryEditPlan::append(
                    safe->playerLibrary_,safe->playerTrackSettings_,
                    picker.getResults());
                if(!plan) {
                    safe->playerError_="Unable to import music; invalid library state";
                    safe->emitPlayerState(true);
                } else {
                    const int first=plan->first;
                    if(safe->commitPlayerTrackEdit(
                        std::move(plan->files),std::move(plan->controls))) {
                        if(safe->playerSelected_<0 && first>=0)
                            safe->playerSelected_=first;
                        safe->playerError_.clear();
                    }
                    safe->emitPlayerState(true);
                }
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
                            auto plan=armix::player::PlayerLibraryEditPlan::folder(
                                initialLibrary,safe->playerLibrary_,
                                safe->playerTrackSettings_,scan,relinks,
                                safe->playerCurrent_);
                            if(!plan) {
                                safe->playerError_="Unable to import folder; invalid library state";
                                safe->emitPlayerState(true);return;
                            }
                            const int added=plan->added,relinked=plan->relinked;
                            const int first=plan->first;
                            if(!safe->commitPlayerTrackEdit(
                                  std::move(plan->files),std::move(plan->controls))) {
                                safe->emitPlayerState(true);return;
                            }
                            if(safe->playerSelected_<0 && first>=0)
                                safe->playerSelected_=first;
                            safe->playerError_=scan.truncated?
                                "Folder scan reached its safe size limit; only some songs were found":
                                added==0 && relinked==0?
                                    "No new supported songs or safe missing-file matches found":
                                    juce::String{};
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
                auto planned=armix::player::PlayerLibraryEditPlan::manualRelink(
                    safe->playerLibrary_,safe->playerTrackSettings_,
                    index,old,chosen);
                using RelinkError=armix::player::PlayerLibraryEditPlan::RelinkError;
                if(planned.error==RelinkError::staleRow)
                    safe->playerError_="Setlist changed; locate the song again";
                else if(planned.error==RelinkError::wrongSong)
                    safe->playerError_="Choose the same song filename to avoid relinking the wrong music";
                else if(planned.error==RelinkError::alreadyPresent)
                    safe->playerError_="This music file is already in your setlist";
                else if(planned.candidate &&
                        safe->commitPlayerTrackEdit(
                            std::move(planned.candidate->files),
                            std::move(planned.candidate->controls))) {
                    // File path identity and KEY/TEMPO/Cue remain stable.
                    // No transport changes before durable acceptance.
                    if(safe->playerCurrent_==index) {
                        safe->engine_.playerStop();
                        safe->playerCurrent_=-1;
                    }
                    safe->playerOutputPendingIndex_=-1;
                    safe->playerError_.clear();
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
            // An active show never reloads its current decoder mid-song.
            // Warm the inactive deck and wait for genuine PCM readiness;
            // the timer publishes the new CURRENT only after fade begins.
            const bool hasCurrent=playerCurrent_>=0 &&
                (phase=="PLAYING" || phase=="PAUSED" ||
                 phase=="READY" || phase=="ENDED");
            if(hasCurrent && playerCurrent_!=playerSelected_) {
                playerLoopCurrent_=false; // operator explicitly changes song
                if(engine_.queuePlayerTransition(
                    playerLibrary_[static_cast<std::size_t>(playerSelected_)],
                    controls.key,controls.tempo,playerFadeMilliseconds_,
                    controls.cueIn,controls.cueOut)) {
                    playerPendingIndex_=playerSelected_;
                    playerError_.clear();
                } else playerError_=
                    "Unable to prepare NEXT on the verified mixer output";
                emitPlayerState(true);
                return;
            }
            if(!engine_.setPlayerPitchTempo(controls.key,controls.tempo)) {
                playerError_="Invalid KEY or TEMPO settings";
                emitPlayerState();return;
            }
            const auto error=engine_.loadPlayerMusic(
                playerLibrary_[static_cast<std::size_t>(playerSelected_)],
                controls.cueIn,controls.cueOut);
            playerError_=error;
            if(error.isEmpty()) {
                playerLoopCurrent_=false;
                playerPendingIndex_=-1;
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
        if(playerPendingIndex_>=0)return;
        // Next is a deliberate escape from rehearsal looping.
        playerLoopCurrent_=false;
        int queued=-1;
        while(!playerQueue_.empty() && queued<0) {
            const auto found=std::find(playerLibrary_.begin(),
                playerLibrary_.end(),playerQueue_.front());
            if(found!=playerLibrary_.end()) {
                queued=static_cast<int>(found-playerLibrary_.begin());
            } else {
                playerQueue_.erase(playerQueue_.begin()); // stale queued file
                playerQueueEpoch_.changed();
            }
        }
        const int next=armix::player::PlayerShowTransitionPolicy::nextIndex(
            playerCurrent_,playerSelected_,
            static_cast<int>(playerLibrary_.size()),queued);
        if(next<0) {
            playerPendingQueueFile_={};
            engine_.playerStop();emitPlayerState();return;
        }
        const auto file=playerLibrary_[static_cast<std::size_t>(next)];
        playerSelected_=next;
        // Explicitly queued CURRENT is a repeat request, not a resume.
        // The standby worker can decode another instance of the same file.
        if(queued==playerCurrent_ && queued>=0) {
            const auto control=playerTrackSettings_[static_cast<std::size_t>(next)];
            if(engine_.queuePlayerTransition(file,control.key,control.tempo,
                   playerFadeMilliseconds_,control.cueIn,control.cueOut)) {
                playerPendingIndex_=next;
                playerPendingQueueFile_=file;
                playerError_.clear();
            } else playerError_="Could not prepare queued song repeat";
            emitPlayerState(true);return;
        }
        playSelectedMusic();
        if(queued>=0 && playerError_.isEmpty()) {
            if(playerPendingIndex_==next) {
                playerPendingQueueFile_=file;
            } else if(playerCurrent_==next) {
                if(armix::player::PlayerShowTransitionPolicy::
                       commitQueuedFront(playerQueue_,file))
                    playerQueueEpoch_.changed();
                playerPendingQueueFile_={};
                emitPlayerState(true);
            }
        }
    }

    void onAction (const juce::var& args)
    {
        auto action = args.getProperty ("action", {}).toString();
        if(action=="secondaryOutputInventory")return requestSecondaryInventory(args);
        if(action=="fxRackFile")return chooseFxRackFile(args);
        if(action=="floatingBuiltinOpen")return; // retired: built-in FX stays in ONE mixer WebView
        if (action=="audioLoopbackRead" || action=="audioLoopbackMeasure") {
            if (!deviceApply_.busy())
                requestPhysicalLoopback(args,action=="audioLoopbackMeasure");
            return;
        }
        if (action=="audioLoopbackCancel")
            return cancelPhysicalLoopback();
        const bool editorNavigation=action=="routingFxEditor"||
            action=="logicalVendorFxEditor"||action=="retainedFxOfflineOpen"||action=="fxEditor";
        if (!editorNavigation && action!="audioDeviceInventory" && action!="secondaryOutputInventory" && action!="copyDiagnosticReport")
            cancelPhysicalLoopback();
        // An explicit destination command cancels stale recovery even when a
        // device transaction is busy. Ordinary mixer controls do not.
        if(action=="secondaryOutputEnable"||action=="secondaryOutputConnect"||
           action=="secondaryOutputArm"||action=="secondaryOutputStop"||
           action=="secondaryOutputSplit"||action=="audioDeviceApply"||
           action=="audioSetup"||action=="audioDriverPanel"||
           (action=="monitor"&&args.getProperty("value",{}).isBool()&&!bool(args.getProperty("value",{})))) {
            pendingVendorStream_.reset();
            cancelSecondaryContinuity();
        }
        if(deviceApply_.busy()&&action=="secondaryOutputStop") {
            // STOP supersedes even an in-flight vendor open. Close the PCM
            // gate now; defer native object destruction until lease release.
            engine_.disarmSecondaryOutput();
            pendingSecondaryStop_=args;
            return;
        }
        if(deviceApply_.busy() && action!="audioDeviceApply"){
            // Never silently swallow an operator's correlated editor click.
            // Admission remains closed while hardware Apply owns the graph.
            if(action=="routingFxEditor")
                emitRoutingFxEditorReceipt(args,false,true,"FX editor unavailable: Audio I/O is applying");
            else if(editorNavigation)
                emitNotice("FX editor unavailable: Audio I/O is applying",true);
            else if(action=="audioDriverPanel")
                emitNotice("Audio device is busy · try again",true);
            else if(action=="routingNativeSourcePatch"){
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("requestId",args.getProperty("requestId",{}));
                ack->setProperty("channelId",args.getProperty("channelId",{}));
                ack->setProperty("ok",false);
                ack->setProperty("error","Audio configuration is finishing");
                if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixNativeSourceAck",juce::var(ack.release()));
            } else if(action=="routingNativeTrackCreate"||action=="routingNativeBusCreate"||
                      action=="routingNativeTrackDelete"||action=="routingNativeBusDelete"){
                const bool create=action=="routingNativeTrackCreate"||action=="routingNativeBusCreate";
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("requestId",args.getProperty("requestId",{}));
                ack->setProperty("channelId",args.getProperty("channelId",{}));
                ack->setProperty("ok",false);
                ack->setProperty("error","Audio configuration is finishing");
                if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                    create?"arMixNativeBusAck":"arMixNativeTrackDeleteAck",juce::var(ack.release()));
            }
            else if(action=="secondaryOutputEnable"||action=="secondaryOutputConnect"||
                    action=="secondaryOutputArm"||action=="secondaryOutputStop"||
                    action=="secondaryOutputSplit"||action=="secondaryMonitorStarter"){
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("requestId",args.getProperty("requestId",{}));
                ack->setProperty("action",action);ack->setProperty("ok",false);
                ack->setProperty("error","Audio device is busy · try again");
                if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixSecondaryOutputAck",juce::var(ack.release()));
            }
            return;
        }
        // Graph/epoch checks also reject stale deferred recovery. Do not
        // discard it merely because the operator moved an unrelated fader.
        if(pendingVendorStream_ && (action.startsWith("nativeMixer")||
           action=="routingDryStop"||action=="routingDryApply"||
           action=="routingDryShowRecall"||action=="routingDryShowRecover"||
           action=="playerChooseOutput"))
            pendingVendorStream_.reset();
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
            playerOutputPendingIndex_=-1;
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixOpenAudioIO",juce::var(true));
            return emitPlayerState();
        }
        if(action=="playerSubscribe")return emitPlayerState(true);
        if(action=="playerVolume") {
            if(value.isInt() || value.isDouble())
                engine_.setPlayerVolume(static_cast<double>(value));
            return emitPlayerState();
        }
        if(action=="playerNewSetlist" || action=="playerSelectSetlist" ||
           action=="playerDeleteSetlist" || action=="playerRenameSetlist" ||
           action=="playerMoveSong" || action=="playerRemoveSong" ||
           action=="playerUndoRemove" || action=="playerQueueNext" ||
           action=="playerQueueAppend" || action=="playerQueueRemove" ||
           action=="playerQueueMove")
            return playerEditSetlist(action,value);
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
            if(action=="playerPitchTempoReset") {
                // Reset KEY/TEMPO only. Song cue IN/OUT and duration trims
                // are independent setlist metadata, never collateral edits.
                next.key=0;
                next.tempo=100;
            }
            else if((direction==1 || direction==-1) &&
                    action=="playerKeyStep")
                next.key=std::clamp(next.key+direction,-6,6);
            else if(direction==1 || direction==-1)
                next.tempo=std::clamp(next.tempo+direction*5,80,120);
            else return; // invalid native control intent cannot mutate DSP
            // NEXT changes persist without touching CURRENT audio. For a
            // loaded CURRENT, use native-first acceptance, durable Save and
            // compensation on failure; never publish unsaved track state.
            using NativeMeta=armix::player::PlayerLiveMetadataCommitPolicy;
            const auto previous=playerTrackSettings_[static_cast<std::size_t>(i)];
            auto staged=playerTrackSettings_;
            staged[static_cast<std::size_t>(i)]=next;
            const auto outcome=NativeMeta::run(
                target.updateLiveDeck,
                [&]{return engine_.setPlayerPitchTempo(next.key,next.tempo);},
                [&]{return commitPlayerTrackEdit(playerLibrary_,std::move(staged));},
                [&]{return engine_.setPlayerPitchTempo(previous.key,previous.tempo);});
            if(outcome==NativeMeta::Outcome::accepted)
                playerError_.clear();
            else if(outcome==NativeMeta::Outcome::nativeRejected)
                playerError_="Unable to apply native KEY or TEMPO";
            else if(outcome==NativeMeta::Outcome::saveRejectedRollbackFailed)
                stopPlayerAfterUnrecoveredMetadata();
            emitPlayerState(true);return;
        }
        if(action=="playerLoopToggle") {
            const bool enable=value.isBool() && static_cast<bool>(value);
            if(!enable) {
                playerLoopCurrent_=false;
                playerError_.clear();
                emitPlayerState();return;
            }
            if(playerPendingIndex_>=0 || playerCurrent_<0 ||
               playerCurrent_>=static_cast<int>(playerTrackSettings_.size()) ||
               playerSelected_!=playerCurrent_) {
                playerError_="Select CURRENT and finish NEXT before enabling loop";
                emitPlayerState();return;
            }
            const auto cue=playerTrackSettings_[
                static_cast<std::size_t>(playerCurrent_)];
            if(!armix::player::PlayerShowTransitionPolicy::validLoop(
                cue.cueIn,cue.cueOut,playerFadeMilliseconds_)) {
                playerError_="Set IN/OUT at least 1.5 seconds apart for Loop A-B";
                emitPlayerState();return;
            }
            playerLoopCurrent_=true;
            playerError_.clear();
            emitPlayerState();return;
        }
        if(action=="playerCueIn" || action=="playerCueOut" ||
           action=="playerCueClear" || action=="playerCueGoIn") {
            if(playerCurrent_<0 || playerCurrent_>=
                   static_cast<int>(playerTrackSettings_.size()) ||
               playerSelected_!=playerCurrent_) {
                playerError_="Select the playing song to edit its cue";
                emitPlayerState();return;
            }
            const auto cues=playerTrackSettings_[static_cast<std::size_t>(
                playerCurrent_)];
            const auto transport=engine_.playerStatus();
            const double now=static_cast<double>(
                transport.getProperty("currentSeconds",0.0));
            const double total=static_cast<double>(
                transport.getProperty("durationSeconds",0.0));
            if(action=="playerCueGoIn") {
                if(!engine_.playerSeekSeconds(cues.cueIn))
                    playerError_="Unable to jump to song IN marker";
                else playerError_.clear();
                emitPlayerState();return;
            }
            if(action!="playerCueClear" &&
               (!std::isfinite(now)||!std::isfinite(total)||
                total<=0.||now<0.||now>=total)) {
                playerError_="Cue position is unavailable";
                emitPlayerState();return;
            }
            using CuePolicy=armix::player::PlayerCueEditPolicy;
            const auto command=action=="playerCueClear"?
                CuePolicy::Command::clear:action=="playerCueIn"?
                CuePolicy::Command::markIn:CuePolicy::Command::markOut;
            const auto nextCue=CuePolicy::plan(
                {cues.cueIn,cues.cueOut},command,now,total);
            if(!nextCue) {
                playerError_=action=="playerCueOut"?
                    "OUT must be after IN":"Invalid song cue marker";
                emitPlayerState();return;
            }
            // Native admission FIRST (as before); stage the proposed
            // archive edit, and restore the previous accepted cue if the
            // durable Save rejects. Never mutate the live control vector
            // until both native and disk accepted the candidate.
            using NativeMeta=armix::player::PlayerLiveMetadataCommitPolicy;
            auto staged=playerTrackSettings_;
            staged[static_cast<std::size_t>(playerCurrent_)].cueIn=nextCue->in;
            staged[static_cast<std::size_t>(playerCurrent_)].cueOut=nextCue->out;
            const auto outcome=NativeMeta::run(true,
                [&]{return engine_.setPlayerCueRange(nextCue->in,nextCue->out);},
                [&]{return commitPlayerTrackEdit(playerLibrary_,std::move(staged));},
                [&]{return engine_.setPlayerCueRange(cues.cueIn,cues.cueOut);});
            if(outcome==NativeMeta::Outcome::nativeRejected) {
                playerError_="Could not update native song markers";
                emitPlayerState();return;
            }
            if(outcome==NativeMeta::Outcome::saveRejectedRollbackFailed)
                stopPlayerAfterUnrecoveredMetadata();
            if(outcome!=NativeMeta::Outcome::accepted) {
                emitPlayerState(true);return;
            }
            if(playerLoopCurrent_ &&
               !armix::player::PlayerShowTransitionPolicy::validLoop(
                   nextCue->in,nextCue->out,playerFadeMilliseconds_))
                playerLoopCurrent_=false;
            playerError_.clear();emitPlayerState(true);return;
        }
        if(action=="playerPlay")return playTransportMusic();
        if(action=="playerSeek") {
            const double seconds=static_cast<double>(value);
            const double total=static_cast<double>(
                engine_.playerStatus().getProperty("durationSeconds",0.0));
            if(std::isfinite(seconds) && seconds>=0. &&
               std::isfinite(total) && total>0. && seconds<=total &&
               engine_.playerSeekSeconds(seconds)) {
                playerError_.clear();
            } else playerError_="Seek unavailable for this song";
            emitPlayerState();return;
        }
        if(action=="playerPause") {
            playerAutoAdvanceGate_.rearm();
            playerPendingQueueFile_={};
            playerPendingIndex_=-1;
            engine_.playerPause();emitPlayerState();return;
        }
        if(action=="playerStop") {
            playerAutoAdvanceGate_.rearm();
            playerLoopCurrent_=false;
            playerOutputPendingIndex_=-1;
            playerPendingQueueFile_={};
            playerPendingIndex_=-1;
            engine_.playerStop();emitPlayerState();return;
        }
        if(action=="playerNext") {
            playerAutoAdvanceGate_.rearm(); // explicit user retry
            return playNextMusic();
        }
        if(action=="playerAutoNext") {
            const bool next=value.isBool() && static_cast<bool>(value);
            if(!commitPlayerPreferences(next,playerFadeMilliseconds_)) {
                emitPlayerState(true);return;
            }
            playerAutoAdvanceGate_.rearm();
            playerError_.clear();emitPlayerState(true);return;
        }
        if(action=="playerFadeMs") {
            const int ms=static_cast<int>(value);
            if(ms!=0 && ms!=180 && ms!=500 && ms!=1500)return;
            if(!commitPlayerPreferences(playerAutoNext_,ms)) {
                emitPlayerState();return;
            }
            if(playerLoopCurrent_ && playerCurrent_>=0 &&
               playerCurrent_<static_cast<int>(playerTrackSettings_.size())) {
                const auto cue=playerTrackSettings_[
                    static_cast<std::size_t>(playerCurrent_)];
                if(!armix::player::PlayerShowTransitionPolicy::validLoop(
                    cue.cueIn,cue.cueOut,ms))
                    playerLoopCurrent_=false;
            }
            playerError_.clear();emitPlayerState();return;
        }
        if(action=="playerSelect" || action=="playerPlayNow") {
            const int i=static_cast<int>(value);
            if(i>=0 && i<static_cast<int>(playerLibrary_.size())) {
                if(i!=playerSelected_)playerOutputPendingIndex_=-1;
                if(i!=playerSelected_ || action=="playerPlayNow")
                    playerAutoAdvanceGate_.rearm();
                playerSelected_=i;
                if(action=="playerPlayNow")playSelectedMusic();
                else {
                    // Selecting NEXT never alters CURRENT. Idle standby
                    // decode starts now so the performer can advance later
                    // with an already-buffered next song.
                    const auto state=engine_.playerStatus().getProperty(
                        "state","").toString();
                    if(i!=playerCurrent_ && playerPendingIndex_<0 &&
                       playerCurrent_>=0 && state=="PLAYING") {
                        const auto settings=playerTrackSettings_[
                            static_cast<std::size_t>(i)];
                        (void)engine_.preloadPlayerMusic(
                            playerLibrary_[static_cast<std::size_t>(i)],
                            settings.key,settings.tempo,
                            settings.cueIn,settings.cueOut);
                    }
                    emitPlayerState(true);
                }
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
            action == "routingGlobalFxBypass" ||
            action == "routingNativeFxInsertBypass" ||
            action == "routingFxRackBypass" ||
            action == "nativeEqSetBand" || action == "nativeGeqSet" ||
            action == "nativeCompSet" ||
            action == "nativeSonKuSet" ||
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
            if(action=="routingGlobalFxBypass" && browser_.loaded) {
                auto ack=std::make_unique<juce::DynamicObject>();
                ack->setProperty("requestId",args.getProperty("requestId",{}));
                ack->setProperty("ok",false);
                ack->setProperty("error",reason);
                browser_.emitEventIfBrowserIsVisible(
                    "arMixGlobalFxAck",juce::var(ack.release()));
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
                args,LiveDryRoutingLab::maxFxPerLane,
                args.getProperty("catalogId",{}).toString()==
                    armix::builtin_fx::AntiNoizProcessor::catalogIdentity);
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
            // Presets are portable CONTROL data, not physical output
            // authorization. A logical mixer has an exact accepted FX token
            // and persisted .smix DSP state even before AUDIO SETUP. Rehydrate
            // that state for disk-only presets; never ask for/arm a device.
            std::uint64_t logicalChannel=0;
            std::size_t logicalSlot=0;
            const bool logicalPreset=fxRequest.valid&&fxRequest.unbound&&
                epoch==0&&catalogValid&&!logicalMixer_.readOnly()&&
                !mixerSessionWorker_.busy()&&!deviceApply_.busy()&&
                logicalFxTarget(args,logicalChannel,logicalSlot)&&
                logicalChannel==channel&&logicalSlot==static_cast<std::size_t>(slot);
            const bool physicalPreset=fxRequest.valid&&!fxRequest.unbound&&
                epoch!=0&&catalogValid;
            const bool identity=(logicalPreset||physicalPreset)&&
                request.startsWith(harmonyLegacy?"hpres-":"fxpres-");
            armix::session::StoredInsert offlineInsert;
            auto offline=logicalPreset?openLogicalBuiltinFx(
                args,offlineInsert,catalog.toRawUTF8()):nullptr;
            auto lease=physicalPreset?engine_.leaseNativeVst3Editor(
                epoch,revision,armix::mixer::ChannelId{channel},
                static_cast<std::size_t>(slot),instance):
                armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* processor=logicalPreset?offline.get():
                lease?lease->processor.get():nullptr;
            const bool exactReady=armix::storage::BuiltInFxAcceptedSnapshot
                ::matchesReady(processor,catalog);
            auto result=std::make_unique<juce::DynamicObject>();
            result->setProperty("requestId",request);
            result->setProperty("operation",operation);
            result->setProperty("catalogId",catalog);
            result->setProperty("channelId",rawChannel);
            result->setProperty("deviceEpoch",rawEpoch);
            result->setProperty("graphRevision",rawRevision);
            result->setProperty("workspaceToken",args.getProperty("workspaceToken",{}));
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
                    (catalog==armix::builtin_fx::GraphicEq31Processor::catalogIdentity&&
                         static_cast<bool>(pendingNativeGeq_.owner))||
                    (catalog==armix::builtin_fx::ChannelCompressorProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeComp_.owner))||
                    (catalog==armix::builtin_fx::AntiNoizProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeAntiNoiz_.owner))||
                    (catalog==armix::builtin_fx::ChannelHarmonyProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeHarmony_.owner))||
                    (catalog==armix::builtin_fx::VocalThrowProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeThrow_.owner))||
                    (catalog==armix::builtin_fx::DeEzzerProcessor::catalogIdentity&&
                         static_cast<bool>(pendingNativeDeEzzer_.owner));
                auto* realtime=dynamic_cast<armix::mixer::RealtimeNativeFxControl*>(processor);
                // Logical processor is a detached, rehydrated CONTROL
                // snapshot. Its settings were committed to .smix already;
                // no callback exists and no audio packet can be pending.
                // Physical processors still require quiescent live DSP.
                if(busy||!realtime||(!logicalPreset&&
                    !realtime->stateSnapshotQuiescent()))
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


        // GEQ31 browser editor: same actual retained ChannelId FX lease,
        // APVTS targets and original native PCM graph as the previous JUCE
        // window. The ONLY UI renderer is now the approved /preview/ page.
        if(action=="nativeGeqSnapshot"||action=="nativeGeqSet") {
            const auto fxRequest=armix::builtin_fx::NativeFxControlRequest::parse(
                args,LiveDryRoutingLab::maxFxPerLane);
            const auto& rawChannel=fxRequest.rawChannel;
            const auto& rawEpoch=fxRequest.rawEpoch;
            const auto& rawRevision=fxRequest.rawRevision;
            const auto& rawInstance=fxRequest.rawInstance;
            const auto& request=fxRequest.request;
            const int slot=fxRequest.slot;
            const auto channel=fxRequest.channel,epoch=fxRequest.epoch;
            const auto revision=fxRequest.revision;
            const bool unbound=fxRequest.unbound,valid=fxRequest.valid;
            armix::session::StoredInsert logicalInsert;
            auto offline=unbound?openLogicalBuiltinFx(args,logicalInsert,
                armix::builtin_fx::GraphicEq31Processor::catalogIdentity):nullptr;
            auto owner=valid&&!unbound?engine_.leaseNativeVst3Editor(
                epoch,revision,armix::mixer::ChannelId{channel},
                static_cast<std::size_t>(slot),rawInstance.toString()):
                armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* processor=dynamic_cast<armix::builtin_fx::GraphicEq31Processor*>(
                unbound?offline.get():owner?owner->processor.get():nullptr);
            if(action=="nativeGeqSnapshot") {
                emitNativeGeqSnapshot(request,rawChannel.toString(),
                    rawEpoch.toString(),rawRevision.toString(),
                    rawInstance.toString(),processor);
                return;
            }
            const auto edit=args.getProperty("edit",{});
            const auto kind=edit.getProperty("kind",{}).toString();
            const auto numeric=fxRequest.sequence("geq-");
            auto readFloat=[](const juce::var& value,float& out) {
                if(!value.isInt()&&!value.isDouble())return false;
                const double n=static_cast<double>(value);
                if(!std::isfinite(n))return false;
                out=static_cast<float>(n);
                return std::isfinite(out);
            };
            bool queued=false;
            if(processor&&numeric>0&&edit.isObject()&&
               !pendingNativeGeq_.owner&&!nativeShowBusy_&&!studioBackupBusy_&&
               !engine_.independentClockActive()) {
                if(kind=="band") {
                    const auto index=edit.getProperty("index",-1);
                    float gain=0.f;
                    if(index.isInt()&&readFloat(edit.getProperty("value",{}),gain))
                        queued=processor->queueBandEdit(
                            static_cast<std::size_t>(static_cast<int>(index)),
                            gain,numeric);
                }else if(kind=="output") {
                    float output=0.f;
                    if(readFloat(edit.getProperty("value",{}),output))
                        queued=processor->queueOutputEdit(output,numeric);
                }else if(kind=="curve"||kind=="flat") {
                    std::array<float,armix::builtin_fx::GraphicEq31Core::bandCount>
                        gains{};
                    float output=processor->acceptedLevelDb();
                    bool curveValid=kind=="flat";
                    if(kind=="curve") {
                        const auto settings=edit.getProperty("settings",{});
                        const auto bands=settings.getProperty("bands",{}).getArray();
                        curveValid=bands&&bands->size()==static_cast<int>(gains.size())&&
                            readFloat(settings.getProperty("levelDb",{}),output);
                        if(curveValid)for(int i=0;i<static_cast<int>(gains.size());++i)
                            if(!readFloat((*bands)[i],gains[static_cast<std::size_t>(i)])) {
                                curveValid=false;break;
                            }
                    }
                    if(curveValid)queued=processor->queueCurveEdit(
                        gains,output,numeric);
                }
                if(queued&&!unbound) {
                    pendingNativeGeq_.owner=owner;
                    pendingNativeGeq_.request=request;
                    pendingNativeGeq_.channelId=rawChannel.toString();
                    pendingNativeGeq_.deviceEpoch=rawEpoch.toString();
                    pendingNativeGeq_.revision=rawRevision.toString();
                    pendingNativeGeq_.instanceId=rawInstance.toString();
                    pendingNativeGeq_.started=juce::Time::getMillisecondCounter();
                }
            }
            if(unbound)
                return completeLogicalFxEdit(args,std::move(logicalInsert),
                    processor,queued,"arMixBuiltinGeqAck");
            auto receipt=std::make_unique<juce::DynamicObject>();
            receipt->setProperty("requestId",request);
            receipt->setProperty("status",queued?"QUEUED":"REJECTED");
            receipt->setProperty("errorCode",queued?"NONE":
                pendingNativeGeq_.owner?"GEQ_CONTROL_BUSY":"STALE_OR_INVALID_GEQ");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixBuiltinGeqQueue",juce::var(receipt.release()));
            return;
        }

        if(action=="nativeDeEzzerSnapshot"||action=="nativeDeEzzerSet"||action=="nativeDeEzzerCue") {
            const auto request=armix::builtin_fx::NativeFxControlRequest::parse(
                args,LiveDryRoutingLab::maxFxPerLane);
            auto owner=deEzzerLease(request);
            auto* proc=owner?dynamic_cast<armix::builtin_fx::DeEzzerProcessor*>(owner->processor.get()):nullptr;
            if(proc&&!proc->isReady())proc=nullptr;
            if(proc&&pendingNativeDeEzzer_.owner!=owner) {
                // Drain a bounded orphan completion after graph replacement;
                // never let an old UI request prevent future accepted capture.
                armix::builtin_fx::de_ezzer::Ack abandoned;
                for(int i=0;i<armix::builtin_fx::de_ezzer::Control::ackCapacity&&proc->pollAck(abandoned);++i){}
            }
            const auto rawEditor=args.getProperty("editorToken",{});
            const bool tokenOk=validDeEzzerEditorToken(rawEditor);
            const auto editorToken=tokenOk?rawEditor.toString():juce::String{};
            const auto key=deEzzerCueKey(request,proc);
            auto& cue=engine_.deEzzerCueControl();
            if(action=="nativeDeEzzerSnapshot") {
                if(proc)proc->requestAnalysis(); // exact current lease; expires without polling
                const auto state=proc?proc->readback():armix::builtin_fx::de_ezzer::Snapshot{};
                if(proc&&tokenOk){
                    if(editorToken!=deEzzerEditorToken_||key!=deEzzerEditorKey_){
                        cue.cancel(false);deEzzerEditorToken_=editorToken;deEzzerEditorKey_=key;
                    }else if(key.valid()&&state.settings.enabled&&!owner->bypassRequested.load(std::memory_order_acquire))
                        (void)cue.renew(key,juce::Time::getMillisecondCounter());
                    else cue.cancel(false);
                }
                auto payload=proc?armix::builtin_fx::de_ezzer::bridge::encode(state):juce::var{};
                if(auto* o=payload.getDynamicObject())o->setProperty("cue",deEzzerCueReadback(key,state.settings.enabled&&
                    !owner->bypassRequested.load(std::memory_order_acquire)));
                emitDeEzzerReply("arMixBuiltinDeEzzerSnapshot",request,proc!=nullptr,
                    proc?"snapshot":nullptr,payload);
                return;
            }
            if(action=="nativeDeEzzerCue"){
                using M=armix::mixer::FxCueMode;
                const auto raw=args.getProperty("mode",{});
                const bool sameEditor=proc&&tokenOk&&editorToken==deEzzerEditorToken_&&
                    key.instance==deEzzerEditorKey_.instance&&key.instanceEpoch==deEzzerEditorKey_.instanceEpoch&&
                    key.epoch==deEzzerEditorKey_.epoch&&key.revision==deEzzerEditorKey_.revision;
                const bool enabled=proc&&proc->readback().settings.enabled&&!owner->bypassRequested.load(std::memory_order_acquire);
                bool ok=false;M mode=M::Mix;
                if(sameEditor&&raw.isString()){
                    if(raw==juce::var("Mix")){cue.cancel(key.valid()&&key==deEzzerEditorKey_);ok=true;}
                    else if(key.valid()&&key==deEzzerEditorKey_&&enabled&&!nativeShowBusy_&&!studioBackupBusy_){
                        if(raw==juce::var("Ess"))mode=M::Ess;
                        else if(raw==juce::var("Sidechain"))mode=M::Sidechain;
                        else if(raw==juce::var("Delta"))mode=M::Delta;
                        if(mode!=M::Mix)ok=cue.start(key,mode,proc->getSampleRate(),juce::Time::getMillisecondCounter());
                    }
                }
                emitDeEzzerReply("arMixBuiltinDeEzzerCue",request,ok,"cue",deEzzerCueReadback(key,enabled));
                return;
            }
            armix::builtin_fx::de_ezzer::Command command{};
            const bool queued=proc&&!pendingNativeDeEzzer_.owner&&!nativeShowBusy_&&!studioBackupBusy_&&
                !engine_.independentClockActive()&&proc->stateSnapshotQuiescent()&&
                armix::builtin_fx::de_ezzer::bridge::decode(args.getProperty("command",{}),command)&&proc->queue(command);
            if(queued)pendingNativeDeEzzer_={owner,request,command.id,command.sequence,juce::Time::getMillisecondCounter()};
            emitDeEzzerReply("arMixBuiltinDeEzzerQueue",request,queued);
            return;
        }

        if(action=="nativeAntiNoizSnapshot"||action=="nativeAntiNoizSet"||action=="nativeAntiNoizLearn"||action=="nativeAntiNoizClear"||action=="nativeAntiNoizAudition") {
            using P=armix::builtin_fx::AntiNoizProcessor;
            using Binding=armix::builtin_fx::AntiNoizNativeBinding;
            const auto request=armix::builtin_fx::NativeFxControlRequest::parse(
                args,LiveDryRoutingLab::maxFxPerLane,true);
            armix::session::StoredInsert stored;
            auto offline=request.valid&&request.unbound?
                openLogicalBuiltinFx(args,stored,P::catalogIdentity):nullptr;
            auto owner=request.valid&&!request.unbound?
                engine_.leaseNativeVst3Editor(request.epoch,request.revision,
                    armix::mixer::ChannelId{request.channel},static_cast<std::size_t>(request.slot),
                    request.rawInstance.toString()):armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* p=dynamic_cast<P*>(request.unbound?offline.get():owner?owner->processor.get():nullptr);
            if(p&&!p->isReady())p=nullptr;
            if(action=="nativeAntiNoizSnapshot"){
                emitAntiNoizReply("arMixBuiltinAntiNoizSnapshot",args,
                    p?"ACCEPTED":"REJECTED",p?"NONE":"INVALID_OR_STALE_ANTI_NOIZ",p,p&&!request.unbound);
                return;
            }
            const auto id=request.sequence("an-");
            const bool learn=action=="nativeAntiNoizLearn",clear=action=="nativeAntiNoizClear",audition=action=="nativeAntiNoizAudition";
            const bool liveCommand=learn||clear||audition;
            P::Core::Settings settings;
            const bool typed=liveCommand||Binding::parseSettings(args.getProperty("settings",{}),settings);
            bool queued=false;
            if(p&&id&&typed&&!pendingNativeAntiNoiz_.owner&&!deviceApply_.busy()&&
               !nativeShowBusy_&&!studioBackupBusy_&&!engine_.independentClockActive()&&
               !engine_.isGraphSwitchPending()&&(!liveCommand||!request.unbound)){
                queued=learn?p->queueLearn(id):clear?p->queueClearProfile(id):audition?p->queueAudition(id):p->queueSettings(settings,id);
                if(queued&&!request.unbound){
                    pendingNativeAntiNoiz_={owner,args,id,request.epoch,request.revision,
                        request.channel,request.slot,juce::Time::getMillisecondCounter(),false};
                }
            }
            if(request.unbound&&!liveCommand)
                return completeLogicalFxEdit(args,std::move(stored),p,queued,"arMixBuiltinAntiNoizAck");
            emitAntiNoizReply("arMixBuiltinAntiNoizQueue",args,queued?"QUEUED":"REJECTED",
                queued?"NONE":liveCommand&&request.unbound?"ANTI_NOIZ_REQUIRES_LIVE_PCM":
                pendingNativeAntiNoiz_.owner?"ANTI_NOIZ_CONTROL_BUSY":"INVALID_OR_STALE_ANTI_NOIZ",
                p);
            return;
        }

        if(action=="nativeDuckerSnapshot"||action=="nativeDuckerSet"||action=="nativeDuckerSources"){
            using D=armix::builtin_fx::AutoDuckerProcessor;
            const auto req=armix::builtin_fx::NativeFxControlRequest::parse(args,LiveDryRoutingLab::maxFxPerLane);
            armix::session::StoredInsert saved;
            auto offline=req.valid&&req.unbound?openLogicalBuiltinFx(args,saved,D::catalogIdentity):nullptr;
            auto owner=req.valid&&!req.unbound?engine_.leaseNativeVst3Editor(req.epoch,req.revision,
                armix::mixer::ChannelId{req.channel},static_cast<std::size_t>(req.slot),req.rawInstance.toString()):
                armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* d=dynamic_cast<D*>(req.unbound?offline.get():owner?owner->processor.get():nullptr);
            if(action=="nativeDuckerSnapshot"){emitNativeDuckerSnapshot(args,d,req.unbound);return;}
            const auto reject=[&](const juce::String& error){
                emitDuckerReply(args,"arMixBuiltinDuckerAck","REJECTED",error);
            };
            if(!d||!req.sequence("ducker-")||nativeShowBusy_||studioBackupBusy_||
               mixerSessionWorker_.busy()||deviceApply_.busy()||engine_.independentClockActive()||
               engine_.isGraphSwitchPending()||!d->stateSnapshotQuiescent()||std::any_of(pendingNativeDuckers_.begin(),pendingNativeDuckers_.end(),
                   [&](const auto& p){return p.owner==owner;}))return reject("AUTO_DUCKER_NOT_READY_OR_BUSY");
            auto next=d->acceptedSettings();
            if(action=="nativeDuckerSources"){
                const auto values=args.getProperty("sources",{});
                const auto* ids=values.getArray();
                if(!ids||ids->size()>static_cast<int>(next.sources.size()))return reject("INVALID_SOURCE_COUNT");
                next.sources={};next.count=static_cast<std::uint32_t>(ids->size());
                for(int k=0;k<ids->size();++k)
                    if(!armix::console::approvedNativeId((*ids)[k],next.sources[static_cast<std::size_t>(k)])||
                       next.sources[static_cast<std::size_t>(k)]==req.channel)return reject("INVALID_SOURCE");
                std::sort(next.sources.begin(),next.sources.begin()+next.count);
                if(!next.validSources())return reject("DUPLICATE_OR_INVALID_SOURCE");
                if(req.unbound){
                    if(logicalMixer_.readOnly())return reject("OFFLINE_WORKSPACE_READ_ONLY");
                    const auto& workspace=logicalMixer_.workspace();
                    const auto& roster=workspace.document().roster;
                    const auto previous=d->acceptedSettings();
                    for(std::size_t k=0;k<next.count;++k)
                        if(!roster.find({next.sources[k]})&&
                           !std::binary_search(previous.sources.begin(),previous.sources.begin()+previous.count,next.sources[k]))
                            return reject("AUTO_DUCKER_SOURCE_UNAVAILABLE");
                    // Validate the UNION of all existing Ducker dependencies
                    // against the offline roster. No hardware or PCM required.
                    armix::mixer::DynamicGraphRequest candidate;
                    for(const auto& row:roster.channels()){
                        for(std::size_t slot=0;slot<row.insertCatalogIds.size();++slot){
                            if(row.insertCatalogIds[slot]!=D::catalogIdentity)continue;
                            D::Settings routes{};
                            if(row.id.value==req.channel&&slot==static_cast<std::size_t>(req.slot))routes=next;
                            else {
                                armix::session::StoredInsert other;
                                std::unique_ptr<juce::AudioProcessor> processor;
                                if(prepareLogicalFx(workspace,row.id,slot,other,processor).isNotEmpty())
                                    return reject("AUTO_DUCKER_SAVED_SOURCE_STATE_INVALID");
                                auto* fx=dynamic_cast<D*>(processor.get());
                                if(!fx)return reject("AUTO_DUCKER_SAVED_SOURCE_STATE_INVALID");
                                routes=fx->acceptedSettings();
                            }
                            for(std::size_t k=0;k<routes.count;++k){
                                const armix::mixer::ChannelId source{routes.sources[k]};
                                if(!roster.find(source))continue;
                                auto& deps=candidate.postFaderSidechains;
                                if(std::none_of(deps.begin(),deps.end(),[&](const auto& dep){
                                    return dep.source==source&&dep.target==row.id;
                                }))deps.push_back({source,row.id});
                            }
                        }
                    }
                    armix::mixer::DynamicPreparedMixerBlock graph;
                    armix::mixer::DynamicGraphError fault;
                    if(!armix::mixer::DynamicPreparedMixerBlock::compile(roster,candidate,graph,fault))
                        return reject(fault.fault==armix::mixer::DynamicGraphFault::cycle?
                            "AUTO_DUCKER_ROUTING_CYCLE":"AUTO_DUCKER_GRAPH_REJECTED");
                    if(!d->setUnpublishedSettings(next))return reject("AUTO_DUCKER_INVALID_SETTINGS");
                    juce::MemoryBlock bytes;d->getStateInformation(bytes);
                    auto safe=juce::Component::SafePointer<ConsoleWindow>(this);
                    if(!logicalMixer_.saveFx(std::move(saved),std::move(bytes),[safe,args](juce::String err){
                        if(safe!=nullptr)safe->emitDuckerReply(args,"arMixBuiltinDuckerAck",
                            err.isEmpty()?"APPLIED":"REJECTED",err);
                    }))return reject("OFFLINE_FX_SAVE_FAILED");
                    return;
                }
                const auto error=engine_.setNativeDuckerSources(owner,{req.channel},
                    static_cast<std::size_t>(req.slot),req.rawInstance.toString(),next);
                if(error.isNotEmpty())return reject(error);
                emitApprovedNativeSnapshot();
                emitDuckerReply(args,"arMixBuiltinDuckerAck","APPLIED");
                return;
            }
            const auto values=args.getProperty("settings",{});
            auto number=[&](const char* key,float& out){
                const auto v=values.getProperty(key,{});
                if(!v.isDouble()&&!v.isInt())return false;
                const auto n=static_cast<double>(v);
                if(!std::isfinite(n))return false;
                out=static_cast<float>(n);return std::isfinite(out);
            };
            auto& v=next.dynamics;
            const bool valid=values.isObject()&&number("thresholdDb",v.thresholdDb)&&number("depthDb",v.depthDb)&&
                number("attackMs",v.attackMs)&&number("holdMs",v.holdMs)&&number("releaseMs",v.releaseMs)&&
                number("hysteresisDb",v.hysteresisDb)&&number("highPassHz",v.highPassHz)&&number("lowPassHz",v.lowPassHz);
            const auto sequence=req.sequence("ducker-");
            if(!valid)return reject("INVALID_DYNAMICS");
            if(req.unbound){
                const bool queued=d->queueSettings(next,sequence);
                return completeLogicalFxEdit(args,std::move(saved),d,queued,"arMixBuiltinDuckerAck");
            }
            if(pendingNativeDuckers_.size()>=32)return reject("AUTO_DUCKER_CONTROL_BUSY");
            if(!d->queueSettings(next,sequence))return reject("INVALID_DYNAMICS_OR_BUSY");
            pendingNativeDuckers_.push_back({owner,args,sequence,juce::Time::getMillisecondCounter(),false});
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

        // 8D-SonKu typed settings; ordinary native FX lease and PCM mailbox.
        if(action=="nativeSonKuSnapshot"||action=="nativeSonKuSet") {
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
                armix::builtin_fx::SonKuSpatialProcessor::catalogIdentity):nullptr;
            auto owner=valid&&!unbound?engine_.leaseNativeVst3Editor(ep,rev,
                armix::mixer::ChannelId{ch},static_cast<std::size_t>(slot),
                rawInstance.toString()):
                armix::mixer::LiveChannelFxGraph::InstanceLease{};
            auto* p=dynamic_cast<armix::builtin_fx::SonKuSpatialProcessor*>(
                unbound?offline.get():owner?owner->processor.get():nullptr);
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
            // Branded ImmerSIB never responds to another 8D-SonKu editor's
            // stale WebView control requests, while retaining shared DSP.
            if(p && dynamic_cast<armix::builtin_fx::ImerSipP92Processor*>(p))
                p=nullptr;
#endif
            if(action=="nativeSonKuSnapshot") {
                emitNativeSonKuSnapshot(request,rawChannel.toString(),
                    rawEpoch.toString(),rawRevision.toString(),rawInstance.toString(),p);
                return;
            }
            armix::sonku::Settings settings{};
            const bool parameters=armix::builtin_fx::sonku_bridge::decodeSettings(
                args.getProperty("settings",{}),settings);
            const auto numeric=fxRequest.sequence("sonku-");
            const auto holdPhase=args.getProperty("holdOrbitPhase",{});
            const bool validMotionIntent=holdPhase.isVoid()||holdPhase.isBool();
            bool queued=false;
            if(p&&numeric&&parameters&&validMotionIntent &&
               !pendingNativeSonKu_.owner&&!nativeShowBusy_&&!studioBackupBusy_&&
               !engine_.independentClockActive()) {
                queued=static_cast<bool>(holdPhase)?
                    p->queueSettings(settings,numeric,true):p->queueSettings(settings,numeric);
                if(queued&&!unbound) {
                    pendingNativeSonKu_.owner=owner;
                    pendingNativeSonKu_.request=request;
                    pendingNativeSonKu_.channelId=rawChannel.toString();
                    pendingNativeSonKu_.deviceEpoch=rawEpoch.toString();
                    pendingNativeSonKu_.revision=rawRevision.toString();
                    pendingNativeSonKu_.instanceId=rawInstance.toString();
                    pendingNativeSonKu_.started=juce::Time::getMillisecondCounter();
                }
            }
            if(unbound)return completeLogicalFxEdit(args,std::move(offlineInsert),
                p,queued,"arMixBuiltinSonKuAck");
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("status",queued?"QUEUED":"REJECTED");
            ack->setProperty("errorCode",queued?"NONE":
                pendingNativeSonKu_.owner?"SONKU_CONTROL_BUSY":"INVALID_OR_STALE_SONKU");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixBuiltinSonKuQueue",juce::var(ack.release()));
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
            if(applied)synchronizeOpenImmerSIBPower();
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
            else if(applied && name=="masterMute") {
                engine_.setMasterMuted(bool(val));
                if(!bool(val))feedbackMasterTripNotified_=false;
            }
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
        // O1: native CONTROL intent, exact verified ASIO physical pair.
        // Never reinterpret an external destination as internal WIRE/M4 OUT.
        if(action=="routingMonitorTiming") {
            const auto id=args.getProperty("requestId",{}).toString();
            const auto policy=args.getProperty("policy",{}).toString();
            const auto ep=args.getProperty("deviceEpoch",{}),rv=args.getProperty("revision",{}),
                       rr=args.getProperty("rosterRevision",{});
            const auto confirmed=args.getProperty("confirmed",{});
            const auto main=args.getProperty("main",{}),monitor=args.getProperty("monitor",{});
            const auto pair=[](const juce::var& v){
                return v.isArray()&&v.size()==2&&v[0].isInt()&&v[1].isInt();
            };
            const auto status=engine_.status().getProperty("dryRoutingLab",{});
            const auto native=status.getProperty("nativeBus",{});
            juce::String error="MONITOR timing: invalid/stale request or output not explicitly muted";
            if(id.startsWith("monitor-timing-")&&id.length()<=64&&operatorOutputMuted_&&
               !deviceApply_.busy()&&!nativeShowBusy_&&!studioBackupBusy_&&!mixerSessionWorker_.busy()&&
               !fxRackFileChooser_&&!mixerFileDialogOpen_&&!namedIoFileChooser_&&
               confirmed.isBool()&&bool(confirmed)&&pair(main)&&pair(monitor)&&
               (policy=="GLOBAL_ALIGNED"||policy=="INDEPENDENT_MONITOR")&&
               armix::console::nativeSessionTokenMatches(ep,status.getProperty("epoch",{}))&&
               armix::console::nativeSessionTokenMatches(rv,native.getProperty("graphRevision",{}))&&
               armix::console::nativeSessionTokenMatches(rr,native.getProperty("rosterRevision",{})))
                error=engine_.setMonitorTimingPolicy(
                    armix::console::nativeSessionTokenValue(ep),armix::console::nativeSessionTokenValue(rv),
                    armix::console::nativeSessionTokenValue(rr),
                    policy=="GLOBAL_ALIGNED"?armix::mixer::MonitorLatencyPolicy::aligned:
                        armix::mixer::MonitorLatencyPolicy::independent,
                    {static_cast<int>(main[0]),static_cast<int>(main[1])},
                    {static_cast<int>(monitor[0]),static_cast<int>(monitor[1])});
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",id);ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            if(error.isEmpty()){
                const auto snapshot=approvedNativeSnapshot();
                ack->setProperty("revision",snapshot.getProperty("graph",{}).getProperty("revision",{}));
                autoMainEpoch_=0;engine_.cancelAsioResetMonitorIntent();
            }
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixMonitorTimingAck",juce::var(ack.release()));
            if(error.isEmpty())emitApprovedNativeSnapshot();else emitNotice(error,true);
            return;
        }
        if(action=="routingPhysicalBusOutput") {
            const auto id=args.getProperty("requestId",{}).toString();
            const auto bus=args.getProperty("bus",{}).toString();
            const auto ep=args.getProperty("deviceEpoch",{}),rv=args.getProperty("revision",{});
            const auto left=args.getProperty("left",{}),right=args.getProperty("right",{});
            const auto oldL=args.getProperty("previousLeft",{}),oldR=args.getProperty("previousRight",{});
            const auto leftName=args.getProperty("leftName",{}),rightName=args.getProperty("rightName",{});
            const auto confirmed=args.getProperty("confirmed",{});
            const auto status=engine_.status().getProperty("dryRoutingLab",{});
            const auto native=status.getProperty("nativeBus",{});
            juce::String error="BUS OUTPUT: invalid/stale request or output not explicitly muted";
            if(id.startsWith("bus-out-")&&id.length()<=64&&(bus=="MAIN"||bus=="MONITOR")&&
               operatorOutputMuted_&&!deviceApply_.busy()&&!nativeShowBusy_&&!studioBackupBusy_&&
               confirmed.isBool()&&static_cast<bool>(confirmed)&&
               left.isInt()&&right.isInt()&&oldL.isInt()&&oldR.isInt()&&
               leftName.isString()&&rightName.isString()&&
               armix::console::nativeSessionTokenMatches(ep,status.getProperty("epoch",{}))&&
               armix::console::nativeSessionTokenMatches(rv,native.getProperty("graphRevision",{})))
                error=engine_.patchPhysicalBusOutput(
                    armix::console::nativeSessionTokenValue(ep),armix::console::nativeSessionTokenValue(rv),
                    bus=="MONITOR",{static_cast<int>(oldL),static_cast<int>(oldR)},
                    {static_cast<int>(left),static_cast<int>(right)},leftName.toString(),rightName.toString());
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",id);ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixPhysicalBusOutputAck",juce::var(ack.release()));
            if(error.isEmpty()){
                autoMainEpoch_=0;engine_.cancelAsioResetMonitorIntent();
                emitApprovedNativeSnapshot();
            }else emitNotice(error,true);
            return;
        }
        if(action=="routingExternalOutput") {
            const auto requestId=args.getProperty("requestId",{}).toString();
            const auto channelText=args.getProperty("channelId",{}).toString();
            const auto ep=args.getProperty("deviceEpoch",{});
            const auto rv=args.getProperty("revision",{});
            const auto confirmed=args.getProperty("confirmed",{});
            const int left=static_cast<int>(args.getProperty("left",-2));
            const int right=static_cast<int>(args.getProperty("right",-2));
            const auto nativeStatus=engine_.status().getProperty("dryRoutingLab",{});
            const auto bus=nativeStatus.getProperty("nativeBus",{});
            const bool tokens=
                armix::console::nativeSessionTokenMatches(
                    ep,nativeStatus.getProperty("epoch",{})) &&
                armix::console::nativeSessionTokenMatches(
                    rv,bus.getProperty("graphRevision",{}));
            std::uint64_t rawId=0;
            const bool idOk=armix::session::parseDecimal(juce::var(channelText),rawId);
            juce::String error;
            if(!requestId.startsWith("out-o-") || requestId.length()>64 || !idOk ||
               !confirmed.isBool() || !((left==-1 && right==-1) || (left>=0 && right>=0)))
                error="EXTERNAL OUT: invalid channel or output request";
            else if(nativeShowBusy_ || studioBackupBusy_)
                error="EXTERNAL OUT: finish the current Show or backup operation first";
            else if(!tokens)
                error="EXTERNAL OUT: stale device or graph revision; refresh Audio I/O";
            else
                error=engine_.setPhysicalExternalOutput(
                    armix::mixer::ChannelId{rawId},
                    armix::console::nativeSessionTokenValue(ep),
                    armix::console::nativeSessionTokenValue(rv),
                    left,right,static_cast<bool>(confirmed));
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",requestId);
            ack->setProperty("channelId",channelText);
            ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            ack->setProperty("deviceEpoch",ep);
            ack->setProperty("graphRevision",rv);
            if(browser_.loaded)
                browser_.emitEventIfBrowserIsVisible(
                    "arMixExternalOutputAck",juce::var(ack.release()));
            diagnostics_.record(error.isEmpty()?"EXTERNAL_OUTPUT_ACCEPTED":"EXTERNAL_OUTPUT_REJECTED",
                error.isEmpty()?("channel="+channelText+" pair="+juce::String(left)+"/"+juce::String(right)):
                    ("channel="+channelText+" pair="+juce::String(left)+"/"+juce::String(right)+" · "+error));
            if(error.isNotEmpty())emitNotice(error,true);
            else {
                // The operator has already confirmed this exact ASIO pair.
                // Complete first-sound automatically; never force MONITOR/ARM
                // again, and never override a deliberate OUTPUT MUTE.
                if(left>=0 && !operatorOutputMuted_ && !engine_.isMonitorEnabled())
                    requestAutomaticMain(false);
                emitApprovedNativeSnapshot(); // callback readback proves LIVE
            }
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
                closeAllPluginEditors();
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
        // The only global FX A/B authority. One atomic read by all active
        // native PCM inserts (including BUS and FX_RETURN), with each plugin's
        // existing latency-aligned bypass envelope. Never touch rack/slot
        // settings, change topology or publish a competing M4 graph.
        if(action=="routingGlobalFxBypass") {
            const auto request=args.getProperty("requestId",{}).toString();
            const auto rawEpoch=args.getProperty("deviceEpoch",{});
            const auto rawRevision=args.getProperty("revision",{});
            const auto desired=args.getProperty("value",{});
            const auto accepted=approvedNativeSnapshot();
            const auto authority=accepted.getProperty("mixerSession",{})
                .getProperty("authority","").toString();
            std::uint64_t ep=0,rev=0;
            const bool valid=request.startsWith("global-fx-") &&
                request.length()<=90 && desired.isBool() &&
                armix::console::approvedNativeId(rawEpoch,ep) &&
                armix::console::approvedNativeId(rawRevision,rev) &&
                (authority=="CHANNEL_ID"||authority=="M4") &&
                !acceptedControlRequests_.contains(request) &&
                !studioBackupBusy_&&!nativeShowBusy_ &&
                !deviceApply_.busy()&&!engine_.independentClockActive();
            bool applied=false;
            if(valid) {
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)
                    acceptedControlRequests_.remove(0);
                applied=engine_.setGlobalFxBypassed(
                    ep,rev,static_cast<bool>(desired));
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("ok",applied);
            ack->setProperty("bypassed",engine_.globalFxBypassed());
            ack->setProperty("error",applied?juce::String():
                "Global FX requires a current accepted physical PCM epoch/revision");
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixGlobalFxAck",juce::var(ack.release()));
            if(applied) {
                synchronizeOpenImmerSIBPower();
                diagnostics_.record("GLOBAL_FX_AB",
                    engine_.globalFxBypassed()?"BYPASS_ALL":"ENABLE_RESTORED");
                emitApprovedNativeSnapshot();
            }
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
            if(applied) {
                synchronizeOpenImmerSIBPower();
                // A native slot/rack ACK is not enough to repaint the
                // other authority (WebView FX Rack). Publish the accepted
                // producer state once per successful gesture, never on hover.
                emitApprovedNativeSnapshot();
            }
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
        if(action=="audioIoDefaults"){
            const auto request=args.getProperty("requestId",{}).toString();
            const auto op=args.getProperty("operation",{}).toString();
            std::uint64_t ep=0,rev=0,roster=0,inRev=0,outRev=0,storeRev=0;
            bool confirmed=false;
            const auto snap=approvedNativeSnapshot();
            juce::String error="I/O default request stale or unavailable";
            if(request.startsWith("io-default-")&&request.length()<=64&&
               !acceptedControlRequests_.contains(request)&&!nativeShowBusy_&&!studioBackupBusy_&&
               !mixerSessionWorker_.busy()&&!deviceApply_.busy()&&!mixerSessionRecoveryPending_&&
               armix::session::strictBool(args.getProperty("confirmed",{}),confirmed)&&confirmed&&
               armix::session::parseDecimal(args.getProperty("revision",{}),storeRev,true)&&storeRev==ioDefaults_.revision()&&
               armix::session::parseDecimal(args.getProperty("deviceEpoch",{}),ep)&&
               armix::session::parseDecimal(args.getProperty("graphRevision",{}),rev)&&
               armix::session::parseDecimal(args.getProperty("rosterRevision",{}),roster)&&
               armix::session::parseDecimal(args.getProperty("inputRevision",{}),inRev)&&
               armix::session::parseDecimal(args.getProperty("outputRevision",{}),outRev)&&
               snap.getProperty("device",{}).getProperty("epoch",{}).toString()==armix::session::decimal(ep)&&
               snap.getProperty("graph",{}).getProperty("revision",{}).toString()==armix::session::decimal(rev)&&
               snap.getProperty("graph",{}).getProperty("rosterRevision",{}).toString()==armix::session::decimal(roster)&&
               engine_.logicalInputRevision()==inRev&&engine_.logicalOutputRevision()==outRev&&
               snap.getProperty("mixerSession",{}).getProperty("authority",{}).toString()=="CHANNEL_ID"){
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)acceptedControlRequests_.remove(0);
                try{
                    if(op=="SAVE"){
                        armix::session::IoPortLayout layout;
                        if(engine_.captureIoPortLayout(ep,rev,roster,inRev,outRev,layout))error=ioDefaults_.save(storeRev,layout);
                        else error="Create logical input or output ports first";
                    }else if(op=="CLEAR")error=ioDefaults_.save(storeRev,{});
                    else if(op=="LOAD"){
                        if(!ioDefaults_.readOnly()&&canInstallIoDefaults()&&engine_.installIoPortDefaults(ioDefaults_.layout()))
                            error.clear();
                        else error="Default can only load into an empty, muted mixer without a checkpoint";
                    }
                }catch(...){error="I/O default transaction failed";}
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);ack->setProperty("ok",error.isEmpty());ack->setProperty("error",error);
            ack->setProperty("revision",armix::session::decimal(ioDefaults_.revision()));
            ack->setProperty("inputRevision",armix::session::decimal(engine_.logicalInputRevision()));
            ack->setProperty("outputRevision",armix::session::decimal(engine_.logicalOutputRevision()));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixIoDefaultsAck",juce::var(ack.release()));
            emitApprovedNativeSnapshot();
            return;
        }
        if(action=="routingLogicalInputEdit"){
            using P=armix::session::LogicalInputProjection;
            using R=armix::device::LogicalInputRegistry;
            const auto request=args.getProperty("requestId",{}).toString();
            const auto op=args.getProperty("operation",{}).toString();
            P::Edit edit;
            std::uint64_t ep=0,rev=0,roster=0,logical=0;
            bool operation=true;
            if(op=="ADD")edit.operation=P::Operation::add;
            else if(op=="RENAME")edit.operation=P::Operation::rename;
            else if(op=="REMOVE")edit.operation=P::Operation::remove;
            else if(op=="BIND")edit.operation=P::Operation::bind;
            else if(op=="UNBIND")edit.operation=P::Operation::unbind;
            else if(op=="ASSIGN")edit.operation=P::Operation::assign;
            else if(op=="CLEAR")edit.operation=P::Operation::clear;
            else operation=false;
            const auto format=args.getProperty("format",{}).toString();
            const auto name=args.getProperty("name",{});
            bool fields=true;
            if(op=="ADD"||op=="RENAME"){
                fields=name.isString();edit.name=name.toString().toStdString();
                if(op=="ADD"){
                    fields=fields&&(format=="MONO"||format=="STEREO");
                    edit.format=format=="MONO"?R::Format::mono:R::Format::stereo;
                }
            }
            if(op!="ADD"&&op!="CLEAR")
                fields=fields&&armix::session::parseDecimal(args.getProperty("portId",{}),edit.port);
            if(op=="ASSIGN"||op=="CLEAR")
                fields=fields&&armix::session::parseDecimal(args.getProperty("channelId",{}),edit.channel);
            if(op=="BIND"){
                const auto l=args.getProperty("left",{}),r=args.getProperty("right",{});
                const auto ln=args.getProperty("leftName",{}),rn=args.getProperty("rightName",{});
                fields=fields&&l.isInt()&&r.isInt()&&ln.isString()&&rn.isString();
                edit.binding.left=static_cast<int>(l);edit.binding.right=static_cast<int>(r);
                edit.binding.leftName=ln.toString().toStdString();edit.binding.rightName=rn.toString().toStdString();
            }
            const auto snap=approvedNativeSnapshot();
            const auto graph=snap.getProperty("graph",{});
            const bool valid=operation&&fields&&request.startsWith("logical-io-")&&request.length()<=64&&
                !acceptedControlRequests_.contains(request)&&!nativeShowBusy_&&!studioBackupBusy_&&
                !mixerSessionWorker_.busy()&&
                armix::session::parseDecimal(args.getProperty("deviceEpoch",{}),ep)&&
                armix::session::parseDecimal(args.getProperty("graphRevision",{}),rev)&&
                armix::session::parseDecimal(args.getProperty("rosterRevision",{}),roster)&&
                armix::session::parseDecimal(args.getProperty("logicalRevision",{}),logical)&&
                snap.getProperty("device",{}).getProperty("epoch",{}).toString()==args.getProperty("deviceEpoch",{}).toString()&&
                graph.getProperty("revision",{}).toString()==args.getProperty("graphRevision",{}).toString()&&
                graph.getProperty("rosterRevision",{}).toString()==args.getProperty("rosterRevision",{}).toString()&&
                snap.getProperty("mixerSession",{}).getProperty("authority",{}).toString()=="CHANNEL_ID";
            LiveDryRoutingLab::NativeBusResult result;result.error="LOGICAL_INPUT_NOT_ADMITTED";
            if(valid){
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)acceptedControlRequests_.remove(0);
                result=engine_.editNativeLogicalInput(ep,rev,roster,logical,std::move(edit));
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);ack->setProperty("ok",valid&&result.accepted);
            ack->setProperty("error",result.error?result.error:"LOGICAL_INPUT_UNKNOWN");
            ack->setProperty("logicalRevision",juce::String(std::to_string(engine_.logicalInputRevision())));
            ack->setProperty("graphRevision",juce::String(std::to_string(result.pcmRevision)));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixLogicalInputAck",juce::var(ack.release()));
            return;
        }
        if(action=="routingLogicalOutputEdit"){
            using P=armix::session::LogicalOutputProjection;
            const auto request=args.getProperty("requestId",{}).toString();
            const auto op=args.getProperty("operation",{}).toString();
            P::Edit edit;
            std::uint64_t ep=0,rev=0,roster=0,logical=0;
            bool operation=true;
            if(op=="ADD")edit.operation=P::Operation::add;
            else if(op=="RENAME")edit.operation=P::Operation::rename;
            else if(op=="REMOVE")edit.operation=P::Operation::remove;
            else if(op=="BIND")edit.operation=P::Operation::bind;
            else if(op=="UNBIND")edit.operation=P::Operation::unbind;
            else if(op=="ASSIGN")edit.operation=P::Operation::assign;
            else if(op=="CLEAR")edit.operation=P::Operation::clear;
            else operation=false;
            const auto name=args.getProperty("name",{});
            bool fields=true;
            if(op=="ADD"||op=="RENAME"){
                fields=name.isString();edit.name=name.toString().toStdString();
            }
            if(op!="ADD"&&op!="CLEAR")
                fields=fields&&armix::session::parseDecimal(args.getProperty("portId",{}),edit.port);
            if(op=="ASSIGN"||op=="CLEAR")
                fields=fields&&armix::session::parseDecimal(args.getProperty("channelId",{}),edit.channel);
            if(op=="BIND"){
                const auto l=args.getProperty("left",{}),r=args.getProperty("right",{});
                const auto ln=args.getProperty("leftName",{}),rn=args.getProperty("rightName",{});
                fields=fields&&l.isInt()&&r.isInt()&&ln.isString()&&rn.isString();
                edit.binding.left=static_cast<int>(l);edit.binding.right=static_cast<int>(r);
                edit.binding.leftName=ln.toString().toStdString();edit.binding.rightName=rn.toString().toStdString();
            }
            edit.confirmed=args.getProperty("confirmed",{})==juce::var(true);
            const auto snap=approvedNativeSnapshot();
            const auto graph=snap.getProperty("graph",{});
            const bool valid=operation&&fields&&request.startsWith("logical-io-")&&request.length()<=64&&
                !acceptedControlRequests_.contains(request)&&!nativeShowBusy_&&!studioBackupBusy_&&
                !mixerSessionWorker_.busy()&&
                armix::session::parseDecimal(args.getProperty("deviceEpoch",{}),ep)&&
                armix::session::parseDecimal(args.getProperty("graphRevision",{}),rev)&&
                armix::session::parseDecimal(args.getProperty("rosterRevision",{}),roster)&&
                armix::session::parseDecimal(args.getProperty("logicalRevision",{}),logical)&&
                snap.getProperty("device",{}).getProperty("epoch",{}).toString()==args.getProperty("deviceEpoch",{}).toString()&&
                graph.getProperty("revision",{}).toString()==args.getProperty("graphRevision",{}).toString()&&
                graph.getProperty("rosterRevision",{}).toString()==args.getProperty("rosterRevision",{}).toString()&&
                snap.getProperty("mixerSession",{}).getProperty("authority",{}).toString()=="CHANNEL_ID";
            LiveDryRoutingLab::NativeBusResult result;result.error="LOGICAL_OUTPUT_NOT_ADMITTED";
            if(valid){
                acceptedControlRequests_.add(request);
                if(acceptedControlRequests_.size()>512)acceptedControlRequests_.remove(0);
                result=engine_.editNativeLogicalOutput(ep,rev,roster,logical,std::move(edit));
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);ack->setProperty("ok",valid&&result.accepted);
            ack->setProperty("error",result.error?result.error:"LOGICAL_OUTPUT_UNKNOWN");
            ack->setProperty("logicalRevision",juce::String(std::to_string(engine_.logicalOutputRevision())));
            ack->setProperty("graphRevision",juce::String(std::to_string(result.pcmRevision)));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixLogicalOutputAck",juce::var(ack.release()));
            return;
        }
        if(action=="routingNativeSourcePatch"){
            const auto request=args.getProperty("requestId",{}).toString();
            const auto rawId=args.getProperty("channelId",{});
            const auto rawEp=args.getProperty("deviceEpoch",{});
            const auto rawRev=args.getProperty("graphRevision",{});
            const auto rawRoster=args.getProperty("rosterRevision",{});
            const auto mode=args.getProperty("mode",{}).toString();
            const auto deck=args.getProperty("deckId","").toString();
            const bool player=mode=="PLAYER";
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
                (mode=="NONE"||mode=="MONO"||mode=="STEREO"||player)&&
                (mode!="NONE"||(left==-1&&right==-1))&&
                (mode!="MONO"||right==-1)&&
                (player?(left==-1&&right==-1&&deck=="1"):deck.isEmpty())&&
                input&&
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
                result=withNativeTopologyContinuity(ep,rev,mode!="NONE",[&]{
                    return engine_.patchNativeInputSource(ep,rev,roster,
                        armix::mixer::ChannelId{id},left,right,
                        mode=="STEREO"||player,player);
                });
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
                    result=withNativeTopologyContinuity(ep,rev,false,[&]{
                        return engine_.createNativeInputChannel(ep,rev,roster,
                            name.toStdString(),armix::mixer::ProcessingFormat::stereo);
                    });
                } else if(!track) {
                    const auto purpose=args.getProperty("purpose","").toString();
                    const bool wet=purpose=="FX";
                    int count=0;
                    juce::StringArray names;
                    if(rows)for(const auto& row:*rows){
                        names.add(row.getProperty("name","").toString());
                        if(row.getProperty("role","").toString()==(wet?"FX_RETURN":"GROUP"))++count;
                    }
                    const juce::String prefix=wet?"FX Return ":purpose=="GROUP"?"Group Bus ":"Aux Bus ";
                    juce::String name;
                    do {name=prefix+juce::String(++count);}while(names.contains(name,true));
                    const auto rawSelection=args.getProperty("selection",{});
                    std::vector<armix::mixer::ChannelId> selection;
                    bool selectionValid=rawSelection.isVoid();
                    if(const auto* ids=rawSelection.getArray()){
                        selectionValid=args.getProperty("purpose","").toString()=="GROUP" &&
                            !ids->isEmpty() && rows && ids->size()<=rows->size();
                        for(const auto& raw:*ids){
                            std::uint64_t id=0;
                            if(!armix::console::approvedNativeId(raw,id)||!id||
                               std::find(selection.begin(),selection.end(),armix::mixer::ChannelId{id})!=selection.end()){
                                selectionValid=false;break;
                            }
                            selection.push_back({id});
                        }
                    }
                    if(!selectionValid)result.error="INVALID_GROUP_SELECTION";
                    else result=withNativeTopologyContinuity(ep,rev,false,[&]{
                        return engine_.editNativeBus(ep,rev,roster,
                            wet?armix::mixer::ChannelRole::fxReturn:armix::mixer::ChannelRole::group,
                            std::span<const armix::mixer::ChannelId>{selection},
                            name.toStdString(),1.f,{},purpose!="AUX");
                    });
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
                result=withNativeTopologyContinuity(ep,rev,false,[&]{
                    return engine_.removeNativeInputChannel(ep,rev,roster,
                        armix::mixer::ChannelId{id});
                });
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
                result=withNativeTopologyContinuity(ep,rev,false,[&]{
                    return engine_.editNativeBus(ep,rev,roster,role,
                        std::span<const armix::mixer::ChannelId>{},"",1.f,
                        armix::mixer::ChannelId{id});
                });
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
            if(ok)synchronizeOpenImmerSIBPower();
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
        if(action=="retainedFxOfflineOpen")return openRetainedFxOffline(args);
        if(action=="logicalVendorFxEditor")return openLogicalVendorEditor(args);
        if (action == "routingFxEditor") {
            const int lane=static_cast<int>(args.getProperty("channel",-1));
            const auto token=args.getProperty("instanceId",{}).toString();
            const auto epoch=static_cast<std::uint64_t>(
                static_cast<juce::int64>(args.getProperty("deviceEpoch",juce::var(0))));
            const auto revision=static_cast<std::uint64_t>(
                static_cast<juce::int64>(args.getProperty("revision",juce::var(0))));
            bool opened=false,retryable=true;
            juce::String error="FX editor unavailable: device, revision or instance changed";
            if(!nativeShowBusy_&&!studioBackupBusy_&&!engine_.isGraphSwitchPending()) {
                armix::mixer::LiveChannelFxGraph::InstanceLease owner;
                std::uint64_t retainedEpoch=epoch;
                std::uint64_t retainedRevision=revision,nativeChannel=0;
                bool nativeTarget=false;
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
                        retainedRevision=rev;
                        nativeChannel=id;
                        nativeTarget=static_cast<bool>(owner);
                    }
                } else owner=engine_.leaseRoutedFxForEditor(lane,slot,epoch,revision,token);
                if(owner && owner->processor) {
                    retryable=false; // plugin/window failures are not retryable admission races
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
                    const bool dedicatedImmerSIB=dynamic_cast<
                        armix::builtin_fx::ImerSipP92Processor*>(owner->processor.get())!=nullptr;
                    auto& shownWindow=dedicatedImmerSIB?immersibEditorWindow_:editorWindow_;
                    auto& shownOwner=dedicatedImmerSIB?immersibEditorOwner_:routedEditorOwner_;
                    auto& shownEpoch=dedicatedImmerSIB?immersibEditorEpoch_:routedEditorEpoch_;
                    auto& shownProcessor=dedicatedImmerSIB?immersibEditorProcessor_:editorProcessor_;
#else
                    auto& shownWindow=editorWindow_;
                    auto& shownOwner=routedEditorOwner_;
                    auto& shownEpoch=routedEditorEpoch_;
                    auto& shownProcessor=editorProcessor_;
#endif
                    std::erase_if(additionalFxEditors_,[this](const auto& extra){
                        return !extra || !extra->owner ||
                            !engine_.retainsRoutedFxEditor(extra->epoch,extra->owner);
                    });
                    const auto extra=std::find_if(additionalFxEditors_.begin(),
                        additionalFxEditors_.end(),[&](const auto& entry){
                        return entry && entry->owner==owner &&
                            entry->processor==owner->processor.get();
                    });
                    // Only a DIFFERENT vendor owner may occupy an additional
                    // HWND. Allow one existing primary owner plus 16 others.
                    bool keepOtherVendorWindow=editorWindow_ && routedEditorOwner_ &&
                        routedEditorOwner_!=owner;
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
                    if(dedicatedImmerSIB)keepOtherVendorWindow=false;
#endif
                    // A second click must raise an already open OR hidden
                    // editor for exactly this instance. Do not
                    // destroy/recreate JUCE's active editor on every click.
                    if(shownWindow && shownOwner==owner &&
                       shownEpoch==retainedEpoch &&
                       shownProcessor==owner->processor.get()) {
                        if(static_cast<bool>(args.getProperty("recenter",false)))
                            shownWindow->present(true);
                        else shownWindow->present();
                        opened=shownWindow->presentationConfirmed();
                        if(opened)error.clear();
                        else error="FX editor window could not be presented";
                    } else if(extra!=additionalFxEditors_.end()) {
                        // Instance owner is re-leased by the current graph;
                        // channel and slot can move, but never clone its UI.
                        (*extra)->epoch=retainedEpoch;
                        (*extra)->channel=nativeChannel;
                        (*extra)->lane=lane;(*extra)->slot=slot;
                        (*extra)->instanceId=token;
                        (*extra)->window->present(static_cast<bool>(
                            args.getProperty("recenter",false)));
                        opened=(*extra)->window->presentationConfirmed();
                        if(opened)error.clear();
                        else error="FX editor window could not be presented";
                    } else if(keepOtherVendorWindow &&
                              additionalFxEditors_.size()>=maxSimultaneousVendorEditors) {
                        error="Close an unused VST3 editor before opening another";
                    } else try {
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
                        if(dedicatedImmerSIB)closeImmerSIBEditor();
                        else
#endif
                            if(!keepOtherVendorWindow)closePluginEditor();
                        if(auto ownedEditor=armix::ui::createHostedAudioEditor(*owner->processor)) {
                            auto* editor=ownedEditor.get();
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
                            if(auto* imer=dynamic_cast<
                                armix::builtin_fx::ImerSipP92Editor*>(editor)) {
                                // Exact retained processor identity is
                                // authoritative. Resolve CURRENT published
                                // PCM revision at each click, not the revision
                                // cached when the floating editor opened.
                                // Read all three independent bypass gates so
                                // global mixer A/B also mutes this editor.
                                auto held=owner;
                                const auto readPower=[this,held,nativeTarget,
                                     nativeChannel,lane,slot,token] {
                                    const auto current=nativeTarget?
                                        engine_.retainedNativeFxPower(
                                            held,armix::mixer::ChannelId{nativeChannel},
                                            static_cast<std::size_t>(slot),token):
                                        engine_.retainedRoutedFxPower(
                                            held,lane,slot,token);
                                    armix::builtin_fx::ImerSipP92Editor::HostBypassState state;
                                    state.valid=current.valid;
                                    state.slotBypassed=current.slotBypassed;
                                    state.rackBypassed=current.rackBypassed;
                                    state.globalBypassed=current.globalBypassed;
                                    return state;
                                };
                                imer->bindHostPower(
                                    [this,held,nativeTarget,nativeChannel,
                                     lane,slot,token](bool bypass) {
                                        const bool applied=nativeTarget?
                                            engine_.bypassRetainedNativeFx(
                                                held,armix::mixer::ChannelId{nativeChannel},
                                                static_cast<std::size_t>(slot),
                                                token,bypass):
                                            engine_.bypassRetainedRoutedFx(
                                                held,lane,slot,token,bypass);
                                        if(applied)emitApprovedNativeSnapshot();
                                        else emitNotice(
                                            "FX Power rejected: accepted graph or instance changed",true);
                                        return applied;
                                    },readPower);
                            }
#endif
                            // Vocal Delay owns only internal wet power/TAIL. Rack
                            // bypass remains the existing independent hard safety gate.
                            if(auto* delay=dynamic_cast<armix::builtin_fx::vocal_delay::SingerEditor*>(editor)) {
                                auto held=owner;
                                const auto access=[this,held,retainedEpoch]{return held&&
                                    !nativeShowBusy_&&!studioBackupBusy_&&!engine_.independentClockActive()&&
                                    !engine_.isGraphSwitchPending()&&engine_.retainsRoutedFxEditor(retainedEpoch,held);};
                                delay->bindHostAccess(access);
                                delay->bindHostBypass([this,held,nativeTarget,nativeChannel,lane,slot,token]{
                                    const auto current=nativeTarget?
                                        engine_.retainedNativeFxPower(held,armix::mixer::ChannelId{nativeChannel},
                                            static_cast<std::size_t>(slot),token):
                                        engine_.retainedRoutedFxPower(held,lane,slot,token);
                                    using B=armix::builtin_fx::vocal_delay::SingerEditor::HostBypass;
                                    return !current.valid?B::unavailable:current.globalBypassed?B::global:
                                        current.rackBypassed?B::rack:current.slotBypassed?B::slot:B::none;
                                });
                                if(nativeTarget)bindDelayMusicSources(*delay,access,held->processor.get());
                                bindDelayUserPresets(*delay,[held,access]{
                                    return access()?dynamic_cast<armix::builtin_fx::vocal_delay::JuceVocalDelayProcessor*>(held->processor.get()):nullptr;
                                });
                            }
                            // REVERB-SIS uses the same retained lease readback
                            // as GEQ31. The indicator never owns another gate.
                            if(auto* reverb=dynamic_cast<
                                armix::builtin_fx::VocalReverbEditor*>(editor)) {
                                auto held=owner;
                                reverb->bindHostPower(
                                    [this,held,nativeTarget,nativeChannel,lane,slot,token](bool bypass) {
                                        const bool ok=nativeTarget?
                                            engine_.bypassRetainedNativeFx(
                                                held,armix::mixer::ChannelId{nativeChannel},
                                                static_cast<std::size_t>(slot),token,bypass):
                                            engine_.bypassRetainedRoutedFx(
                                                held,lane,slot,token,bypass);
                                        if(ok)emitApprovedNativeSnapshot();
                                        else emitNotice("Reverb-Sis Power rejected: FX lease changed",true);
                                        return ok;
                                    },
                                    [this,held,nativeTarget,nativeChannel,lane,slot,token] {
                                        const auto current=nativeTarget?
                                            engine_.retainedNativeFxPower(
                                                held,armix::mixer::ChannelId{nativeChannel},
                                                static_cast<std::size_t>(slot),token):
                                            engine_.retainedRoutedFxPower(
                                                held,lane,slot,token);
                                        return armix::builtin_fx::VocalReverbEditor::HostBypassState{
                                            current.valid,current.slotBypassed,
                                            current.rackBypassed,current.globalBypassed};
                                    });
                            }
                            if(auto* guitar=dynamic_cast<armix::builtin_fx::GuitarRigEditor*>(editor)) {
                                auto held=owner;
                                guitar->bindHostBypassReadback([this,held,nativeTarget,nativeChannel,lane,slot,token] {
                                    const auto current=nativeTarget?
                                        engine_.retainedNativeFxPower(held,armix::mixer::ChannelId{nativeChannel},
                                            static_cast<std::size_t>(slot),token):
                                        engine_.retainedRoutedFxPower(held,lane,slot,token);
                                    using Overlay=armix::builtin_fx::GuitarRigEditor::HostOverlay;
                                    return !current.valid?Overlay::unavailable:
                                        current.globalBypassed?Overlay::global:
                                        current.rackBypassed?Overlay::rack:
                                        current.slotBypassed?Overlay::slot:Overlay::none;
                                });
                            }
                            if(auto* reverb=dynamic_cast<armix::builtin_fx::VocalReverbEditor*>(editor)) {
                                auto held=owner;
                                bindReverbUserPresets(*reverb,[this,held,nativeTarget,nativeChannel,lane,slot,token]()
                                    -> armix::builtin_fx::VocalReverbProcessor* {
                                    if(nativeShowBusy_||studioBackupBusy_||engine_.independentClockActive()||
                                       !held||!held->processor)return nullptr;
                                    const auto state=nativeTarget?
                                        engine_.retainedNativeFxPower(held,armix::mixer::ChannelId{nativeChannel},
                                            static_cast<std::size_t>(slot),token):
                                        engine_.retainedRoutedFxPower(held,lane,slot,token);
                                    return state.valid?dynamic_cast<armix::builtin_fx::VocalReverbProcessor*>(
                                        held->processor.get()):nullptr;
                                },false);
                            }
                            // Graphic EQ uses the same EXACT retained mixer
                            // FX bypass authority/readback as ImmerSIB.
                            // No GUI-local toggle can mutate a second graph.
                            if(auto* geq=dynamic_cast<
                                armix::builtin_fx::GraphicEq31Editor*>(editor)) {
                                auto held=owner;
                                const auto readPower=[this,held,nativeTarget,
                                    nativeChannel,lane,slot,token] {
                                    const auto current=nativeTarget?
                                        engine_.retainedNativeFxPower(
                                            held,armix::mixer::ChannelId{nativeChannel},
                                            static_cast<std::size_t>(slot),token):
                                        engine_.retainedRoutedFxPower(
                                            held,lane,slot,token);
                                    armix::builtin_fx::GraphicEq31Editor::HostBypassState state;
                                    state.valid=current.valid;
                                    state.slotBypassed=current.slotBypassed;
                                    state.rackBypassed=current.rackBypassed;
                                    state.globalBypassed=current.globalBypassed;
                                    return state;
                                };
                                geq->bindHostPower(
                                    [this,held,nativeTarget,nativeChannel,
                                     lane,slot,token](bool bypass) {
                                        const bool ok=nativeTarget?
                                            engine_.bypassRetainedNativeFx(
                                                held,armix::mixer::ChannelId{nativeChannel},
                                                static_cast<std::size_t>(slot),
                                                token,bypass):
                                            engine_.bypassRetainedRoutedFx(
                                                held,lane,slot,token,bypass);
                                        if(ok)emitApprovedNativeSnapshot();
                                        else emitNotice(
                                            "Graphic EQ power rejected: FX lease changed",
                                            true);
                                        return ok;
                                    },readPower);

                                // Graphic EQ user presets use the SAME native
                                // typed .armixfx store as PEQ/Compressor.
                                // Every disk operation validates this retained
                                // insert lease; no FX editor owns files/graph.
                                using G=armix::builtin_fx::GraphicEq31Processor;
                                using Store=armix::storage::BuiltInFxUserPresetStore;
                                constexpr auto catalog=G::catalogIdentity;
                                const auto validLease=[this,held,nativeTarget,
                                    nativeChannel,lane,slot,token] {
                                    if(nativeShowBusy_||studioBackupBusy_||
                                       engine_.independentClockActive()||
                                       !held||!held->processor)return false;
                                    const auto state=nativeTarget?
                                        engine_.retainedNativeFxPower(
                                            held,armix::mixer::ChannelId{nativeChannel},
                                            static_cast<std::size_t>(slot),token):
                                        engine_.retainedRoutedFxPower(
                                            held,lane,slot,token);
                                    return state.valid;
                                };
                                geq->bindUserPresetService(
                                    [this,validLease,catalog] {
                                        std::vector<armix::builtin_fx::
                                            GraphicEq31Editor::UserPresetEntry> list;
                                        if(!validLease())return list;
                                        for(const auto& item:
                                            builtinFxUserPresets_.enumerate(catalog))
                                            list.push_back({item.id,item.name});
                                        return list;
                                    },
                                    [this,validLease,held,catalog](const juce::String& name) {
                                        if(!validLease()||
                                           !Store::safeText(name,80)) {
                                            emitNotice("Graphic EQ preset name/FX instance invalid",true);
                                            return juce::String{};
                                        }
                                        auto* fx=dynamic_cast<G*>(held->processor.get());
                                        if(!armix::storage::BuiltInFxAcceptedSnapshot
                                            ::matchesReady(fx,catalog)||
                                           !fx->stateSnapshotQuiescent()) {
                                            emitNotice("Graphic EQ DSP state not ready",true);
                                            return juce::String{};
                                        }
                                        auto settings=armix::storage::
                                            BuiltInFxAcceptedSnapshot::acceptedSettings(fx,catalog);
                                        armix::storage::BuiltInFxUserPreset entry;
                                        entry.catalogId=catalog;
                                        entry.category="My Presets";
                                        entry.name=name;
                                        entry.settings=std::move(settings);
                                        const auto error=builtinFxUserPresets_.saveNew(entry);
                                        if(error.isNotEmpty()) {
                                            emitNotice("Graphic EQ preset not saved: "+error,true);
                                            return juce::String{};
                                        }
                                        emitNotice("Graphic EQ preset saved: "+name);
                                        return entry.id;
                                    },
                                    [this,validLease,held,catalog](const juce::String& presetId) {
                                        if(!validLease()||!Store::safeId(presetId)) {
                                            emitNotice("Graphic EQ preset lease invalid",true);
                                            return false;
                                        }
                                        auto* fx=dynamic_cast<G*>(held->processor.get());
                                        if(!armix::storage::BuiltInFxAcceptedSnapshot
                                            ::matchesReady(fx,catalog)||
                                           !fx->stateSnapshotQuiescent()) {
                                            emitNotice("Graphic EQ DSP state not ready",true);
                                            return false;
                                        }
                                        armix::storage::BuiltInFxUserPreset entry;
                                        const auto error=builtinFxUserPresets_
                                            .read(catalog,presetId,entry);
                                        if(error.isNotEmpty()||
                                           !Store::validateSettings(catalog,entry.settings)) {
                                            emitNotice("Graphic EQ preset rejected: "+error,true);
                                            return false;
                                        }
                                        const auto bands=entry.settings
                                            .getProperty("bands",{}).getArray();
                                        std::array<float,armix::builtin_fx::GraphicEq31Core::bandCount> values{};
                                        if(!bands||bands->size()!=static_cast<int>(values.size()))
                                            return false;
                                        for(std::size_t i=0;i<values.size();++i)
                                            values[i]=static_cast<float>(
                                                static_cast<double>((*bands)[static_cast<int>(i)]));
                                        // Entire payload was validated before any
                                        // change. Legacy band-only preset defaults
                                        // the explicit Output Level to unity (0 dB).
                                        const float levelDb=static_cast<float>(
                                            static_cast<double>(entry.settings
                                                .getProperty("levelDb",0.0)));
                                        const auto previous=fx->acceptedBands();
                                        const float previousLevel=fx->acceptedLevelDb();
                                        if(!fx->setBandsFromPreset(values,levelDb))
                                            return false;
                                        const auto accepted=fx->acceptedBands();
                                        bool matches=std::abs(
                                            fx->acceptedLevelDb()-levelDb)<=.06f;
                                        for(std::size_t i=0;i<values.size();++i)
                                            if(std::abs(accepted[i]-values[i])>.06f)
                                                matches=false;
                                        if(!matches) {
                                            (void)fx->setBandsFromPreset(
                                                previous,previousLevel);
                                            emitNotice("Graphic EQ preset readback rejected",true);
                                            return false;
                                        }
                                        emitNotice("Graphic EQ preset loaded: "+entry.name);
                                        return true;
                                    });
                            }
                            PluginEditorWindow* createdWindow=nullptr;
                            if(keepOtherVendorWindow) {
                                auto entry=std::make_unique<AdditionalFxEditor>();
                                entry->owner=owner;entry->epoch=retainedEpoch;
                                entry->channel=nativeChannel;entry->lane=lane;
                                entry->slot=slot;entry->instanceId=token;
                                entry->processor=owner->processor.get();
                                entry->window=std::make_unique<PluginEditorWindow>(
                                    owner->processor->getName(),std::move(ownedEditor),*this);
                                if(static_cast<bool>(args.getProperty("recenter",false)))
                                    entry->window->present(true);
                                createdWindow=entry->window.get();
                                additionalFxEditors_.push_back(std::move(entry));
                            } else {
                                shownOwner=owner;
                                shownEpoch=retainedEpoch;
                                shownWindow=std::make_unique<PluginEditorWindow>(
                                    owner->processor->getName(),std::move(ownedEditor),*this);
                                if(static_cast<bool>(args.getProperty("recenter",false)))
                                    shownWindow->present(true); // new HWND: centre on mixer too
                                shownProcessor=owner->processor.get();
                                createdWindow=shownWindow.get();
                            }
                            opened=createdWindow&&createdWindow->presentationConfirmed();
                            if(opened)error.clear();
                            else error="FX editor window could not be presented";
                        } else error="This VST3 has no native editor";
                    } catch(const std::exception& ex) {
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
                        if(dedicatedImmerSIB)closeImmerSIBEditor();
                        else
#endif
                            if(!keepOtherVendorWindow)closePluginEditor();
                        error="Vendor editor failed: "+juce::String(ex.what());
                    } catch(...) {
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
                        if(dedicatedImmerSIB)closeImmerSIBEditor();
                        else
#endif
                            if(!keepOtherVendorWindow)closePluginEditor();
                        error="Vendor editor failed (plugin exception)";
                    }
                }
            }
            // G-MV36: correlated HWND feedback only, never installed PCM proof.
            emitRoutingFxEditorReceipt(args,opened,retryable,error);
            if(opened)diagnostics_.record("ROUTED_FX_EDITOR_OPENED",
                "lane="+juce::String(lane)+" slot="+juce::String(slot));
            else diagnostics_.record("ROUTED_FX_EDITOR_REJECTED",error);
            return;
        }

        if(action=="nativeMixerOpenArchive") {
            ensureLogicalWorkspace();
            bool confirmed=false;
            std::uint64_t revision=0;
            if(!armix::session::strictBool(args.getProperty("confirmed",{}),confirmed)||!confirmed||
               !armix::session::parseDecimal(args.getProperty("baseRevision",{}),revision,true)||
               args.getProperty("workspaceToken","").toString()!=logicalMixer_.workspace().workspaceToken()||
               revision!=logicalMixer_.workspace().revision())
                return emitMixerSessionAck("ARCHIVE","Offline session changed; select the archive again");
            chooseMixerFile(false,true);
            return;
        }
        if(action=="nativeMixerNewLive") {
            createLiveSession(args);
            return;
        }
        if(action=="nativeMixerNewOffline") {
            createOfflineSession(args);
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
                closeAllPluginEditors();
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
                                    safe->closeAllPluginEditors();
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
                            safe->engine_.setMonitorEnabled(false);
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
        if(action=="secondaryMonitorStarter") {
            std::uint64_t ep=0,rev=0,roster=0;
            LiveDryRoutingLab::NativeBusResult result;
            result.error="Wait for audio to finish applying";
            if(!nativeShowBusy_&&!studioBackupBusy_&&!mixerSessionWorker_.busy()&&
               !pendingRetainedFxCapture_&&!retainedOfflineFxActive_&&
               armix::console::approvedNativeId(value.getProperty("deviceEpoch",{}),ep)&&
               armix::console::approvedNativeId(value.getProperty("graphRevision",{}),rev)&&
               armix::console::approvedNativeId(value.getProperty("rosterRevision",{}),roster))
                result=engine_.seedIndependentMonitor(ep,rev,roster);
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",args.getProperty("requestId",{}));
            ack->setProperty("action",action);
            ack->setProperty("ok",result.accepted);
            ack->setProperty("error",result.accepted?juce::String{}:juce::String(result.error));
            ack->setProperty("graphRevision",juce::String(std::to_string(result.pcmRevision)));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixSecondaryOutputAck",juce::var(ack.release()));
            if(result.accepted)emitApprovedNativeSnapshot();
            return emitNotice(result.accepted?"Headphone starter mix created · adjust MONITOR sends to taste":result.error,!result.accepted);
        }
        if(action=="secondaryOutputSplit"){
            juce::String error;
            std::uint64_t ep=0,rev=0,roster=0;
            const auto enable=value.getProperty("enabled",{});
            if(studioBackupBusy_||nativeShowBusy_||!enable.isBool()||
               !armix::console::approvedNativeId(value.getProperty("deviceEpoch",{}),ep)||
               !armix::console::approvedNativeId(value.getProperty("graphRevision",{}),rev)||
               !armix::console::approvedNativeId(value.getProperty("rosterRevision",{}),roster))
                error="DMX: stale request or mixer operation in progress";
            else if(!bool(value.getProperty("smart",false)))
                error=engine_.patchDmxSplitOutput(ep,rev,roster,bool(enable));
            else {
                // One explicit mode change owns both output gates and the
                // existing native graph transaction. Never mutate PCM in JS.
                auto lease=deviceApply_.tryBegin(armix::device::NativeDeviceApplyCoordinator::Purpose::secondaryOutput);
                const auto intent=engine_.captureSecondaryResumeIntent();
                const bool resumeHardware=!operatorOutputMuted_&&
                    (engine_.isMonitorEnabled()||engine_.wantsMainContinuation());
                const auto generation=secondaryRetry_.generation();
                if(!lease||!intent||!engine_.secondarySourceMatches(*intent)||
                   !engine_.secondaryResumeReady()||ep!=engine_.dryRoutingLabEpoch()||
                   !engine_.nativeTopologyMatches(ep,rev)||mixerSessionWorker_.busy()||
                   pendingSecondaryStop_||pendingRetainedFxCapture_||retainedOfflineFxActive_||engine_.hasRuntimeFxFault())
                    error="Connect the additional output and wait for audio to be ready";
                else {
                    autoMainEpoch_=0;
                    engine_.setMonitorEnabled(false);
                    engine_.disarmSecondaryOutput();
                    error=engine_.patchDmxSplitOutput(ep,rev,roster,bool(enable),true);
                    const auto acceptedRevision=engine_.outputArmRevision();
                    if(generation==secondaryRetry_.generation()&&!pendingSecondaryStop_&&
                       engine_.dryRoutingLabEpoch()==ep&&
                       (error.isEmpty()||acceptedRevision==rev)) {
                        // On rejection restore the prior verified route; on
                        // success wait for callback mapping before restoring.
                        if(resumeHardware)requestAutomaticMain(false);
                        pendingVendorStream_=PendingVendorStream{*intent,ep,acceptedRevision,
                            static_cast<std::uint64_t>(monotonicMilliseconds()+5000),
                            resumeHardware};
                    }
                    emitApprovedNativeSnapshot();
                }
            }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",args.getProperty("requestId",{}));
            ack->setProperty("action",action);
            ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            const auto route=engine_.status().getProperty("dryRoutingLab",{});
            ack->setProperty("secondaryMainOnly",route.getProperty("secondaryMainOnly",false));
            // The ACK names the exact installed PCM revision, not controlRevision.
            // The GUI separately verifies the observed physical mapping and endpoint.
            ack->setProperty("graphRevision",route.getProperty("nativeBus",{}).getProperty("graphRevision",{}));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixSecondaryOutputAck",juce::var(ack.release()));
            return emitNotice(error.isNotEmpty()?error:
                (bool(enable)?"Separate headphone mix selected · edit MONITOR sends in the mixer":
                "Same mix selected · restoring verified outputs"),error.isNotEmpty());
        }
        if(action=="secondaryOutputEnable" || action=="secondaryOutputConnect" ||
           action=="secondaryOutputArm" || action=="secondaryOutputStop") {
            juce::String error;
            auto lease=deviceApply_.tryBegin(armix::device::NativeDeviceApplyCoordinator::Purpose::secondaryOutput);
            const auto generation=secondaryRetry_.generation();
            if(!lease)error="Audio device is busy";
            else if(action=="secondaryOutputStop")engine_.disconnectSecondaryOutput();
            else if(studioBackupBusy_ || nativeShowBusy_||pendingSecondaryStop_)
                error="Audio configuration is finishing · try again";
            else if(action=="secondaryOutputConnect" || action=="secondaryOutputEnable") {
                if(!value.isString() || value.toString().isEmpty() || value.toString().length()>240)
                    error="Select a real named Windows playback endpoint";
                else {
                    const auto token=args.getProperty("inventoryId",{}).toString();
                    std::wstring expected;
                    if(token.isNotEmpty()) {
                        const auto it=secondaryPublishedIds_.find(value.toString());
                        if(token!=secondaryPublishedInventory_||it==secondaryPublishedIds_.end())
                            error="Playback device list changed · refreshing";
                        else expected=it->second;
                    }
                    // Legacy explicit named actions remain supported; approved
                    // GUI always sends the inventory token, never a raw ID.
                    if(error.isEmpty())error=action=="secondaryOutputEnable"?
                        engine_.enableSecondaryOutput(value.toString(),expected):
                        engine_.connectSecondaryOutput(value.toString(),expected);
                }
            } else if(action=="secondaryOutputArm") {
                if(!value.isBool()) error="Expected explicit Secondary output ARM true/false";
                else error=engine_.armSecondaryOutput(bool(value));
            }
            if(action!="secondaryOutputStop"&&generation!=secondaryRetry_.generation()) {
                engine_.disconnectSecondaryOutput();
                error="Output request superseded";
            }
            if(error.isEmpty()&&(action=="secondaryOutputEnable"||
               (action=="secondaryOutputArm"&&bool(value))))
                if(auto intent=engine_.captureSecondaryResumeIntent()) {
                    if(engine_.devices().getCurrentAudioDeviceType()=="ASIO")
                        secondaryContinuity_=SecondaryContinuity{*intent,engine_.dryRoutingLabEpoch(),engine_.outputArmRevision()};
                    rememberSecondaryPreference(*intent);
                }
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            const auto latest=engine_.status();
            ack->setProperty("requestId",args.getProperty("requestId",{}));
            ack->setProperty("action",action);
            ack->setProperty("ready",latest.getProperty("secondaryOutputReady",false));
            ack->setProperty("armed",latest.getProperty("secondaryOutputArmed",false));
            ack->setProperty("endpoint",latest.getProperty("secondaryOutputName",""));
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible(
                "arMixSecondaryOutputAck",juce::var(ack.release()));
            diagnostics_.record(error.isEmpty()?"SECONDARY_OUTPUT_CHANGED":"SECONDARY_OUTPUT_REJECTED",
                error.isNotEmpty()?error:action);
            return emitNotice(error.isNotEmpty()?error:
                (action=="secondaryOutputStop"?"Additional playback output disconnected":
                 action=="secondaryOutputEnable"?"Additional output enabled · verify audio in receiving app":
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
            closeAllPluginEditors();
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
            closeAllPluginEditors();
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
        else if (action == "audioNamedProfileFile")
        {
            chooseNamedIoProfile(args);
        }
        else if (action == "audioNamedPatchRead")
        {
            const auto request=args.getProperty("requestId",{}).toString();
            if(request.startsWith("io-preset-")&&request.length()<=64&&browser_.loaded){
                const auto active=currentDeviceSnapshot();
                auto data=std::make_unique<juce::DynamicObject>();
                data->setProperty("requestId",request);
                data->setProperty("active",active);
                data->setProperty("namedPatches",namedPatches_.snapshot(active));
                browser_.emitEventIfBrowserIsVisible("arMixNamedPatchReadback",juce::var(data.release()));
            }
        }
        else if (action == "audioNamedPatchEdit")
        {
            const auto request=args.getProperty("requestId",{}).toString();
            const auto active=currentDeviceSnapshot();
            const auto error=(request.startsWith("io-preset-")&&request.length()<=64&&
                !nativeShowBusy_&&!studioBackupBusy_&&!engine_.independentClockActive()&&
                !engine_.hasTemporaryDiagnosticDevice())
                ?namedPatches_.apply(args,active):juce::String("I/O preset edit unavailable");
            auto ack=std::make_unique<juce::DynamicObject>();
            ack->setProperty("requestId",request);
            ack->setProperty("ok",error.isEmpty());
            ack->setProperty("error",error);
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixNamedPatchAck",juce::var(ack.release()));
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
        else if (action == "audioDriverPanel")
        {
            showAsioControlPanel(args);
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
            // Legacy explicit recovery retains its device safety cleanup, but
            // every user-facing setup now opens the approved custom WebView.
            emitApprovedNativeSnapshot();
            emitDeviceInventory(false);
            if(browser_.loaded)browser_.emitEventIfBrowserIsVisible("arMixOpenAudioIO",juce::var(true));
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
            // Separate bounded attribution record: preserve parseable native
            // evidence even if the full device/FX status is truncated.
            auto rtr=std::make_unique<juce::DynamicObject>();
            for(const char* key:{"callbackCount","overBudgetCount","deviceGeneration",
                "callbackModulo4Observed","callbackModulo4OverBudget","callbackModulo4Provenance",
                "nativeFxWorstUs","nativeFxWorstName","nativeFxWorstInstance","nativeFxTimingLeases",
                "masterAnalysisState","masterAnalysisDroppedFramesLifetime","masterAnalysisSegmentRestartsLifetime"})
                if(const auto value=status.getProperty(key,{});!value.isVoid())
                    rtr->setProperty(key,value);
            diagnostics_.record("AUDIO_GLITCH_RTR",juce::JSON::toString(juce::var(rtr.release()),true));
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
            closeAllPluginEditors();
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
            closeAllPluginEditors();
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
                closeAllPluginEditors();
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
            closeAllPluginEditors();
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
            if (auto* processor = engine_.pluginAt (slot))
            {
                if (editorWindow_ && !routedEditorOwner_ &&
                    editorProcessor_ == processor)
                {
                    editorWindow_->present(); // repeated FX Rack click
                }
                else
                {
                    closePluginEditor();
                    if (auto editor = armix::ui::createHostedAudioEditor(*processor))
                    {
                        editorWindow_ = std::make_unique<PluginEditorWindow> (
                            processor->getName(), std::move(editor), *this);
                        editorProcessor_=processor;
                    }
                    else
                        emitNotice ("This VST3 does not expose a graphical editor.", true);
                }
            }
            else
            {
                // The requested slot no longer owns a processor. Never
                // leave a floating editor bound to a retired plugin.
                closePluginEditor();
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
            if(arm && retainedOfflineFxActive_ && fault.isEmpty())
                fault="Apply Audio I/O to restore the edited offline mixer before enabling output";
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
            if (fault.isNotEmpty())protectKnownFeedbackLoop(fault);
            if (fault.isEmpty()) {
                operatorOutputMuted_=!arm; // an explicit user control, not setup mute
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

    bool finishAudioSetupAndRecoverMixer(MixerBindingMode bindingMode=MixerBindingMode::exact,
                                         bool verifiedReturn=false) {
        if(pendingRetainedFxCapture_ || logicalVendorEditor_ || mixerSessionWorker_.busy())return false;
        if(retainedOfflineFxActive_) {
            // Do not first reattach old PCM and then acknowledge Apply. The
            // offline workspace is the newer authoritative edit document.
            if(!logicalMixer_.loaded() || logicalMixer_.readOnly() ||
               !engine_.hasRetainedNativeSession() ||
               engine_.retainedNativeEpoch()!=retainedOfflineOwnerEpoch_ ||
               !engine_.allOutputsDisarmed()) {
                mixerSessionError_="Offline mixer owner changed; saved edits retained";
                return false;
            }
            if(bindingMode==MixerBindingMode::exact && engine_.sameRetainedAsioDriver())
                bindingMode=MixerBindingMode::verifiedAsioRestart;
            auto doc=logicalMixer_.workspace().document();
            auto states=logicalMixer_.workspace().states();
            const bool resume=doc.resumeMain.value_or(false);
            const auto id=doc.id;
            if(verifiedReturn ? !engine_.directRouteAuthorized() :
                               !engine_.finishManualAudioSetup(false,true)) {
                mixerSessionError_="Audio device verification failed; offline edits retained";
                return false;
            }
            auto error=resolveMixerSessionCatalog(doc,false);
            if(error.isEmpty())error=engine_.restoreNativeMixerSession(std::move(doc),states,false,nullptr,bindingMode);
            if(error.isNotEmpty()) {
                mixerSessionError_=error;
                diagnostics_.record("OFFLINE_MIXER_REBIND_REJECTED",error);
                return false;
            }
            retainedOfflineFxActive_=false;
            retainedOfflineEpoch_=retainedOfflineOwnerEpoch_=retainedOfflineRevision_=0;
            mixerSessionRecoveryPending_=false;
            mixerSessionId_=id;
            lastMixerSessionFingerprint_.clear();
            mixerSessionError_.clear();
            mixerSessionStatus_="RESTORED_MUTED";
            closeAllPluginEditors();
            diagnostics_.record("OFFLINE_MIXER_REBIND_ACCEPTED","Edited workspace published to verified PCM");
            if(resume && !operatorOutputMuted_)requestAutomaticMain(false);
            return true;
        }
        const bool bound=engine_.finishManualAudioSetup(bindingMode!=MixerBindingMode::verifiedMainPinApply);
        if(!engine_.directRouteAuthorized()) {
            engine_.setOutputMuted(true);
            mixerSessionError_="No verified stereo playback device; audio remains gated";
            diagnostics_.record("DIRECT_ROUTE_REJECTED",mixerSessionError_);
            return false;
        }
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

    // Auto-publish the newest *committed* offline document only when an
    // already-open, positively verified original callback is progressing.
    // A driver reopen, third-party VST3 creation, or pin correction remains
    // an explicit Apply operation; never stall a timer on those vendor APIs.
    void serviceRetainedOfflineRecovery() {
        if(!retainedOfflineFxActive_ || pendingRetainedFxCapture_ ||
           !logicalMixer_.loaded() || logicalMixer_.readOnly() ||
           !engine_.hasRetainedNativeSession() ||
           engine_.retainedNativeEpoch()!=retainedOfflineOwnerEpoch_ ||
           !engine_.allOutputsDisarmed() || deviceApply_.busy() ||
           mixerSessionWorker_.busy() || logicalVendorEditor_ ||
           nativeShowBusy_ || studioBackupBusy_ || catalog_.busy() ||
           engine_.isGraphSwitchPending() || engine_.independentClockActive() ||
           engine_.hasPendingDirectRestore() ||
           engine_.diagnosticRoute()!=armix::diagnostics::Route::normal)return;
        if(std::any_of(floatingBuiltinFx_.begin(),floatingBuiltinFx_.end(),[](const auto& held){
            return held && held->window && held->window->isVisible();
        }))return; // no interruption of an active offline editor
        // Keep ordinary VST editor recovery protection in every build.
        if(editorWindow_&&editorWindow_->isVisible())return;
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
        if(immersibEditorWindow_&&immersibEditorWindow_->isVisible())return;
#endif
        const auto now=monotonicMilliseconds(),epoch=engine_.dryRoutingLabEpoch();
        const auto callbacks=engine_.directCallbackCount();
        if(offlineReturnEpoch_!=epoch) {
            offlineReturnEpoch_=epoch;offlineReturnSinceMs_=now;
            offlineReturnCallback_=callbacks;offlineReturnProgressMs_=0;
            offlineAutoNextVerifyMs_=0;
            return;
        }
        if(callbacks!=offlineReturnCallback_) {
            offlineReturnCallback_=callbacks;offlineReturnProgressMs_=now;
        }
        // Observe fresh callback progress, not merely an open driver handle.
        if(!offlineReturnProgressMs_ || now-offlineReturnProgressMs_>500 ||
           now-offlineReturnSinceMs_<350 ||
           now-retainedOfflineLastActivityMs_<1500)return;
        const auto& workspace=logicalMixer_.workspace();
        const auto revision=workspace.revision();
        const auto token=workspace.workspaceToken();
        if(offlineAutoAttemptEpoch_==epoch && offlineAutoAttemptRevision_==revision &&
           offlineAutoAttemptWorkspace_==token)return;
        // Built-ins have host-controlled preparation. An arbitrary vendor
        // DLL can stall in instantiate/setState and is not a timer action.
        for(const auto& fx:workspace.document().inserts)
            if(!armix::builtin_fx::findNativeBuiltInFx(fx.catalogId))return;
        auto* device=engine_.devices().getCurrentAudioDevice();
        if(!device || !device->isOpen())return;
        auto applying=deviceApply_.tryBegin(
            armix::device::NativeDeviceApplyCoordinator::Purpose::asioRecovery);
        if(!applying)return;
        if(now<offlineAutoNextVerifyMs_)return;
        offlineAutoNextVerifyMs_=now+750;
        auto mode=MixerBindingMode::exact;
        if(device->getTypeName()=="ASIO") {
            if(!engine_.authorizeRetainedAsioRestart() || engine_.asioRestartPinCorrection())return;
            mode=MixerBindingMode::verifiedAsioRestart;
        } else if(!engine_.verifiedRetainedWasapiReturn())return;
        // Consume one attempt per exact epoch/workspace revision, not per tick.
        offlineAutoAttemptEpoch_=epoch;offlineAutoAttemptRevision_=revision;
        offlineAutoAttemptWorkspace_=token;
        const auto started=monotonicMilliseconds();
        const bool restored=finishAudioSetupAndRecoverMixer(mode,true);
        diagnostics_.record("OFFLINE_AUTO_REBIND_MS",
            juce::String(static_cast<juce::int64>(monotonicMilliseconds()-started)));
        if(restored) {
            asioRecoveryEpoch_=engine_.dryRoutingLabEpoch();
            wasapiRecovery_.recovered();
            diagnostics_.record("OFFLINE_AUTO_REBIND_ACCEPTED","Verified original callback; latest mixer restored");
        } else {
            diagnostics_.record("OFFLINE_AUTO_REBIND_DEFERRED",mixerSessionError_);
        }
        emitApprovedNativeSnapshot();
    }

    void serviceAsioMixerRestart() {
        if(pendingRetainedFxCapture_ || retainedOfflineFxActive_)return; // never rebind stale PCM over edited .smix
        const auto epoch=engine_.dryRoutingLabEpoch();
        const bool retained=engine_.hasRetainedNativeSession();
        const bool silent=!mixerSessionRecoveryPending_ &&
            engine_.hasPristineSilentMixerAwaitingAsio();
        auto* current=engine_.devices().getCurrentAudioDevice();
        const bool missingDevice=!current || !current->isOpen();
        if(retained && (missingDevice || directCallbackStalled_))return; // never reopen a stalled vendor on the GUI timer

        if((epoch==asioRecoveryEpoch_ && !missingDevice &&
            !directCallbackStalled_ && !engine_.mainInterruptedByDevice()) ||
           (!retained && !silent) ||
           deviceApply_.busy() ||
           nativeShowBusy_ || studioBackupBusy_ || mixerSessionWorker_.busy() || catalog_.busy() ||
           engine_.isGraphSwitchPending() || engine_.independentClockActive() ||
           engine_.diagnosticRoute()!=armix::diagnostics::Route::normal)return;
        auto* device=engine_.devices().getCurrentAudioDevice();
        if(device) {
            if(device->getTypeName()!="ASIO" ||
               !engine_.sameRetainedAsioDriver())return;
            if(device->isOpen() && !directCallbackStalled_ &&
               engine_.directRouteAuthorized() && engine_.hasBoundMixerRoute()) {
                // Do not reopen a healthy ASIO stream on display idle.
                if(engine_.mainInterruptedByDevice() &&
                   engine_.wantsMainContinuation() && !operatorOutputMuted_)
                    requestAutomaticMain(false);
                asioRecoveryEpoch_=epoch;
                return;
            }
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
        if(!device || !device->isOpen() || directCallbackStalled_ ||
           device->getActiveOutputChannels().countNumberOfSetBits()<2) {
            // A missing, closed or output-less ASIO device requires a vendor call.
            // Never reopen an authored mixer from the UI timer, even if the driver reports open with no outputs.
            if(retained) {asioRecoveryGate_.waitForReadback(now);return;}
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
            const auto error=openSelectedAudioDevice("ASIO",setup,true); // force fresh driver stream, not a no-op same setup
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
            if(retained) {
                asioRecoveryGate_.bindFailed(now);
                diagnostics_.record("ASIO_PIN_CORRECTION_DEFERRED","Apply Audio I/O to verify changed pins");
                return;
            }
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
        // Automatic recovery must not serialize and instantiate an entire
        // retained VST3 graph on a GUI timer. Reuse exact same-format leases.
        const auto error=engine_.reattachRetainedAsioMixer()?juce::String{}:
            juce::String("Retained mixer needs verified Audio I/O Apply");
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
        mixerSessionStatus_=retainedOfflineFxActive_?
            "RETAINED_OFFLINE_FX_REAPPLY_REQUIRED":"RESTORED_MUTED";
        mixerSessionError_.clear();
        const bool previousMain=engine_.takeAsioResetMonitorIntent();
        if((previousMain || engine_.wantsMainContinuation()) && !operatorOutputMuted_)
            requestAutomaticMain(false);
        if(engine_.isMonitorEnabled())mixerSessionStatus_="RESTORED_MONITORING";
        diagnostics_.record("ASIO_REATTACHED","Verified driver and current MAIN pair");
        emitApprovedNativeSnapshot();
    }

    // Load only the previously committed device XML, even when hardware
    // is absent at cold start. A JUCE default is never an approved profile.
    void rememberSavedWasapiProfile(const juce::XmlElement* saved) {
        if(!saved || saved->getTagName()!="DEVICESETUP" ||
           saved->getStringAttribute("deviceType")!="Windows Audio")return;
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        setup.inputDeviceName=saved->getStringAttribute("audioInputDeviceName");
        setup.outputDeviceName=saved->getStringAttribute("audioOutputDeviceName");
        if(setup.outputDeviceName.isEmpty())return;
        setup.sampleRate=saved->getDoubleAttribute("audioDeviceRate",0.);
        setup.bufferSize=saved->getIntAttribute("audioDeviceBufferSize",0);
        if(saved->hasAttribute("audioDeviceInChans")) {
            setup.useDefaultInputChannels=false;
            setup.inputChannels.parseString(saved->getStringAttribute("audioDeviceInChans"),2);
        }
        if(saved->hasAttribute("audioDeviceOutChans")) {
            setup.useDefaultOutputChannels=false;
            setup.outputChannels.parseString(saved->getStringAttribute("audioDeviceOutChans"),2);
        }
        verifiedWasapiSetup_=std::move(setup);
    }

    // Read back both physical device names AND actual pin masks. Backend
    // name alone does not prove a Windows default-endpoint switch retained
    // the selected speakers. A default pin mask becomes fixed only after the
    // user's device actually opens and is verified, not at a missing-device
    // startup. Never route audio to another Windows Audio endpoint.
    static bool matchesPinnedWasapiReadback(
        const juce::AudioDeviceManager::AudioDeviceSetup& wanted,
        const juce::AudioDeviceManager::AudioDeviceSetup& actual,
        juce::AudioIODevice* device) noexcept {
        return device && device->isOpen() &&
            wanted.outputDeviceName.isNotEmpty() &&
            actual.inputDeviceName==wanted.inputDeviceName &&
            actual.outputDeviceName==wanted.outputDeviceName &&
            device->getActiveOutputChannels().countNumberOfSetBits()>=2 &&
            (wanted.useDefaultInputChannels ||
             device->getActiveInputChannels()==wanted.inputChannels) &&
            (wanted.useDefaultOutputChannels ||
             device->getActiveOutputChannels()==wanted.outputChannels);
    }

    // Recovery only for a verified saved Windows Audio endpoint; no
    // fallback, no graph CLEAR, no attempt while an explicit device dialog
    // owns the lifecycle. The current native graph remains the PCM authority.
    void rememberVerifiedWasapiSetup() {
        if(!audioEngineStarted_ || deviceApply_.busy() ||
           engine_.independentClockActive() ||
           engine_.diagnosticRoute()!=armix::diagnostics::Route::normal ||
           !engine_.directRouteAuthorized() ||
           engine_.hasRetainedUnboundMixer())return;
        auto& manager=engine_.devices();
        auto* device=manager.getCurrentAudioDevice();
        if(!device || !device->isOpen())return;
        if(manager.getCurrentAudioDeviceType()!="Windows Audio" ||
           device->getTypeName()!="Windows Audio") {
            // An explicitly approved ASIO replaces the old WASAPI intent.
            verifiedWasapiSetup_.reset();
            wasapiRecovery_.cancel();
            return;
        }
        juce::AudioDeviceManager::AudioDeviceSetup actual;
        manager.getAudioDeviceSetup(actual);
        if(!verifiedWasapiSetup_ && actual.outputDeviceName.isNotEmpty() &&
           device->getActiveOutputChannels().countNumberOfSetBits()>=2)
            verifiedWasapiSetup_=actual;
        if(verifiedWasapiSetup_ &&
           matchesPinnedWasapiReadback(*verifiedWasapiSetup_,actual,device)) {
            // JUCE "default channel" requests are resolved now. Remember
            // exact physical masks so later driver re-enumeration cannot
            // silently substitute another stereo output or microphone.
            auto& pinned=*verifiedWasapiSetup_;
            if(pinned.useDefaultInputChannels) {
                pinned.inputChannels=device->getActiveInputChannels();
                pinned.useDefaultInputChannels=false;
            }
            if(pinned.useDefaultOutputChannels) {
                pinned.outputChannels=device->getActiveOutputChannels();
                pinned.useDefaultOutputChannels=false;
            }
            if(!directCallbackStalled_)wasapiRecovery_.recovered();
        }
    }

    void observeNativeCallbackHeartbeat() {
        const auto now=monotonicMilliseconds();
        const auto count=engine_.directCallbackCount();
        // A long message-loop pause (including true laptop sleep) is NOT
        // proof of a dead audio callback. Restart the observation window.
        if(callbackWatchTickMs_==0 || now<callbackWatchTickMs_ ||
           now-callbackWatchTickMs_>1500 || count!=callbackWatchCount_) {
            callbackProgressMs_=now;
            directCallbackStalled_=false;
        } else {
            auto* device=engine_.devices().getCurrentAudioDevice();
            directCallbackStalled_=device && device->isOpen() &&
                engine_.directRouteAuthorized() && !engine_.independentClockActive() &&
                engine_.hasBoundMixerRoute() && now>=callbackProgressMs_ &&
                now-callbackProgressMs_>=3500;
        }
        callbackWatchTickMs_=now;
        callbackWatchCount_=count;
    }

    void serviceWasapiDeviceContinuity() {
        if(pendingRetainedFxCapture_ || retainedOfflineFxActive_)return;
        if(!audioEngineStarted_ || !verifiedWasapiSetup_ ||
           engine_.independentClockActive() || deviceApply_.busy() ||
           nativeShowBusy_ || studioBackupBusy_ || mixerSessionWorker_.busy() ||
           catalog_.busy() || engine_.isGraphSwitchPending() ||
           engine_.diagnosticRoute()!=armix::diagnostics::Route::normal)return;
        auto& manager=engine_.devices();
        auto* device=manager.getCurrentAudioDevice();
        if(engine_.hasRetainedNativeSession()) {
            // OS/JUCE STOP/START may restore the exact endpoint. Reuse leases;
            // no driver reopen or vendor-state reconstruction from this timer.
            const auto now=monotonicMilliseconds();
            wasapiRecovery_.fault(now);
            if(!wasapiRecovery_.due(now))return;
            if(device && device->isOpen() && !directCallbackStalled_ &&
               engine_.directRouteAuthorized() && engine_.recoverSameWasapiMixer()) {
                wasapiRecovery_.recovered();
                mixerSessionStatus_="RESTORED_MUTED";
                mixerSessionError_.clear();
                if(engine_.wantsMainContinuation() && !operatorOutputMuted_)
                    requestAutomaticMain(false);
                emitApprovedNativeSnapshot();
            } else wasapiRecovery_.failed(now);
            return;
        }
        juce::AudioDeviceManager::AudioDeviceSetup live;
        manager.getAudioDeviceSetup(live);
        const bool expected=device && device->isOpen() &&
            manager.getCurrentAudioDeviceType()=="Windows Audio" &&
            device->getTypeName()=="Windows Audio" && engine_.directRouteAuthorized() &&
            !engine_.hasRetainedUnboundMixer() &&
            matchesPinnedWasapiReadback(*verifiedWasapiSetup_,live,device);
        if(expected && !directCallbackStalled_) {
            wasapiRecovery_.recovered();
            // A short JUCE STOP/START must not leave MAIN latched off.
            if(engine_.mainInterruptedByDevice() && engine_.wantsMainContinuation() &&
               !operatorOutputMuted_)requestAutomaticMain(false);
            return;
        }
        // Healthy intentionally selected ASIO is never replaced by an old
        // WASAPI profile. Nor is an unknown fallback given sound permission.
        if(expected==false && device && device->isOpen() &&
           manager.getCurrentAudioDeviceType()!="Windows Audio" &&
           engine_.directRouteAuthorized())return;
        const auto now=monotonicMilliseconds();
        wasapiRecovery_.fault(now);
        if(!wasapiRecovery_.due(now))return;
        auto applying=deviceApply_.tryBegin(
            armix::device::NativeDeviceApplyCoordinator::Purpose::wasapiRecovery);
        if(!applying)return;
        const auto wanted=*verifiedWasapiSetup_;
        engine_.beginManualAudioSetup(true);
        auto error=openSelectedAudioDevice("Windows Audio",wanted,
            !device || !device->isOpen() || directCallbackStalled_);
        if(error.isEmpty()) {
            auto* opened=manager.getCurrentAudioDevice();
            juce::AudioDeviceManager::AudioDeviceSetup actual;
            manager.getAudioDeviceSetup(actual);
            if(!opened || !opened->isOpen() ||
               manager.getCurrentAudioDeviceType()!="Windows Audio" ||
               actual.inputDeviceName!=wanted.inputDeviceName ||
               actual.outputDeviceName!=wanted.outputDeviceName ||
               !matchesPinnedWasapiReadback(wanted,actual,opened))
                error="Previously verified WASAPI endpoints are not available";
            else if(!finishAudioSetupAndRecoverMixer())
                error="WASAPI reopened, but retained native mixer binding is not ready";
        }
        if(error.isEmpty()) {
            wasapiRecovery_.recovered();
            directCallbackStalled_=false;
            callbackProgressMs_=monotonicMilliseconds();
            if(engine_.wantsMainContinuation() && !operatorOutputMuted_)
                requestAutomaticMain(false);
            diagnostics_.record("WASAPI_CONTINUITY_RESTORED",
                "Exact saved endpoint and native mixer verified; MAIN intent preserved");
            emitApprovedNativeSnapshot();
        } else {
            wasapiRecovery_.failed(now);
            diagnostics_.record("WASAPI_CONTINUITY_RETRY",error);
            // Do not persist the temporary null/default manager setup.
        }
    }

    // Only a positively identified virtual-cable self-loop trips the true
    // MASTER mute; an ordinary device/driver fault never touches that fader.
    // Internal graph cycles are rejected by the existing native DAG compiler.
    void protectKnownFeedbackLoop(const juce::String& fault) {
        if(!fault.contains("VB-Cable capture/playback self-loop"))return;
        engine_.setMasterMuted(true);
        if(!feedbackMasterTripNotified_) {
            feedbackMasterTripNotified_=true;
            diagnostics_.record("MASTER_FEEDBACK_AUTO_MUTE",
                "Identical VB-Cable capture/playback route detected");
            emitNotice("Feedback loop detected. MASTER MUTE engaged; correct the input/output route.",true);
            emitApprovedNativeSnapshot();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if(deviceApply_.busy())return; // synchronous transaction owns recovery
        // An OS/JUCE device notification is NOT an operator configuration
        // edit. A missing device or temporary default must never overwrite
        // the last verified audio-devices.xml selection.
        // The retention gate closes only on an actual epoch/owner change.
        diagnostics_.record ("DEVICE_CHANGE", "Device manager event");
        if(!pendingRetainedFxCapture_ && !retainedOfflineFxActive_ && !deviceApply_.busy() &&
           engine_.recoverSameWasapiMixer()) {
            mixerSessionStatus_="RESTORED_MUTED";
            mixerSessionError_.clear();
            if(engine_.wantsMainContinuation() && !operatorOutputMuted_)
                requestAutomaticMain(false); // same verified pair only
            diagnostics_.record("DYNAMIC_MIXER_REATTACHED",
                "Verified WASAPI callback epoch; previous MAIN intent retained");
            emitApprovedNativeSnapshot();
        }
    }

    void timerCallback() override
    {
#if defined(_WIN32)
        // A startup splash can delay HWND creation; attach once the real JUCE
        // peer exists. Never install a low-level keyboard hook.
        if (!updaterProbeMode_)
            if(auto* peer=getPeer()) {
                hotkeys_.attach(peer->getNativeHandle());
                attachAudioPowerNotifications(peer->getNativeHandle());
            }
#endif
        if(deviceApply_.busy())return; // a vendor modal loop may re-enter JUCE
        if(pendingSecondaryStop_) {
            auto stop=std::move(*pendingSecondaryStop_);
            pendingSecondaryStop_.reset();
            onAction(stop);
        }
        serviceSecondaryInventory();
        servicePhysicalLoopback();
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
        servicePendingNativeGeq();
        servicePendingNativeComp();
        servicePendingNativeDucker();
        servicePendingNativeDeEzzer();
        servicePendingNativeAntiNoiz();
        servicePendingNativeSonKu();
        servicePendingNativeHarmony();
        servicePendingNativeThrow();
        saveClosedLogicalVendorEditor();
        // A hidden JUCE/WebView2 peer may defer first navigation. Expose the
        // compositor while the opaque always-on-top splash masks its first
        // frame. Never call toBack(): it can leave the finished mixer behind
        // unrelated desktop windows when Windows refuses foreground activation.
        if (!webViewPeerWarmed_)
        {
            webViewPeerWarmed_ = true;
            setVisible (true);
            browser_.resized(); // first visible HWND, not its pre-splash size
        }
        // Existing message-thread 12 Hz timer: no extra worker or DSP work.
        // Never allow a hidden broken GUI to continue unattended audio.
        if (!browser_.loaded && !webViewStartupNoticeSent_ &&
            juce::Time::getMillisecondCounter() - webViewStartupTick_ > 25000u)
        {
            webViewStartupNoticeSent_ = true;
            // The branded emergency notice is non-modal. Do not allow a
            // delayed browser handshake to start audio behind it.
            stopTimer();
            setEnabled(false);
            diagnostics_.record ("WEBVIEW2_GUI_READY_TIMEOUT", "No native ArSonMix UI handshake");
            engine_.stop();
            if (onStartupStage_) onStartupStage_ (3); // dismiss splash before error dialog
            if (!updaterProbeMode_)
                showWebViewStartupError ("The embedded mixer did not initialize in 25 seconds."
                                         " Check the WebView2 Runtime and browser-cache folder permissions.");
            else
                writeStartupDiagnostic("WEBVIEW_GUI_READY_TIMEOUT");
            return;
        }
        // Health probes verify WebView/native readiness without opening any
        // audio device, restoring plugins or writing a replacement .smix.
        if (updaterProbeMode_ && audioEngineStarted_)
            return;
        // First visible GUI frame must precede device discovery. Reuse the
        // existing timer; never add a worker or poll the audio callback.
        if (!audioEngineStarted_)
        {
            if (!browser_.loaded || mixerSessionWorker_.busy() || logicalVendorEditor_ ||
                juce::Time::getMillisecondCounter() - guiReadyTick_ < 150u)
                return;
            audioEngineStarted_ = true;
            if (updaterProbeMode_)
            {
                diagnostics_.record ("UPDATER_GUI_PROBE", "Native bridge ready; device and session intentionally untouched");
                if (onStartupStage_) onStartupStage_ (2);
                return;
            }
            auto saved = juce::XmlDocument::parse (
                machineSettingsDirectory().getChildFile ("audio-devices.xml"));
            rememberSavedWasapiProfile(saved.get());
            startupError_ = engine_.start (saved.get());
            engine_.devices().addChangeListener (this);
            diagnostics_.record ("STARTUP",
                startupError_.isEmpty() ? "Audio device initialized" : startupError_);
            // Only pristine Windows first runs are candidates. Saved sessions,
            // explicit ASIO/WASAPI profiles and recovered mixers are excluded.
            if (saved == nullptr &&
                !armix::session::NativeMixerSessionStore(settingsDirectory()).exists() &&
                startupError_.isEmpty())
                inspectFirstRunAudio();
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
                if(guard->interrupted()||vendorSafeRecoveryActive()) {
                    mixerSessionStatus_="STARTUP_FX_SAFE_RECOVERY";
                    mixerSessionError_="Previous VST3 lifetime ended unexpectedly. "
                        "Session preserved; reopen a verified mixer file manually after checking plugins.";
                    const auto previous=armix::startup::RuntimeVendorCrashJournal::process().previousOperation();
                    if(previous.isNotEmpty())diagnostics_.record("LAST_VENDOR_OPERATION",previous);
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
            }else if(vendorSafeRecoveryActive()) {
                mixerSessionStatus_="STARTUP_FX_SAFE_RECOVERY";
                emitNotice("Previous vendor lifetime ended unexpectedly. MAIN automatic resume is disabled; open a verified mixer file to recover.",true);
            }else if(startupError_.isEmpty()){
                if(!ioDefaults_.layout().empty()&&!ioDefaults_.readOnly()&&canInstallIoDefaults()){
                    try{
                        if(engine_.installIoPortDefaults(ioDefaults_.layout()))
                            diagnostics_.record("IO_DEFAULTS_APPLIED","Fresh silent mixer; no channel assignments or output arm");
                        else diagnostics_.record("IO_DEFAULTS_DEFERRED","No verified pristine device");
                    }catch(...){diagnostics_.record("IO_DEFAULTS_DEFERRED","Default preparation failed");}
                }
                requestAutomaticMain();
            }
        }
        // Do not clear the marker as soon as constructors return: Waves and
        // other VST3 vendors may crash in their first audio callbacks.
        if(startupFxGuard_ && browser_.loaded &&
           juce::Time::getMillisecondCounter()-startupFxGuardTick_>=15000u) {
            if(startupFxGuard_->complete())startupFxGuard_.reset();
        }
        // Report caught C++ DSP exceptions on CONTROL, at most once per
        // fault episode. Do not log, allocate or reopen a plugin in RT.
        const auto faultNow=juce::Time::getMillisecondCounter();
        if(faultNow-runtimeFxFaultPollTick_>=1000u) {
            runtimeFxFaultPollTick_=faultNow;
            const bool faulted=engine_.hasRuntimeFxFault();
            if(faulted&&!runtimeFxFaultNoticeSent_) {
                autoMainEpoch_=0;
                diagnostics_.record("VST3_RUNTIME_FAULT_LATCHED","Affected FX path muted; replace or remove the failed instance");
                emitNotice("An FX threw during processing. Its path is muted. Remove/re-add the failed FX or open a verified mixer file; the saved checkpoint is preserved.",true);
            }
            runtimeFxFaultNoticeSent_=faulted;
        }
        observeNativeCallbackHeartbeat();
#if defined(_WIN32)
        if(actualSystemResumePending_) {
            actualSystemResumePending_=false;
            diagnostics_.record("WINDOWS_SYSTEM_RESUME",
                actualSystemSuspendObserved_?"Real suspend/resume":"Resume event");
            actualSystemSuspendObserved_=false;
            // Do not restart a healthy audio device after a display-only event.
            if(engine_.mainInterruptedByDevice() || !engine_.directRouteAuthorized())
                wasapiRecovery_.endpointReturned(monotonicMilliseconds());
        }
#endif
        serviceRetainedOfflineRecovery();
        serviceAsioMixerRestart();
        serviceWasapiDeviceContinuity();
        rememberVerifiedWasapiSetup();
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
        serviceVendorStreamRecovery();
        serviceSecondaryContinuity();
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
            auto health = engine_.status();
            const auto cpu=sampleFooterHostCpuPercent();
            if(!cpu.isVoid()){
                health.getDynamicObject()->setProperty("footerProcessCpuPercent",cpu);
                health.getDynamicObject()->setProperty("footerCpuProvenance",
                    "WINDOWS_PROCESS_CPU_ALL_LOGICAL_CORES_1HZ_NOT_AUDIO_CALLBACK_OR_SYSTEM_TOTAL");
            }
            const auto evidence=makeEvidenceSample (health);
            crackleEvidence_.push (evidence);
            recordAudioPathTransition(health);
            diagnostics_.record ("AUDIO_HEALTH", juce::JSON::toString (makeAudioHealthRecord (health,evidence.memory), true));
        }

        if (deviceSettingsDirty_)
        {
            // Keep a newly approved device selection pending while the
            // driver is temporarily closed; never serialize JUCE's empty
            // default and never silently discard the unsaved user binding.
            auto* activeDevice=engine_.devices().getCurrentAudioDevice();
            if (!engine_.hasTemporaryDiagnosticDevice() && !engine_.independentClockActive() &&
                engine_.directRouteAuthorized() && !engine_.hasRetainedUnboundMixer() &&
                !deviceApply_.busy() &&
                activeDevice != nullptr && activeDevice->isOpen())
            if (auto xml = engine_.devices().createStateXml())
            {
                deviceSettingsDirty_ = false;
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
        // A hidden/reloading WebView MUST NOT suspend the native player's
        // control-thread ACK, Auto Next, or A-B loop. Browser events are
        // optional observers; JUCE transport is the authority.
        if (browser_.loaded && !initialStateSent_)
        {
            initialStateSent_ = true;
            emitCatalog();
            emitScanProgress();
            emitChain();
            emitPresets();
            emitPlayerState(true);
        }
        if (browser_.loaded && catalog_.busy() && timerTicks_ % 3 == 0)
            emitScanProgress();
        // Run transport deadlines at 12 Hz only while necessary. The
        // browser still receives lightweight state at its old 4 Hz pace.
        if(timerTicks_%3==0 || playerAutoNext_ ||
           playerLoopCurrent_ || playerPendingIndex_>=0) {
            // Start only a genuinely prepared NEXT and ACK the row identity
            // in the same UI-owner thread. No session/graph mutation here.
            if(engine_.servicePlayerTransition()) {
                // A native ACK is a NEW logical song/loop epoch even if
                // standby A/B happen to publish the same generation.
                playerAutoAdvanceGate_.rearm();
                if(playerPendingIndex_>=0 &&
                   playerPendingIndex_<static_cast<int>(playerLibrary_.size())) {
                    playerCurrent_=playerPendingIndex_;
                    playerSelected_=playerCurrent_;
                    if(playerPendingQueueFile_!=juce::File{} &&
                       armix::player::PlayerShowTransitionPolicy::
                           commitQueuedFront(playerQueue_,
                                             playerPendingQueueFile_))
                        playerQueueEpoch_.changed();
                }
                playerPendingQueueFile_={};
                playerPendingIndex_=-1;
                playerError_.clear();
                emitPlayerState(true,false);
            } else if(playerPendingIndex_>=0 &&
                      engine_.playerTransitionFailed()) {
                // Preserve queued entry on decode error: the performer can
                // retry. A failed NEXT must not consume the user's setlist.
                playerPendingQueueFile_={};
                playerPendingIndex_=-1;
                playerError_="NEXT could not decode; current song is unchanged";
                emitPlayerState(true);
            }
            const auto transport=engine_.playerStatus();
            const auto phase=transport.getProperty(
                "state","").toString();
            const bool fading=static_cast<bool>(
                transport.getProperty("crossfading",false));
            const bool preparing=static_cast<bool>(
                transport.getProperty("transitionPending",false));
            const bool activeCurrent=playerCurrent_>=0 &&
                playerCurrent_<static_cast<int>(playerLibrary_.size());
            int queued=-1;
            if(!playerQueue_.empty()) {
                const auto found=std::find(playerLibrary_.begin(),
                    playerLibrary_.end(),playerQueue_.front());
                if(found!=playerLibrary_.end())
                    queued=static_cast<int>(found-playerLibrary_.begin());
            }
            const int upcoming=activeCurrent?
                armix::player::PlayerShowTransitionPolicy::nextIndex(
                    playerCurrent_,playerSelected_,
                    static_cast<int>(playerLibrary_.size()),
                    queued):-1;
            // Rehearsal loop has priority over Auto Next and queued
            // songs, which remain untouched until the operator presses Next.
            // Reuse the exact M3 standby worker for a click-safe A-B return;
            // never seek inside the hardware callback.
            if(playerLoopCurrent_ && activeCurrent &&
               phase=="PLAYING" && !fading && !preparing &&
               playerPendingIndex_<0) {
                const auto loop=playerTrackSettings_[
                    static_cast<std::size_t>(playerCurrent_)];
                if(armix::player::PlayerShowTransitionPolicy::validLoop(
                    loop.cueIn,loop.cueOut,playerFadeMilliseconds_)) {
                    const auto currentFile=playerLibrary_[
                        static_cast<std::size_t>(playerCurrent_)];
                    (void)engine_.preloadPlayerMusic(currentFile,
                        loop.key,loop.tempo,loop.cueIn,loop.cueOut);
                    const auto position=static_cast<double>(
                        transport.getProperty("currentSeconds",0.0));
                    const auto duration=static_cast<double>(
                        transport.getProperty("durationSeconds",0.0));
                    const int fade=std::max(40,playerFadeMilliseconds_);
                    // The in-flight transition is the only loop latch:
                    // alternating A/B decoders can share generation IDs.
                    // On failure, turn looping off below (no retry storm).
                    if(armix::player::PlayerShowTransitionPolicy::
                           armBeforeEnd(position,duration,fade,0.085,loop.tempo)) {
                        if(engine_.queuePlayerTransition(currentFile,
                           loop.key,loop.tempo,fade,loop.cueIn,loop.cueOut)) {
                            playerPendingIndex_=playerCurrent_;
                            playerPendingQueueFile_={};
                            playerError_.clear();
                        } else {
                            playerError_="Loop standby failed; original music remains";
                            playerLoopCurrent_=false;
                        }
                    }
                } else playerLoopCurrent_=false;
            }
            // Queue is explicit operator intent: warm NEXT even while Auto
            // Next is OFF. Auto alone may prepare the following setlist row.
            if(!playerLoopCurrent_ && (playerAutoNext_ || queued>=0) &&
               phase=="PLAYING" && !fading && !preparing &&
               playerPendingIndex_<0 && upcoming>=0 &&
               (upcoming!=playerCurrent_ || queued>=0)) {
                const auto preset=playerTrackSettings_[
                    static_cast<std::size_t>(upcoming)];
                (void)engine_.preloadPlayerMusic(
                    playerLibrary_[static_cast<std::size_t>(upcoming)],
                    preset.key,preset.tempo,
                    preset.cueIn,preset.cueOut);
            }
            if(playerAutoNext_ && !playerLoopCurrent_ && activeCurrent &&
               !fading && !preparing && playerPendingIndex_<0 &&
               upcoming>=0 &&
               (upcoming!=playerCurrent_ || queued>=0) &&
               (phase=="PLAYING" || phase=="ENDED")) {
                // CUT is an end-only switch. Positive fade begins before
                // EOF, bounded by media remaining + one 12 Hz timer tick.
                // Failure latches until a user gesture; successful native
                // ACK rearms the gate for the NEXT logical song, including
                // consecutive repeats of exactly the same media file.
                const double position=static_cast<double>(
                    transport.getProperty("currentSeconds",0.0));
                const double duration=static_cast<double>(
                    transport.getProperty("durationSeconds",0.0));
                const bool preEnd=playerFadeMilliseconds_>0 &&
                    armix::player::PlayerShowTransitionPolicy::armBeforeEnd(
                        position,duration,playerFadeMilliseconds_,0.085,
                        static_cast<int>(transport.getProperty(
                            "tempoPercent",100)));
                if((preEnd || phase=="ENDED") &&
                   playerAutoAdvanceGate_.claim()) {
                    playNextMusic();
                }
            }
            if(browser_.loaded && timerTicks_%3==0)emitPlayerState();
        }
        // Only UI/status publication depends on WebView availability.
        // No native playback continuity, cleanup or next-song ACK does.
        if(!browser_.loaded)return;
        const double statusStart = timing ? juce::Time::getMillisecondCounterHiRes() : 0.0;
        auto state = engine_.status();
        if(auto* streamState=state.getDynamicObject()) {
            streamState->setProperty("secondaryOutputRecovering",secondaryRecoveryPending());
            streamState->setProperty("secondaryOutputRecoveryName",secondaryRecoveryName());
        }
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
            const auto evidence=makeEvidenceSample (state);
            crackleEvidence_.push (evidence);
            // UX-1: reuse THIS 1 Hz native status and memory sample, with no
            // extra timer, audio-thread probe or separate browser event.
            // Fields exist ONLY on this one 12 Hz arMixStatus snapshot each
            // second, so WebView updates at most 1 Hz. No fake 0% on failure.
            auto* health = state.getDynamicObject();
            health->setProperty ("footerHealthSequence",
                                 static_cast<int> (timerTicks_ / 12));
            const auto cpu = sampleFooterHostCpuPercent();
            if (!cpu.isVoid())
                health->setProperty ("footerProcessCpuPercent", cpu);
            if (evidence.memory.processAvailable)
                health->setProperty ("footerProcessWorkingSetMiB",
                    static_cast<juce::int64> (evidence.memory.processWorkingSetMiB));
            health->setProperty ("footerCpuProvenance",
                "WINDOWS_PROCESS_CPU_ALL_LOGICAL_CORES_1HZ_NOT_AUDIO_CALLBACK_OR_SYSTEM_TOTAL");
            health->setProperty ("footerRamProvenance",
                "WINDOWS_PROCESS_WORKING_SET_NOT_AUDIO_BUFFER_ALLOCATION");
            recordAudioPathTransition(state);
            diagnostics_.record ("AUDIO_HEALTH", juce::JSON::toString (makeAudioHealthRecord (state,evidence.memory), true));
        }
        // Guard borrowed VST parameter pointers when a native graph edit,
        // device epoch change or Show recall retires the instance.
        if(nativeOrbitLease_ && orbit_.active() &&
           (!engine_.retainsRoutedFxEditor(nativeOrbitEpoch_,nativeOrbitLease_)))
            stopOrbit();
        if(routedEditorOwner_ &&
           !engine_.retainsRoutedFxEditor(routedEditorEpoch_,routedEditorOwner_))
            closePluginEditor();
        // Reclaim every retired vendor HWND before the respective FX lease.
        // This is message-thread O(open editors), never PCM callback work.
        std::erase_if(additionalFxEditors_,[this](const auto& entry){
            return !entry || !entry->owner ||
                !engine_.retainsRoutedFxEditor(entry->epoch,entry->owner);
        });
#if defined(ARMIX_IMMERSIB_RELEASE_CANDIDATE) && ARMIX_IMMERSIB_RELEASE_CANDIDATE
        if(immersibEditorOwner_ &&
           !engine_.retainsRoutedFxEditor(immersibEditorEpoch_,immersibEditorOwner_))
            closeImmerSIBEditor();
#endif
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

#if defined(_WIN32)
// A non-modal desktop notice: no JUCE modal loop, no loss of live mixer controls.
// The close X dismisses without consent; ONLY the OK action writes approval.
// Update installation remains strictly deferred until a normal mixer exit.
class ArSonMixUpdateNotice final : public juce::DocumentWindow
{
public:
    ArSonMixUpdateNotice (const juce::String& tag, std::function<bool()> onAccept)
        : DocumentWindow ("ArSonMix · Update", juce::Colour (0xff141d27), closeButton),
          onAccept_ (std::move (onAccept))
    {
        panel_.message = "Version " + tag + " is ready.\n"
                         "It will install automatically after you close ArSonMix.\n"
                         "Your session and audio will not be interrupted.";
        panel_.ok.onClick = [this]
        {
            // Never say OK succeeded when the per-user approval file is
            // unreadable/unwritable: leave this NON-modal window available
            // for a retry without interrupting audio.
            if (onAccept_ && onAccept_())
            {
                setVisible (false);
                return;
            }
            panel_.message = "Could not save update approval.\n"
                             "Your mixer remains available.\n"
                             "Check disk permissions, then choose Retry.";
            panel_.ok.setButtonText ("Retry");
            panel_.repaint();
        };
        setLookAndFeel(&caption_);
        setUsingNativeTitleBar (false);
        setTitleBarButtonsRequired(closeButton,false);
        setTitleBarHeight (31);
        setIcon(approvedAppIcon());
        setResizable (false, false);
        setContentNonOwned (&panel_, false);
        centreWithSize (428, 185);
        setVisible (true);
        toFront (true);
    }
    ~ArSonMixUpdateNotice() override {
        clearContentComponent();
        setLookAndFeel(nullptr);
    }
    void closeButtonPressed() override { setVisible (false); }

private:
    armix::ui::MixerCaptionLookAndFeel caption_;
    struct Panel final : juce::Component
    {
        juce::String message;
        ArSonNoticeButton ok {"OK",true};
        Panel()
        {
            addAndMakeVisible (ok);
        }
        void resized() override { ok.setBounds (getWidth() - 106, getHeight() - 43, 88, 29); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff141d27));
            g.setColour (juce::Colour (0xffe5edf7));
            g.setFont (juce::Font (juce::FontOptions (14.0f)));
            g.drawFittedText (message, 20, 15, getWidth() - 40, getHeight() - 69,
                              juce::Justification::topLeft, 3);
        }
    } panel_;
    std::function<bool()> onAccept_;
};
#endif

class ConsoleApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "ArSonMix"; }
    const juce::String getApplicationVersion() override { return "0.5.0-native-catalog"; }
    // Prevent competing audio-device owners and WebView2 sessions.
    bool moreThanOneInstanceAllowed() override {
        return armix::vst::ScanIsolation::isWorker(getCommandLineParameterArray());
    }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (recoveryNotice&&recoveryNotice->isVisible()) {
            recoveryNotice->toFront(true);return;
        }
        if (window_)
        {
            window_->setMinimised (false);
            window_->toFront (true);
            window_->grabKeyboardFocus();
        }
    }
    void initialise (const juce::String& commandLine) override
    {
        const auto args=getCommandLineParameterArray();
        if (armix::vst::ScanIsolation::isWorker(args)) {
            scanWorkerMode_=true;
#if defined(_WIN32)
            ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
            const auto hr=::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
#endif
            int result=armix::vst::ScanIsolation::worker(args);
#if defined(_WIN32)
            if (SUCCEEDED(hr)) ::CoUninitialize();
#endif
            setApplicationReturnValue(result);
            quit();
            return;
        }
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
        // Cold-start archive only: never open a device, WebView or mixer worker.
        // No implicit destructive reset or auto-updater after this invocation.
        if(commandLine.trim()=="--archive-mixer-session" && !isAutoUpdateHealthProbe()) {
            recoveryOnly_=true;
            showRecoveryNotice("Recover mixer session",
                "Archive the saved mixer checkpoint, including any corrupt FX state? The original files are preserved. No audio device opens. Restart ArSonMix normally after recovery.",
                "Archive session","Cancel",[this](bool accepted) {
                    if(!accepted){systemRequestedQuit();return;}
                    juce::File archived;
                    const auto error=armix::session::NativeMixerSessionStore(settingsDirectory()).quarantineForStartupRecovery(archived);
                    const juce::String message=error.isNotEmpty()?error:
                        archived.exists()?"Saved mixer archived: "+archived.getFullPathName()+"\nStart ArSonMix normally.":
                        juce::String("No saved mixer checkpoint found.");
                    writeStartupDiagnostic(error.isEmpty()?"MIXER_SESSION_ARCHIVED":"MIXER_SESSION_ARCHIVE_FAILED",message);
                    showRecoveryNotice("Recovery result",message,"Close",{},
                        [this](bool){systemRequestedQuit();});
                });
            return;
        }
        // Paint the approved dark native splash BEFORE browser cache I/O,
        // WebView2 initialization, device scan or native console construction.
        if (!isAutoUpdateHealthProbe())
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
        {
#if defined(_WIN32)
            if (isAutoUpdateHealthProbe())
            {
                // Give the supervisor time to observe GUI_READY_NO_AUDIO,
                // then quit by JUCE's ordinary message-thread teardown.
                if (updateProbeCompleteTick_ != 0 &&
                    juce::Time::getMillisecondCounter() - updateProbeCompleteTick_ >= 3500u)
                {
                    systemRequestedQuit();
                    return;
                }
            }
            else if (++updatePromptTicks_ % 40 == 0)
                serviceSmartUpdateAcknowledgement();
#endif
            return;
        }
        startupAttempted_ = true;
        writeStartupDiagnostic("WEBVIEW_PROFILE_PREFLIGHT");
        // Remain active only as a lightweight update handshake (4 Hz).
        // Do not touch audio-device selection, callback, or GUI geometry.
        startTimer (250);
        const auto folder = webViewProfile();
        if (!folder.createDirectory().wasOk() && !folder.isDirectory())
        {
            splash_.reset();
            // The updater's GUI-only validation must never strand an invisible
            // error dialog. Return control to the supervisor, which will keep
            // the authenticated rollback installer available.
            if (isAutoUpdateHealthProbe())
            {
                systemRequestedQuit();
                return;
            }
            showWebViewStartupError ("Cannot create the browser-cache folder: " + folder.getFullPathName());
            return;
        }
        if (!juce::WebBrowserComponent::areOptionsSupported (webViewOptions()))
        {
            splash_.reset();
            if (isAutoUpdateHealthProbe())
            {
                systemRequestedQuit();
                return;
            }
            showWebViewStartupError ("The native browser backend could not be initialized on this machine.");
            return;
        }
        writeStartupDiagnostic("WEBVIEW_AWAITING_GUI_READY");
        window_ = std::make_unique<ConsoleWindow> ([this] (int stage)
        {
            if (splash_) splash_->setStage (stage);
            if (stage >= 2)
            {
                if (stage==2) writeStartupDiagnostic("GUI_READY");
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
                            .replaceWithText ("GUI_READY_NO_AUDIO");
                }
#endif


                if (isAutoUpdateHealthProbe())
                {
                    if (window_) window_->setVisible (false);
                    updateProbeCompleteTick_ = juce::Time::getMillisecondCounter();
                }
                else if (stage == 2)
                    presentMixerOnStartup();
            }
        });
#if defined(_WIN32)
        if (!isAutoUpdateHealthProbe())
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
        if (scanWorkerMode_) return; // no mixer session or updater on worker exit
        stopTimer();
        const bool hadMixerWindow=window_!=nullptr;
        recoveryNotice.reset();
#if defined(_WIN32)
        updateNotice_.reset();
#endif
        window_.reset();
        // Vendor processors, editors and driver owners have now been destroyed.
        // A GUI-only update health probe must not erase crash evidence.
        if(hadMixerWindow&&!isAutoUpdateHealthProbe())
            (void)armix::startup::RuntimeVendorCrashJournal::process().finishCleanExit();
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
        if (hadMixerWindow && !recoveryOnly_ && !innoIsClosing && !isAutoUpdateHealthProbe() &&
            ::GetSystemMetrics (SM_SHUTTINGDOWN) == 0)
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
    bool scanWorkerMode_=false;
    void presentMixerOnStartup()
    {
        // GUI ready is the *only* normal-launch transition that may foreground
        // the mixer. No polling, always-on-top mixer or focus stealing after
        // a user starts working in another app. The updater's silent probe
        // must remain entirely invisible.
        if (startupMixerPresented_ || !window_ || isAutoUpdateHealthProbe())
            return;
        startupMixerPresented_ = true;
        window_->setVisible (true);
        window_->setMinimised (false);
        window_->toFront (true);
#if defined(_WIN32)
        if (auto* peer = window_->getPeer())
        {
            auto* hwnd = static_cast<HWND> (peer->getNativeHandle());
            if (hwnd != nullptr && ::IsWindow (hwnd))
            {
                // Windows may ignore JUCE's initial foreground request after
                // the splash was a separate TOPMOST HWND. Raise once through
                // the native Z-order, then immediately return to NOTOPMOST.
                // This is not a persistent Always On Top setting.
                constexpr auto flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
                ::SetWindowPos (hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags);
                ::SetWindowPos (hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
                ::SetForegroundWindow (hwnd); // best effort; Windows owns focus policy
            }
        }
#endif
        window_->grabKeyboardFocus();
    }
    bool startupMixerPresented_ = false;
    bool recoveryOnly_ = false;

#if defined(_WIN32)
    void serviceSmartUpdateAcknowledgement()
    {
        // The separately running updater has already staged and SHA-checked
        // both packages. Never claim a prompt can validate publisher identity.
        if (!window_)
            return;
        const auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
        if (local.isEmpty())
            return;
        const auto folder = juce::File (local).getChildFile ("ArSonMix")
                                               .getChildFile ("Updates");
        const auto stateFile = folder.getChildFile ("state.json");
        if (!stateFile.existsAsFile() || stateFile.getSize() > 8192)
            return;
        const auto state = juce::JSON::parse (stateFile.loadFileAsString());
        const auto* object = state.getDynamicObject();
        if (object == nullptr || object->getProperty ("phase").toString() != "staged"
            || object->getProperty ("operation").toString() != "upgrade")
            return;
        const auto tag = object->getProperty ("candidateTag").toString();
        const auto candidate = object->getProperty ("candidateSha").toString();
        const auto previous = object->getProperty ("previousSha").toString();
        if (tag.length() > 60 || !tag.startsWith ("v")
            || candidate.length() != 40 || previous.length() != 40
            || !candidate.containsOnly ("0123456789abcdef")
            || !previous.containsOnly ("0123456789abcdef"))
            return;
        const auto approvalFile = folder.getChildFile ("approved.json");
        if (approvalFile.existsAsFile() && approvalFile.getSize() <= 2048)
        {
            const auto saved = juce::JSON::parse (approvalFile.loadFileAsString());
            if (const auto* ack = saved.getDynamicObject())
                if (ack->getProperty ("candidateTag").toString() == tag
                    && ack->getProperty ("candidateSha").toString() == candidate
                    && ack->getProperty ("previousSha").toString() == previous)
                    return; // Already acknowledged; no repeat dialog.
        }
        if (lastUpdatePromptedSha_ == candidate)
            return; // Never nag during one mixer session.
        if (updateNotice_ && updateNotice_->isVisible())
            return; // Do not open another notice over a currently visible one.
        lastUpdatePromptedSha_ = candidate;
        // The approved user need not see download progress or installer flags.
        // This branded floating notice is NON-modal:
        // a performer can still move faders or patch WIRE during a live session.
        updateNotice_ = std::make_unique<ArSonMixUpdateNotice> (tag,
            [approvalFile, tag, candidate, previous]
            {
                auto approval = juce::DynamicObject::Ptr (new juce::DynamicObject());
                approval->setProperty ("candidateTag", tag);
                approval->setProperty ("candidateSha", candidate);
                approval->setProperty ("previousSha", previous);
                // A usability acknowledgement only. PowerShell verifies the
                // published release, both installer hashes and process locks.
                return approvalFile.replaceWithText (
                    juce::JSON::toString (juce::var (approval.get()), false));
            });
    }
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
    int updatePromptTicks_ = 0;
    std::unique_ptr<ArSonMixUpdateNotice> updateNotice_;
    juce::uint32 updateProbeCompleteTick_ = 0;
    juce::String lastUpdatePromptedSha_;
#endif
    std::unique_ptr<ArSonMixStartupSplash> splash_;
    std::unique_ptr<ConsoleWindow> window_;
};
}
START_JUCE_APPLICATION (ConsoleApp)
