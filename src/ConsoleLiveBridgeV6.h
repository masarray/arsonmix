// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include "../integration/ConsoleNativeContractV6.h"
#include "../engine/core/ConsolePuzzleV8Wire.h"
#include <cstdint>
#include <deque>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>

// Native Console v6 adapter, exclusively on the JUCE MESSAGE/CONTROL thread.
// This projects the real M4 callback/device readback WITHOUT promoting its
// fixed strip indexes to fictitious ChannelIds. The production ChannelId
// migration must replace this deliberately fail-closed G0 capability gate.
// It never owns PCM, clones VST3, or calls the audio device from the browser.
namespace armix::console {
class LiveConsoleBridgeV6 final {
public:
    static constexpr int schemaVersion=6;

    // Source: LiveAudioEngine::status() with its real JUCE callback telemetry,
    // driver active masks and epoch. No browser invented physical pins.
    static juce::var snapshot(const juce::var& live) {
        // Single actual native producer for both JUCE WebView and S0
        // fixture. No JSON shape authored by the JS/GUI.
        PuzzleV8Snapshot w;
        const auto route=live.getProperty("dryRoutingLab",juce::var());
        w.epoch=decimalFromNative(route.getProperty("epoch",juce::var())).toStdString();
        w.legacyRoutingRevision=decimalFromNative(
            route.getProperty("revision",juce::var())).toStdString();
        w.legacyActive=static_cast<bool>(route.getProperty("active",false));
        w.legacyRouteAccepted=static_cast<bool>(route.getProperty("ready",false)) &&
            static_cast<bool>(route.getProperty("hasAcceptedRoute",false));
        w.backend=live.getProperty("driver","").toString().toStdString();
        w.captureDevice=live.getProperty("inputDevice","").toString().toStdString();
        w.playbackDevice=live.getProperty("outputDevice","").toString().toStdString();
        const double rate=static_cast<double>(live.getProperty("rate",0.));
        if(std::isfinite(rate) && rate>0. && rate<=384000.)
            w.sampleRate=static_cast<int>(rate);
        const int frames=static_cast<int>(live.getProperty("buffer",0));
        if(frames>0)w.bufferFrames=frames;
        const bool connected=static_cast<bool>(live.getProperty("connected",false));
        w.outputArmed=connected && w.legacyRouteAccepted &&
            static_cast<bool>(live.getProperty("monitor",false)) &&
            static_cast<bool>(live.getProperty("directRouteAuthorized",false));
        const auto peak=static_cast<double>(live.getProperty("inputPeak",0.));
        if(std::isfinite(peak) && peak>=0.)w.rawCapturePeak=peak;
        w.rawCapturePresent=connected &&
            static_cast<double>(live.getProperty("callbackCount",0.))>0. &&
            static_cast<int>(live.getProperty("callbackInputChannels",0))>0 &&
            w.rawCapturePeak>0.0005;
        w.mainPcmPresent=w.outputArmed &&
            static_cast<double>(live.getProperty("peak",0.))>0.0002;
        w.monitorPcmPresent=w.outputArmed &&
            static_cast<double>(route.getProperty("monitorPeakL",0.))>0.0002;
        const auto pins=route.getProperty("ports",juce::var());
        const auto peaks=live.getProperty("captureCallbackPeaks",juce::var());
        const auto* readings=peaks.getArray();
        if(const auto* physical=pins.getArray())
            for(const auto& p:*physical) {
                const auto number=p.getProperty("port",juce::var());
                const auto ordinal=p.getProperty("callback",juce::var());
                if(!number.isInt()||!ordinal.isInt())continue;
                PuzzleV8Pin pin;
                pin.port=static_cast<int>(number);
                pin.callback=static_cast<int>(ordinal);
                if(pin.port<0 || pin.callback<0)continue;
                pin.label=p.getProperty("label","").toString().toStdString();
                if(readings && pin.callback<readings->size()) {
                    const auto value=readings->getReference(pin.callback);
                    if(value.isInt()||value.isDouble()) {
                        const auto peakValue=static_cast<double>(value);
                        if(std::isfinite(peakValue)&&peakValue>=0.) {
                            pin.hasRawPeak=true;pin.rawPeak=peakValue;
                        }
                    }
                }
                w.inputs.push_back(std::move(pin));
            }
        const auto outputs=route.getProperty("outputPorts",juce::var());
        if(const auto* ports=outputs.getArray())
            for(int ordinal=0;ordinal<ports->size();++ordinal) {
                const auto value=ports->getReference(ordinal);
                if(!value.isInt())continue;
                const int physical=static_cast<int>(value);
                if(physical<0)continue;
                w.outputs.push_back({physical,ordinal,{},{},0.});
            }
        // Authoritative installed renderer is still the old M4 PCM graph.
        // Never promote CH10's label to a real wet FX_RETURN or advertise
        // universal CREATE/GROUP/INSERT until one ChannelId callback owns it.
        w.productionCallbackLive=false;
        const auto bytes=PuzzleV8Wire::snapshot(w);
        return juce::JSON::parse(juce::String::fromUTF8(bytes.c_str()));
    }

    // Exact native wire: opaque, unique-per-editor requestId and uint64
    // canonical DECIMAL STRINGS, never a lossy double / JS Number cast.
    // This G0 adapter has no supported mutating operation: an invalid/stale
    // request or unsupported role must preserve the currently audible M4.
    juce::var rejectUnsupportedIntent(const juce::var& intent,
                                       const juce::var& acceptedSnapshot) {
        const auto request=intent.getProperty("requestId",juce::var());
        const auto id=request.isString()?request.toString():juce::String{};
        const auto requestBytes=id.toStdString();
        const auto device=acceptedSnapshot.getProperty("device",{});
        const auto graph=acceptedSnapshot.getProperty("graph",{});
        const auto epoch=device.getProperty("epoch","0").toString();
        juce::String fault="UNIVERSAL_CALLBACK_NOT_READY";
        if(!armix::integration::validConsoleRequestId(requestBytes))
            fault="INVALID_REQUEST_ID";
        else if(completed_.find(requestBytes)!=completed_.end())
            fault="DUPLICATE_REQUEST_ID";
        else {
            if(recentIds_.size()==maxRecentIds) {
                completed_.erase(recentIds_.front());
                recentIds_.pop_front();
            }
            recentIds_.push_back(requestBytes);
            completed_.insert(requestBytes);
            // G0 is fail closed by design. Distinguish malformed/stale wire
            // from a legitimate mutation that requires the M4->M5 cutover.
            std::uint64_t asked=0,baseGraph=0,baseRoster=0;
            const bool wellFormed=decodeDecimal(intent.getProperty("deviceEpoch",{}),asked) &&
                decodeDecimal(intent.getProperty("baseGraphRevision",{}),baseGraph) &&
                decodeDecimal(intent.getProperty("baseRosterRevision",{}),baseRoster) &&
                intent.getProperty("operation",{}).isString();
            if(!wellFormed)fault="INVALID_DECIMAL_WIRE";
            else if(intent.getProperty("deviceEpoch",{}).toString()!=epoch)
                fault="STALE_DEVICE_EPOCH";
            else if(intent.getProperty("baseGraphRevision",{}).toString()!=
                    graph.getProperty("revision","0").toString() ||
                    intent.getProperty("baseRosterRevision",{}).toString()!=
                    graph.getProperty("rosterRevision","0").toString())
                fault="STALE_GRAPH_REVISION";
            else {
                const auto target=intent.getProperty("targetChannelId",juce::var());
                armix::mixer::ChannelId channel{};
                if(!target.isVoid() &&
                   (!target.isString() ||
                    !armix::integration::parseNativeChannelId(
                        target.toString().toStdString(),channel)))
                    fault="INVALID_CHANNEL_ID";
            }
        }
        PuzzleV8Ack ack;
        ack.requestId=requestBytes;
        ack.status="REJECTED";
        ack.deviceEpoch=epoch.toStdString();
        ack.graphRevision=graph.getProperty("revision","0").toString().toStdString();
        ack.rosterRevision=graph.getProperty("rosterRevision","0").toString().toStdString();
        ack.errorCode=fault.toStdString();
        const auto bytes=PuzzleV8Wire::ack(ack);
        return juce::JSON::parse(juce::String::fromUTF8(bytes.c_str()));
    }

    static bool decodeDecimal(const juce::var& raw,std::uint64_t& result) noexcept {
        if(!raw.isString())return false;
        const auto str=raw.toString();
        if(str.isEmpty()||str.length()>20 ||
           (str.length()>1 && str[0]=='0'))return false;
        std::uint64_t n=0;
        for(int i=0;i<str.length();++i) {
            const auto c=str[i];
            if(c<'0'||c>'9')return false;
            const auto digit=static_cast<unsigned>(c-'0');
            if(n>(std::numeric_limits<std::uint64_t>::max()-digit)/10)return false;
            n=n*10+digit;
        }
        result=n;return true;
    }

private:
    static juce::String decimalFromNative(const juce::var& value) {
        if(!(value.isInt()||value.isInt64()))return "0";
        const auto n=static_cast<juce::int64>(value);
        return n>=0?juce::String(n):juce::String("0");
    }
    static constexpr std::size_t maxRecentIds=256;
    // Bounded completed IDs on the serialized message thread, never the
    // native RT callback. Replayed old requests remain nonmutating in G0.
    std::unordered_set<std::string> completed_;
    std::deque<std::string> recentIds_;
};
} // namespace armix::console
