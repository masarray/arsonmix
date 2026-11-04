// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>

// M2 bridge: the installed M4 callback is NOT yet a universal ChannelId
// renderer. Publish only the real active JUCE pins/telemetry, with an EMPTY
// ChannelId graph. No ordinal-to-ChannelId conversion, fictional capabilities,
// duplicate DSP/audio callback, or implicit hardware arming.
namespace armix::console::approvedM2 {
inline juce::String decimal (const juce::var& v) {
    if (v.isInt() || v.isInt64()) {
        const auto n=static_cast<juce::int64>(v);
        return n > 0 ? juce::String(n) : juce::String("1");
    }
    if (v.isString()) {
        const auto t=v.toString();
        if (t.isNotEmpty() && t.length()<=20 && t.containsOnly("0123456789")
            && t[0]!='0') return t;
    }
    return "1"; // UNREADY control session, not a hardware epoch or real pin.
}
inline juce::var build (const juce::var& installed,
                        const juce::var& actualDevice) {
    auto result=std::make_unique<juce::DynamicObject>();
    result->setProperty("schemaVersion",6);
    result->setProperty("productionCallbackLive",false);
    const auto route=installed.getProperty("dryRoutingLab",juce::var());
    auto device=std::make_unique<juce::DynamicObject>();
    device->setProperty("epoch",decimal(route.getProperty("epoch",juce::var())));
    device->setProperty("backend",actualDevice.getProperty("backend",""));
    device->setProperty("captureDevice",actualDevice.getProperty("captureDevice",""));
    device->setProperty("playbackDevice",actualDevice.getProperty("playbackDevice",""));
    device->setProperty("sampleRate",actualDevice.getProperty("rate",0));
    device->setProperty("bufferFrames",actualDevice.getProperty("buffer",0));
    const bool open=static_cast<bool>(actualDevice.getProperty("open",false));
    device->setProperty("outputArmed",open &&
        !static_cast<bool>(actualDevice.getProperty("outputMuted",true)));
    juce::Array<juce::var> inputPorts,outputPorts,inputEndpoints,outputEndpoints;
    const auto addPorts=[&](const char* enabledName,const char* labelsName,
                            const char* prefix,juce::Array<juce::var>& ports,
                            juce::Array<juce::var>& endpoints) {
        const auto enabled=actualDevice.getProperty(enabledName,juce::var());
        const auto labels=actualDevice.getProperty(labelsName,juce::var());
        const auto* indices=enabled.getArray();
        const auto* names=labels.getArray();
        if (!open || !indices || !names) return;
        for (const auto& value:*indices) {
            if (!value.isInt()) continue;
            const int pin=static_cast<int>(value);
            if (pin<0 || pin>=names->size() || pin>255) continue;
            const auto label=names->getReference(pin).toString();
            if (label.isEmpty()) continue;
            auto entry=std::make_unique<juce::DynamicObject>();
            entry->setProperty("id",juce::String(prefix)+":"+juce::String(pin));
            entry->setProperty("port",pin);
            entry->setProperty("callback",static_cast<int>(ports.size()));
            entry->setProperty("label",label);
            entry->setProperty("active",true);
            ports.add(pin);
            endpoints.add(juce::var(entry.release()));
        }
    };
    addPorts("enabledInputs","inputNames","in",inputPorts,inputEndpoints);
    addPorts("enabledOutputs","outputNames","out",outputPorts,outputEndpoints);
    device->setProperty("enabledInputs",juce::var(inputPorts));
    device->setProperty("enabledOutputs",juce::var(outputPorts));
    device->setProperty("inputEndpoints",juce::var(inputEndpoints));
    device->setProperty("outputEndpoints",juce::var(outputEndpoints));
    result->setProperty("device",juce::var(device.release()));

    auto graph=std::make_unique<juce::DynamicObject>();
    graph->setProperty("revision","0");
    graph->setProperty("rosterRevision","0");
    graph->setProperty("channels",juce::var(juce::Array<juce::var>()));
    graph->setProperty("destinations",juce::var(juce::Array<juce::var>()));
    auto caps=std::make_unique<juce::DynamicObject>();
    for (const char* field: {"callbackOwnsChannelIds","fullVendorFxMigration",
            "showAndOutputParity","physicalCaptureMapped","dynamicChannels",
            "groups","fxReturns","monitor","sendsOnFaders"})
        caps->setProperty(field,false);
    caps->setProperty("supportedOps",juce::var(juce::Array<juce::var>()));
    graph->setProperty("capabilities",juce::var(caps.release()));
    result->setProperty("graph",juce::var(graph.release()));

    auto telemetry=std::make_unique<juce::DynamicObject>();
    const auto peak=static_cast<double>(installed.getProperty("inputPeak",0.0));
    const double safePeak=std::isfinite(peak) && peak>=0.0 ? peak : 0.0;
    telemetry->setProperty("rawCapturePeak",safePeak);
    telemetry->setProperty("rawCapturePresent",open && !inputPorts.isEmpty() &&
        static_cast<double>(installed.getProperty("callbackCount",0.0))>0.0 &&
        safePeak>0.0005);
    telemetry->setProperty("mainPcmPresent",false); // No accepted ChannelId MAIN.
    telemetry->setProperty("monitorPcmPresent",false);
    result->setProperty("telemetry",juce::var(telemetry.release()));
    return juce::var(result.release());
}
} // namespace armix::console::approvedM2
