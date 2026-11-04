// SPDX-License-Identifier: AGPL-3.0-only
// M3-B CONTROL-thread projection regression. No mock PCM is installed.
#include "../integration/ApprovedNativeV8Projection.h"
#include "../integration/ApprovedNativeControlIntent.h"
#include "../integration/ApprovedNativeRuntimeSnapshot.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
using namespace armix::console;

static juce::var parse(const char* bytes) {
    auto json=juce::JSON::parse(juce::String::fromUTF8(bytes));
    assert(json.isObject());
    return json;
}
static juce::var canonicalSnapshot() {
    return parse(R"JSON({
      "schemaVersion":6,"productionCallbackLive":false,
      "device":{"epoch":"91","backend":"WASAPI","enabledInputs":[0,2],"enabledOutputs":[0,1]},
      "graph":{"revision":"0","rosterRevision":"0","channels":[],"edges":[],
        "capabilities":{"callbackOwnsChannelIds":false,"supportedOps":[]}},
      "telemetry":{"channelMeterRmsPeak":{},"rawCapturePresent":false}
    })JSON");
}
static juce::var realBusReadbackFixture() {
    // Fixtures carry the shape of actual accepted producer readback ONLY.
    // They can exercise serialization, but never establish callback authority.
    return parse(R"JSON({
      "graphRevision":"25","rosterRevision":"9",
      "channels":[
        {"id":"9007199254740993","name":"Vocal","role":"INPUT","format":"MONO",
         "primaryOutput":"18446744073709551615","fader":0.5,"pan":0.0,
         "mute":false,"solo":false,"inserts":[],
         "peakL":0.25,"peakR":0.25},
        {"id":"18446744073709551615","name":"MAIN","role":"MAIN","format":"STEREO",
         "primaryOutput":"0","fader":1.0,"pan":0.0,
         "mute":false,"solo":false,"inserts":[],
         "peakL":0.125,"peakR":0.125}
      ],
      "edges":[
        {"edgeId":"9223372036854775808","from":"9007199254740993",
         "to":"18446744073709551615","tap":"POST_FADER",
         "gainLinear":1.0,"primaryOutput":true}
      ]
    })JSON");
}
static void noMutationOnReject(juce::var snapshot,juce::var bus,bool admitted) {
    const auto before=juce::JSON::toString(snapshot,false);
    assert(!projectApprovedNativeV8(snapshot,bus,admitted));
    assert(juce::JSON::toString(snapshot,false)==before);
}

static void controlIntentTranslationCases(const juce::var& canonical,
                                          const juce::var& rawBus) {
    // Simulate an admitted native CONTROL ABI, not physical PCM. Real engine
    // MUST expose these capabilities only after CH1-10 and dynamic IDs work.
    auto accepted=juce::JSON::parse(juce::JSON::toString(canonical,false));
    auto bus=juce::JSON::parse(juce::JSON::toString(rawBus,false));
    juce::Array<juce::var> publicOps;
    for(const char* op:{"SET_FADER","SET_PAN","SET_MUTE","SET_SOLO"})
        publicOps.add(juce::String(op));
    accepted.getProperty("graph",{}).getProperty("capabilities",{})
        .getDynamicObject()->setProperty("supportedOps",juce::var(publicOps));
    juce::Array<juce::var> nativeOps;
    for(const char* op:{"NATIVE_SET_CHANNEL_MIX",
                        "NATIVE_SET_CHANNEL_MUTE_SOLO","NATIVE_SET_BUS_MIX"})
        nativeOps.add(juce::String(op));
    bus.getDynamicObject()->setProperty("supportedOps",juce::var(nativeOps));
    const auto source=juce::String("9007199254740993");
    const auto device=accepted.getProperty("device",{});
    const auto graph=accepted.getProperty("graph",{});
    auto makeIntent=[&](const char* operation,const juce::var& value,
                        const juce::String& id)->juce::var {
        auto payload=std::make_unique<juce::DynamicObject>();
        payload->setProperty(juce::String(operation)=="SET_FADER"?
                             "valueDb":"value",value);
        auto request=std::make_unique<juce::DynamicObject>();
        request->setProperty("requestId","m3b-controlled-intent-001");
        request->setProperty("deviceEpoch",device.getProperty("epoch",{}));
        request->setProperty("baseGraphRevision",graph.getProperty("revision",{}));
        request->setProperty("baseRosterRevision",graph.getProperty("rosterRevision",{}));
        request->setProperty("operation",operation);
        request->setProperty("targetChannelId",id);
        request->setProperty("payload",juce::var(payload.release()));
        return juce::var(request.release());
    };
    const auto fader=makeIntent("SET_FADER",juce::var(-12.0),source);
    const auto translated=translateApprovedNativeControl(fader,accepted,bus);
    assert(translated.ok());
    assert(translated.translated.getProperty("operation","").toString()
           =="NATIVE_SET_CHANNEL_MIX");
    assert(translated.translated.getProperty("targetChannelId","").toString()
           ==source);
    assert(translated.translated.getProperty("requestId","").toString()
           =="m3b-controlled-intent-001");
    const auto gain=translated.translated.getProperty("payload",{})
                                     .getProperty("gain",{});
    const auto keptPan=translated.translated.getProperty("payload",{})
                                        .getProperty("pan",{});
    assert(std::abs(static_cast<double>(gain)-std::pow(10.,-12./20.))<0.000001);
    assert(std::abs(static_cast<double>(keptPan))<0.000001);
    const auto pan=translateApprovedNativeControl(
        makeIntent("SET_PAN",juce::var(0.75),source),accepted,bus);
    assert(pan.ok());
    assert(std::abs(static_cast<double>(pan.translated.getProperty("payload",{})
                   .getProperty("gain",{}))-.5)<0.000001);
    assert(std::abs(static_cast<double>(pan.translated.getProperty("payload",{})
                   .getProperty("pan",{}))-.75)<0.000001);
    const auto muted=translateApprovedNativeControl(
        makeIntent("SET_MUTE",juce::var(true),source),accepted,bus);
    assert(muted.ok());
    assert(muted.translated.getProperty("operation","").toString()
           =="NATIVE_SET_CHANNEL_MUTE_SOLO");
    assert(static_cast<bool>(muted.translated.getProperty("payload",{})
                                    .getProperty("mute",false)));
    assert(!static_cast<bool>(muted.translated.getProperty("payload",{})
                                     .getProperty("solo",true)));
    const auto solo=translateApprovedNativeControl(
        makeIntent("SET_SOLO",juce::var(true),source),accepted,bus);
    assert(solo.ok());
    assert(!static_cast<bool>(solo.translated.getProperty("payload",{})
                                    .getProperty("mute",true)));
    assert(static_cast<bool>(solo.translated.getProperty("payload",{})
                                    .getProperty("solo",false)));
    // Nothing in the original request or accepted readback was edited.
    assert(fader.getProperty("operation","").toString()=="SET_FADER");
    assert(graph.getProperty("channels",{})[0].getProperty("controls",{})
                .getProperty("faderDb",0.0)==canonical.getProperty("graph",{})
                .getProperty("channels",{})[0].getProperty("controls",{})
                .getProperty("faderDb",0.0));
    const auto before=juce::JSON::toString(accepted,false);
    auto fail=[&](const juce::var& request,const juce::var& pub,
                  const juce::var& native)->juce::String {
        const auto oldPub=juce::JSON::toString(pub,false);
        const auto oldBus=juce::JSON::toString(native,false);
        const auto result=translateApprovedNativeControl(request,pub,native);
        assert(!result.ok());
        assert(!result.translated.isObject());
        assert(juce::JSON::toString(pub,false)==oldPub);
        assert(juce::JSON::toString(native,false)==oldBus);
        return result.error;
    };
    assert(fail(fader,canonicalSnapshot(),bus)=="CHANNELID_CALLBACK_NOT_ADMITTED");
    assert(fail(makeIntent("SET_FADER",juce::var(-100.),source),
                accepted,bus)=="INVALID_FADER_VALUE");
    assert(fail(makeIntent("SET_PAN",juce::var(2.),source),
                accepted,bus)=="INVALID_PAN_VALUE");
    assert(fail(makeIntent("SET_MUTE",juce::var(1),source),
                accepted,bus)=="INVALID_MUTE_SOLO_VALUE");
    assert(fail(makeIntent("SET_FADER",juce::var(-12.),
                           "9007199254740994"),accepted,bus)
           =="UNKNOWN_OR_STALE_CHANNELID");
    assert(fail(makeIntent("SET_MUTE",juce::var(true),
                           "18446744073709551615"),accepted,bus)
           =="CHANNEL_ROLE_NOT_MUTABLE");
    const auto stale=makeIntent("SET_PAN",juce::var(0.5),source);
    stale.getDynamicObject()->setProperty("baseGraphRevision","24");
    assert(fail(stale,accepted,bus)=="STALE_NATIVE_CONTROL_REVISION");
    const auto fakeOp=makeIntent("SET_FADER",juce::var(-12.),source);
    fakeOp.getDynamicObject()->setProperty("operation","NATIVE_SET_CHANNEL_MIX");
    assert(fail(fakeOp,accepted,bus)=="UNSUPPORTED_APPROVED_CONTROL");
    auto unadvertised=juce::JSON::parse(juce::JSON::toString(accepted,false));
    unadvertised.getProperty("graph",{}).getProperty("capabilities",{})
        .getDynamicObject()->setProperty("supportedOps",
                                         juce::var(juce::Array<juce::var>{}));
    assert(fail(fader,unadvertised,bus)=="PUBLIC_CONTROL_NOT_ADMITTED");
    auto noLease=juce::JSON::parse(juce::JSON::toString(bus,false));
    noLease.getDynamicObject()->setProperty("supportedOps",
                                            juce::var(juce::Array<juce::var>{}));
    assert(fail(fader,accepted,noLease)=="NATIVE_PCM_OPERATION_NOT_ADMITTED");
    auto staleReadback=juce::JSON::parse(juce::JSON::toString(bus,false));
    staleReadback.getProperty("channels",{})[0].getDynamicObject()
        ->setProperty("fader",0.25);
    assert(fail(fader,accepted,staleReadback)=="STALE_NATIVE_CONTROL_READBACK");
    auto wrongRole=juce::JSON::parse(juce::JSON::toString(bus,false));
    wrongRole.getProperty("channels",{})[0].getDynamicObject()
        ->setProperty("role","GROUP");
    assert(fail(fader,accepted,wrongRole)=="NATIVE_ROLE_MISMATCH");
    assert(juce::JSON::toString(accepted,false)==before);
}


static void installedCallbackAdmissionRegression(const juce::var& rawBus) {
    // This fixture is ONLY accepted native readback; not a hardware device.
    // It proves Main.cpp cannot publish a fake live graph from G0/M4 JSON.
    auto snap=canonicalSnapshot();
    auto dev=snap.getProperty("device",{});
    dev.getDynamicObject()->setProperty("sampleRate",48000.0);
    dev.getDynamicObject()->setProperty("bufferFrames",256);
    juce::Array<juce::var> inputs,outputs;
    inputs.add(0);outputs.add(0);outputs.add(1);
    dev.getDynamicObject()->setProperty("enabledInputs",juce::var(inputs));
    dev.getDynamicObject()->setProperty("enabledOutputs",juce::var(outputs));
    auto live=parse(R"JSON({"connected":true,"directRouteAuthorized":true,
      "adaptiveClockActive":false,"dryRoutingLab":{"ready":true,
      "hasAcceptedRoute":true,"topologyFault":false,
      "nativeChannelIdPcmActive":true,"epoch":91}})JSON");
    live.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("nativeBus",rawBus);
    auto fail=[&](const juce::var& candidate) {
        auto before=juce::JSON::toString(snap,false);
        assert(!projectInstalledNativeChannelIdGraph(snap,candidate));
        assert(juce::JSON::toString(snap,false)==before);
    };
    fail(parse(R"JSON({"dryRoutingLab":{"nativeBus":{}}})JSON"));
    auto deny=juce::JSON::parse(juce::JSON::toString(live,false));
    deny.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("nativeChannelIdPcmActive",false);
    fail(deny);
    deny=juce::JSON::parse(juce::JSON::toString(live,false));
    deny.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("topologyFault",true);
    fail(deny);
    deny=juce::JSON::parse(juce::JSON::toString(live,false));
    deny.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("epoch",92);
    fail(deny);
    deny=juce::JSON::parse(juce::JSON::toString(live,false));
    deny.getDynamicObject()->setProperty("directRouteAuthorized",false);
    fail(deny);
    deny=juce::JSON::parse(juce::JSON::toString(live,false));
    deny.getDynamicObject()->setProperty("adaptiveClockActive",true);
    fail(deny);
    deny=juce::JSON::parse(juce::JSON::toString(live,false));
    deny.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("nativeBus",juce::var());
    fail(deny);
    assert(synchronizeApprovedRouteDeviceEpoch(snap,live));
    assert(projectInstalledNativeChannelIdGraph(snap,live));
    // Real restart: lab routing epoch counts STOP+START, unlike M2
    // deviceStartCount. This G0-to-native authority switch must advance.
    auto disconnected=canonicalSnapshot();
    // Simulate the SAME actual driver pin masks after reconnect. A default
    // G0 no-device fixture would otherwise correctly refuse native admit.
    auto disconnectedDevice=disconnected.getProperty("device",{}).getDynamicObject();
    disconnectedDevice->setProperty("sampleRate",48000.0);
    disconnectedDevice->setProperty("bufferFrames",256);
    disconnectedDevice->setProperty("enabledInputs",juce::var(inputs));
    disconnectedDevice->setProperty("enabledOutputs",juce::var(outputs));
    auto dead=juce::JSON::parse(juce::JSON::toString(live,false));
    dead.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("nativeChannelIdPcmActive",false);
    dead.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("epoch",93);
    assert(synchronizeApprovedRouteDeviceEpoch(disconnected,dead));
    assert(disconnected.getProperty("device",{})
               .getProperty("epoch","").toString()=="93");
    assert(!projectInstalledNativeChannelIdGraph(disconnected,dead));
    const auto ghost=disconnected.getProperty("graph",{});
    assert(!static_cast<bool>(ghost.getProperty("capabilities",{})
                            .getProperty("callbackOwnsChannelIds",false)));
    auto restarted=juce::JSON::parse(juce::JSON::toString(live,false));
    restarted.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("epoch",93);
    assert(synchronizeApprovedRouteDeviceEpoch(disconnected,restarted));
    assert(projectInstalledNativeChannelIdGraph(disconnected,restarted));
    assert(disconnected.getProperty("graph",{})
                .getProperty("revision","").toString()=="25");
    // Never roll back to an older route generation after device reconnect.
    auto stale=juce::JSON::parse(juce::JSON::toString(restarted,false));
    stale.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("epoch",90);
    assert(!synchronizeApprovedRouteDeviceEpoch(disconnected,stale));
    assert(disconnected.getProperty("device",{})
               .getProperty("epoch","").toString()=="93");
    const auto graph=snap.getProperty("graph",{});
    assert(static_cast<bool>(graph.getProperty("capabilities",{})
                                 .getProperty("callbackOwnsChannelIds",false)));
    assert(graph.getProperty("channels",{}).getArray()->size()==2);
    assert(graph.getProperty("capabilities",{})
                 .getProperty("supportedOps",{}).getArray()->isEmpty());
    assert(!static_cast<bool>(snap.getProperty("productionCallbackLive",false)));
}

int main() {
    auto snap=canonicalSnapshot();
    auto bus=realBusReadbackFixture();
    // Merely having matching native-shaped JSON cannot enable fake LIVE.
    noMutationOnReject(snap,bus,false);
    assert(nativeV8Decimal(juce::String("18446744073709551615")));
    assert(!nativeV8Decimal(juce::String("18446744073709551616")));
    assert(!nativeV8Decimal(juce::String("9007199254740993x")));
    assert(!nativeV8Decimal(juce::String("0002")));
    assert(!nativeV8Decimal(juce::String("0")));

    assert(projectApprovedNativeV8(snap,bus,true));
    const auto graph=snap.getProperty("graph",{});
    const auto chans=graph.getProperty("channels",{});
    const auto wires=graph.getProperty("edges",{});
    assert(graph.getProperty("revision","").toString()=="25");
    assert(graph.getProperty("rosterRevision","").toString()=="9");
    assert(graph.getProperty("mainChannelId","").toString()=="18446744073709551615");
    assert(chans.getArray() && chans.getArray()->size()==2);
    assert(wires.getArray() && wires.getArray()->size()==1);
    assert(chans[0].getProperty("id","").toString()=="9007199254740993");
    assert(wires[0].getProperty("edgeId","").toString()=="9223372036854775808");
    assert(std::abs(static_cast<double>(
        chans[0].getProperty("controls",{}).getProperty("faderDb",0.0))
        +6.020599913279624)<0.00001);
    assert(static_cast<bool>(graph.getProperty("capabilities",{})
        .getProperty("callbackOwnsChannelIds",false)));
    // NOTE: structural ChannelId proof NEVER promotes universal product-live.
    assert(!static_cast<bool>(snap.getProperty("productionCallbackLive",false)));

    controlIntentTranslationCases(snap,bus);
    installedCallbackAdmissionRegression(bus);

    auto missingMain=realBusReadbackFixture();
    missingMain.getProperty("channels",{})[1].getDynamicObject()
        ->setProperty("role","GROUP");
    noMutationOnReject(canonicalSnapshot(),missingMain,true);

    auto badEdge=realBusReadbackFixture();
    badEdge.getProperty("edges",{})[0].getDynamicObject()
        ->setProperty("to","18446744073709551614");
    noMutationOnReject(canonicalSnapshot(),badEdge,true);

    auto badIdentifier=realBusReadbackFixture();
    badIdentifier.getProperty("channels",{})[0].getDynamicObject()
        ->setProperty("id","9007199254740993.0");
    noMutationOnReject(canonicalSnapshot(),badIdentifier,true);

    auto badGain=realBusReadbackFixture();
    badGain.getProperty("channels",{})[0].getDynamicObject()
        ->setProperty("fader",-0.1);
    noMutationOnReject(canonicalSnapshot(),badGain,true);

    auto duplicate=realBusReadbackFixture();
    duplicate.getProperty("channels",{})[1].getDynamicObject()
        ->setProperty("id","9007199254740993");
    noMutationOnReject(canonicalSnapshot(),duplicate,true);

    auto staleDevice=canonicalSnapshot();
    staleDevice.getProperty("device",{}).getDynamicObject()
        ->setProperty("epoch","0");
    noMutationOnReject(staleDevice,realBusReadbackFixture(),true);

    auto unavailableGraph=canonicalSnapshot();
    unavailableGraph.getDynamicObject()->removeProperty("graph");
    noMutationOnReject(unavailableGraph,realBusReadbackFixture(),true);
    return 0;
}
