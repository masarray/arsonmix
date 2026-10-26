// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "StereoPan.h"
#include "NativePresetArchive.h"
#include "VstBusLayout.h"
#include "LiveGraphHandoff.h"
#include "DiagnosticTone.h"
#include "../engine/device/JuceIndependentClockDevice.h"
#include "../engine/device/WasapiEndpointIdentityGuard.h"
#include "../engine/core/RealtimeHealthHistogram.h"
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
// Legacy Add/Remove/Move still detach; seamless structural editing is deferred.
class LiveAudioEngine final : private juce::AudioIODeviceCallback
{
public:
    static constexpr int maxFx = 8;

    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::String name, catalogId, path;
        int uid = 0;
        bool bypass = false;
        int latency = 0;
    };

    struct Graph
    {
        std::array<Slot, maxFx> slots {};
        std::array<std::atomic<bool>, maxFx> bypassFlags {};
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
        handoff_.initialize (graphOwned_.get());
    }

    ~LiveAudioEngine() override { stop(); }

    juce::AudioDeviceManager& devices() noexcept { return deviceManager_; }

    juce::String start (const juce::XmlElement* savedSettings = nullptr)
    {
        // A mixer behaves like a signal path: ready I/O flows automatically.
        // The former MONITOR enable gate caused silent master/output confusion.
        // Panic OUTPUT MUTE remains available at all times.
        monitor_.store (true);
        const auto error = deviceManager_.initialise (1, 2, savedSettings, true);
        if (error.isNotEmpty()) return error;
        deviceManager_.addAudioCallback (this);
        attached_ = true;
        return {};
    }

    void stop()
    {
        independent_.close();
        detach();
        handoff_.resetAfterStop (graphOwned_.get());
        stagedOwner_.reset();
        for (int i = 0; i < graphOwned_->count; ++i)
        {
            if (graphOwned_->slots[i].processor) graphOwned_->slots[i].processor->releaseResources();
            graphOwned_->slots[i] = Slot {};
        }
        graphOwned_->count = 0;
        deviceManager_.closeAudioDevice();
    }

    void setMonitorEnabled (bool value) noexcept { monitor_.store (value); }
    bool isMonitorEnabled() const noexcept { return monitor_.load(); }
    // Emergency output mute, not a prerequisite for receiving audio.
    void setOutputMuted (bool mute) noexcept { monitor_.store (!mute, std::memory_order_release); }
    bool isOutputMuted() const noexcept { return !monitor_.load (std::memory_order_acquire); }

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
            savedDirectSetup_.reset();
            attach();
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
                savedDirectSetup_.reset();
                attach();
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
        using Route = armix::diagnostics::Route;
        if (handoff_.busy()) return "Finish the pending FX graph transition first";
        if (independent_.active()) return "Disable independent clock mode before endpoint isolation tests";
        if (wanted == diagnosticRoute_.load (std::memory_order_acquire) &&
            !(wanted == Route::normal && savedLiveSetup_)) return {};
        if (wanted < Route::normal || wanted > Route::captureOnly)
            return "Unknown audio diagnostic route";

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
                if (rollback.isNotEmpty()) setOutputMuted (true);
                return "Unable to isolate endpoint: " + (error.isNotEmpty() ? error : "Device not available")
                     + (rollback.isNotEmpty() ? " | restore failed: " + rollback : juce::String());
            }
            savedLiveSetup_ = original;
        }
        diagnosticRoute_.store (wanted, std::memory_order_release);
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
        for (int i = 0; i < old->count; ++i)
            if (old->slots[i].processor) old->slots[i].processor->releaseResources();
        old.reset(); // Third-party destruction exclusively on message thread.
        handoff_.completeRetirement();
        completedSwaps_.fetch_add (1, std::memory_order_relaxed);
        return true;
    }

    juce::AudioPluginInstance* pluginAt (int index) noexcept
    {
        return index >= 0 && index < graphOwned_->count ? graphOwned_->slots[index].processor.get() : nullptr;
    }

    juce::var chainState() const
    {
        juce::Array<juce::var> nodes;
        for (int i = 0; i < graphOwned_->count; ++i)
        {
            auto node = std::make_unique<juce::DynamicObject>();
            node->setProperty ("index", i);
            node->setProperty ("name", graphOwned_->slots[i].name);
            node->setProperty ("id", graphOwned_->slots[i].catalogId);
            node->setProperty ("bypass", graphOwned_->slots[i].bypass);
            node->setProperty ("latency", graphOwned_->slots[i].latency);
            nodes.add (juce::var (node.release()));
        }
        return juce::var (nodes);
    }

    juce::String addVst3 (const juce::String& filename, int uid,
                          const juce::String& catalogId)
    {
        if (handoff_.busy()) return "FX graph transition in progress; retry shortly";
        if (graphOwned_->count >= maxFx) return "FX Rack is full (maximum 8 VST3 effects)";
        auto* device = deviceManager_.getCurrentAudioDevice();
        if (device == nullptr) return "Configure a working audio input/output device first";
        auto* fmt = formats_.getFormat (0);
        if (fmt == nullptr || ! fmt->fileMightContainThisPluginType (filename))
            return "VST3 module not found: " + filename;

        juce::OwnedArray<juce::PluginDescription> descs;
        fmt->findAllTypesForFile (descs, filename);
        juce::PluginDescription* chosen = nullptr;
        for (auto* d : descs)
            if (d != nullptr && d->uniqueId == uid && !d->isInstrument)
            {
                chosen = d;
                break;
            }
        if (chosen == nullptr)
            return "Effect not found inside VST3 module. Rescan the catalog.";

        juce::String error;
        auto instance = formats_.createPluginInstance (
            *chosen, device->getCurrentSampleRate(),
            juce::jmax (32, device->getCurrentBufferSizeSamples()), error);
        if (! instance)
            return "Failed to instantiate VST3: " + error;

        const auto busError = armix::vst::prepareStereoEffectLayout (*instance);
        if (busError.isNotEmpty())
            return chosen->name + ": " + busError;

        detach();
        Slot node;
        node.name = chosen->name;
        node.catalogId = catalogId;
        node.path = filename;
        node.uid = uid;
        node.processor = std::move (instance);
        graphOwned_->slots[graphOwned_->count] = std::move (node);
        graphOwned_->bypassFlags[graphOwned_->count].store (false, std::memory_order_release);
        ++graphOwned_->count;
        attach();
        return {};
    }

    // Capture VST3 vendor binary state, not the legacy WebView2 mock settings.
    // Must only run when output is muted (caller enforces): callback is detached
    // exactly once, no plugin state methods ever execute on the audio thread.
    juce::String captureSnapshot (armix::storage::Snapshot& snapshot)
    {
        if (!isOutputMuted()) return "Mute hardware output before saving a native preset";
        if (handoff_.busy()) return "Finish pending FX graph transition before saving";
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
                const auto& slot = graphOwned_->slots[i];
                if (!slot.processor) { error = "Invalid active FX slot"; break; }
                armix::storage::FxState state;
                state.path = slot.path;
                state.name = slot.name;
                state.catalogId = slot.catalogId;
                state.uid = slot.uid;
                state.bypass = graphOwned_->bypassFlags[i].load (std::memory_order_acquire);
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
            try
            {
                // State is restored before insertion; live audio still uses the
                // old plan until every new instance has finished construction.
                processor->prepareToPlay (rate, buffer);
                if (fx.binary.getSize() > 0)
                    processor->setStateInformation (
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
            slot.processor = std::move (processor);
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
        if (handoff_.busy()) return false;
        if (index < 0 || index >= graphOwned_->count) return false;
        // Message-thread metadata and an atomic value consumed by the audio thread.
        graphOwned_->slots[index].bypass = value;
        graphOwned_->bypassFlags[index].store (value, std::memory_order_release);
        return true;
    }

    bool removeAt (int index)
    {
        if (handoff_.busy()) return false;
        if (index < 0 || index >= graphOwned_->count) return false;
        detach();
        if (graphOwned_->slots[index].processor)
            graphOwned_->slots[index].processor->releaseResources();
        for (int i = index; i + 1 < graphOwned_->count; ++i)
            graphOwned_->slots[i] = std::move (graphOwned_->slots[i + 1]);
        graphOwned_->slots[--graphOwned_->count] = Slot {};
        for (int i = 0; i < graphOwned_->count; ++i)
            graphOwned_->bypassFlags[i].store (graphOwned_->slots[i].bypass, std::memory_order_release);
        attach();
        return true;
    }

    bool moveSlot (int from, int to)
    {
        if (handoff_.busy()) return false;
        if (from < 0 || from >= graphOwned_->count || to < 0 || to >= graphOwned_->count || from == to)
            return false;
        detach();
        auto moved = std::move (graphOwned_->slots[from]);
        if (from < to)
            for (int i = from; i < to; ++i) graphOwned_->slots[i] = std::move (graphOwned_->slots[i + 1]);
        else
            for (int i = from; i > to; --i) graphOwned_->slots[i] = std::move (graphOwned_->slots[i - 1]);
        graphOwned_->slots[to] = std::move (moved);
        for (int i = 0; i < graphOwned_->count; ++i)
            graphOwned_->bypassFlags[i].store (graphOwned_->slots[i].bypass, std::memory_order_release);
        attach();
        return true;
    }

    juce::var status()
    {
        auto o = std::make_unique<juce::DynamicObject>();
        const bool dual = independent_.active();
        auto* d = dual ? independent_.renderDevice()
                       : deviceManager_.getCurrentAudioDevice();
        auto* capture = dual ? independent_.captureDevice() : d;
        o->setProperty ("connected", d != nullptr && capture != nullptr);
        o->setProperty ("adaptiveClockActive", dual);
        o->setProperty ("adaptiveClockFaulted", dual && independent_.faulted());
        o->setProperty ("adaptiveClockMode", dual ? "INDEPENDENT_WASAPI_RENDER_MASTER" : "DIRECT");
        o->setProperty ("driver", dual ? "Windows Audio / dual independent clocks"
                                         : deviceManager_.getCurrentAudioDeviceType());
        o->setProperty ("captureRate", capture ? capture->getCurrentSampleRate() : 0.0);
        o->setProperty ("renderRate", d ? d->getCurrentSampleRate() : 0.0);
        o->setProperty ("device", d ? d->getName() : "No device");
        o->setProperty ("rate", d ? d->getCurrentSampleRate() : 0.0);
        o->setProperty ("buffer", d ? d->getCurrentBufferSizeSamples() : 0);
        o->setProperty ("monitor", monitor_.load());
        o->setProperty ("masterMute", masterMute_.load());
        o->setProperty ("channelMute", channelMute_.load());
        o->setProperty ("channelPan", channelPan_.load());
        // Live controls are authoritative: never trust an old WebView localStorage fader/mute.
        o->setProperty ("channelDb", juce::Decibels::gainToDecibels (
            channelGain_.load (std::memory_order_relaxed), -60.f));
        o->setProperty ("masterDb", juce::Decibels::gainToDecibels (
            masterGain_.load (std::memory_order_relaxed), -60.f));
        o->setProperty ("dryTest", dryTest_.load());
        const auto route = diagnosticRoute();
        o->setProperty ("diagnosticRoute", armix::diagnostics::routeName (route));
        o->setProperty ("diagnosticTemporary", savedLiveSetup_.has_value());
        o->setProperty ("deviceGeneration", static_cast<double> (deviceStartCount_.load()));
        o->setProperty ("lateCallbackIntervals", static_cast<double> (lateCallbackIntervals_.load()));
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
            item->setProperty ("name", graphOwned_->slots[i].name);
            item->setProperty ("lastUs", static_cast<double> (lastPluginUsBySlot_[i].load()));
            item->setProperty ("worstUs", static_cast<double> (worstPluginUsBySlot_[i].load()));
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
    static constexpr int maxFrames = 8192;
    juce::AudioDeviceManager deviceManager_;
    armix::device::JuceIndependentClockDevice independent_;
    std::optional<juce::AudioDeviceManager::AudioDeviceSetup> savedDirectSetup_;
    juce::AudioPluginFormatManager formats_;
    std::unique_ptr<Graph> graphOwned_ {std::make_unique<Graph>()};
    std::unique_ptr<Graph> stagedOwner_;
    armix::rt::LiveGraphHandoff<Graph> handoff_;
    bool attached_ = false;
    juce::AudioBuffer<float> scratch_;
    armix::diagnostics::DiagnosticTone diagnosticTone_;
    std::atomic<armix::diagnostics::Route> diagnosticRoute_ {armix::diagnostics::Route::normal};
    std::atomic<bool> routeReconfiguring_ {false};
    std::optional<juce::AudioDeviceManager::AudioDeviceSetup> savedLiveSetup_;
    // Indexed atomic bypass flags: instant RT-safe toggles, no graph detachment.
    std::atomic<bool> monitor_ { false }, channelMute_ { false }, masterMute_ { false }, dryTest_ { false };
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
    std::atomic<std::uint64_t> testOutputFrames_ {0}, testCaptureFrames_ {0};
    std::atomic<std::uint64_t> worstPluginUs_ {0}, slowPlugins_ {0};
    std::atomic<std::uint64_t> deviceStartCount_ {0}, deviceStopCount_ {0};
    std::array<std::atomic<std::uint64_t>, maxFx> lastPluginUsBySlot_ {};
    std::array<std::atomic<std::uint64_t>, maxFx> worstPluginUsBySlot_ {};
    std::uint64_t previousCallbackStartUs_ = 0;
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
        deviceStartCount_.fetch_add (1, std::memory_order_relaxed);
        sampleRateHz_ = juce::jmax (1.0, d->getCurrentSampleRate());
        diagnosticTone_.prepare (sampleRateHz_); // device stopped; no RT allocation
        for (int i = 0; i < maxFx; ++i)
        {
            lastPluginUsBySlot_[i].store (0, std::memory_order_relaxed);
            worstPluginUsBySlot_[i].store (0, std::memory_order_relaxed);
        }
        previousCallbackStartUs_ = 0;
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
                if (!graphOwned_->bypassFlags[i].load (std::memory_order_relaxed)) latency += slot.latency;
            }
        }
        totalLatency_.store (latency);
        inputPeak_.store (0.f);
        postFxPeak_.store (0.f);
        outputPeak_.store (0.f);
    }

    void audioDeviceStopped() override
    {
        deviceStopCount_.fetch_add (1, std::memory_order_relaxed);
        inputPeak_.store (0.f);
        outputPeak_.store (0.f);
    }

    void audioDeviceIOCallbackWithContext (
        const float* const* inputs, int inputChannels, float* const* outputs,
        int outputChannels, int samples, const juce::AudioIODeviceCallbackContext&) override
    {
        const auto cycle = handoff_.beginCycle();
        const std::uint64_t beginUs = clockMicros();
        if (samples <= 0) return;
        const double periodUs = 1000000.0 * static_cast<double> (samples) / sampleRateHz_;
        // Scheduling gaps only suggest instability: pause/suspend and WASAPI
        // callback scheduling can create false positives. Never label xruns.
        if (previousCallbackStartUs_ != 0 && periodUs > 0)
        {
            const auto gap = beginUs - previousCallbackStartUs_;
            publishMax (worstCallbackGapUs_, gap);
            if (static_cast<double> (gap) > periodUs * 1.5)
                lateCallbackIntervals_.fetch_add (1, std::memory_order_relaxed);
            if (static_cast<double> (gap) > periodUs * 2.3)
                callbackGaps_.fetch_add (1, std::memory_order_relaxed);
        }
        previousCallbackStartUs_ = beginUs;
        callbackInputs_.store (inputChannels, std::memory_order_relaxed);
        callbackOutputs_.store (outputChannels, std::memory_order_relaxed);
        callbackCount_.fetch_add (1, std::memory_order_relaxed);
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
        const float rampStep = samples > 1 ? 1.f / static_cast<float> (samples - 1) : 0.f;
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
                for (int fx = 0; fx < cycle.graph->count; ++fx)
                {
                    auto& slot = cycle.graph->slots[fx];
                    if (slot.processor && ! cycle.graph->bypassFlags[fx].load (std::memory_order_relaxed))
                    {
                        const auto pluginBeginUs = clockMicros();
                        slot.processor->processBlock (scratchView, midi);
                        const auto pluginUs = clockMicros() - pluginBeginUs;
                        lastPluginUsBySlot_[fx].store (pluginUs, std::memory_order_relaxed);
                        publishMax (worstPluginUsBySlot_[fx], pluginUs);
                        publishMax (worstPluginUs_, pluginUs);
                        if (static_cast<double> (pluginUs) > periodUs * 0.75)
                            slowPlugins_.fetch_add (1, std::memory_order_relaxed);
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
                float channelPeak = 0.f;
                for (int i = 0; i < frames; ++i)
                {
                    const float mapped = toneTest ? left[i]
                        : c == 0 ? matrix.ll * left[i] + matrix.lr * right[i]
                                               : matrix.rl * left[i] + matrix.rr * right[i];
                    const auto phase = (offset + i) * rampStep;
                    const float ramp = cycle.fadeOut ? 1.f - phase
                                     : cycle.fadeIn ? phase : 1.f;
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
        inputPeak_.store (juce::jmin (1.f, inPeak), std::memory_order_relaxed);
        postFxPeak_.store (juce::jmin (1.f, postFxPeak), std::memory_order_relaxed);
        outputPeak_.store (juce::jmin (1.f, outPeak), std::memory_order_relaxed);
        outputPeakL_.store (juce::jmin (1.f, outPeakL), std::memory_order_relaxed);
        outputPeakR_.store (juce::jmin (1.f, outPeakR), std::memory_order_relaxed);
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
