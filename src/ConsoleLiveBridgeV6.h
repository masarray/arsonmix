// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include "../integration/ConsoleNativeContractV6.h"
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
        const auto route=live.getProperty("dryRoutingLab",{});
        const auto ep=decimalFromNative(route.getProperty("epoch",juce::var()));
        auto result=std::make_unique<juce::DynamicObject>();
        result->setProperty("schemaVersion",schemaVersion);
        result->setProperty("productionCallbackLive",false);

        auto device=std::make_unique<juce::DynamicObject>();
        device->setProperty("epoch",ep);
        device->setProperty("backend",live.getProperty("driver","").toString());
        device->setProperty("captureDevice",live.getProperty("inputDevice","").toString());
        device->setProperty("playbackDevice",live.getProperty("outputDevice","").toString());
        device->setProperty("sampleRate",live.getProperty("rate",juce::var()));
        device->setProperty("bufferFrames",live.getProperty("buffer",juce::var()));
        const bool routeAccepted=static_cast<bool>(route.getProperty("ready",false)) &&
            static_cast<bool>(route.getProperty("hasAcceptedRoute",false));
        const bool direct=static_cast<bool>(live.getProperty("directRouteAuthorized",false));
        const bool armed=static_cast<bool>(live.getProperty("monitor",false)) &&
            static_cast<bool>(live.getProperty("connected",false)) &&
            direct && routeAccepted;
        device->setProperty("outputArmed",armed);
        // Exact enabled physical pin maps to JUCE callback ordinal; raw
        // samples are measured by the installed input callback BEFORE MUTE.
        // Omit per-pin evidence if the native observation is unavailable.
        const auto physicalInputs=route.getProperty("ports",juce::var());
        const auto perPort=live.getProperty("captureCallbackPeaks",juce::var());
        const auto* observed=perPort.getArray();
        juce::Array<juce::var> inputEndpoints;
        if(const auto* pins=physicalInputs.getArray())
            for(const auto& pin:*pins) {
                const auto cb=static_cast<int>(pin.getProperty("callback",-1));
                const auto phys=pin.getProperty("port",juce::var());
                if(!phys.isInt()||cb<0)continue;
                auto obj=std::make_unique<juce::DynamicObject>();
                obj->setProperty("port",phys);
                obj->setProperty("callback",cb);
                obj->setProperty("label",pin.getProperty("label",""));
                if(observed && cb<observed->size()) {
                    const auto value=observed->getReference(cb);
                    if(value.isDouble()||value.isInt()) {
                        const auto peak=static_cast<double>(value);
                        if(std::isfinite(peak)&&peak>=0.) {
                            obj->setProperty("rawPeak",peak);
                            obj->setProperty("rawPresent",peak>0.0005);
                        }
                    }
                }
                inputEndpoints.add(juce::var(obj.release()));
            }
        device->setProperty("inputEndpoints",juce::var(inputEndpoints));
        juce::Array<juce::var> outputs;
        const auto enabledOutputPins=route.getProperty("outputPorts",juce::var());
        if(const auto* enabled=enabledOutputPins.getArray()) {
            for(int i=0;i<enabled->size();++i){
                const auto p=enabled->getReference(i);
                if(!p.isInt())continue;
                auto item=std::make_unique<juce::DynamicObject>();
                item->setProperty("port",p);
                item->setProperty("callback",i);
                outputs.add(juce::var(item.release()));
            }
        }
        device->setProperty("outputEndpoints",juce::var(outputs));
        result->setProperty("device",juce::var(device.release()));

        // No universal roster or stable ChannelId exists in the installed M4
        // audio callback yet. In particular, never present "CH10 FX Return"
        // as a true wet FX_RETURN node and never advertise +CHANNEL/GROUP.
        auto graph=std::make_unique<juce::DynamicObject>();
        graph->setProperty("revision","0");
        graph->setProperty("rosterRevision","0");
        graph->setProperty("channels",juce::var(juce::Array<juce::var>{}));
        graph->setProperty("destinations",juce::var(juce::Array<juce::var>{}));
        auto capabilities=std::make_unique<juce::DynamicObject>();
        capabilities->setProperty("callbackOwnsChannelIds",false);
        capabilities->setProperty("fullVendorFxMigration",false);
        capabilities->setProperty("showAndOutputParity",false);
        capabilities->setProperty("physicalCaptureMapped",false);
        capabilities->setProperty("dynamicChannels",false);
        capabilities->setProperty("groups",false);
        capabilities->setProperty("fxReturns",false);
        capabilities->setProperty("monitor",false);
        capabilities->setProperty("sendsOnFaders",false);
        capabilities->setProperty("supportedOps",juce::var(juce::Array<juce::var>{}));
        graph->setProperty("capabilities",juce::var(capabilities.release()));
        result->setProperty("graph",juce::var(graph.release()));

        auto telemetry=std::make_unique<juce::DynamicObject>();
        const auto peak=static_cast<double>(live.getProperty("inputPeak",0.0));
        const auto callbackCount=static_cast<double>(live.getProperty("callbackCount",0.0));
        const auto callbackInputs=static_cast<int>(live.getProperty("callbackInputChannels",0));
        const bool raw=static_cast<bool>(live.getProperty("connected",false)) &&
            callbackCount>0 && callbackInputs>0 && peak>0.0005;
        telemetry->setProperty("rawCapturePresent",raw);
        telemetry->setProperty("rawCapturePeak",peak);
        telemetry->setProperty("mainPcmPresent",armed &&
            static_cast<double>(live.getProperty("peak",0.0))>0.0002);
        telemetry->setProperty("monitorPcmPresent",armed &&
            static_cast<double>(route.getProperty("monitorPeakL",0.0))>0.0002);
        telemetry->setProperty("channelMeterRmsPeak",juce::var(juce::Array<juce::var>{}));
        result->setProperty("telemetry",juce::var(telemetry.release()));

        // Separate LEGACY diagnostic metadata cannot be mistaken for accepted
        // ChannelId graph revisions. Output remains governed by old M4.
        auto legacy=std::make_unique<juce::DynamicObject>();
        legacy->setProperty("active",static_cast<bool>(route.getProperty("active",false)));
        legacy->setProperty("routeAccepted",routeAccepted);
        legacy->setProperty("routingRevision",decimalFromNative(
            route.getProperty("revision",juce::var())));
        legacy->setProperty("channelPeaks",route.getProperty("channelPeaks",
            juce::var(juce::Array<juce::var>{})));
        result->setProperty("legacyM4",juce::var(legacy.release()));
        return juce::var(result.release());
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
        auto ack=std::make_unique<juce::DynamicObject>();
        ack->setProperty("requestId",id);
        ack->setProperty("status","REJECTED");
        const auto device=acceptedSnapshot.getProperty("device",{});
        const auto graph=acceptedSnapshot.getProperty("graph",{});
        const auto epoch=device.getProperty("epoch","0").toString();
        ack->setProperty("deviceEpoch",epoch);
        ack->setProperty("graphRevision",graph.getProperty("revision","0"));
        ack->setProperty("rosterRevision",graph.getProperty("rosterRevision","0"));
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
        ack->setProperty("errorCode",fault);
        ack->setProperty("productionCallbackLive",false);
        return juce::var(ack.release());
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
