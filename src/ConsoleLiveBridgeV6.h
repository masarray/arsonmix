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
#include <span>
#include <vector>

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
        // The installed ABI is still M4 ordinals, but the single JUCE
        // callback may now select the real native ChannelId PCM renderer
        // with original CH1/Orbit lease after accepted admission. Neither
        // physical mode implies universal CH11/GROUP/WIRE ownership.
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
            // The prepared/unpatched native M4 graph is a real silent route.
            // It allows direct IN gesture to activate the FIRST source.
            // This does not set productionCallbackLive or fake PCM.
            const auto configured=route.getProperty("acceptedRoute",juce::var());
            const auto prepared=route.getProperty("preparedM4Route",juce::var());
            const auto topo=configured.isObject()?configured:prepared;
            if(topo.isObject()) {
                const auto sends=topo.getProperty("sends",juce::var());
                const auto returns=topo.getProperty("auxes",juce::var());
                if(sends.isArray())object->setProperty("acceptedSends",sends);
                if(returns.isArray())object->setProperty("auxReturns",returns);
                // Legacy M4 physical inputs/output membership are CONTROL
                // readbacks from the actually admitted 10-strip route. Never
                // infer them from presentation order or a UI draft.
                const auto channels=topo.getProperty("channels",juce::var());
                if(connected && w.legacyActive &&
                   (w.legacyRouteAccepted ||
                       (static_cast<bool>(route.getProperty("ready",false)) &&
                        prepared.isObject())) &&
                   !static_cast<bool>(route.getProperty("topologyFault",false)) &&
                   channels.isArray() &&
                   channels.getArray()->size()==armix::mixer::stripCount) {
                    object->setProperty("acceptedChannels",channels);
                    object->setProperty("controlPlane","LEGACY_M4_ORDINAL");
                    // Real accepted PCM provenance for FAT/GUI diagnostics.
                    // Read from LiveAudioEngine's installed callback, not
                    // guessed browser roles. This does NOT enable universal
                    // supportedOps, ChannelIds, or graph revision.
                    const bool native=static_cast<bool>(route.getProperty(
                        "nativeChannelIdPcmActive",false));
                    object->setProperty("pcmRenderer",native
                        ?"CHANNEL_ID_10CH4AUX":"M4_COMPAT");
                    object->setProperty("originalCh1GraphLeased",
                        native && static_cast<bool>(route.getProperty(
                            "originalCh1GraphLeased",false)));
                    // Separate scoped native bus transport, not universal
                    // v8 graph capabilities or synthetic M4 strip ordinals.
                    const auto bus=route.getProperty("nativeBus",juce::var());
                    if(native && bus.isObject()) {
                        object->setProperty("nativeBus",bus);
                    }
                    // Separate namespace: these operations mutate the one
                    // INSTALLED M4 JUCE PCM graph. They are NEVER universal
                    // graph.capabilities.supportedOps or ChannelIds.
                    juce::Array<juce::var> allowed;
                    for(const char* op: {"SET_SOURCE","SET_PROCESSING_FORMAT",
                         "SET_PRIMARY_OUT","SET_FADER","SET_PAN","SET_MUTE","SET_SOLO"})
                        allowed.add(juce::var(juce::String(op)));
                    if(returns.isArray())allowed.add(juce::var("SET_AUX_FADER"));
                    if(sends.isArray()&&!sends.getArray()->isEmpty())
                        allowed.add(juce::var("SET_SEND_LEVEL"));
                    if(inserts.isArray()) {
                        bool hasNativeFx=false;
                        for(const auto& rack:*inserts.getArray())
                            if(rack.isArray()&&!rack.getArray()->isEmpty()) {
                                hasNativeFx=true;break;
                            }
                        if(hasNativeFx) {
                            allowed.add(juce::var("SET_INSERT_BYPASS"));
                            allowed.add(juce::var("OPEN_INSERT_EDITOR"));
                        }
                    }
                    object->setProperty("supportedOps",juce::var(allowed));
                }
            }
            const auto monitorSends=route.getProperty("monitorSends",juce::var());
            if(monitorSends.isArray())
                object->setProperty("monitorSends",monitorSends);
            const auto main=route.getProperty("mainOutputs",juce::var());
            if(main.isArray())object->setProperty("mainOutputs",main);
            const auto monitor=route.getProperty("monitorOutputs",juce::var());
            if(monitor.isArray()) {
                object->setProperty("monitorOutputs",monitor);
                if(object->hasProperty("supportedOps") &&
                   monitor.getArray()->size()==2) {
                    auto operations=object->getProperty("supportedOps");
                    if(auto* list=operations.getArray()) {
                        list->add(juce::var("SET_MONITOR_LEVEL"));
                        object->setProperty("supportedOps",operations);
                    }
                }
            }
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
                     !((operation=="SET_MONITOR_LEVEL"||
                        operation=="SET_AUX_FADER")&&channelId<=14)))
                error="INVALID_LEGACY_CHANNEL_ID";
            // Never trust a guessed M4 ordinal or synthetic editor status.
            // Native accepted hardware, actual route rows and the current
            // topology-fault bit must have produced an explicit M4 authority
            // envelope in this very same status snapshot.
            else if(acceptedSnapshot.getProperty("legacyM4",{})
                    .getProperty("controlPlane","").toString()!="LEGACY_M4_ORDINAL" ||
                    !engine.dryRoutingLabActive() ||
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
                } else if(operation=="SET_AUX_FADER") {
                    // Native AUX return fader AFTER its real VST3 rack.
                    // 11..14 are explicit legacy M4 auxiliary ordinals;
                    // never pretend these are stable universal ChannelIds.
                    if(channelId<11||channelId>14)
                        error="INVALID_LEGACY_AUX_CHANNEL";
                    else {
                        const auto raw=payload.getProperty("value",{});
                        if(!raw.isInt()&&!raw.isDouble())
                            error="INVALID_AUX_FADER";
                        else {
                            const double gain=static_cast<double>(raw);
                            if(!std::isfinite(gain)||gain<0.||gain>4.)
                                error="INVALID_AUX_FADER";
                            else {
                                accepted=engine.setDryRoutingLabAuxBusGain(
                                    static_cast<int>(channelId-11),ep,
                                    m4Revision,static_cast<float>(gain));
                                if(!accepted)error="AUX_RETURN_NOT_ADMITTED";
                            }
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


    // E3 scoped native bus control transport. The installed callback already
    // owns this stable ChannelId graph; output is DISARMED for structural
    // changes, and readback comes from the newly accepted PCM publisher.
    // This is deliberately a SEPARATE legacyM4.nativeBus namespace rather
    // than claiming that universal CH11/WIRE/SHOW is available.
    template<class Engine,class NativeFxAction>
    juce::var processNativeBusIntent(const juce::var& intent,
                                     const juce::var& snapshot,
                                     Engine& engine,bool ioBusy,
                                     NativeFxAction&& onNativeFx) {
        const auto req=intent.getProperty("requestId",juce::var());
        const auto request=req.isString()?req.toString():juce::String();
        const auto key=request.toStdString();
        const auto device=snapshot.getProperty("device",juce::var());
        const auto graph=snapshot.getProperty("graph",juce::var());
        const auto legacy=snapshot.getProperty("legacyM4",juce::var());
        const auto bus=legacy.getProperty("nativeBus",juce::var());
        const auto epoch=device.getProperty("epoch","0").toString();
        juce::String error;
        bool applied=false;
        std::uint64_t nativeRev=0,rosterRev=0,requestedEpoch=0,legacyRevision=0;
        armix::mixer::ChannelId created{};
        if(!armix::integration::validConsoleRequestId(key))
            error="INVALID_REQUEST_ID";
        else if(completed_.find(key)!=completed_.end())
            error="DUPLICATE_REQUEST_ID";
        else {
            if(recentIds_.size()==maxRecentIds) {
                completed_.erase(recentIds_.front());recentIds_.pop_front();
            }
            recentIds_.push_back(key);completed_.insert(key);
            const auto operation=intent.getProperty("operation","").toString();
            const auto payload=intent.getProperty("payload",juce::var());
            if(ioBusy)error="NATIVE_SHOW_OR_BACKUP_BUSY";
            else if(!decodeDecimal(intent.getProperty("deviceEpoch",{}),requestedEpoch) ||
               !decodeDecimal(intent.getProperty("legacyRoutingRevision",{}),legacyRevision) ||
               !decodeDecimal(intent.getProperty("nativeGraphRevision",{}),nativeRev) ||
               !decodeDecimal(intent.getProperty("nativeRosterRevision",{}),rosterRev) ||
               intent.getProperty("deviceEpoch","").toString()!=epoch ||
               intent.getProperty("legacyRoutingRevision","").toString()!=
                    legacy.getProperty("routingRevision","").toString())
                error="STALE_NATIVE_BUS_DEVICE_OR_ROUTE";
            else if(!bus.isObject() ||
                    bus.getProperty("graphRevision","").toString()!=
                        intent.getProperty("nativeGraphRevision","").toString() ||
                    bus.getProperty("rosterRevision","").toString()!=
                        intent.getProperty("nativeRosterRevision","").toString() ||
                    legacy.getProperty("controlPlane","").toString()!="LEGACY_M4_ORDINAL")
                error="NATIVE_BUS_GRAPH_NOT_ADMITTED";
            else if(!payload.isObject())error="INVALID_NATIVE_BUS_PAYLOAD";
            else if(operation=="NATIVE_CREATE_CHANNEL" ||
                    operation=="NATIVE_PATCH_CHANNEL") {
                if(operation=="NATIVE_CREATE_CHANNEL") {
                    const auto name=payload.getProperty("name","").toString();
                    const auto format=payload.getProperty("format","MONO").toString();
                    if(name.isEmpty()||name.length()>64||
                       (format!="MONO"&&format!="STEREO"))
                        error="INVALID_NATIVE_CHANNEL_REQUEST";
                    else {
                        const auto out=engine.createNativeInputChannel(
                            requestedEpoch,nativeRev,rosterRev,name.toStdString(),
                            format=="STEREO"?armix::mixer::ProcessingFormat::stereo:
                                             armix::mixer::ProcessingFormat::mono);
                        applied=out.accepted;
                        if(!applied)error=out.error;
                        else {created=out.created;nativeRev=out.pcmRevision;
                              rosterRev=out.rosterRevision;}
                    }
                }else{
                    std::uint64_t id=0;
                    const auto left=payload.getProperty("physicalLeft",juce::var());
                    const auto right=payload.getProperty("physicalRight",juce::var(-1));
                    const auto width=payload.getProperty("sourceFormat","MONO").toString();
                    if(!decodeDecimal(intent.getProperty("targetChannelId",{}),id)||
                       id==0||!left.isInt()||!right.isInt()||
                       (width!="MONO"&&width!="STEREO"))
                        error="INVALID_NATIVE_SOURCE_REQUEST";
                    else {
                        const auto out=engine.patchNativeInputSource(
                            requestedEpoch,nativeRev,rosterRev,
                            armix::mixer::ChannelId{id},static_cast<int>(left),
                            static_cast<int>(right),width=="STEREO");
                        applied=out.accepted;
                        if(!applied)error=out.error;
                        else {nativeRev=out.pcmRevision;
                              rosterRev=out.rosterRevision;}
                    }
                }
            }
            else if(operation=="NATIVE_LOAD_INSERT" ||
                    operation=="NATIVE_SET_INSERT_BYPASS" ||
                    operation=="NATIVE_OPEN_INSERT_EDITOR") {
                std::uint64_t target=0;
                if(!decodeDecimal(intent.getProperty("targetChannelId",{}),target) ||
                   !target)error="INVALID_NATIVE_VST3_TARGET_ID";
                else {
                    // Native catalog ownership and VST3 instance preparation
                    // happen exclusively on the JUCE control thread.
                    const auto response=onNativeFx(
                        operation,armix::mixer::ChannelId{target},payload,
                        requestedEpoch,nativeRev,rosterRev);
                    applied=response.accepted;
                    if(!applied)error=response.error;
                    else {nativeRev=response.pcmRevision;
                          rosterRev=response.rosterRevision;}
                }
            }
            else if(operation=="NATIVE_SET_CHANNEL_MIX"||
                    operation=="NATIVE_SET_CHANNEL_MUTE_SOLO"){
                std::uint64_t id=0;
                if(!decodeDecimal(intent.getProperty("targetChannelId",{}),id)||!id)
                    error="INVALID_DYNAMIC_INPUT_ID";
                else if(operation=="NATIVE_SET_CHANNEL_MIX"){
                    const auto g=payload.getProperty("gain",juce::var());
                    const auto p=payload.getProperty("pan",juce::var());
                    if(!(g.isDouble()||g.isInt())||
                       !(p.isDouble()||p.isInt()))
                        error="INVALID_DYNAMIC_MIX_VALUE";
                    else {
                        const auto gain=static_cast<double>(g);
                        const auto pan=static_cast<double>(p);
                        if(!std::isfinite(gain)||gain<0.||gain>4.||
                           !std::isfinite(pan)||pan< -1.||pan>1.)
                            error="INVALID_DYNAMIC_MIX_VALUE";
                        else {
                            applied=engine.setNativeInputMix(
                                requestedEpoch,nativeRev,
                                armix::mixer::ChannelId{id},
                                static_cast<float>(gain),static_cast<float>(pan));
                            if(!applied)error="DYNAMIC_INPUT_MIX_REJECTED";
                        }
                    }
                }else{
                    const auto m=payload.getProperty("mute",juce::var());
                    const auto s=payload.getProperty("solo",juce::var());
                    if(!m.isBool()||!s.isBool())
                        error="INVALID_DYNAMIC_MUTE_SOLO";
                    else {
                        applied=engine.setNativeInputMuteSolo(
                            requestedEpoch,nativeRev,
                            armix::mixer::ChannelId{id},
                            static_cast<bool>(m),static_cast<bool>(s));
                        if(!applied)error="DYNAMIC_INPUT_MUTE_SOLO_REJECTED";
                    }
                }
            }
            else if(operation=="NATIVE_SET_BUS_MIX") {
                std::uint64_t target=0;
                const auto rawGain=payload.getProperty("gain",juce::var());
                const auto rawPan=payload.getProperty("pan",juce::var());
                if(!decodeDecimal(intent.getProperty("targetChannelId",{}),target) ||
                   target==0 ||
                   !(rawGain.isInt()||rawGain.isDouble()) ||
                   !(rawPan.isInt()||rawPan.isDouble()))
                    error="INVALID_NATIVE_BUS_MIX";
                else {
                    const auto gain=static_cast<double>(rawGain);
                    const auto pan=static_cast<double>(rawPan);
                    if(!std::isfinite(gain)||gain<0.||gain>4. ||
                       !std::isfinite(pan)||pan< -1.||pan>1.)
                        error="INVALID_NATIVE_BUS_MIX";
                    else {
                        applied=engine.setNativeBusMix(
                            requestedEpoch,nativeRev,
                            armix::mixer::ChannelId{target},
                            static_cast<float>(gain),static_cast<float>(pan));
                        if(!applied)error="NATIVE_BUS_MIX_REJECTED";
                    }
                }
            }
            else if(operation!="NATIVE_CREATE_GROUP_SELECTED" &&
                    operation!="NATIVE_CREATE_FX_RETURN_SELECTED" &&
                    operation!="NATIVE_REMOVE_BUS")
                error="UNSUPPORTED_NATIVE_BUS_OPERATION";
            else {
                using namespace armix::mixer;
                std::vector<ChannelId> selected;
                ChannelId remove{};
                auto role=ChannelRole::group;
                juce::String name;
                float gain=.25f;
                if(operation=="NATIVE_REMOVE_BUS") {
                    std::uint64_t id=0;
                    if(!decodeDecimal(intent.getProperty("targetChannelId",{}),id) ||
                       id==0)error="INVALID_NATIVE_BUS_ID";
                    else remove={id};
                } else {
                    role=operation=="NATIVE_CREATE_GROUP_SELECTED"
                        ?ChannelRole::group:ChannelRole::fxReturn;
                    name=payload.getProperty("name","").toString();
                    const auto raw=payload.getProperty("selectedChannelIds",juce::var());
                    const auto* ids=raw.getArray();
                    const auto g=payload.getProperty("sendGain",juce::var(.25));
                    const double value=static_cast<double>(g);
                    if(name.isEmpty()||name.length()>64 ||
                       !ids||ids->isEmpty()||ids->size()>64 ||
                       !(g.isDouble()||g.isInt()) ||
                       !std::isfinite(value)||value<0.||value>4.) {
                        error="INVALID_NATIVE_BUS_SELECTION";
                    } else {
                        gain=static_cast<float>(value);
                        selected.reserve(static_cast<std::size_t>(ids->size()));
                        for(const auto& item:*ids) {
                            std::uint64_t id=0;
                            if(!decodeDecimal(item,id)||id==0) {
                                error="INVALID_NATIVE_BUS_SOURCE_ID";break;
                            }
                            selected.push_back({id});
                        }
                    }
                }
                if(error.isEmpty()) {
                    auto result=engine.editNativeBus(
                        requestedEpoch,nativeRev,rosterRev,role,
                        std::span<const ChannelId>(selected),name.toStdString(),
                        gain,remove);
                    applied=result.accepted;
                    if(!applied)error=result.error;
                    else {
                        created=result.created;
                        nativeRev=result.pcmRevision;
                        rosterRev=result.rosterRevision;
                    }
                }
            }
        }
        // Preserve universal revision 0. Provide native namespace revision
        // and created stable ID ONLY after the actual hardware PCM ACK.
        PuzzleV8Ack ack;
        ack.requestId=key;
        ack.status=applied?"ACCEPTED":"REJECTED";
        ack.deviceEpoch=epoch.toStdString();
        ack.graphRevision=graph.getProperty("revision","0").toString().toStdString();
        ack.rosterRevision=graph.getProperty("rosterRevision","0").toString().toStdString();
        ack.errorCode=error.toStdString();
        const auto json=PuzzleV8Wire::ack(ack);
        auto wire=juce::JSON::parse(juce::String::fromUTF8(json.c_str()));
        if(applied)if(auto* object=wire.getDynamicObject()) {
            object->setProperty("nativeGraphRevision",
                juce::String(std::to_string(nativeRev)));
            object->setProperty("nativeRosterRevision",
                juce::String(std::to_string(rosterRev)));
            object->setProperty("createdChannelId",
                juce::String(std::to_string(created.value)));
        }
        return wire;
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
