// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include "../integration/ConsoleNativeContractV6.h"
#include "../engine/core/ConsolePuzzleV8Wire.h"
#include <cstdint>
#include <deque>
#include <cmath>
#include <string_view>
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
        // The control session has an epoch even before a driver is opened.
        // 1 is the UNREADY session generation, NOT a fabricated physical
        // source; any later verified device epoch supersedes this token.
        auto actualEpoch=decimalFromNative(route.getProperty("epoch",juce::var()));
        if(actualEpoch=="0")actualEpoch="1";
        w.epoch=actualEpoch.toStdString();
        w.legacyRoutingRevision=decimalFromNative(
            route.getProperty("revision",juce::var())).toStdString();
        // The accepted fixed-M4 revision stays under legacyM4 only.
        // A ChannelId revision is never copied from an ordinal M4 graph.
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
        auto actual=juce::JSON::parse(juce::String::fromUTF8(bytes.c_str()));
        // Optional diagnosed M4 accepted controls are sourced from the
        // installed JUCE control thread, not stale immutable routing faders.
        // UNREADY fixture stays byte-identical; these are NOT ChannelIds.
        const auto legacy=actual.getProperty("legacyM4",{});
        if(auto* object=legacy.getDynamicObject()) {
            // Current native M4 (not a fake ChannelId roster): exactly the
            // signal that is audible in this physical device callback.
            const auto controls=route.getProperty("channelControls",juce::var());
            const auto peaksL=route.getProperty("channelPeaksL",juce::var());
            const auto peaksR=route.getProperty("channelPeaksR",juce::var());
            const auto inserts=route.getProperty("fxInstances",juce::var());
            if(controls.isArray())object->setProperty("channelControls",controls);
            if(peaksL.isArray())object->setProperty("channelPeaksL",peaksL);
            if(peaksR.isArray())object->setProperty("channelPeaksR",peaksR);
            if(inserts.isArray())object->setProperty("fxInstances",inserts);
            const auto topo=route.getProperty("acceptedRoute",juce::var());
            if(topo.isObject()) {
                const auto sends=topo.getProperty("sends",juce::var());
                const auto returns=topo.getProperty("auxes",juce::var());
                if(sends.isArray())object->setProperty("acceptedSends",sends);
                if(returns.isArray())object->setProperty("auxReturns",returns);
            }
            const auto monitorSends=route.getProperty("monitorSends",juce::var());
            if(monitorSends.isArray())
                object->setProperty("monitorSends",monitorSends);
            const auto main=route.getProperty("mainOutputs",juce::var());
            if(main.isArray())object->setProperty("mainOutputs",main);
            const auto monitor=route.getProperty("monitorOutputs",juce::var());
            if(monitor.isArray())object->setProperty("monitorOutputs",monitor);
        }
        return actual;
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


    // S1 production bridge: operate the ACTUALLY INSTALLED M4 graph through
    // LiveAudioEngine. A Legacy M4 lane is addressed by decimal "1".."10"
    // only for this explicit internal migration transport. This does NOT
    // claim that callbackOwnsChannelIds, CH11, GROUP or FX_RETURN exist.
    // The same native audio owner, plugin editor and Show remain installed.
    template<class Engine,class OpenEditor>
    juce::var processProductionIntent(const juce::var& intent,
                                      const juce::var& acceptedSnapshot,
                                      Engine& engine,OpenEditor&& openEditor) {
        const auto request=intent.getProperty("requestId",{});
        const auto requestId=request.isString()?request.toString():juce::String{};
        const auto bytes=requestId.toStdString();
        const auto device=acceptedSnapshot.getProperty("device",{});
        const auto graph=acceptedSnapshot.getProperty("graph",{});
        const auto epoch=device.getProperty("epoch","0").toString();
        const auto oldRev=graph.getProperty("revision","0").toString();
        const auto oldRoster=graph.getProperty("rosterRevision","0").toString();
        const auto legacyRev=acceptedSnapshot.getProperty("legacyM4",{})
                                .getProperty("routingRevision","0").toString();
        juce::String error;
        bool accepted=false;
        if(!armix::integration::validConsoleRequestId(bytes))error="INVALID_REQUEST_ID";
        else if(completed_.find(bytes)!=completed_.end())error="DUPLICATE_REQUEST_ID";
        else {
            if(recentIds_.size()==maxRecentIds) {
                completed_.erase(recentIds_.front());
                recentIds_.pop_front();
            }
            recentIds_.push_back(bytes);
            completed_.insert(bytes);
            std::uint64_t ep=0,rev=0,roster=0,channelId=0,m4Revision=0;
            const auto target=intent.getProperty("targetChannelId",{});
            const auto payload=intent.getProperty("payload",{});
            const auto operation=intent.getProperty("operation",{}).toString();
            if(!decodeDecimal(intent.getProperty("deviceEpoch",{}),ep) ||
               !decodeDecimal(intent.getProperty("baseGraphRevision",{}),rev) ||
               !decodeDecimal(intent.getProperty("baseRosterRevision",{}),roster))
                error="INVALID_DECIMAL_WIRE";
            else if(intent.getProperty("deviceEpoch",{}).toString()!=epoch)
                error="STALE_DEVICE_EPOCH";
            else if(intent.getProperty("baseGraphRevision",{}).toString()!=oldRev ||
                    intent.getProperty("baseRosterRevision",{}).toString()!=oldRoster)
                error="STALE_GRAPH_REVISION";
            // M4's presentation ordinals are NOT universal ChannelIds.
            // A legacy-console intent requires an explicit independent
            // revision fence. Without it, reject rather than silently
            // alias the ChannelId graph's revision 0 to an M4 revision.
            else if(!decodeDecimal(intent.getProperty("legacyRoutingRevision",{}),
                                    m4Revision) ||
                    intent.getProperty("legacyRoutingRevision","").toString()!=legacyRev)
                error="STALE_LEGACY_ROUTING_REVISION";
            else if(!target.isString() || !decodeDecimal(target,channelId) ||
                    channelId<1 ||
                    (channelId>10 &&
                     !(operation=="SET_MONITOR_LEVEL"&&channelId<=14)))
                error="INVALID_LEGACY_CHANNEL_ID";
            else if(!engine.dryRoutingLabActive() ||
                    engine.dryRoutingLabEpoch()!=ep ||
                    engine.dryRoutingLabRevision()!=m4Revision)
                error="PRODUCTION_GRAPH_NOT_ADMITTED";
            else if(!payload.isObject())error="INVALID_PAYLOAD";
            else {
                const int lane=static_cast<int>(channelId-1);
                if(operation=="SET_SOURCE") {
                    const auto id=payload.getProperty("portId",{}).toString();
                    int left=-1;
                    if(parsePortId(id,"in:",left)) {
                        const auto rightId=payload.getProperty("rightPortId",{}).toString();
                        int right=-1;
                        const auto format=payload.getProperty("sourceFormat","MONO").toString();
                        if(format=="STEREO" && !parsePortId(rightId,"in:",right))
                            error="INVALID_STEREO_RIGHT_PIN";
                        else if(format!="MONO"&&format!="STEREO")
                            error="INVALID_SOURCE_FORMAT";
                        else error=engine.switchDryRoutingLabSource(
                            lane,ep,m4Revision,format,left,right);
                    } else error="INVALID_PHYSICAL_INPUT_PORT";
                    accepted=error.isEmpty();
                } else if(operation=="SET_FADER"||operation=="SET_PAN"||
                          operation=="SET_MUTE"||operation=="SET_SOLO") {
                    const bool binary=operation=="SET_MUTE"||operation=="SET_SOLO";
                    const auto value=payload.getProperty(binary?"value":"valueDb",{});
                    juce::String key;
                    float control=0.f;
                    if(binary) {
                        const auto b=payload.getProperty("value",{});
                        const auto on=payload.getProperty("enabled",b);
                        if(!on.isBool())error="INVALID_BOOLEAN_CONTROL";
                        else control=static_cast<bool>(on)?1.f:0.f;
                    } else {
                        const auto number=payload.getProperty(
                            operation=="SET_FADER"?"valueDb":"value",{});
                        if(!number.isInt()&&!number.isDouble())
                            error="INVALID_NUMERIC_CONTROL";
                        else {
                            const double v=static_cast<double>(number);
                            if(!std::isfinite(v))error="NON_FINITE_CONTROL";
                            else control=static_cast<float>(v);
                        }
                    }
                    if(error.isEmpty()) {
                        if(operation=="SET_FADER")key="faderDb";
                        else if(operation=="SET_PAN")key="pan";
                        else if(operation=="SET_MUTE")key="mute";
                        else key="solo";
                        accepted=engine.setDryRoutingLabParameter(
                            lane,ep,m4Revision,key,control);
                        if(!accepted)error="NATIVE_CONTROL_REJECTED";
                    }
                } else if(operation=="CONNECT_AUX_SEND") {
                    const auto bus=payload.getProperty("auxIndex",{});
                    const auto tapName=payload.getProperty("tap","POST").toString();
                    const auto gain=payload.getProperty("value",{});
                    const auto pan=payload.getProperty("pan",juce::var(0.0));
                    if(!bus.isInt()||static_cast<int>(bus)<0||
                       static_cast<int>(bus)>=armix::mixer::auxBusCount)
                        error="INVALID_AUX_RETURN";
                    else if(tapName!="PRE"&&tapName!="POST")
                        error="INVALID_SEND_TAP";
                    else if((!gain.isInt()&&!gain.isDouble())||
                            (!pan.isInt()&&!pan.isDouble()))
                        error="INVALID_SEND_GAIN_OR_PAN";
                    else {
                        const double level=static_cast<double>(gain);
                        const double balance=static_cast<double>(pan);
                        if(!std::isfinite(level)||level<0.||level>1.||
                           !std::isfinite(balance)||balance< -1.||balance>1.)
                            error="INVALID_SEND_GAIN_OR_PAN";
                        else {
                            error=engine.connectDryRoutingLabAuxSend(
                                lane,static_cast<int>(bus),ep,m4Revision,
                                tapName=="PRE"?armix::mixer::SendTap::preFader:
                                               armix::mixer::SendTap::postFader,
                                static_cast<float>(level),static_cast<float>(balance));
                            accepted=error.isEmpty();
                        }
                    }
                } else if(operation=="SET_MONITOR_LEVEL") {
                    const auto raw=payload.getProperty("value",{});
                    if(!raw.isInt()&&!raw.isDouble())
                        error="INVALID_MONITOR_LEVEL";
                    else {
                        const double gain=static_cast<double>(raw);
                        if(!std::isfinite(gain)||gain<0.||gain>1.)
                            error="INVALID_MONITOR_LEVEL";
                        else {
                            accepted=engine.setDryRoutingLabMonitorSendGain(
                                lane,ep,m4Revision,static_cast<float>(gain));
                            if(!accepted)error="MONITOR_BUS_NOT_ADMITTED";
                        }
                    }
                } else if(operation=="SET_SEND_LEVEL") {
                    // A SEND level is native realtime PCM, NOT a new graph
                    // edge. Existing AUX1..4 are independent FX return buses.
                    const auto aux=payload.getProperty("auxIndex",{});
                    const auto tapName=payload.getProperty("tap","POST").toString();
                    const auto raw=payload.getProperty("value",{});
                    if(!aux.isInt()||static_cast<int>(aux)<0||
                       static_cast<int>(aux)>=armix::mixer::auxBusCount)
                        error="INVALID_AUX_RETURN";
                    else if(tapName!="PRE"&&tapName!="POST")
                        error="INVALID_SEND_TAP";
                    else if(!raw.isInt()&&!raw.isDouble())
                        error="INVALID_SEND_LEVEL";
                    else {
                        const double value=static_cast<double>(raw);
                        if(!std::isfinite(value)||value<0.||value>1.)
                            error="INVALID_SEND_LEVEL";
                        else {
                            const auto tap=tapName=="PRE"?
                                armix::mixer::SendTap::preFader:
                                armix::mixer::SendTap::postFader;
                            accepted=engine.setDryRoutingLabAuxSendGain(
                                lane,static_cast<int>(aux),ep,m4Revision,
                                static_cast<float>(value),tap);
                            if(!accepted)error="NATIVE_SEND_NOT_ADMITTED";
                        }
                    }
                } else if(operation=="SET_PROCESSING_FORMAT"||
                          operation=="SET_PRIMARY_OUT") {
                    const auto nativeValue=payload.getProperty(
                        operation=="SET_PRIMARY_OUT"?"destinationId":"format",{})
                        .toString();
                    error=engine.repatchDryRoutingLabChannel(
                        lane,ep,m4Revision,operation,nativeValue);
                    accepted=error.isEmpty();
                } else if(operation=="SET_INSERT_BYPASS"||
                          operation=="OPEN_INSERT_EDITOR") {
                    // The token must match an actually prepared vendor
                    // AudioPluginInstance in the current accepted M4 graph.
                    // Never invent a GUI insert, instantiate, clone or scan
                    // VST3 while changing BYPASS / opening an editor.
                    const auto token=payload.getProperty("insertId","").toString();
                    const auto statuses=engine.status().getProperty("dryRoutingLab",{});
                    const auto fx=statuses.getProperty("fxInstances",{});
                    const auto* lanes=fx.getArray();
                    int foundSlot=-1;
                    if(token.isNotEmpty() && lanes && lane<lanes->size()) {
                        const auto bank=lanes->getReference(lane);
                        if(const auto* slots=bank.getArray())
                            for(int k=0;k<slots->size();++k)
                                if(slots->getReference(k).getProperty("instanceId","")
                                        .toString()==token) {
                                    if(foundSlot>=0){foundSlot=-1;break;}
                                    foundSlot=k;
                                }
                    }
                    if(foundSlot<0)error="UNKNOWN_NATIVE_INSERT_INSTANCE";
                    else if(operation=="SET_INSERT_BYPASS") {
                        const auto value=payload.getProperty("bypassed",{});
                        if(!value.isBool())error="INVALID_BYPASS_VALUE";
                        else {
                            accepted=engine.setDryRoutingLabFxBypass(
                                lane,foundSlot,ep,m4Revision,token,static_cast<bool>(value));
                            if(!accepted)error="NATIVE_INSERT_BYPASS_REJECTED";
                        }
                    } else {
                        error=openEditor(lane,foundSlot,ep,m4Revision,token);
                        accepted=error.isEmpty();
                    }
                } else error="UNSUPPORTED_UNIVERSAL_OPERATION";
            }
        }
        const auto after=armix::console::LiveConsoleBridgeV6::snapshot(engine.status());
        const auto afterGraph=after.getProperty("graph",{});
        PuzzleV8Ack ack;
        ack.requestId=bytes;
        ack.status=accepted?"ACCEPTED":"REJECTED";
        ack.deviceEpoch=after.getProperty("device",{}).getProperty("epoch",epoch).toString().toStdString();
        ack.graphRevision=afterGraph.getProperty("revision",oldRev).toString().toStdString();
        ack.rosterRevision=afterGraph.getProperty("rosterRevision",oldRoster).toString().toStdString();
        ack.errorCode=error.toStdString();
        const auto json=PuzzleV8Wire::ack(ack);
        return juce::JSON::parse(juce::String::fromUTF8(json.c_str()));
    }

    static bool parsePortId(const juce::String& wire,const char* prefix,int& physical) {
        if(!wire.startsWith(prefix))return false;
        const auto text=wire.substring(static_cast<int>(std::char_traits<char>::length(prefix)));
        std::uint64_t value=0;
        if(!decodeDecimal(juce::var(text),value)||value>255)return false;
        physical=static_cast<int>(value);
        return true;
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
