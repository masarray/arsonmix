// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <cstdint>
#include <limits>

// M2 message-thread adapter for the ONE approved Pages WebView2 mixer.
// This G0 adapter reports TRUE current device pins and unadmitted ChannelId
// capability. It MUST NOT invent PCM, approve uninstalled engine ops, create a
// callback, allocate from the audio thread, or borrow historic demo ChannelIds.
// ENGINE #266 replaces G0 with actual accepted topology on production cutover.
namespace armix::native_console_m2
{
inline juce::String decimal (std::uint64_t number)
{
    return juce::String (std::to_string (number));
}

inline std::uint64_t deviceGeneration (const juce::var& live)
{
    const auto value = live.getProperty ("deviceGeneration", {});
    if (! value.isInt() && ! value.isInt64() && ! value.isDouble()) return 0;
    const auto number = static_cast<double> (value);
    if (! std::isfinite (number) || number < 0 ||
        number > 9007199254740991.0 || std::floor (number) != number)
        return 0;
    return static_cast<std::uint64_t> (number);
}

inline juce::var realDevicePorts (const juce::StringArray& labels,
                                   const juce::BigInteger& active,
                                   const char* direction,
                                   juce::Array<juce::var>& enabled)
{
    juce::Array<juce::var> endpoints;
    int callbackOrdinal = 0;
    for (int pin = 0; pin < juce::jmin (labels.size(), 64); ++pin)
    {
        if (! active[pin]) continue;
        enabled.add (pin);
        auto row = std::make_unique<juce::DynamicObject>();
        row->setProperty ("id", juce::String (direction) + juce::String (pin));
        row->setProperty ("port", pin);
        row->setProperty ("callback", callbackOrdinal++);
        row->setProperty ("label", labels[pin]);
        row->setProperty ("active", true);
        endpoints.add (juce::var (row.release()));
    }
    return juce::var (endpoints);
}

inline juce::var snapshot (const juce::var& live,
                            juce::AudioDeviceManager& manager,
                            bool allOutputsDisarmed)
{
    // Message-thread read of the REAL engine and real current JUCE device.
    // When separate clocks are active, direct JUCE input pins cannot be
    // mistaken for capture-device callback pin mapping.
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("schemaVersion", 6);
    root->setProperty ("productionCallbackLive", false);
    root->setProperty ("provenance", "G0_CURRENT_JUCE_DEVICE_ONLY__CHANNELID_NOT_ADMITTED");

    auto dev = std::make_unique<juce::DynamicObject>();
    dev->setProperty ("epoch", decimal (deviceGeneration (live)));
    dev->setProperty ("backend", manager.getCurrentAudioDeviceType());
    dev->setProperty ("captureDevice", live.getProperty ("inputDevice", juce::var ("")));
    dev->setProperty ("playbackDevice", live.getProperty ("outputDevice", juce::var ("")));
    juce::Array<juce::var> enabledInputs, enabledOutputs;
    juce::var inputs (juce::Array<juce::var>{}), outputs (juce::Array<juce::var>{});
    const auto* active = manager.getCurrentAudioDevice();
    const bool direct = ! static_cast<bool> (live.getProperty ("adaptiveClockActive", false));
    if (active != nullptr && active->isOpen())
    {
        outputs = realDevicePorts (active->getOutputChannelNames(),
                                   active->getActiveOutputChannels(), "out:", enabledOutputs);
        if (direct)
            inputs = realDevicePorts (active->getInputChannelNames(),
                                      active->getActiveInputChannels(), "in:", enabledInputs);
    }
    dev->setProperty ("inputEndpoints", inputs);
    dev->setProperty ("outputEndpoints", outputs);
    dev->setProperty ("enabledInputs", juce::var (enabledInputs));
    dev->setProperty ("enabledOutputs", juce::var (enabledOutputs));
    dev->setProperty ("sampleRate", active != nullptr && active->isOpen()
                                          ? active->getCurrentSampleRate() : 0.0);
    dev->setProperty ("bufferFrames", active != nullptr && active->isOpen()
                                           ? active->getCurrentBufferSizeSamples() : 0);
    dev->setProperty ("outputArmed", ! allOutputsDisarmed);
    root->setProperty ("device", juce::var (dev.release()));

    auto caps = std::make_unique<juce::DynamicObject>();
    caps->setProperty ("callbackOwnsChannelIds", false);
    caps->setProperty ("fullVendorFxMigration", false);
    caps->setProperty ("dynamicChannels", false);
    caps->setProperty ("groups", false);
    caps->setProperty ("fxReturns", false);
    caps->setProperty ("monitor", false);
    caps->setProperty ("supportedOps", juce::var (juce::Array<juce::var>{}));
    auto graph = std::make_unique<juce::DynamicObject>();
    graph->setProperty ("revision", juce::String ("0"));
    graph->setProperty ("rosterRevision", juce::String ("0"));
    graph->setProperty ("channels", juce::var (juce::Array<juce::var>{}));
    graph->setProperty ("destinations", juce::var (juce::Array<juce::var>{}));
    graph->setProperty ("edges", juce::var (juce::Array<juce::var>{}));
    graph->setProperty ("capabilities", juce::var (caps.release()));
    root->setProperty ("graph", juce::var (graph.release()));

    auto telemetry = std::make_unique<juce::DynamicObject>();
    telemetry->setProperty ("rawCapturePresent", false);
    telemetry->setProperty ("rawCapturePeak", 0.0);
    telemetry->setProperty ("mainPcmPresent", false);
    telemetry->setProperty ("monitorPcmPresent", false);
    root->setProperty ("telemetry", juce::var (telemetry.release()));
    return juce::var (root.release());
}

inline juce::String rejectReason (const juce::var& intent, const juce::var& accepted)
{
    if (! intent.isObject()) return "INVALID_INTENT";
    const auto request = intent.getProperty ("requestId", {});
    if (! request.isString() || request.toString().isEmpty() ||
        request.toString().length() > 128) return "INVALID_REQUEST_ID";
    const auto device = accepted.getProperty ("device", {});
    const auto graph = accepted.getProperty ("graph", {});
    const auto e = intent.getProperty ("deviceEpoch", {});
    const auto gr = intent.getProperty ("baseGraphRevision", {});
    const auto rr = intent.getProperty ("baseRosterRevision", {});
    if (! e.isString() || ! gr.isString() || ! rr.isString())
        return "INVALID_REVISION_TYPE";
    if (e.toString() != device.getProperty ("epoch", {}).toString())
        return "STALE_DEVICE";
    if (gr.toString() != graph.getProperty ("revision", {}).toString())
        return "STALE_GRAPH";
    if (rr.toString() != graph.getProperty ("rosterRevision", {}).toString())
        return "STALE_ROSTER";
    return "UNSUPPORTED_OP"; // G0 intentionally permits ZERO PCM mutations.
}

inline juce::var reject (const juce::var& intent, const juce::var& accepted)
{
    auto ack = std::make_unique<juce::DynamicObject>();
    const auto request = intent.getProperty ("requestId", {});
    ack->setProperty ("requestId", request.isString() ? request : juce::var (""));
    ack->setProperty ("status", "REJECTED");
    ack->setProperty ("errorCode", rejectReason (intent, accepted));
    ack->setProperty ("deviceEpoch",
                      accepted.getProperty ("device", {}).getProperty ("epoch", "0"));
    const auto graph = accepted.getProperty ("graph", {});
    ack->setProperty ("graphRevision", graph.getProperty ("revision", "0"));
    ack->setProperty ("rosterRevision", graph.getProperty ("rosterRevision", "0"));
    ack->setProperty ("productionCallbackLive", false);
    return juce::var (ack.release());
}
} // namespace armix::native_console_m2
