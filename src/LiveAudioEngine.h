// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "StereoPan.h"
#include "NativePresetArchive.h"
#include "VstBusLayout.h"
#include "LiveGraphHandoff.h"
#include "LiveDryRoutingLab.h"
#include "../engine/dsp/NativeMasterLoudness.h"
#include "DiagnosticTone.h"
#include "../engine/device/JuceIndependentClockDevice.h"
#include "../engine/device/LiveSecondaryOutputDevice.h"
#include "../engine/core/LiveOutputRouteGate.h"
#include "../engine/device/AsioPriorityDeviceManager.h"
#include "../engine/device/AsioRegistryIdentityProbe.h"
#include "../engine/device/DirectRouteFence.h"
#include "../engine/device/WasapiEndpointIdentityGuard.h"
#include "../engine/core/FxOrderPlan.h"
#include "../engine/core/FxMoveRamp.h"
#include "../engine/core/RealtimeHealthHistogram.h"
#include "../engine/core/CallbackCadenceEvidence.h"
#include "../engine/core/FxBypassEnvelope.h"
#include "../engine/core/NativeOutputPairTap.h"
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

// One live stereo input path and up to 8 actual VST3 processors.
// The audio callback never scans files, creates plugins, mutates the chain,
// enters UI code, locks host mutexes, logs, or changes device settings.
// P2.5 recall pre-stages a graph and transitions at callback boundaries.
// R0B: ADD/REMOVE and preset recall use bounded graph handoff; MOVE uses atomic order publication.
class LiveAudioEngine final : private juce::AudioIODeviceCallback
{
public:
    static constexpr int maxFx = 8;

    // One last-owner vendor teardown, always on the control thread. No manual
    // refcount-dependent releaseResources calls at scattered exit paths.
    struct PluginRelease final
    {
        void operator()(juce::AudioPluginInstance* processor) const noexcept
        {
            if (!processor) return;
            try { processor->releaseResources(); } catch (...) {}
            delete processor;
        }
    };
    static std::shared_ptr<juce::AudioPluginInstance> ownPlugin (
        std::unique_ptr<juce::AudioPluginInstance> processor)
    {
        return {processor.release(),PluginRelease{}};
    }

    struct Slot
    {
        std::shared_ptr<juce::AudioPluginInstance> processor;
        juce::String name, catalogId, path;
        int uid = 0;
        bool bypass = false;
        int latency = 0;
    };

    struct Graph
    {
        // Eight 4-bit physical slot indices in one x64 lock-free word.
        // Moving an insert publishes one complete order for the NEXT audio
        // block; the processor objects and their binary states never move.
        static constexpr std::uint64_t canonicalOrder = armix::dsp::FxOrderPlan::canonical;
        std::atomic<std::uint64_t> orderPacked {canonicalOrder};
        static int physicalAt (std::uint64_t order, int logical) noexcept
        {
            return armix::dsp::FxOrderPlan::physicalAt (order, logical);
        }
        int physicalAt (int logical) const noexcept
        {
            return physicalAt (orderPacked.load (std::memory_order_acquire), logical);
        }
        std::array<Slot, maxFx> slots {};
        std::array<std::atomic<bool>, maxFx> bypassFlags {};
        // Owned and advanced exclusively by the realtime render callback.
        // Physical-slot identity survives an atomic logical reorder (PR #102).
        std::array<armix::dsp::FxBypassEnvelope, maxFx> bypassEnvelope {};
        int count = 0;
        int latency = 0;
        bool hasRecallState = false;
        float channelDb = 0.f, masterDb = 0.f, pan = 0.f;
        float channelLinear = 1.f, masterLinear = 1.f;
        bool channelMute = false, masterMute = false;
    };

    LiveAudioEngine()
    {
        formats_.addFormat (std::make_unique<juce::VST3PluginFormat>());
        scratch_.setSize (2, maxFrames, false, true, false);
        // One reusable preallocated stereo dry snapshot for transient FX bypass.
        dryScratch_.setSize (2, maxFrames, false, true, false);
        handoff_.initialize (graphOwned_.get());
    }

    ~LiveAudioEngine() override { stop(); }

    juce::AudioDeviceManager& devices() noexcept { return deviceManager_; }
    // Message/control-thread reporting only. The normal JUCE manager is
    // intentionally closed while independent WASAPI capture/render runs.
    // Diagnostics must use the ACTUAL render/capture device in that mode.
    juce::AudioIODevice* diagnosticRenderDevice() noexcept
    {
        return independent_.active() ? independent_.renderDevice()
                                    : deviceManager_.getCurrentAudioDevice();
    }
    juce::AudioIODevice* diagnosticCaptureDevice() noexcept
    {
        return independent_.active() ? independent_.captureDevice()
                                    : deviceManager_.getCurrentAudioDevice();
    }

    juce::String start (const juce::XmlElement* savedSettings = nullptr)
    {
        // No saved physical INPUT PATCH is automatically trusted on launch.
        // A verified device starts with one native 10CH/4AUX SILENT topology;
        // the user must explicitly patch inputs and arm OUTPUT separately.
        monitor_.store (false, std::memory_order_release);
        directFence_.suspend(); // silence until the selected backend is verified
        asioPinnedRegistration_ = {};
        asioRegistrationScope_ = "NONE";
        // Register the host callback BEFORE opening any physical device.
        // If saved ASIO is absent/busy and startup deliberately fails closed,
        // AUDIO SETUP can still select a replacement and JUCE will call the
        // already-registered host's aboutToStart() on the new device.
        // No RT worker, no second callback registration on successful start.
        deviceManager_.addAudioCallback (this);
        attached_ = true;
        const bool savedRoute = savedSettings != nullptr
            && savedSettings->hasTagName ("DEVICESETUP");
        const auto expectedBackend = savedRoute
            ? savedSettings->getStringAttribute ("deviceType") : juce::String();
        // Saved ASIO device labels are never sufficient to trust a registered
        // COM driver class. The native registry lookup mirrors pinned JUCE's
        // SOFTWARE/ASIO view. Recheck after JUCE has actually opened it.
        // This comparison is a control-thread transaction, not a callback.
        std::optional<armix::device::AsioRegistrationPolicy::Identity> savedAsioPreflight;
        if (savedRoute && expectedBackend == "ASIO")
        {
            auto name = savedSettings->getStringAttribute ("audioDeviceName");
            if (name.isEmpty()) name = savedSettings->getStringAttribute ("audioOutputDeviceName");
            if (name.isEmpty()) name = savedSettings->getStringAttribute ("audioInputDeviceName");
            const auto identity = armix::device::AsioRegistryIdentityProbe::resolve (
                std::wstring (name.toWideCharPointer()));
            if (!identity.registration.ok())
            {
                // JUCE requires initialise() to establish its default minimum
                // channel requirements for AUDIO SETUP, even if this saved ASIO
                // route must never be opened. Only initialise a transient
                // Windows Audio fallback while the DIRECT fence is SUSPENDED,
                // then close it. Never instantiate the rejected ASIO class.
                juce::XmlElement safeRequirements ("DEVICESETUP");
                safeRequirements.setAttribute ("deviceType", "Windows Audio");
                (void) deviceManager_.initialise (1, 2, &safeRequirements, false);
                deviceManager_.closeAudioDevice();
                asioRegistrationScope_ = "SAVED_ASIO_PREFLIGHT_REJECTED";
                return "Saved ASIO registration preflight rejected: " +
                    juce::String (armix::device::AsioRegistrationPolicy::reason (
                        identity.registration.error));
            }
            savedAsioPreflight = identity.registration.identity;
        }
        // Saved explicit device choice MUST NOT be silently replaced by
        // JUCE's selectDefaultDeviceOnFailure path (e.g. ASIO -> WASAPI).
        auto error = deviceManager_.initialise (1, 2, savedSettings, !savedRoute);
        if (error.isNotEmpty() && !savedRoute && deviceManager_.singleAsioPreferred())
        {
            // This was only an automatic FIRST-RUN ASIO preference, not the
            // user's chosen ASIO driver. Keep WASAPI supported if the sole
            // discovered ASIO driver cannot open. Report the provenance.
            const auto asioError = error;
            deviceManager_.setCurrentAudioDeviceType ("Windows Audio", false);
            if (deviceManager_.getCurrentAudioDevice() != nullptr &&
                deviceManager_.getCurrentAudioDeviceType() == "Windows Audio")
            {
                startupFallbackReason_ = "AUTO_ASIO_FAILED_WASAPI_DIRECT";
                error.clear();
            }
            else error = "Automatic ASIO open failed: " + asioError
                       + " | Windows Audio fallback unavailable";
        }
        if (error.isNotEmpty()) return error;
        const auto* active = deviceManager_.getCurrentAudioDevice();
        if (active == nullptr)
            return "Selected audio backend has no open device";
        if (savedRoute)
        {
            // JUCE's missing-backend XML compatibility logic may otherwise
            // infer another driver by friendly name, even with fallback=false.
            // A persisted manually selected ASIO/WASAPI route must not hijack
            // a different physical output on startup.
            const auto expectedDuplexName = savedSettings->getStringAttribute ("audioDeviceName");
            const auto inputName = expectedDuplexName.isNotEmpty()
                ? expectedDuplexName
                : savedSettings->getStringAttribute ("audioInputDeviceName");
            const auto outputName = expectedDuplexName.isNotEmpty()
                ? expectedDuplexName
                : savedSettings->getStringAttribute ("audioOutputDeviceName");
            juce::AudioDeviceManager::AudioDeviceSetup actual;
            deviceManager_.getAudioDeviceSetup (actual);
            if ((expectedBackend.isNotEmpty() &&
                 deviceManager_.getCurrentAudioDeviceType() != expectedBackend)
                || (inputName.isNotEmpty() && actual.inputDeviceName != inputName)
                || (outputName.isNotEmpty() && actual.outputDeviceName != outputName))
            {
                deviceManager_.closeAudioDevice();
                return "Saved audio route identity changed: choose an explicit ASIO/WASAPI device in AUDIO SETUP";
            }
        }
        // The native registration comparison MUST complete while the DIRECT
        // fence is still suspended. approveDirectRoute() publishes permission
        // only AFTER its post-open full CLSID/key check has succeeded.
        if (!approveDirectRoute (savedAsioPreflight
                                 ? &*savedAsioPreflight : nullptr))
        {
            deviceManager_.closeAudioDevice();
            return "Device backend or saved ASIO registration changed; choose AUDIO SETUP explicitly";
        }
        if (savedAsioPreflight)
            asioRegistrationScope_ = "SAVED_PREFLIGHT_POSTOPEN_REGISTRATION_CONSISTENT";
        const auto sessionError=startUnpatchedProductionSession();
        if(sessionError.isNotEmpty())
            return "Native production mixer cannot start: "+sessionError;
        return {};
    }

    void stop()
    {
        dryRoutingLab_.disable();
        directFence_.suspend();
        asioPinnedRegistration_ = {};
        asioRegistrationScope_ = "NONE";
        secondaryOutput_.stop(); // stop independent OBS consumer before audio graph teardown
        independent_.close();
        detach();
        handoff_.resetAfterStop (graphOwned_.get());
        // Last owner releases vendor resources exactly once after detach.
        // Reused instances remain valid until their final control-owned slot.
        stagedOwner_.reset();
        fxMovePhase_.store (0, std::memory_order_release);
        fxMovePendingOrder_.store (Graph::canonicalOrder, std::memory_order_relaxed);
        for (int i = 0; i < graphOwned_->count; ++i)
        {
            graphOwned_->slots[i] = Slot {}; // last-owner control release
        }
        graphOwned_->count = 0;
        deviceManager_.closeAudioDevice();
        dryRoutingLab_.poll();
    }

    // Route MAIN to a user-selected secondary playback device
    // (hardware or software). No second MIX/FX engine.
    juce::String connectSecondaryOutput(const juce::String& selected) {
        if(!isOutputMuted() || independent_.active() ||
           !directFence_.permitted() || !dryRoutingLab_.hasExplicitRoute() ||
           diagnosticRoute_.load(std::memory_order_acquire)
                !=armix::diagnostics::Route::normal)
            return "MUTE hardware OUTPUT, APPLY real input routing and use verified DIRECT before secondary device setup";
        auto* device=deviceManager_.getCurrentAudioDevice();
        if(!device)return "Direct capture/output hardware is not available";
        return secondaryOutput_.open(selected,device->getName(),
                                  device->getCurrentSampleRate(),
                                  device->getCurrentBufferSizeSamples());
    }
    juce::String armSecondaryOutput(bool wanted) {
        if(wanted && (independent_.active() || !directFence_.permitted() ||
           !dryRoutingLab_.hasExplicitRoute() ||
           routeReconfiguring_.load(std::memory_order_acquire) ||
           diagnosticRoute_.load(std::memory_order_acquire)
                !=armix::diagnostics::Route::normal))
            return "Secondary output requires accepted native route and verified DIRECT clock";
        return secondaryOutput_.arm(wanted) ? juce::String() :
            juce::String("Secondary playback endpoint is unavailable or has a device fault; reconnect explicitly");
    }
    void disconnectSecondaryOutput(){secondaryOutput_.stop();}
    bool secondaryArmed() const noexcept{return secondaryOutput_.armed();}

    // FAST1: control-thread output arm preflight; physical MUTE always works.
    // No auto-arm on device reopen, no RT allocation or driver reconfiguration.
    juce::String liveOutputArmFault()
    {
        auto* device=deviceManager_.getCurrentAudioDevice();
        if (!device || !device->isOpen() || !directFence_.permitted()
            || independent_.active())
            return "Select a verified DIRECT ASIO/Windows Audio device first.";
        if (routeReconfiguring_.load(std::memory_order_acquire) || handoff_.busy()
            || !dryRoutingLab_.active() || !dryRoutingLab_.ready()
            || !dryRoutingLab_.hasExplicitRoute())
            return "Apply a real INPUT PATCH and wait for native routing ACK.";
        if (device->getActiveOutputChannels().countNumberOfSetBits()<2)
            return "MAIN needs two verified hardware output pins.";
        const auto readback=dryRoutingLab_.status(device);
        const auto accepted=readback.getProperty("acceptedRoute",juce::var());
        const auto channels=accepted.getProperty("channels",juce::var());
        const auto* rows=channels.getArray();
        if (!rows) return "No native-accepted channel mapping available.";
        bool physicalSource=false;
        for (const auto& row:*rows)
            if(row.getProperty("mode",juce::var()).toString()=="MONO" ||
               row.getProperty("mode",juce::var()).toString()=="STEREO")
            {physicalSource=true;break;}
        if (!physicalSource)return "No physical input assigned. Connect CH1/CH2 first.";
        return {};
    }
    void setMonitorEnabled (bool value) noexcept { monitor_.store (value); }
    bool isMonitorEnabled() const noexcept { return monitor_.load(); }
    // Emergency output mute, not a prerequisite for receiving audio.
    void setOutputMuted (bool mute) noexcept { monitor_.store (!mute, std::memory_order_release); }
    bool isOutputMuted() const noexcept { return !monitor_.load (std::memory_order_acquire); }
    bool allOutputsDisarmed() const noexcept {
        return isOutputMuted() && !secondaryOutput_.armed();
    }
    bool directRouteAuthorized() const noexcept { return directFence_.permitted(); }
    bool unexpectedDirectRoute() const noexcept
    {
        return directFence_.fault() == armix::device::DirectRouteFence::Fault::unexpectedRoute;
    }
    // Native AUDIO SETUP is the explicit user authorization boundary.
    // Suppress any live output while selecting/changing a device, even if JUCE
    // starts its new callback before the selector window is closed.
    void beginManualAudioSetup() noexcept {
        setOutputMuted(true);
        directFence_.suspend();
        dryRoutingLab_.disable(); // explicit control-thread reset; not RT
    }
    bool finishManualAudioSetup() {
        if(!approveDirectRoute())return false;
        return startUnpatchedProductionSession().isEmpty();
    }

    // Device changes are exclusively CONTROL-THREAD transactions.
    // Normal ASIO / verified single-clock devices remain on the zero-SRC
    // JUCE direct callback. Independent capture/render is opt-in in P0.
    bool independentClockActive() const noexcept { return independent_.active(); }
    // Control-thread-only: true after an unsuccessful DIRECT rollback.
    // The original endpoint selection is retained for bounded recovery.
    bool hasPendingDirectRestore() const noexcept { return savedDirectSetup_.has_value(); }

    // Explicit user escape hatch when the previous endpoint has disappeared.
    // Called only from AUDIO SETUP on the message thread. No RT work, and no
    // fallback silently routed to unknown speakers.
    bool abandonPendingDirectRestoreForSetup()
    {
        if (independent_.active() || !savedDirectSetup_) return false;
        setOutputMuted (true);
        detach();
        deviceManager_.closeAudioDevice();
        savedDirectSetup_.reset();
        // Register with the manager even while no device is open. JUCE then
        // prepares this callback when the user selects a replacement endpoint.
        // attach() deliberately requires a live device; it cannot do this.
        deviceManager_.addAudioCallback (this);
        attached_ = true;
        return true;
    }
    bool independentClockFaulted() const noexcept { return independent_.active() && independent_.faulted(); }
    void pollIndependentClockHealth (std::uint64_t nowMs) noexcept
    {
        independent_.pollStreamHealth (nowMs);
    }

    juce::String setIndependentClockEnabled (bool wanted)
    {
        if (dryRoutingLab_.hasExplicitRoute())
            return "CLEAR PATCH and MUTE OUTPUT before changing the device clock";
        if (handoff_.busy()) return "Finish pending VST3 graph switch before changing audio backend";
        if (wanted == independent_.active() && !(savedDirectSetup_ && !wanted)) return {};
        if (wanted && savedDirectSetup_) return "Restore old device backend before another transition";
        if (wanted && (hasTemporaryDiagnosticDevice() ||
                       diagnosticRoute() != armix::diagnostics::Route::normal))
            return "Return SIGNAL CHECK to NORMAL before adaptive clock mode";

        struct Transition final
        {
            std::atomic<bool>& flag;
            explicit Transition (std::atomic<bool>& b):flag(b)
                { flag.store (true,std::memory_order_release); }
            ~Transition() { flag.store (false,std::memory_order_release); }
        } muting (routeReconfiguring_);

        if (!wanted)
        {
            independent_.close();
            if (!savedDirectSetup_) return {};
            const auto error = deviceManager_.setAudioDeviceSetup (*savedDirectSetup_, false);
            if (error.isNotEmpty() || !verifyRestoredDirectDevice (*savedDirectSetup_))
            {
                setOutputMuted (true);
                return "Direct device restoration failed: " +
                    (error.isNotEmpty() ? error : juce::String ("Expected capture/render endpoints or channel masks were not reopened"));
            }
            if (!resumeVerifiedDirectRoute())
            {
                setOutputMuted (true);
                return "DIRECT backend changed during restoration; user must select a device";
            }
            savedDirectSetup_.reset();
            attach();
            const auto sessionError=startUnpatchedProductionSession();
            if(sessionError.isNotEmpty())
                return "DIRECT restored but native mixer unpatched session failed: "+sessionError;
            return {};
        }
        // ASIO or another known direct hardware clock bypasses the SRC.
        if (deviceManager_.getCurrentAudioDeviceType() != "Windows Audio")
            return "Independent mode is limited to WASAPI shared endpoints; existing ASIO/device path stays direct";
        juce::AudioDeviceManager::AudioDeviceSetup original;
        deviceManager_.getAudioDeviceSetup (original);
        if (original.inputDeviceName.isEmpty() || original.outputDeviceName.isEmpty())
            return "Both capture and render endpoints must be configured before independent mode";
        // Fail before touching the working DIRECT stream. JUCE's Windows
        // adapter chooses hardware by *friendly name*. Native MMDevice IDs
        // must resolve uniquely to that selection, with a second comparison
        // after both independent JUCE managers have actually opened.
        const auto identity = armix::device::WasapiEndpointIdentityGuard::resolve (
            original.inputDeviceName, original.outputDeviceName);
        if (!identity.ok()) return "Independent Windows endpoint preflight failed: " + identity.error;
        // Only release the unpatched graph AFTER all opt-in backend
        // preflight gates pass. A rejected unsupported driver must leave the
        // native NORMAL mixer ready, not stranded without a session.
        if(dryRoutingLab_.active()) {
            setOutputMuted(true);
            dryRoutingLab_.disable();
        }
        directFence_.suspend(); // independent path bypasses only this DIRECT fence
        detach();
        deviceManager_.closeAudioDevice();
        savedDirectSetup_ = original;
        auto error = independent_.open (original,
                                        static_cast<juce::AudioIODeviceCallback*> (this),
                                        identity.selection);
        if (error.isNotEmpty())
        {
            independent_.close();
            const auto restore = deviceManager_.setAudioDeviceSetup (original, false);
            const bool restored = restore.isEmpty() && verifyRestoredDirectDevice (original);
            if (restored)
            {
                if (resumeVerifiedDirectRoute())
                {
                    savedDirectSetup_.reset();
                    attach();
                    (void) startUnpatchedProductionSession(); // fail silent on rollback
                }
                else setOutputMuted (true);
            }
            else setOutputMuted (true);
            return "Independent clock startup failed: " + error
                 + (!restored ? " | direct rollback failed: " +
                     (restore.isNotEmpty() ? restore : juce::String ("Expected endpoints unavailable"))
                     : juce::String (" | direct device restored"));
        }
        return {};
    }
    // Called on the JUCE message thread, never from an audio callback.
    // One-sided tests actually close the unused device endpoint. The saved
    // user setup is restored on exit; temporary routes must not be persisted.
    juce::String setDiagnosticRoute (armix::diagnostics::Route wanted)
    {
        if (dryRoutingLab_.hasExplicitRoute())
            return "CLEAR PATCH and MUTE OUTPUT before changing SIGNAL CHECK";
        using Route = armix::diagnostics::Route;
        if (handoff_.busy()) return "Finish the pending FX graph transition first";
        if (independent_.active()) return "Disable independent clock mode before endpoint isolation tests";
        if (wanted == diagnosticRoute_.load (std::memory_order_acquire) &&
            !(wanted == Route::normal && savedLiveSetup_)) return {};
        if (wanted < Route::normal || wanted > Route::captureOnly)
            return "Unknown audio diagnostic route";
        if(!savedLiveSetup_ && (wanted == Route::outputOnlyTone ||
                               wanted == Route::captureOnly)) {
            juce::AudioDeviceManager::AudioDeviceSetup current;
            deviceManager_.getAudioDeviceSetup(current);
            if(wanted == Route::outputOnlyTone && current.outputDeviceName.isEmpty())
                return "Choose an audio output before starting the output-only test";
            if(wanted == Route::captureOnly && current.inputDeviceName.isEmpty())
                return "Choose an audio input before starting the capture-only test";
        }

        // Explicit diagnostic isolation is still available when no user patch
        // is active. Release the silent production graph on CONTROL before
        // selecting a one-sided device. No phantom hardware output or CH1 mix.
        if(dryRoutingLab_.active()) {
            setOutputMuted(true);
            dryRoutingLab_.disable();
        }
        // Muting the old/new combined backend callback during an intermediate
        // device restart prevents briefly leaking microphone audio when changing
        // from OUTPUT ONLY to CAPTURE ONLY (which first restores full duplex).
        struct TransitionMute final
        {
            std::atomic<bool>& flag;
            explicit TransitionMute (std::atomic<bool>& target) : flag (target)
                { flag.store (true, std::memory_order_release); }
            ~TransitionMute() { flag.store (false, std::memory_order_release); }
        } transitionMute (routeReconfiguring_);

        if (savedLiveSetup_)
        {
            const auto restore = *savedLiveSetup_;
            const auto previousRoute = diagnosticRoute_.load (std::memory_order_acquire);
            diagnosticRoute_.store (Route::normal, std::memory_order_release);
            const auto error = deviceManager_.setAudioDeviceSetup (restore, false);
            if (error.isNotEmpty())
            {
                // Keep the temporary-route identity/restore token for retry.
                // A driver can disappear and reappear during a diagnostic run.
                diagnosticRoute_.store (previousRoute, std::memory_order_release);
                setOutputMuted (true);
                return "Cannot restore previous audio routing: " + error;
            }
            savedLiveSetup_.reset();
        }

        if (wanted == Route::outputOnlyTone || wanted == Route::captureOnly)
        {
            juce::AudioDeviceManager::AudioDeviceSetup original;
            deviceManager_.getAudioDeviceSetup (original);
            if (wanted == Route::outputOnlyTone && original.outputDeviceName.isEmpty())
                return "Choose an audio output before starting the output-only test";
            if (wanted == Route::captureOnly && original.inputDeviceName.isEmpty())
                return "Choose an audio input before starting the capture-only test";
            auto isolated = original;
            if (wanted == Route::outputOnlyTone)
            {
                isolated.inputDeviceName.clear();
                isolated.inputChannels.clear();
                isolated.useDefaultInputChannels = false;
            }
            else
            {
                isolated.outputDeviceName.clear();
                isolated.outputChannels.clear();
                isolated.useDefaultOutputChannels = false;
            }
            // The backend stop/start sequence provides a safe callback boundary.
            // Set mode before starting a new one-sided device.
            diagnosticRoute_.store (wanted, std::memory_order_release);
            juce::String error = deviceManager_.setAudioDeviceSetup (isolated, false);
            const auto* active = deviceManager_.getCurrentAudioDevice();
            // Fail closed even when an audio backend ignores requested endpoint isolation.
            if (error.isEmpty() && active != nullptr)
            {
                if (wanted == Route::outputOnlyTone &&
                    (active->getActiveInputChannels().countNumberOfSetBits() != 0 ||
                     active->getActiveOutputChannels().isZero()))
                    error = "The driver did not provide output-only isolation";
                if (wanted == Route::captureOnly &&
                    (active->getActiveOutputChannels().countNumberOfSetBits() != 0 ||
                     active->getActiveInputChannels().isZero()))
                    error = "The driver did not provide capture-only isolation";
            }
            if (error.isNotEmpty() || active == nullptr)
            {
                diagnosticRoute_.store (Route::normal, std::memory_order_release);
                const auto rollback = deviceManager_.setAudioDeviceSetup (original, false);
                if (rollback.isEmpty() && resumeVerifiedDirectRoute()) {
                    (void) startUnpatchedProductionSession(); // original DIRECT, silent
                } else setOutputMuted(true);
                return "Unable to isolate endpoint: " + (error.isNotEmpty() ? error : "Device not available")
                     + (rollback.isNotEmpty() ? " | restore failed: " + rollback : juce::String());
            }
            savedLiveSetup_ = original;
        }
        diagnosticRoute_.store (wanted, std::memory_order_release);
        if(wanted == Route::normal) {
            if(!resumeVerifiedDirectRoute()) {
                setOutputMuted(true);
                return "Diagnostic DIRECT route identity changed; choose a device in AUDIO SETUP";
            }
            const auto error=startUnpatchedProductionSession();
            if(error.isNotEmpty())return "Cannot resume native production mixer: "+error;
        }
        return {};
    }

    armix::diagnostics::Route diagnosticRoute() const noexcept
    {
        return diagnosticRoute_.load (std::memory_order_acquire);
    }
    bool hasTemporaryDiagnosticDevice() const noexcept { return savedLiveSetup_.has_value(); }
    void setDryTestEnabled (bool value) noexcept { dryTest_.store (value); }
    bool isDryTestEnabled() const noexcept { return dryTest_.load(); }
    void setMasterGainDb (float value) noexcept
    {
        if (std::isfinite (value)) masterGain_.store (juce::Decibels::decibelsToGain (juce::jlimit (-60.f, 10.f, value)));
    }
    // Restored Show snapshots preserve exact linear zero, unlike a dB
    // conversion clamped at -60 dB. Message thread, atomic publish only.
    void setMasterGainLinear(float value) noexcept {
        if(std::isfinite(value) && value>=0.f && value<=2.f)
            masterGain_.store(value,std::memory_order_release);
    }
    void setChannelGainDb (float value) noexcept
    {
        if (std::isfinite (value)) channelGain_.store (juce::Decibels::decibelsToGain (juce::jlimit (-60.f, 10.f, value)));
    }
    // True stereo-to-stereo pan: centre preserves stereo; extremes fold
    // both stereo input channels to the chosen side at unity for dual-mono.
    // Parameter is normalized on the UI thread; callback only loads atomic.
    void setChannelPan (float value) noexcept
    {
        if (std::isfinite (value))
            channelPan_.store (juce::jlimit (-1.f, 1.f, value), std::memory_order_relaxed);
    }
    void setChannelMuted (bool value) noexcept { channelMute_.store (value); }
    void setMasterMuted (bool value) noexcept { masterMute_.store (value); }
    void setMasterMono (bool value) noexcept { masterMono_.store (value, std::memory_order_relaxed); }

    int chainSize() const noexcept { return graphOwned_->count; }
    bool isGraphSwitchPending() const noexcept { return handoff_.busy(); }
    // Invoke from the JUCE message thread, at most once per timer tick.
    // Retire only after the callback has acknowledged and left the old graph.
    bool finalizeGraphSwitch()
    {
        auto* retired = handoff_.takeRetired();
        if (!retired) return false;
        jassert (retired == graphOwned_.get());
        if (retired != graphOwned_.get() || !stagedOwner_) return false;
        auto old = std::move (graphOwned_);
        graphOwned_ = std::move (stagedOwner_);
        old.reset(); // Vendor cleanup only on final CONTROL-owned reference.
        totalLatency_.store(graphOwned_->latency,std::memory_order_release);
        handoff_.completeRetirement();
        completedSwaps_.fetch_add (1, std::memory_order_relaxed);
        return true;
    }

    // Non-RT UI readers show the requested order even during the two-buffer
    // de-click handoff. DSP still uses the active order until fade-out reaches 0.
    std::uint64_t visibleFxOrder() const noexcept
    {
        return fxMovePhase_.load (std::memory_order_acquire) != 0
            ? fxMovePendingOrder_.load (std::memory_order_acquire)
            : graphOwned_->orderPacked.load (std::memory_order_acquire);
    }

    juce::AudioPluginInstance* pluginAt (int index) noexcept
    {
        return index >= 0 && index < graphOwned_->count
            ? graphOwned_->slots[Graph::physicalAt (visibleFxOrder(), index)].processor.get() : nullptr;
    }

    juce::var chainState() const
    {
        juce::Array<juce::var> nodes;
        const auto logicalOrder = visibleFxOrder();
        for (int i = 0; i < graphOwned_->count; ++i)
        {
            auto node = std::make_unique<juce::DynamicObject>();
            node->setProperty ("index", i);
            const auto& slot = graphOwned_->slots[Graph::physicalAt (logicalOrder, i)];
            node->setProperty ("name", slot.name);
            node->setProperty ("id", slot.catalogId);
            node->setProperty ("bypass", slot.bypass);
            node->setProperty ("latency", slot.latency);
            nodes.add (juce::var (node.release()));
        }
        return juce::var (nodes);
    }

    // R0B FX lifecycle: one prepared graph transaction shared by ADD,
    // REMOVE, and preset recall. Only the CONTROL thread clones strong
    // ownership of vendor processors. RT uses raw pointer views from the
    // selected graph, never a shared_ptr copy, constructor or destructor.
    std::unique_ptr<Graph> preparedFxEdit() const
    {
        auto next=std::make_unique<Graph>();
        next->count=graphOwned_->count;
        // A previous lock-free MOVE may have changed logical order, not
        // physical plugin addresses. Normalize only in the NEW graph.
        const auto order=graphOwned_->orderPacked.load(std::memory_order_acquire);
        for(int i=0;i<next->count;++i)
            next->slots[i]=graphOwned_->slots[Graph::physicalAt(order,i)];
        return next;
    }

    void prepareFxEditEnvelope(Graph& next,double rate) const
    {
        next.latency=0;
        for(int i=0;i<next.count;++i)
        {
            auto& slot=next.slots[i];
            next.bypassFlags[i].store(slot.bypass,std::memory_order_relaxed);
            next.bypassEnvelope[i].prepare(slot.bypass,rate,slot.latency);
            if(!slot.bypass)next.latency+=slot.latency;
        }
    }

    bool publishFxEdit(std::unique_ptr<Graph> next,
                       juce::AudioIODevice* device,
                       std::uint64_t expectedEpoch,
                       double expectedRate,int expectedFrames)
    {
        // All checks precede staging: a rejected edit must not alter the
        // currently audible graph or any accepted route revision.
        if(!next || !attached_ || !device || independent_.active() ||
           !directFence_.permitted() || handoff_.busy() ||
           fxMovePhase_.load(std::memory_order_acquire)!=0 ||
           routeReconfiguring_.load(std::memory_order_acquire) ||
           device!=deviceManager_.getCurrentAudioDevice() ||
           expectedEpoch!=deviceStartCount_.load(std::memory_order_acquire) ||
           expectedRate!=device->getCurrentSampleRate() ||
           expectedFrames!=juce::jmax(32,device->getCurrentBufferSizeSamples()))
            return false;
        prepareFxEditEnvelope(*next,device->getCurrentSampleRate());
        stagedOwner_=std::move(next);
        if(!handoff_.stage(stagedOwner_.get()))
        {
            stagedOwner_.reset();
            return false;
        }
        return true;
    }

    juce::String addVst3(const juce::String& filename,int uid,
                        const juce::String& catalogId)
    {
        if(independent_.active())return "Return to DIRECT mode before editing VST3";
        if(handoff_.busy() || fxMovePhase_.load(std::memory_order_acquire)!=0)
            return "Finish current FX transition before adding another insert";
        if(graphOwned_->count>=maxFx)return "FX Rack is full (maximum 8 VST3 effects)";
        auto* device=deviceManager_.getCurrentAudioDevice();
        if(!device || !attached_ || !directFence_.permitted())
            return "Select a verified DIRECT audio device before editing VST3";
        auto* fmt=formats_.getFormat(0);
        if(!fmt || !fmt->fileMightContainThisPluginType(filename))
            return "VST3 module not found: "+filename;
        const auto epoch=deviceStartCount_.load(std::memory_order_acquire);
        const auto rate=device->getCurrentSampleRate();
        const auto frames=juce::jmax(32,device->getCurrentBufferSizeSamples());
        if(!(rate>0.0) || frames>maxFrames)
            return "Unsupported device sample rate or buffer quantum";
        juce::OwnedArray<juce::PluginDescription> descriptions;
        fmt->findAllTypesForFile(descriptions,filename);
        juce::PluginDescription* chosen=nullptr;
        for(auto* d:descriptions)
            if(d && d->uniqueId==uid && !d->isInstrument) { chosen=d;break; }
        if(!chosen)return "Effect identity not found inside VST3 module";
        juce::String error;
        auto instance=formats_.createPluginInstance(*chosen,rate,frames,error);
        if(!instance)return "Failed to instantiate VST3: "+error;
        const auto layout=armix::vst::prepareStereoEffectLayout(*instance);
        if(layout.isNotEmpty())return chosen->name+": "+layout;
        auto prepared=ownPlugin(std::move(instance));
        int latency=0;
        try {
            prepared->prepareToPlay(rate,frames);
            latency=prepared->getLatencySamples();
        }
        catch(const std::exception& ex){return "VST3 prepare failed: "+juce::String(ex.what());}
        catch(...){return "VST3 prepare failed (unknown exception)";}
        // Explicit PDC gate: PRE/POST sends must never combine misaligned FX.
        // This rejects a new latent CH1 insert, even if bypassed at insertion.
        if(dryRoutingLab_.hasExplicitRoute() && latency!=0)
            return "Native sends require zero-latency CH1 FX until PDC is implemented";
        // Construct/prepare entirely on the native message thread. Existing
        // processor identities, binary state, Orbit and tails are shared with
        // the old graph until an acknowledged buffer-boundary swap.
        auto next=preparedFxEdit();
        Slot added;
        added.name=chosen->name;added.catalogId=catalogId;
        added.path=filename;added.uid=uid;
        added.latency=latency;
        added.processor=std::move(prepared);
        next->slots[next->count++]=std::move(added);
        // A failed publish destroys the candidate on CONTROL, invoking
        // PluginRelease only for newly unreferenced vendor instances.
        if(!publishFxEdit(std::move(next),device,epoch,rate,frames))
            return "Audio device or FX graph changed during ADD; retry when stable";
        return {};
    }

    // Capture VST3 vendor binary state, not the legacy WebView2 mock settings.
    // Must only run when output is muted (caller enforces): callback is detached
    // exactly once, no plugin state methods ever execute on the audio thread.
    juce::String captureSnapshot (armix::storage::Snapshot& snapshot)
    {
        if (dryRoutingLab_.hasExplicitRoute()) return "CLEAR PATCH before capturing CH1 native FX state";
        if (!allOutputsDisarmed()) return "MUTE physical output and DISARM STREAM before saving a native preset";
        if (handoff_.busy() || fxMovePhase_.load (std::memory_order_acquire) != 0) return "Finish pending FX graph transition before saving";
        snapshot.effects.clear();
        snapshot.channelDb = juce::Decibels::gainToDecibels (
            channelGain_.load (std::memory_order_relaxed), -60.f);
        snapshot.masterDb = juce::Decibels::gainToDecibels (
            masterGain_.load (std::memory_order_relaxed), -60.f);
        snapshot.pan = channelPan_.load (std::memory_order_relaxed);
        snapshot.channelMute = channelMute_.load (std::memory_order_relaxed);
        snapshot.masterMute = masterMute_.load (std::memory_order_relaxed);
        detach();
        juce::String error;
        try
        {
            for (int i = 0; i < graphOwned_->count; ++i)
            {
                const int physical = graphOwned_->physicalAt (i);
                const auto& slot = graphOwned_->slots[physical];
                if (!slot.processor) { error = "Invalid active FX slot"; break; }
                armix::storage::FxState state;
                state.path = slot.path;
                state.name = slot.name;
                state.catalogId = slot.catalogId;
                state.uid = slot.uid;
                state.bypass = graphOwned_->bypassFlags[physical].load (std::memory_order_acquire);
                slot.processor->getStateInformation (state.binary);
                if (state.binary.getSize() > armix::storage::NativePresetArchive::maxStateBytes)
                {
                    error = "Plugin state exceeds 8 MB: " + slot.name;
                    break;
                }
                snapshot.effects.push_back (std::move (state));
            }
        }
        catch (...)
        {
            error = "Plugin threw while saving its state";
        }
        attach();
        if (error.isNotEmpty()) snapshot.effects.clear();
        return error;
    }

    // Stage all VST3 binary states before publication; callback never owns
    // or destroys a plugin. No manual output mute is required for recall.
    juce::String restoreSnapshot (const armix::storage::Snapshot& snapshot)
    {
        if (dryRoutingLab_.hasExplicitRoute()) return "CLEAR PATCH before recalling CH1 native preset";
        if (handoff_.busy())
            return "A live FX graph transition is already in progress";
        if (snapshot.effects.size() > maxFx) return "Too many plugins in preset";
        auto* device = deviceManager_.getCurrentAudioDevice();
        if (!device) return "No active audio device";
        auto* fmt = formats_.getFormat (0);
        if (!fmt) return "No VST3 format available";
        const auto rate = device->getCurrentSampleRate();
        const auto buffer = juce::jmax (32, device->getCurrentBufferSizeSamples());
        const auto deviceEpoch = deviceStartCount_.load (std::memory_order_acquire);

        std::array<Slot, maxFx> stagedSlots;
        for (int i = 0; i < static_cast<int> (snapshot.effects.size()); ++i)
        {
            const auto& fx = snapshot.effects[static_cast<size_t> (i)];
            if (fx.binary.getSize() > armix::storage::NativePresetArchive::maxStateBytes ||
                !juce::File (fx.path).exists() || !fx.path.endsWithIgnoreCase (".vst3"))
                return "Plugin is missing or invalid: " + fx.name;
            juce::OwnedArray<juce::PluginDescription> descriptions;
            fmt->findAllTypesForFile (descriptions, fx.path);
            juce::PluginDescription* match = nullptr;
            for (auto* candidate : descriptions)
                if (candidate != nullptr && candidate->uniqueId == fx.uid &&
                    !candidate->isInstrument)
                    { match = candidate; break; }
            if (!match) return "VST3 plugin identity missing: " + fx.name;

            juce::String error;
            auto processor = formats_.createPluginInstance (*match, rate, buffer, error);
            if (!processor) return "Cannot open preset plugin " + fx.name + ": " + error;
            const auto busError = armix::vst::prepareStereoEffectLayout (*processor);
            if (busError.isNotEmpty())
                return "Unsupported VST3 bus layout for " + fx.name + ": " + busError;
            auto owned=ownPlugin(std::move(processor));
            try
            {
                // Each prepared candidate has one control-thread cleanup owner,
                // even if vendor restore or a later stage fails.
                owned->prepareToPlay (rate, buffer);
                if (fx.binary.getSize() > 0)
                    owned->setStateInformation (
                        fx.binary.getData(), static_cast<int> (fx.binary.getSize()));
            }
            catch (...)
            {
                return "Plugin failed to restore its binary state: " + fx.name;
            }
            Slot slot;
            slot.name = match->name;
            slot.catalogId = fx.catalogId;
            slot.path = fx.path;
            slot.uid = fx.uid;
            slot.bypass = fx.bypass;
            slot.processor = std::move (owned);
            stagedSlots[static_cast<size_t> (i)] = std::move (slot);
        }

        // Reject a graph prepared against stale rate/buffer.
        if (deviceEpoch != deviceStartCount_.load (std::memory_order_acquire) ||
            device != deviceManager_.getCurrentAudioDevice() ||
            rate != device->getCurrentSampleRate() ||
            buffer != juce::jmax (32, device->getCurrentBufferSizeSamples()))
            return "Audio device changed during preset preparation. Retry recall.";

        // Ownership stays with the control thread. The audio thread switches
        // only an immutable pointer at a callback boundary (never detach).
        auto staged = std::make_unique<Graph>();
        staged->slots = std::move (stagedSlots);
        staged->count = static_cast<int> (snapshot.effects.size());
        staged->hasRecallState = true;
        staged->channelDb = snapshot.channelDb;
        staged->masterDb = snapshot.masterDb;
        staged->channelLinear = juce::Decibels::decibelsToGain (snapshot.channelDb);
        staged->masterLinear = juce::Decibels::decibelsToGain (snapshot.masterDb);
        staged->pan = snapshot.pan;
        staged->channelMute = snapshot.channelMute;
        staged->masterMute = snapshot.masterMute;
        for (int i = 0; i < staged->count; ++i)
        {
            staged->bypassFlags[i].store (staged->slots[i].bypass, std::memory_order_relaxed);
            staged->slots[i].latency = staged->slots[i].processor->getLatencySamples();
            staged->bypassEnvelope[i].prepare (staged->slots[i].bypass, rate,
                                                staged->slots[i].latency);
            if (!staged->slots[i].bypass) staged->latency += staged->slots[i].latency;
        }
        stagedOwner_ = std::move (staged);
        if (!handoff_.stage (stagedOwner_.get()))
        {
            stagedOwner_.reset();
            return "Cannot schedule live FX graph swap";
        }
        return {};
    }

    bool setBypass (int index, bool value)
    {
        if (handoff_.busy() || fxMovePhase_.load (std::memory_order_acquire) != 0) return false;
        if (index < 0 || index >= graphOwned_->count) return false;
        // Message-thread metadata and an atomic value consumed by the audio thread.
        const int physical = Graph::physicalAt (visibleFxOrder(), index);
        // No PDC: a bypassed latent CH1 processor must not be re-enabled
        // after routing was ACKed (otherwise parallel sends misalign).
        if(dryRoutingLab_.hasExplicitRoute() && !value &&
           graphOwned_->slots[physical].latency!=0)return false;
        graphOwned_->slots[physical].bypass = value;
        graphOwned_->bypassFlags[physical].store (value, std::memory_order_release);
        // Recompute the advertised steady-state graph latency on the control
        // thread. The audible transition is managed sample-by-sample by RT.
        int activeLatency=0;
        for(int i=0;i<graphOwned_->count;++i)
            if(!graphOwned_->bypassFlags[i].load(std::memory_order_acquire))
                activeLatency+=graphOwned_->slots[i].latency;
        totalLatency_.store(activeLatency,std::memory_order_release);
        return true;
    }

    bool removeAt(int index)
    {
        if(index<0 || index>=graphOwned_->count || independent_.active() ||
           handoff_.busy() || fxMovePhase_.load(std::memory_order_acquire)!=0)
            return false;
        auto* device=deviceManager_.getCurrentAudioDevice();
        if(!device || !attached_ || !directFence_.permitted())return false;
        const auto epoch=deviceStartCount_.load(std::memory_order_acquire);
        auto next=preparedFxEdit();
        // Do not call vendor releaseResources or destroy a processor here:
        // an in-flight audio buffer still owns the original graph lease.
        for(int i=index;i+1<next->count;++i)
            next->slots[i]=std::move(next->slots[i+1]);
        next->slots[--next->count]=Slot{};
        return publishFxEdit(std::move(next),device,epoch,
                             device->getCurrentSampleRate(),
                             juce::jmax(32,device->getCurrentBufferSizeSamples()));
    }

    bool moveSlot (int from, int to)
    {
        // The processor instances remain at stable PHYSICAL addresses.
        // A 64-bit release store publishes the complete logical order.
        // The callback takes one acquire snapshot per audio buffer.
        // R0B slice: CH1 rack MOVE is a pure atomic physical-slot reorder,
        // including while the native 10CH/4AUX graph borrows this same rack.
        // The routed callback below owns the identical two-block de-click
        // publication that the legacy callback already uses.
        if (independent_.active()) return false;
        if (handoff_.busy() || fxMovePhase_.load (std::memory_order_acquire) != 0)
            return false;
        const int count = graphOwned_->count;
        if (from < 0 || to < 0 || from >= count || to >= count || from == to)
            return false;
        const auto previous = graphOwned_->orderPacked.load (std::memory_order_acquire);
        const auto next = armix::dsp::FxOrderPlan::moved (previous, count, from, to);
        // Plugin objects are not moved. Acknowledge UI intent immediately,
        // then run output ramp 1->0, publish at 0, ramp 0->1 in callback.
        if (!attached_ && !independent_.active())
        {
            graphOwned_->orderPacked.store (next, std::memory_order_release);
            return true; // no callback exists to acknowledge a fade
        }
        fxMovePendingOrder_.store (next, std::memory_order_relaxed);
        fxMovePhase_.store (1, std::memory_order_release);
        return true;
    }

    // R0B B3 single graph owner: verified physical DIRECT device only.
    // Never restore a previous device's input port numbers or auto-arm output.
    // All plan preparation / JUCE vars remain on the control thread.
    juce::String startUnpatchedProductionSession() {
        setOutputMuted(true);
        secondaryOutput_.disarm(); // never carry LIVE STREAM into a new unpatched graph
        auto* device=deviceManager_.getCurrentAudioDevice();
        if(!device || !directFence_.permitted() || independent_.active())
            return "No verified DIRECT full-duplex device";
        return dryRoutingLab_.startUnpatchedSession(
            directFence_.permitted(),independent_.active(),graphOwned_->count,
            formats_,device->getCurrentSampleRate(),
            juce::jmax(32,device->getCurrentBufferSizeSamples()));
    }

    // Historical STOP command retained as a safe CLEAR PATCH alias.
    // Returning to legacy CH1 is explicitly forbidden in NORMAL production.
    juce::String clearProductionRouting() {
        setOutputMuted(true);
        secondaryOutput_.disarm();
        dryRoutingLab_.disable();
        return startUnpatchedProductionSession();
    }

    // Explicit message-thread transaction. NO VST is processed in this LAB.
    juce::String applyDryRoutingLab (
        const juce::var& command,
        const LiveDryRoutingLab::FxSelections& fx,
        const LiveDryRoutingLab::FxRestores* restored=nullptr) {
        if (!allOutputsDisarmed()) return "MUTE hardware output and DISARM STREAM before APPLY routing";
        if (handoff_.busy()) return "A VST3 graph handoff is in progress";
        if (routeReconfiguring_.load(std::memory_order_acquire))
            return "Audio device is changing; retry when stable";
        if (diagnosticRoute_.load(std::memory_order_acquire)
            != armix::diagnostics::Route::normal)
            return "Return SIGNAL CHECK to NORMAL before routing LAB";
        if (savedDirectSetup_ || savedLiveSetup_)
            return "Restore the full DIRECT audio device before routing LAB";
        auto* device=deviceManager_.getCurrentAudioDevice();
        if(!device)return "Select a verified DIRECT audio device first";
        // Without PDC, do not mix a latent CH1 chain with uncompensated
        // PRE/POST channel/AUX sends. Preserve existing rack and reject safely.
        // Include bypassed slots too: enabling them later must not introduce
        // uncompensated latency into CH1 -> BUS/AUX signal paths.
        for(int i=0;i<graphOwned_->count;++i)
            if(graphOwned_->slots[i].latency!=0)
                return "CH1 rack contains a latent VST3: zero-latency settings are required for native multi-channel sends";
        return dryRoutingLab_.apply(command,directFence_.permitted(),
                   independent_.active(),graphOwned_->count,fx,formats_,
                   device->getCurrentSampleRate(),
                   juce::jmax(32,device->getCurrentBufferSizeSamples()),
                   true,restored);
    }
    void disableDryRoutingLab() noexcept {
        setOutputMuted(true); // never expose CH1 unexpectedly on STOP
        dryRoutingLab_.disable();
    }
    juce::String switchDryRoutingLabSource(int strip,std::uint64_t epoch,
            std::uint64_t revision,const juce::String& mode,
            int left,int right) noexcept {
        if(independent_.active()||routeReconfiguring_.load(std::memory_order_acquire)||
           handoff_.busy()||!directFence_.permitted()||
           diagnosticRoute_.load(std::memory_order_acquire)
               !=armix::diagnostics::Route::normal||
           savedDirectSetup_||savedLiveSetup_)
            return "SOURCE SWITCH: live DIRECT device or graph unavailable";
        return dryRoutingLab_.switchPhysicalSource(strip,epoch,revision,mode,left,right);
    }
    bool setDryRoutingLabParameter(int strip,std::uint64_t epoch,
            std::uint64_t revision,const juce::String& parameter,
            float value) noexcept {
        return dryRoutingLab_.setChannelParameter(strip,epoch,revision,parameter,value);
    }
    bool setDryRoutingLabAuxBusGain(int bus,std::uint64_t epoch,
            std::uint64_t revision,float gain) noexcept {
        if(independent_.active()||routeReconfiguring_.load(std::memory_order_acquire)||
           diagnosticRoute_.load(std::memory_order_acquire)
               !=armix::diagnostics::Route::normal||
           !directFence_.permitted()||savedDirectSetup_||savedLiveSetup_)
            return false;
        return dryRoutingLab_.setAuxBusGain(bus,epoch,revision,gain);
    }
    bool setDryRoutingLabAuxSendGain(int source,int bus,std::uint64_t epoch,
            std::uint64_t revision,float gain) noexcept {
        if(independent_.active()||routeReconfiguring_.load(std::memory_order_acquire)||
           diagnosticRoute_.load(std::memory_order_acquire)
               !=armix::diagnostics::Route::normal||
           !directFence_.permitted()||savedDirectSetup_||savedLiveSetup_)
            return false;
        return dryRoutingLab_.setAuxSendGain(source,bus,epoch,revision,gain);
    }
    bool setDryRoutingLabFxBypass(int lane,int slot,std::uint64_t epoch,
            std::uint64_t revision,const juce::String& instanceId,
            bool bypass) noexcept {
        if(routeReconfiguring_.load(std::memory_order_acquire)||
           diagnosticRoute_.load(std::memory_order_acquire)
               !=armix::diagnostics::Route::normal)return false;
        return dryRoutingLab_.setFxBypass(lane,slot,epoch,revision,instanceId,bypass);
    }
    armix::mixer::LiveChannelFxGraph::InstanceLease leaseRoutedFxForEditor(
            int lane,int slot,std::uint64_t epoch,std::uint64_t revision,
            const juce::String& instanceId) const noexcept {
        if(routeReconfiguring_.load(std::memory_order_acquire)||
           diagnosticRoute_.load(std::memory_order_acquire)
               !=armix::diagnostics::Route::normal ||
           independent_.active())return {};
        return dryRoutingLab_.leaseFxEditor(lane,slot,epoch,revision,instanceId);
    }
    juce::String captureRoutedFxStates(std::uint64_t epoch,std::uint64_t revision,
            std::vector<LiveDryRoutingLab::CapturedFxState>& states) noexcept {
        if(!allOutputsDisarmed()||independent_.active()||
           routeReconfiguring_.load(std::memory_order_acquire)||
           diagnosticRoute_.load(std::memory_order_acquire)
              !=armix::diagnostics::Route::normal)
            return "MUTE OUTPUT and use verified DIRECT before FX state capture";
        return dryRoutingLab_.captureRoutedFxStates(epoch,revision,states);
    }
    std::uint64_t dryRoutingLabEpoch() const noexcept {return dryRoutingLab_.epoch();}
    std::uint64_t dryRoutingLabRevision() const noexcept {return dryRoutingLab_.revision();}
    bool dryRoutingLabActive() const noexcept {return dryRoutingLab_.active();}
    void pollDryRoutingLab() noexcept {dryRoutingLab_.poll();}

    juce::var status()
    {
        dryRoutingLab_.poll(); // off-RT reclaim after callback quiescence
        auto o = std::make_unique<juce::DynamicObject>();
        const bool dual = independent_.active();
        auto* d = dual ? independent_.renderDevice()
                       : deviceManager_.getCurrentAudioDevice();
        auto* capture = dual ? independent_.captureDevice() : d;
        o->setProperty ("dryRoutingLab",dryRoutingLab_.status(capture));
        // NORMAL mode is never silently served by the legacy CH1 fallback.
        // This state is for operator feedback; it does not authorize output.
        o->setProperty ("productionRoutingState",
            dryRoutingLab_.hasExplicitRoute() ? "NATIVE_ROUTE_ACCEPTED"
            : dryRoutingLab_.ready() ? "NATIVE_UNPATCHED_SILENT"
            : dryRoutingLab_.active() ? "NATIVE_ROUTE_STALE_FAIL_SILENT"
            : "UNPATCHED_SILENT");
        o->setProperty ("connected", d != nullptr && capture != nullptr);
        // WASAPI virtual cable output is a second CLOCK only, not another
        // DSP graph. These metrics are real ASRC frames, not driver XRUNs.
        o->setProperty ("secondaryOutputReady",secondaryOutput_.ready());
        o->setProperty ("secondaryOutputArmed",secondaryOutput_.armed());
        o->setProperty ("secondaryOutputFault",secondaryOutput_.faulted());
        o->setProperty ("secondaryOutputName",secondaryOutput_.name());
        o->setProperty ("secondaryOutputEndpointHash",secondaryOutput_.endpointHash());
        o->setProperty ("secondaryOutputRate",secondaryOutput_.rate());
        o->setProperty ("secondaryOutputBridgeMs",secondaryOutput_.targetMs());
        const auto streamFifo=secondaryOutput_.status();
        o->setProperty ("secondaryOutputFrames",static_cast<double>(streamFifo.renderedFrames));
        o->setProperty ("secondaryOutputUnderflow",static_cast<double>(streamFifo.underflowFrames));
        o->setProperty ("secondaryOutputPriming",static_cast<double>(streamFifo.primingFrames));
        o->setProperty ("secondaryOutputOverflow",static_cast<double>(streamFifo.overflowFrames));
        o->setProperty ("adaptiveClockActive", dual);
        o->setProperty ("adaptiveClockFaulted", dual && independent_.faulted());
        o->setProperty ("adaptiveClockMode", dual ? "INDEPENDENT_WASAPI_RENDER_MASTER" : "DIRECT");
        const auto backend = dual ? juce::String ("Windows Audio")
                                  : deviceManager_.getCurrentAudioDeviceType();
        o->setProperty ("directRouteAuthorized", directFence_.permitted());
        o->setProperty ("directRouteFault",
            armix::device::DirectRouteFence::reason (directFence_.fault()));
        o->setProperty ("directSuppressedFrames",
            static_cast<double> (directSuppressedFrames_.load (std::memory_order_relaxed)));
        o->setProperty ("directRouteProtection",
            "PINNED_BACKEND_AND_DRIVER_NAME_NOT_HARDWARE_ID");
        o->setProperty ("directRouteChannelMapGuard",
            "EXACT_ACTIVE_INPUT_AND_OUTPUT_CHANNEL_BITMASKS");
        o->setProperty ("asioRegistryAttestation", asioRegistrationScope_);
        o->setProperty ("asioRegistryIdentityProvenance",
            "WINDOWS_ASIO_REGISTRY_CLSID_NOT_PHYSICAL_HARDWARE_OR_OPEN_COM_OBJECT");
        const auto asioHash = armix::device::AsioRegistryIdentityProbe::diagnosticFingerprint (
            asioPinnedRegistration_);
        o->setProperty ("asioRegisteredClassHash", asioHash != 0
            ? juce::String::toHexString (static_cast<juce::int64> (asioHash))
            : juce::String());
        o->setProperty ("liveBackendClass", dual ? "WASAPI_BRIDGED_ASRC"
            : backend == "ASIO" ? "ASIO_DIRECT_ZERO_EXTRA_FIFO"
            : backend.startsWith ("Windows Audio") ? "WASAPI_DIRECT_ZERO_EXTRA_FIFO"
            : "OTHER_DIRECT");
        o->setProperty ("asioFirstBootEligible", deviceManager_.singleAsioPreferred());
        o->setProperty ("startupFallbackReason", startupFallbackReason_);
        o->setProperty ("driverReportedInputLatencyFrames",
            capture ? juce::var (capture->getInputLatencyInSamples()) : juce::var());
        o->setProperty ("driverReportedOutputLatencyFrames",
            d ? juce::var (d->getOutputLatencyInSamples()) : juce::var());
        o->setProperty ("driverLatencyProvenance",
            "DRIVER_HINT_NOT_MEASURED_ROUND_TRIP");
        o->setProperty ("driver", dual ? "Windows Audio / dual independent clocks"
                                         : deviceManager_.getCurrentAudioDeviceType());
        o->setProperty ("captureRate", capture ? capture->getCurrentSampleRate() : 0.0);
        o->setProperty ("renderRate", d ? d->getCurrentSampleRate() : 0.0);
        o->setProperty ("device", d ? d->getName() : "No device");
        o->setProperty ("rate", d ? d->getCurrentSampleRate() : 0.0);
        o->setProperty ("buffer", d ? d->getCurrentBufferSizeSamples() : 0);
        o->setProperty ("monitor", monitor_.load());
        o->setProperty ("masterMute", masterMute_.load());
        o->setProperty ("masterMono", masterMono_.load (std::memory_order_relaxed));
        o->setProperty ("channelMute", channelMute_.load());
        o->setProperty ("channelPan", channelPan_.load());
        // Live controls are authoritative: never trust an old WebView localStorage fader/mute.
        o->setProperty ("channelDb", juce::Decibels::gainToDecibels (
            channelGain_.load (std::memory_order_relaxed), -60.f));
        o->setProperty ("masterDb", juce::Decibels::gainToDecibels (
            masterGain_.load (std::memory_order_relaxed), -60.f));
        o->setProperty ("masterLinear", masterGain_.load (std::memory_order_relaxed));
        o->setProperty ("dryTest", dryTest_.load());
        const auto route = diagnosticRoute();
        o->setProperty ("diagnosticRoute", armix::diagnostics::routeName (route));
        o->setProperty ("diagnosticTemporary", savedLiveSetup_.has_value());
        o->setProperty ("deviceGeneration", static_cast<double> (deviceStartCount_.load()));
        o->setProperty ("lateCallbackIntervals", static_cast<double> (lateCallbackIntervals_.load()));
        o->setProperty ("callbackCadenceProvenance",
            armix::rt::CallbackCadenceEvidence::provenance());
        o->setProperty ("callbackCadenceComparedIntervals",
            static_cast<double> (cadenceComparedIntervals_.load (std::memory_order_relaxed)));
        o->setProperty ("callbackCadenceClockRegressions",
            static_cast<double> (cadenceClockRegressions_.load (std::memory_order_relaxed)));
        o->setProperty ("callbackCadenceLastExpectedUs",
            static_cast<double> (cadenceLastExpectedUs_.load (std::memory_order_relaxed)));
        o->setProperty ("callbackCadenceGapThresholds", "SUGGESTIVE_1_5X_AND_2_3X_PREVIOUS_BLOCK_PERIOD");
        o->setProperty ("testOutputFrames", static_cast<double> (testOutputFrames_.load()));
        o->setProperty ("testCaptureFrames", static_cast<double> (testCaptureFrames_.load()));
        o->setProperty ("renderUnderrunSource", "UNAVAILABLE_THROUGH_CURRENT_JUCE_AUDIODEVICE");
        // AudioIODevice::getXRunCount is backend-specific, not comprehensive.
        // Pinned JUCE WASAPI reports input capture discontinuity flags only.
        const int reported = capture != nullptr ? capture->getXRunCount() : -1;
        o->setProperty ("backendXrunCount", reported >= 0 ? juce::var (reported) : juce::var());
        const auto type = dual ? juce::String ("Windows Audio")
                               : deviceManager_.getCurrentAudioDeviceType();
        o->setProperty ("backendXrunMeaning", reported < 0 ? "UNAVAILABLE"
            : type.startsWith ("Windows Audio") ? "WASAPI_CAPTURE_DISCONTINUITIES_ONLY"
            : "DRIVER_DEFINED");
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager_.getAudioDeviceSetup (setup);
        o->setProperty ("inputDevice", dual && capture ? capture->getName() : setup.inputDeviceName);
        o->setProperty ("outputDevice", dual && d ? d->getName() : setup.outputDeviceName);
        o->setProperty ("deviceInputChannels", capture ? capture->getActiveInputChannels().countNumberOfSetBits() : 0);
        o->setProperty ("deviceOutputChannels", d ? d->getActiveOutputChannels().countNumberOfSetBits() : 0);
        o->setProperty ("callbackInputChannels", callbackInputs_.load());
        o->setProperty ("callbackOutputChannels", callbackOutputs_.load());
        o->setProperty ("callbackCount", static_cast<double> (callbackCount_.load()));
        o->setProperty ("callbackDurationUs", static_cast<double> (lastCallbackUs_.load()));
        o->setProperty ("worstCallbackUs", static_cast<double> (worstCallbackUs_.load()));
        o->setProperty ("slowCallbackCount", static_cast<double> (slowCallbacks_.load()));
        o->setProperty ("overBudgetCount", static_cast<double> (overBudgetCallbacks_.load()));
        o->setProperty ("callbackGapCount", static_cast<double> (callbackGaps_.load()));
        o->setProperty ("worstCallbackGapUs", static_cast<double> (worstCallbackGapUs_.load()));
        o->setProperty ("worstPluginUs", static_cast<double> (worstPluginUs_.load()));
        o->setProperty ("deviceStartCount", static_cast<double> (deviceStartCount_.load()));
        o->setProperty ("deviceStopCount", static_cast<double> (deviceStopCount_.load()));
        juce::Array<juce::var> pluginTimings;
        for (int i = 0; i < graphOwned_->count; ++i)
        {
            auto item = std::make_unique<juce::DynamicObject>();
            item->setProperty ("slot", i + 1);
            const int physical = graphOwned_->physicalAt (i);
            item->setProperty ("name", graphOwned_->slots[physical].name);
            item->setProperty ("lastUs", static_cast<double> (lastPluginUsBySlot_[physical].load()));
            item->setProperty ("worstUs", static_cast<double> (worstPluginUsBySlot_[physical].load()));
            pluginTimings.add (juce::var (item.release()));
        }
        o->setProperty ("pluginTimings", juce::var (pluginTimings));
        o->setProperty ("slowPluginCount", static_cast<double> (slowPlugins_.load()));
        o->setProperty ("periodUs", d && d->getCurrentSampleRate() > 0
            ? 1000000.0 * d->getCurrentBufferSizeSamples() / d->getCurrentSampleRate() : 0.0);
        o->setProperty ("performanceNote", "Over-budget and callback-gap counts are timing indicators, not driver-confirmed xruns.");
        auto histogram = realtimeHistogram_.snapshot();
        o->setProperty ("callbackP95BudgetUpperPct", histogram.p95Ceiling < 0
            ? juce::var() : juce::var (histogram.p95Ceiling * 100.0));
        o->setProperty ("callbackP99BudgetUpperPct", histogram.p99Ceiling < 0
            ? juce::var() : juce::var (histogram.p99Ceiling * 100.0));
        o->setProperty ("callbackP95BeyondDeadline", histogram.p95Ceiling < 0);
        o->setProperty ("callbackP99BeyondDeadline", histogram.p99Ceiling < 0);
        o->setProperty ("callbackBudgetSamples", static_cast<double> (histogram.total));
        o->setProperty ("inputPeak", inputPeak_.load());
        o->setProperty ("postFxPeak", postFxPeak_.load());
        o->setProperty ("peak", outputPeak_.load());
        o->setProperty ("peakLeft", outputPeakL_.load());
        o->setProperty ("peakRight", outputPeakR_.load());
        const auto loudness = masterLoudness_.read();
        const bool measuredDirect = !dual && loudness.supported;
        o->setProperty ("masterAnalysisProvenance",
            measuredDirect ? "MEASURED_48K_BS1770_K_4X_FIR" : "UNSUPPORTED");
        if (measuredDirect && loudness.integratedValid)
            o->setProperty ("masterLufs", loudness.integratedLufs);
        if (measuredDirect && loudness.truePeakValid)
            o->setProperty ("masterDbtp", loudness.dbtp);
        o->setProperty ("latencySamples", totalLatency_.load());
        o->setProperty ("chainCount", graphOwned_->count);
        o->setProperty ("graphSwitchPending", handoff_.busy());
        o->setProperty ("completedGraphSwaps", static_cast<double> (completedSwaps_.load()));
        if (dual)
        {
            const auto clock = independent_.bridgeStatus();
            o->setProperty ("bridgeOccupancyFrames", static_cast<double> (clock.fillFrames));
            o->setProperty ("bridgeOverflowFrames", static_cast<double> (clock.overflowFrames));
            o->setProperty ("bridgeUnderflowFrames", static_cast<double> (clock.underflowFrames));
            o->setProperty ("bridgePrimingFrames", static_cast<double> (clock.primingFrames));
            o->setProperty ("bridgeNonFiniteCaptureSamples",
                static_cast<double> (clock.nonFiniteInputSamples));
            o->setProperty ("bridgeCorrectionPpm", clock.correctionPpm);
            o->setProperty ("bridgeTargetLatencyMs", independent_.bridgeTargetLatencyMs());
            o->setProperty ("bridgeEndpointIdentityVerification",
                "UNIQUE_WINDOWS_ID_PREFLIGHT_AND_POSTOPEN_NOT_ID_BOUND");
            o->setProperty ("bridgeCaptureEndpointIdHash", independent_.captureIdentityHash());
            o->setProperty ("bridgeRenderEndpointIdHash", independent_.renderIdentityHash());
            o->setProperty ("bridgeCaptureCallbacks", static_cast<double> (independent_.captureCallbacks()));
            o->setProperty ("bridgeRenderCallbacks", static_cast<double> (independent_.renderCallbacks()));
            o->setProperty ("bridgeNegotiationFault", independent_.formatFaultReason());
            o->setProperty ("bridgeNegotiationProtection",
                "FULL_CHANNEL_MASK_RATE_PERIOD_AT_START_PLUS_RT_CHANNEL_COUNT");
            const auto watch = independent_.streamWatchdogStatus();
            o->setProperty ("bridgeWatchdogFault",
                armix::device::IndependentStreamWatchdog::name (watch.stall));
            o->setProperty ("bridgeCaptureSilentMs", static_cast<double> (watch.captureSilentMs));
            o->setProperty ("bridgeRenderSilentMs", static_cast<double> (watch.renderSilentMs));
            o->setProperty ("bridgeWatchdogArmed", watch.armed);
            o->setProperty ("bridgeWatchdogThresholdMs",
                static_cast<double> (armix::device::IndependentStreamWatchdog::silenceThresholdMs));
            const auto continuity = independent_.continuityStatus();
            o->setProperty ("bridgeContinuityFault",
                armix::device::BridgeContinuityGate::name (continuity.fault));
            o->setProperty ("bridgeContinuityWindowReason",
                armix::device::BridgeContinuityGate::name (continuity.lastWindowFault));
            o->setProperty ("bridgeContinuityBadWindows",
                static_cast<int> (continuity.consecutiveBadWindows));
            o->setProperty ("bridgeContinuityLossBp",
                static_cast<int> (continuity.lossBasisPoints));
            o->setProperty ("bridgeContinuityWindowFrames",
                static_cast<double> (continuity.windowFrames));
        }
        return juce::var (o.release());
    }

private:
    bool approveDirectRoute (
        const armix::device::AsioRegistrationPolicy::Identity* expectedAsio = nullptr)
    {
        auto* device=deviceManager_.getCurrentAudioDevice();
        if (device == nullptr || device->getActiveInputChannels().isZero()
            || device->getActiveOutputChannels().isZero())
            return false;
        // JUCE BigInteger has no fixed 64-bit channel limit: preserve all
        // channel bits when locking an explicitly approved ASIO/WASAPI route.
        // Device setup/control thread only; NEVER run hashing in processBlock.
        const auto backend=device->getTypeName();
        const auto name=device->getName();
        const auto inputMask=device->getActiveInputChannels().toString (16);
        const auto outputMask=device->getActiveOutputChannels().toString (16);
        armix::device::AsioRegistrationPolicy::Identity candidate;
        if (backend == "ASIO")
        {
            const auto selected = armix::device::AsioRegistryIdentityProbe::resolve (
                std::wstring (name.toWideCharPointer()));
            if (!selected.registration.ok() ||
                (expectedAsio != nullptr &&
                 !expectedAsio->sameDriverClass(selected.registration.identity)))
            {
                directFence_.suspend();
                asioRegistrationScope_ = !selected.registration.ok()
                    ? juce::String (armix::device::AsioRegistrationPolicy::reason (
                        selected.registration.error))
                    : juce::String ("SAVED_ASIO_DRIVER_REGISTRATION_CHANGED_DURING_OPEN");
                return false;
            }
            candidate = selected.registration.identity;
        }
        const bool approved = directFence_.approve (
            backend.toRawUTF8(), name.toRawUTF8(),
            inputMask.toRawUTF8(), outputMask.toRawUTF8());
        if (approved)
        {
            asioPinnedRegistration_ = std::move(candidate);
            asioRegistrationScope_ = backend == "ASIO"
                ? "POSTOPEN_REGISTERED_ASIO_DRIVER_CLASS_PINNED"
                : "NOT_ASIO";
        }
        return approved;
    }
    bool resumeVerifiedDirectRoute()
    {
        auto* device=deviceManager_.getCurrentAudioDevice();
        if (device == nullptr || device->getActiveInputChannels().isZero()
            || device->getActiveOutputChannels().isZero())
            return false;
        const auto backend=device->getTypeName();
        const auto name=device->getName();
        const auto inputMask=device->getActiveInputChannels().toString (16);
        const auto outputMask=device->getActiveOutputChannels().toString (16);
        if (backend == "ASIO")
        {
            const auto selected = armix::device::AsioRegistryIdentityProbe::resolve (
                std::wstring (name.toWideCharPointer()));
            if (!selected.registration.ok() ||
                !asioPinnedRegistration_.sameDriverClass(selected.registration.identity))
                return false; // no new ASIO driver class without explicit user approval
        }
        else if (asioPinnedRegistration_.valid()) return false;
        return directFence_.resumeExpected (backend.toRawUTF8(), name.toRawUTF8(),
                                            inputMask.toRawUTF8(), outputMask.toRawUTF8());
    }
    static constexpr int maxFrames = 8192;
    armix::device::AsioPriorityDeviceManager deviceManager_;
    armix::device::DirectRouteFence directFence_;
    // Control-thread only; retain full class identity for exact comparison,
    // publish correlation HASH only. Never read registry in audio callback.
    armix::device::AsioRegistrationPolicy::Identity asioPinnedRegistration_;
    juce::String asioRegistrationScope_ {"NONE"};
    std::atomic<std::uint64_t> directSuppressedFrames_ {0};
    juce::String startupFallbackReason_; // control-thread-only startup diagnostics
    armix::device::JuceIndependentClockDevice independent_;
    armix::device::LiveSecondaryOutputDevice secondaryOutput_;
    std::optional<juce::AudioDeviceManager::AudioDeviceSetup> savedDirectSetup_;
    juce::AudioPluginFormatManager formats_;
    std::unique_ptr<Graph> graphOwned_ {std::make_unique<Graph>()};
    std::unique_ptr<Graph> stagedOwner_;
    armix::rt::LiveGraphHandoff<Graph> handoff_;
    // 0 idle, 1 fade-out, 2 fade-in. One controller producer, one RT reader.
    // No allocation, plugin re-instantiation or extra processing pass.
    std::atomic<int> fxMovePhase_ {0};
    std::atomic<std::uint64_t> fxMovePendingOrder_ {Graph::canonicalOrder};
    bool attached_ = false;
    juce::AudioBuffer<float> scratch_;
    juce::AudioBuffer<float> dryScratch_;
    armix::dsp::NativeMasterLoudness masterLoudness_;
    armix::diagnostics::DiagnosticTone diagnosticTone_;
    LiveDryRoutingLab dryRoutingLab_;
    std::atomic<armix::diagnostics::Route> diagnosticRoute_ {armix::diagnostics::Route::normal};
    std::atomic<bool> routeReconfiguring_ {false};
    std::optional<juce::AudioDeviceManager::AudioDeviceSetup> savedLiveSetup_;
    // Indexed atomic bypass flags: instant RT-safe toggles, no graph detachment.
    std::atomic<bool> monitor_ { false }, channelMute_ { false }, masterMute_ { false }, masterMono_ { false }, dryTest_ { false };
    std::atomic<float> masterGain_ { 1.f }, channelGain_ { 1.f }, channelPan_ { 0.f };
    std::atomic<float> inputPeak_ { 0.f }, postFxPeak_ { 0.f }, outputPeak_ { 0.f };
    std::atomic<float> outputPeakL_ { 0.f }, outputPeakR_ { 0.f };
    std::atomic<int> callbackInputs_ { 0 }, callbackOutputs_ { 0 };
    std::atomic<unsigned long long> callbackCount_ { 0 };
    // Audio-thread-only timestamp and cheap atomically published counters.
    // No log writes, allocations, locks or WebView interactions in callback.
    armix::rt::RealtimeHealthHistogram realtimeHistogram_;
    std::atomic<std::uint64_t> lastCallbackUs_ {0}, worstCallbackUs_ {0};
    std::atomic<std::uint64_t> slowCallbacks_ {0}, overBudgetCallbacks_ {0};
    std::atomic<std::uint64_t> callbackGaps_ {0}, worstCallbackGapUs_ {0};
    std::atomic<std::uint64_t> lateCallbackIntervals_ {0};
    std::atomic<std::uint64_t> cadenceComparedIntervals_ {0}, cadenceClockRegressions_ {0};
    std::atomic<std::uint64_t> cadenceLastExpectedUs_ {0};
    std::atomic<std::uint64_t> testOutputFrames_ {0}, testCaptureFrames_ {0};
    std::atomic<std::uint64_t> worstPluginUs_ {0}, slowPlugins_ {0};
    std::atomic<std::uint64_t> deviceStartCount_ {0}, deviceStopCount_ {0};
    std::array<std::atomic<std::uint64_t>, maxFx> lastPluginUsBySlot_ {};
    std::array<std::atomic<std::uint64_t>, maxFx> worstPluginUsBySlot_ {};
    armix::rt::CallbackCadenceEvidence cadence_; // one render producer; lifecycle reset
    double sampleRateHz_ = 48000.0;

    static std::uint64_t clockMicros() noexcept
    {
        return static_cast<std::uint64_t> (
            std::chrono::duration_cast<std::chrono::microseconds> (
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static void publishMax (std::atomic<std::uint64_t>& dst, std::uint64_t value) noexcept
    {
        auto old = dst.load (std::memory_order_relaxed);
        while (old < value && !dst.compare_exchange_weak (
            old, value, std::memory_order_relaxed)) {}
    }
    std::atomic<int> totalLatency_ { 0 };
    std::atomic<unsigned long long> completedSwaps_ {0};

    // Do not equate JUCE's empty error string with the selected hardware
    // being present. Drivers can disappear between device discovery and open;
    // a wrong fallback endpoint must never be presented as restored audio.
    bool verifyRestoredDirectDevice (
        const juce::AudioDeviceManager::AudioDeviceSetup& expected)
    {
        const auto* active = deviceManager_.getCurrentAudioDevice();
        if (active == nullptr ||
            deviceManager_.getCurrentAudioDeviceType() != "Windows Audio" ||
            active->getActiveInputChannels().isZero() ||
            active->getActiveOutputChannels().isZero()) return false;
        juce::AudioDeviceManager::AudioDeviceSetup current;
        deviceManager_.getAudioDeviceSetup (current);
        return current.inputDeviceName == expected.inputDeviceName
            && current.outputDeviceName == expected.outputDeviceName
            && (expected.useDefaultInputChannels ||
                current.inputChannels == expected.inputChannels)
            && (expected.useDefaultOutputChannels ||
                current.outputChannels == expected.outputChannels);
    }

    void detach()
    {
        if (attached_)
        {
            deviceManager_.removeAudioCallback (this);
            attached_ = false;
        }
    }
    void attach()
    {
        if (! attached_ && deviceManager_.getCurrentAudioDevice() != nullptr)
        {
            deviceManager_.addAudioCallback (this);
            attached_ = true;
        }
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        secondaryOutput_.invalidateSource(); // source clock may have changed
        dryRoutingLab_.prepareDevice(d); // exact active mask, before callback
        if (!independent_.active())
        {
            const auto backend=d->getTypeName();
            const auto name=d->getName();
            const auto inputMask=d->getActiveInputChannels().toString (16);
            const auto outputMask=d->getActiveOutputChannels().toString (16);
            directFence_.observeStart (backend.toRawUTF8(), name.toRawUTF8(),
                                       inputMask.toRawUTF8(), outputMask.toRawUTF8());
        }
        deviceStartCount_.fetch_add (1, std::memory_order_relaxed);
        sampleRateHz_ = juce::jmax (1.0, d->getCurrentSampleRate());
        diagnosticTone_.prepare (sampleRateHz_); // device stopped; no RT allocation
        masterLoudness_.prepare (sampleRateHz_); // fixed buffers, 48k DIRECT only
        for (int i = 0; i < maxFx; ++i)
        {
            lastPluginUsBySlot_[i].store (0, std::memory_order_relaxed);
            worstPluginUsBySlot_[i].store (0, std::memory_order_relaxed);
        }
        cadence_.prepare (sampleRateHz_);
        cadenceLastExpectedUs_.store (0, std::memory_order_relaxed);
        int latency = 0;
        for (int i = 0; i < graphOwned_->count; ++i)
        {
            auto& slot = graphOwned_->slots[i];
            if (slot.processor)
            {
                slot.processor->releaseResources();
                slot.processor->prepareToPlay (d->getCurrentSampleRate(),
                    juce::jmax (32, d->getCurrentBufferSizeSamples()));
                slot.latency = slot.processor->getLatencySamples();
                const bool bypass=graphOwned_->bypassFlags[i].load (std::memory_order_relaxed);
                graphOwned_->bypassEnvelope[i].prepare(bypass,sampleRateHz_,slot.latency);
                if (!bypass) latency += slot.latency;
            }
        }
        totalLatency_.store (latency);
        inputPeak_.store (0.f);
        postFxPeak_.store (0.f);
        outputPeak_.store (0.f);
    }

    void audioDeviceStopped() override
    {
        secondaryOutput_.invalidateSource(); // explicit reconnect required after device stop
        masterLoudness_.prepare (0.0); // clear validity across device epoch
        dryRoutingLab_.deviceStopped();
        deviceStopCount_.fetch_add (1, std::memory_order_relaxed);
        inputPeak_.store (0.f);
        outputPeak_.store (0.f);
    }

    // R0 bridge: existing CH1 multi-insert vendor state/Orbit stays owned by
    // the live FX handoff. The audio callback holds its Cycle lease throughout
    // this render pass, so an old plugin graph cannot retire in mid-block.
    // This is a temporary ownership adapter until all inserts are represented
    // by the same native multi-lane graph; not a second rendering engine.
    struct LegacyChannelOneLease final { LiveAudioEngine* host; Graph* graph; };
    static void processLegacyChannelOne(void* token,int lane,float* left,
                                        float* right,int frames) noexcept
    {
        if(lane!=0 || !token || !left || !right || frames<=0 || frames>maxFrames)return;
        const auto& lease=*static_cast<LegacyChannelOneLease*>(token);
        auto* const host=lease.host;
        auto* const graph=lease.graph;
        if(!host || !graph || graph->count<=0)return;
        float* channels[]{left,right};
        juce::AudioBuffer<float> buffer(channels,2,frames);
        juce::MidiBuffer midi;
        const auto order=graph->orderPacked.load(std::memory_order_acquire);
        for(int logical=0;logical<graph->count;++logical)
        {
            const int physical=Graph::physicalAt(order,logical);
            auto& slot=graph->slots[physical];
            if(!slot.processor)continue;
            auto& bypass=graph->bypassEnvelope[physical];
            bypass.request(graph->bypassFlags[physical].load(std::memory_order_acquire));
            const bool blend=bypass.transitioning();
            if(blend)
            {
                juce::FloatVectorOperations::copy(host->dryScratch_.getWritePointer(0),left,frames);
                juce::FloatVectorOperations::copy(host->dryScratch_.getWritePointer(1),right,frames);
            }
            if(bypass.shouldProcessWet())
            {
                try {slot.processor->processBlock(buffer,midi);}
                catch(...)
                {
                    juce::FloatVectorOperations::clear(left,frames);
                    juce::FloatVectorOperations::clear(right,frames);
                    return; // vendor exception must not unwind JUCE callback
                }
            }
            if(blend)
            {
                const float* dryL=host->dryScratch_.getReadPointer(0);
                const float* dryR=host->dryScratch_.getReadPointer(1);
                for(int i=0;i<frames;++i)
                {
                    const auto gains=bypass.next();
                    left[i]=gains.wet*left[i]+gains.dry*dryL[i];
                    right[i]=gains.wet*right[i]+gains.dry*dryR[i];
                }
            }
            midi.clear();
        }
    }

    void audioDeviceIOCallbackWithContext (
        const float* const* inputs, int inputChannels, float* const* outputs,
        int outputChannels, int samples, const juce::AudioIODeviceCallbackContext&) override
    {
        if (samples <= 0) return;
        // Fail closed if JUCE silently rerouted ASIO/WASAPI after hotplug or
        // while the user is changing devices. Atomic gate only: no name hash,
        // allocations, OS calls or device restart on the realtime callback.
        // An independent WASAPI bridge has its own verified endpoint guard.
        const auto activeDiagnosticRoute = diagnosticRoute_.load (std::memory_order_relaxed);
        // DRY_THROUGH still routes a live microphone to the hardware output:
        // it MUST use the same DIRECT authorization fence as NORMAL. Only
        // genuinely isolated OUTPUT_ONLY_TONE / CAPTURE_ONLY tests may bypass
        // the full-duplex route fence while the driver is one-sided.
        if (!independent_.active()
            && activeDiagnosticRoute != armix::diagnostics::Route::outputOnlyTone
            && activeDiagnosticRoute != armix::diagnostics::Route::captureOnly
            && !directFence_.permitted())
        {
            for (int c=0; c<outputChannels; ++c)
                if (outputs[c]) juce::FloatVectorOperations::clear (outputs[c], samples);
            directSuppressedFrames_.fetch_add (static_cast<std::uint64_t> (samples),
                                               std::memory_order_relaxed);
            return;
        }
        const auto cycle = handoff_.beginCycle();
        // R0B B3: production NORMAL audio has a single native routing owner.
        // Until a device-epoch-validated 10CH/4AUX route is published, output
        // stays silent. Never fall back to legacy CH1 after STOP/hotplug or on
        // first boot; that could feed an unrelated speaker without a patch.
        // Diagnostic isolation is explicitly exempt so zero-FX hardware FAT
        // still supports OUTPUT_ONLY_TONE/CAPTURE_ONLY/DRY_THROUGH.
        // Keep the existing FX graph handoff progressing even when unpatched:
        // a staged ADD/REMOVE must ACK and retire on the callback thread.
        // Only atomic reads and deterministic output writes; no RT heap or I/O.
        if (activeDiagnosticRoute == armix::diagnostics::Route::normal
            && !dryRoutingLab_.active())
        {
            for (int c = 0; c < outputChannels; ++c)
                if (outputs && outputs[c])
                    juce::FloatVectorOperations::clear(outputs[c], samples);
            inputPeak_.store(0.f, std::memory_order_relaxed);
            postFxPeak_.store(0.f, std::memory_order_relaxed);
            outputPeak_.store(0.f, std::memory_order_relaxed);
            outputPeakL_.store(0.f, std::memory_order_relaxed);
            outputPeakR_.store(0.f, std::memory_order_relaxed);
            handoff_.endCycle(cycle); // ACK prepared FX graph even in silence
            return;
        }

        const std::uint64_t beginUs = clockMicros();
        const double periodUs = 1000000.0 * static_cast<double> (samples) / sampleRateHz_;
        // Scheduling gaps are NOT driver-confirmed xruns. Critically the
        // expected START-to-START period comes from the PREVIOUS callback's
        // number of frames, not the current block (JUCE can vary quantum).
        // O(1), single audio producer; lifecycle resets before stream begins.
        const auto cadence = cadence_.observe (beginUs, samples);
        if (cadence.comparable())
        {
            cadenceComparedIntervals_.fetch_add (1, std::memory_order_relaxed);
            cadenceLastExpectedUs_.store (
                static_cast<std::uint64_t> (cadence.expectedIntervalUs + 0.5),
                std::memory_order_relaxed);
            publishMax (worstCallbackGapUs_, cadence.actualIntervalUs);
            if (cadence.kind == armix::rt::CallbackCadenceEvidence::Kind::late ||
                cadence.kind == armix::rt::CallbackCadenceEvidence::Kind::veryLate)
                lateCallbackIntervals_.fetch_add (1, std::memory_order_relaxed);
            if (cadence.kind == armix::rt::CallbackCadenceEvidence::Kind::veryLate)
                callbackGaps_.fetch_add (1, std::memory_order_relaxed);
        }
        else if (cadence.kind == armix::rt::CallbackCadenceEvidence::Kind::clockRegression)
            cadenceClockRegressions_.fetch_add (1, std::memory_order_relaxed);
        callbackInputs_.store (inputChannels, std::memory_order_relaxed);
        callbackOutputs_.store (outputChannels, std::memory_order_relaxed);
        callbackCount_.fetch_add (1, std::memory_order_relaxed);
        // P1E: the LIVE dry-routed 10-strip path is an explicit operator
        // action, with an immutable preallocated P1D graph. Its own zero-VST
        // callback path NEVER falls through to CH1 on stale/hotplug routes.
        // DirectFence was already checked above; no output to unknown device.
        if (dryRoutingLab_.active())
        {
            const auto outputGate=armix::mixer::LiveOutputRouteGate::evaluate(
                dryRoutingLab_.active(),
                directFence_.permitted() && !independent_.active(),
                activeDiagnosticRoute == armix::diagnostics::Route::normal,
                !routeReconfiguring_.load(std::memory_order_acquire),
                !masterMute_.load(std::memory_order_relaxed),
                monitor_.load(std::memory_order_relaxed),
                secondaryOutput_.armed());
            const bool outputPermitted=outputGate.renderMain;
            // Both native 10CH routing and the existing CH1 vendor FX chain
            // execute in ONE stereo output render pass. Never mix a parallel
            // CH1 output on top or copy vendor processors into another graph.
            LegacyChannelOneLease ch1Lease{this,cycle.graph};
            std::array<int,2> nativeMainPair{{-1,-1}};
            std::array<int,2> nativeMonitorPair{{-1,-1}};
            const bool routed=dryRoutingLab_.process(inputs,inputChannels,outputs,
                     outputChannels,samples,
                     masterGain_.load(std::memory_order_relaxed),
                     !outputPermitted,&ch1Lease,&processLegacyChannelOne,
                     masterMono_.load(std::memory_order_relaxed),
                     &nativeMainPair,&nativeMonitorPair);
            // Constant-time, callback-local physical PCM tap. Nothing is
            // reconfigured or acquired from another graph generation.
            const auto nativeMain=armix::mixer::NativeOutputPairTap::select(
                outputs,outputChannels,nativeMainPair);
            const auto nativeMonitor=armix::mixer::NativeOutputPairTap::select(
                outputs,outputChannels,nativeMonitorPair);
            // STOP can race after the outer active() check. Never leave a
            // callback's output untouched, and NEVER fall through into CH1.
            if(!routed) {
                for(int c=0;c<outputChannels;++c)
                    if(outputs && outputs[c])
                        juce::FloatVectorOperations::clear(outputs[c],samples);
            }
            // R0B: when routed CH1 borrows the original vendor rack, apply
            // the SAME two-buffer FX MOVE de-click as the original callback.
            // The physical instance pointers do not change. Publish the new
            // logical order only after the old render has faded to zero.
            // No heap, plugin lifecycle, callback detach, locks or UI on RT.
            const int routedFxMove=fxMovePhase_.load(std::memory_order_acquire);
            float movedPeakL=0.f,movedPeakR=0.f;
            // Graph handoff and physical-order MOVE are mutually exclusive.
            // Apply a single output de-click gain per sample, AFTER graph DSP,
            // without touching third-party plugin lifecycle on the callback.
            if(routedFxMove!=0 || cycle.fadeOut || cycle.fadeIn) {
                const float step=samples>1?1.f/static_cast<float>(samples-1):0.f;
                // Fade both INDEPENDENT physical buses on FX MOVE or graph
                // handoff, not only MAIN. Same callback, no second DSP pass.
                for(int c=0;c<4;++c) {
                    auto* const out=c<2?nativeMain[static_cast<std::size_t>(c)]
                        :nativeMonitor[static_cast<std::size_t>(c-2)];
                    if(!out)continue;
                    float peak=0.f;
                    for(int i=0;i<samples;++i) {
                        const float phase=static_cast<float>(i)*step;
                        const float graphGain=cycle.fadeOut?1.f-phase:
                            cycle.fadeIn?phase:1.f;
                        const float moveGain=armix::dsp::FxMoveRamp::gain(
                            routedFxMove,i,samples);
                        out[i]*=graphGain*moveGain;
                        peak=juce::jmax(peak,std::abs(out[i]));
                    }
                    if(c==0)movedPeakL=peak;
                    else movedPeakR=peak;
                }
                if(routedFxMove==1 && cycle.graph) {
                    cycle.graph->orderPacked.store(
                        fxMovePendingOrder_.load(std::memory_order_acquire),
                        std::memory_order_release);
                    fxMovePhase_.store(2,std::memory_order_release);
                } else if(routedFxMove==2)
                    fxMovePhase_.store(0,std::memory_order_release);
            }
            if(cycle.fadeOut)handoff_.endCycle(cycle); // RT pointer publish only
            // OUTPUT1: publish the SAME post-FX, post-fader, de-clicked MAIN
            // PCM once. The virtual endpoint never invokes MIX/VST3 again.
            // Hardware monitor mute remains INDEPENDENT: streaming may run
            // while every physical output on the current device is silent.
            if(routed && outputGate.secondary && nativeMain[0] && nativeMain[1])
                secondaryOutput_.publish(nativeMain[0],nativeMain[1],samples);
            // MS3A2: loudness/dBTP measure the same selected hardware
            // destination as the actual MAIN render and FX move fade.
            const float* measured[2] = {nativeMain[0],nativeMain[1]};
            masterLoudness_.process(measured,2,samples);
            // Only actually routed audio can produce a live output meter.
            const float left=routed?dryRoutingLab_.peakLeft():0.f;
            const float right=routed?dryRoutingLab_.peakRight():0.f;
            const float audibleLeft=(routedFxMove!=0||cycle.fadeOut||cycle.fadeIn)?movedPeakL:left;
            const float audibleRight=(routedFxMove!=0||cycle.fadeOut||cycle.fadeIn)?movedPeakR:right;
            inputPeak_.store(0.f,std::memory_order_relaxed);
            postFxPeak_.store(juce::jmin(1.f,juce::jmax(left,right)),std::memory_order_relaxed);
            outputPeakL_.store(juce::jmin(1.f,audibleLeft),std::memory_order_relaxed);
            outputPeakR_.store(juce::jmin(1.f,audibleRight),std::memory_order_relaxed);
            outputPeak_.store(juce::jmin(1.f,juce::jmax(audibleLeft,audibleRight)),std::memory_order_relaxed);
            // ADD/REMOVE still need the R0B prepared shared-owner lifecycle;
            // only CH1 MOVE is now safe inside the routed render callback.
            const auto elapsed=clockMicros()-beginUs;
            lastCallbackUs_.store(elapsed,std::memory_order_relaxed);
            publishMax(worstCallbackUs_,elapsed);
            realtimeHistogram_.observe(elapsed,periodUs);
            if(static_cast<double>(elapsed)>periodUs*0.80)
                slowCallbacks_.fetch_add(1,std::memory_order_relaxed);
            if(static_cast<double>(elapsed)>periodUs)
                overBudgetCallbacks_.fetch_add(1,std::memory_order_relaxed);
            // Secondary PCM is already copied; preserve logical MAIN meter
            // evidence before disarming the physical MAIN/MONITOR pins.
            if(!outputGate.hardware)
                for(int channel=0;channel<outputChannels;++channel)
                    if(outputs && outputs[channel])
                        juce::FloatVectorOperations::clear(outputs[channel],samples);
            return;
        }
        const bool enabled = monitor_.load (std::memory_order_relaxed)
                          && !routeReconfiguring_.load (std::memory_order_acquire);
        const auto route = diagnosticRoute_.load (std::memory_order_relaxed);
        const bool toneTest = route == armix::diagnostics::Route::outputOnlyTone;
        const bool captureTest = route == armix::diagnostics::Route::captureOnly;
        const bool dry = dryTest_.load (std::memory_order_relaxed)
                      || route != armix::diagnostics::Route::normal;
        if (toneTest && outputChannels > 0)
            testOutputFrames_.fetch_add (static_cast<std::uint64_t> (samples), std::memory_order_relaxed);
        if (captureTest && inputChannels > 0)
            testCaptureFrames_.fetch_add (static_cast<std::uint64_t> (samples), std::memory_order_relaxed);
        const bool muted = channelMute_.load (std::memory_order_relaxed)
                        || masterMute_.load (std::memory_order_relaxed);
        const float gain = masterGain_.load (std::memory_order_relaxed)
                         * channelGain_.load (std::memory_order_relaxed);
        const float pan = channelPan_.load (std::memory_order_relaxed);
        // Precomputed stereo pan matrix; no trigonometry, locks or allocations
        // in the realtime sample loop. 0 = transparent identity matrix.
        const auto matrix = armix::dsp::StereoPanMatrix::fromNormalized (pan);
        const bool mono = masterMono_.load (std::memory_order_relaxed);
        // Collapse POST-pan L/R with a 0.5 + 0.5 sum; keep equal outputs.
        // Coefficients are computed once per block outside the per-sample loop.
        const float monoL = 0.5f * (matrix.ll + matrix.rl);
        const float monoR = 0.5f * (matrix.lr + matrix.rr);
        const float rampStep = samples > 1 ? 1.f / static_cast<float> (samples - 1) : 0.f;
        const int fxMovePhase = fxMovePhase_.load (std::memory_order_acquire);
        // The same envelope applies to every output channel and sample.
        // Old graph reaches zero before the callback publishes new order.
        const bool fxFadeOut = fxMovePhase == 1;
        const bool fxFadeIn = fxMovePhase == 2;
        float inPeak = 0.f, postFxPeak = 0.f, outPeak = 0.f;
        float outPeakL = 0.f, outPeakR = 0.f;
        for (int offset = 0; offset < samples; offset += maxFrames)
        {
            const int frames = juce::jmin (maxFrames, samples - offset);
            // Preallocated backing store: no resizing/capacity bookkeeping in RT.
            // Two-channel AudioBuffer view refers to scratch_ without ownership.
            // JUCE uses its inline channel pointer storage for two channels.
            juce::AudioBuffer<float> scratchView (
                scratch_.getArrayOfWritePointers(), 2, frames);
            if (toneTest)
            {
                auto* left = scratch_.getWritePointer (0);
                auto* right = scratch_.getWritePointer (1);
                const bool audible = enabled && !masterMute_.load (std::memory_order_relaxed);
                for (int i = 0; i < frames; ++i)
                    left[i] = right[i] = diagnosticTone_.next (audible);
            }
            for (int c = 0; c < 2; ++c)
            {
                float* dst = scratch_.getWritePointer (c);
                const float* src = inputChannels > c ? inputs[c]
                                  : inputChannels > 0 ? inputs[0] : nullptr;
                if (!toneTest)
                {
                    if (src)
                        juce::FloatVectorOperations::copy (dst, src + offset, frames);
                    else
                        juce::FloatVectorOperations::clear (dst, frames);
                }
                for (int i = 0; i < frames; ++i)
                    if (!toneTest) inPeak = juce::jmax (inPeak, std::abs (dst[i]));
            }
            // MIDI capacity is empty unless the plugin itself changes it.
            juce::MidiBuffer midi;
            if (!dry)
            {
                // One atomic order snapshot per block: stable physical FX and bypass identity.
                const auto order = cycle.graph->orderPacked.load (std::memory_order_acquire);
                for (int fx = 0; fx < cycle.graph->count; ++fx)
                {
                    const int physical = Graph::physicalAt (order, fx);
                    auto& slot = cycle.graph->slots[physical];
                    if (!slot.processor) {midi.clear();continue;}
                    auto& bypass=cycle.graph->bypassEnvelope[physical];
                    bypass.request(cycle.graph->bypassFlags[physical].load(std::memory_order_acquire));
                    // Steady wet/dry requires no copy or extra arithmetic.
                    // A transition temporarily snapshots the exact input to
                    // this insert; a second full FX graph is never created.
                    const bool blend=bypass.transitioning();
                    if (blend)
                        for(int ch=0;ch<2;++ch)
                            juce::FloatVectorOperations::copy(
                                dryScratch_.getWritePointer(ch),
                                scratch_.getReadPointer(ch),frames);
                    if (bypass.shouldProcessWet())
                    {
                        const auto pluginBeginUs = clockMicros();
                        slot.processor->processBlock (scratchView, midi);
                        const auto pluginUs = clockMicros() - pluginBeginUs;
                        lastPluginUsBySlot_[physical].store (pluginUs, std::memory_order_relaxed);
                        publishMax (worstPluginUsBySlot_[physical], pluginUs);
                        publishMax (worstPluginUs_, pluginUs);
                        if (static_cast<double> (pluginUs) > periodUs * 0.75)
                            slowPlugins_.fetch_add (1, std::memory_order_relaxed);
                    }
                    if (blend)
                    {
                        float* wetL=scratch_.getWritePointer(0);
                        float* wetR=scratch_.getWritePointer(1);
                        const float* dryL=dryScratch_.getReadPointer(0);
                        const float* dryR=dryScratch_.getReadPointer(1);
                        for(int i=0;i<frames;++i)
                        {
                            const auto g=bypass.next();
                            wetL[i]=g.wet*wetL[i]+g.dry*dryL[i];
                            wetR[i]=g.wet*wetR[i]+g.dry*dryR[i];
                        }
                    }
                    midi.clear();
                }
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* processed = scratch_.getReadPointer (ch);
                for (int i = 0; i < frames; ++i)
                    postFxPeak = juce::jmax (postFxPeak, std::abs (processed[i]));
            }
            for (int c = 0; c < outputChannels; ++c)
            {
                if (!outputs[c]) continue;
                const float* left = scratch_.getReadPointer (0);
                const float* right = scratch_.getReadPointer (1);
                float* dst = outputs[c] + offset;
                const float fromL = mono ? monoL : (c == 0 ? matrix.ll : matrix.rl);
                const float fromR = mono ? monoR : (c == 0 ? matrix.lr : matrix.rr);
                float channelPeak = 0.f;
                for (int i = 0; i < frames; ++i)
                {
                    const float mapped = toneTest ? left[i] : fromL * left[i] + fromR * right[i];
                    const auto phase = (offset + i) * rampStep;
                    const float graphRamp = cycle.fadeOut ? 1.f - phase
                                          : cycle.fadeIn ? phase : 1.f;
                    const float fxRamp = armix::dsp::FxMoveRamp::gain (
                        fxMovePhase, offset + i, samples);
                    const float ramp = graphRamp * fxRamp;
                    const float value = captureTest ? 0.f
                        : toneTest ? mapped * ramp
                        : (enabled && !muted) ? mapped * gain * ramp : 0.f;
                    dst[i] = value;
                    channelPeak = juce::jmax (channelPeak, std::abs (value));
                }
                outPeak = juce::jmax (outPeak, channelPeak);
                if (c == 0) outPeakL = juce::jmax (outPeakL, channelPeak);
                if (c == 1) outPeakR = juce::jmax (outPeakR, channelPeak);
            }
        }
        if (!independent_.active())
        {
            const float* measured[2] = {
                outputs && outputChannels>0 ? outputs[0] : nullptr,
                outputs && outputChannels>1 ? outputs[1] : nullptr
            };
            masterLoudness_.process(measured,juce::jmin(outputChannels,2),samples);
        }
        inputPeak_.store (juce::jmin (1.f, inPeak), std::memory_order_relaxed);
        postFxPeak_.store (juce::jmin (1.f, postFxPeak), std::memory_order_relaxed);
        outputPeak_.store (juce::jmin (1.f, outPeak), std::memory_order_relaxed);
        outputPeakL_.store (juce::jmin (1.f, outPeakL), std::memory_order_relaxed);
        outputPeakR_.store (juce::jmin (1.f, outPeakR), std::memory_order_relaxed);
        if (fxFadeOut)
        {
            // The last emitted sample of this block is exactly zero.
            // Switch order only after all outputs were rendered. Next block
            // starts at zero under the NEW order, so there is no step edge.
            cycle.graph->orderPacked.store (
                fxMovePendingOrder_.load (std::memory_order_acquire),
                std::memory_order_release);
            fxMovePhase_.store (2, std::memory_order_release);
        }
        else if (fxFadeIn)
            fxMovePhase_.store (0, std::memory_order_release);

        if (cycle.fadeOut)
        {
            // Ownership remains with stagedOwner_ until message thread ack.
            handoff_.endCycle (cycle);
            const auto* switched = handoff_.current();
            if (switched && switched->hasRecallState)
            {
                channelGain_.store (switched->channelLinear, std::memory_order_relaxed);
                masterGain_.store (switched->masterLinear, std::memory_order_relaxed);
                setChannelPan (switched->pan);
                setChannelMuted (switched->channelMute);
                setMasterMuted (switched->masterMute);
                totalLatency_.store (switched->latency, std::memory_order_relaxed);
            }
        }
        const auto elapsedUs = clockMicros() - beginUs;
        lastCallbackUs_.store (elapsedUs, std::memory_order_relaxed);
        publishMax (worstCallbackUs_, elapsedUs);
        realtimeHistogram_.observe (elapsedUs, periodUs);
        if (static_cast<double> (elapsedUs) > periodUs * 0.80)
            slowCallbacks_.fetch_add (1, std::memory_order_relaxed);
        if (static_cast<double> (elapsedUs) > periodUs)
            overBudgetCallbacks_.fetch_add (1, std::memory_order_relaxed);
    }
};
