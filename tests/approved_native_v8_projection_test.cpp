// SPDX-License-Identifier: AGPL-3.0-only
// M3-B CONTROL-thread projection regression. No mock PCM is installed.
#include "../integration/ApprovedNativeV8Projection.h"
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
