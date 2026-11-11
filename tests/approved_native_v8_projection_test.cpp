// SPDX-License-Identifier: AGPL-3.0-only
// M3-B CONTROL-thread projection regression. No mock PCM is installed.
#include "../integration/ApprovedNativeV8Projection.h"
#include "../src/ApprovedNativeV8Projection.h"
#include "../integration/ApprovedNativeControlIntent.h"
#include "../integration/ApprovedNativeRuntimeSnapshot.h"
#include "../integration/FirstSoundSignalEvidence.h"
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
using namespace armix::console;


// Compile-time and runtime proof that accepted v8 native intents route to
// the actual ChannelId setter rather than a fake WebView-only state change.
struct NativeControlProbe {
    std::uint64_t lastEpoch=0,lastRevision=0,lastId=0;
    float lastGain= -1.f,lastPan= -2.f;
    bool lastMute=false,lastSolo=false;
    bool setNativeInputMix(std::uint64_t e,std::uint64_t r,
                           armix::mixer::ChannelId id,float g,float p) {
        lastEpoch=e;lastRevision=r;lastId=id.value;lastGain=g;lastPan=p;
        return true;
    }
    bool setNativeBusMix(std::uint64_t e,std::uint64_t r,
                         armix::mixer::ChannelId id,float g,float p) {
        return setNativeInputMix(e,r,id,g,p);
    }
    bool setNativeInputMuteSolo(std::uint64_t e,std::uint64_t r,
                        armix::mixer::ChannelId id,bool m,bool s) {
        lastEpoch=e;lastRevision=r;lastId=id.value;
        lastMute=m;lastSolo=s;return true;
    }
    bool setNativeBusMuteSolo(std::uint64_t e,std::uint64_t r,
                              armix::mixer::ChannelId id,bool m,bool s) {
        return setNativeInputMuteSolo(e,r,id,m,s);
    }
};
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
static void nativeInsertCatalogInstanceProjectionRegression() {
    auto source=realBusReadbackFixture();
    auto item=std::make_unique<juce::DynamicObject>();
    item->setProperty("instanceId","vst3-instance-unique-7");
    item->setProperty("id","vendor.eq.catalog");
    item->setProperty("name","Vendor EQ");
    item->setProperty("bypassed",false);
    juce::Array<juce::var> inserts;
    inserts.add(juce::var(item.release()));
    source.getProperty("channels",{})[0].getDynamicObject()
        ->setProperty("inserts",juce::var(inserts));
    source.getProperty("channels",{})[0].getDynamicObject()
        ->setProperty("fxRackBypassed",true);
    auto snapshot=canonicalSnapshot();
    assert(projectApprovedNativeV8(snapshot,source,true));
    auto legacy=canonicalSnapshot();
    const auto beforeLegacy=juce::JSON::toString(legacy,true);
    assert(!projectApprovedNativeV8(legacy,source)); // shape alone grants no legacy admission
    assert(juce::JSON::toString(legacy,true)==beforeLegacy);
    juce::Array<juce::var> installedOps;
    installedOps.add("NATIVE_APPLY_WIRE_TRANSACTION");
    source.getDynamicObject()->setProperty("supportedOps",juce::var(installedOps));
    assert(projectApprovedNativeV8(legacy,source));
    assert(juce::JSON::toString(legacy,true)==juce::JSON::toString(snapshot,true));
    const auto live=snapshot.getProperty("graph",{})
        .getProperty("channels",{})[0].getProperty("inserts",{})[0];
    assert(live.getProperty("id","").toString()=="vst3-instance-unique-7");
    assert(live.getProperty("catalogId","").toString()=="vendor.eq.catalog");
    assert(!static_cast<bool>(live.getProperty("bypassed",true)));
    assert(static_cast<bool>(snapshot.getProperty("graph",{})
        .getProperty("channels",{})[0].getProperty("fxRackBypassed",false)));
    // A missing catalog token must not be projected as a verified processor.
    auto invalid=juce::JSON::parse(juce::JSON::toString(source,false));
    invalid.getProperty("channels",{})[0].getProperty("inserts",{})[0]
        .getDynamicObject()->removeProperty("id");
    auto untouched=canonicalSnapshot();
    const auto before=juce::JSON::toString(untouched,false);
    assert(!projectApprovedNativeV8(untouched,invalid,true));
    assert(juce::JSON::toString(untouched,false)==before);

    // M6: the native projection is the first fail-closed boundary for
    // unstable VST3 lease tokens. JS must never be asked to guess which
    // physical plugin/editor is owned by a duplicated instance identity.
    const auto rejectUnchanged=[](const juce::var& bus) {
        auto untouched=canonicalSnapshot();
        const auto before=juce::JSON::toString(untouched,false);
        assert(!projectApprovedNativeV8(untouched,bus,true));
        assert(juce::JSON::toString(untouched,false)==before);
    };
    auto duplicateAcrossChannels=juce::JSON::parse(
        juce::JSON::toString(source,false));
    duplicateAcrossChannels.getProperty("channels",{})[1].getDynamicObject()
        ->setProperty("inserts",
            duplicateAcrossChannels.getProperty("channels",{})[0]
                .getProperty("inserts",{}));
    rejectUnchanged(duplicateAcrossChannels);

    auto duplicateSameChannel=juce::JSON::parse(
        juce::JSON::toString(source,false));
    auto* existing=duplicateSameChannel.getProperty("channels",{})[0]
        .getProperty("inserts",{}).getArray();
    assert(existing && existing->size()==1);
    existing->add(juce::JSON::parse(juce::JSON::toString((*existing)[0],false)));
    rejectUnchanged(duplicateSameChannel);

    auto overfullRack=juce::JSON::parse(
        juce::JSON::toString(source,false));
    juce::Array<juce::var> nine;
    for(int i=0;i<9;++i) {
        auto fx=std::make_unique<juce::DynamicObject>();
        fx->setProperty("instanceId","unique-FX-"+juce::String(i+1));
        fx->setProperty("id","vendor.eq.catalog");
        fx->setProperty("name","Vendor EQ");
        fx->setProperty("bypassed",false);
        nine.add(juce::var(fx.release()));
    }
    overfullRack.getProperty("channels",{})[0].getDynamicObject()
        ->setProperty("inserts",juce::var(nine));
    rejectUnchanged(overfullRack);
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
    NativeControlProbe producer;
    assert(executeApprovedNativeControl(producer,translated.translated));
    assert(producer.lastEpoch==91 && producer.lastRevision==25);
    assert(producer.lastId==9007199254740993ULL);
    assert(std::abs(producer.lastGain-std::pow(10.,-12./20.))<0.000001);
    assert(!executeApprovedNativeControl(producer,juce::var{}));
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
    // A PAN-only gesture must preserve the original LINEAR PCM gain.
    // In particular, -60 dB in the GUI can be a nonzero 0.001 producer
    // gain, while +12 dB can be a clamped readback of native gain 4.0.
    // Recomputing from projected dB would unintentionally silence/attenuate
    // the same channel despite a perfectly accepted PAN intent.
    for(const double level : {0.0,0.001,4.0}) {
        auto physicalBus=juce::JSON::parse(juce::JSON::toString(bus,false));
        physicalBus.getProperty("channels",{})[0].getDynamicObject()
            ->setProperty("fader",level);
        auto physicalAccepted=canonicalSnapshot();
        assert(projectApprovedNativeV8(physicalAccepted,physicalBus,true));
        physicalAccepted.getProperty("graph",{}).getProperty("capabilities",{})
            .getDynamicObject()->setProperty("supportedOps",juce::var(publicOps));
        const auto panOnly=translateApprovedNativeControl(
            makeIntent("SET_PAN",juce::var(-0.35),source),
            physicalAccepted,physicalBus);
        assert(panOnly.ok());
        const auto cmd=panOnly.translated.getProperty("payload",{});
        assert(std::abs(static_cast<double>(
            cmd.getProperty("gain",{}))-level)<0.000000000001);
        assert(std::abs(static_cast<double>(
            cmd.getProperty("pan",{}))+0.35)<0.000000000001);
        if(level==0.001) {
            const auto explicitlySilent=translateApprovedNativeControl(
                makeIntent("SET_FADER",juce::var(-60.),source),
                physicalAccepted,physicalBus);
            assert(explicitlySilent.ok());
            assert(static_cast<double>(explicitlySilent.translated
                .getProperty("payload",{}).getProperty("gain",-1.0))==0.);
        }
    }
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


// P1 WIRE gateway regression: all ChannelId/EdgeId conversion is validated
// on the CONTROL thread. A test probe is not a hardware PCM producer.
struct NativeWireProbe : NativeControlProbe {
    std::uint64_t epoch=0,pcmRevision=0,rosterRevision=0;
    std::vector<armix::mixer::WireChange> applied;
    // Match the actual LiveDryRoutingLab::NativeBusResult ABI.
    struct Result { bool accepted=false; const char* error="NATIVE_REJECTED"; };
    Result applyNativeWireTransaction(std::uint64_t ep,std::uint64_t rev,
        std::uint64_t roster,std::span<const armix::mixer::WireChange> edits) {
        epoch=ep;pcmRevision=rev;rosterRevision=roster;
        applied.assign(edits.begin(),edits.end());
        return {true,""};
    }
};
// Processing-width intent is an admitted ChannelId operation, not the M4
// channel ordinal. Probe tests exact 64-bit IDs without claiming hardware.
struct NativeFormatProbe : NativeControlProbe {
    std::uint64_t epoch=0,revision=0,rosterRevision=0,id=0;
    armix::mixer::ProcessingFormat width=armix::mixer::ProcessingFormat::mono;
    struct Result {bool accepted=false;const char* error="REJECTED";};
    Result editNativeProcessingFormat(std::uint64_t ep,std::uint64_t rev,
        std::uint64_t roster,armix::mixer::ChannelId target,
        armix::mixer::ProcessingFormat next) {
        epoch=ep;revision=rev;rosterRevision=roster;
        id=target.value;width=next;
        return {true,""};
    }
};
static void approvedNativeMstTranslationRegression() {
    auto installed=realBusReadbackFixture();
    juce::Array<juce::var> nativeOps;
    nativeOps.add("NATIVE_SET_PROCESSING_FORMAT");
    installed.getDynamicObject()->setProperty("supportedOps",juce::var(nativeOps));
    auto accepted=canonicalSnapshot();
    assert(projectApprovedNativeV8(accepted,installed,true));
    juce::Array<juce::var> publicOps;
    publicOps.add("SET_PROCESSING_FORMAT");
    accepted.getProperty("graph",{}).getProperty("capabilities",{})
        .getDynamicObject()->setProperty("supportedOps",juce::var(publicOps));
    auto payload=std::make_unique<juce::DynamicObject>();
    payload->setProperty("format","STEREO");
    auto command=std::make_unique<juce::DynamicObject>();
    command->setProperty("requestId","native-mst-test-1");
    command->setProperty("deviceEpoch","91");
    command->setProperty("baseGraphRevision","25");
    command->setProperty("baseRosterRevision","9");
    command->setProperty("targetChannelId","9007199254740993");
    command->setProperty("operation","SET_PROCESSING_FORMAT");
    command->setProperty("payload",juce::var(payload.release()));
    auto intent=juce::var(command.release());
    const auto result=translateApprovedNativeControl(intent,accepted,installed);
    assert(result.ok());
    assert(result.translated.getProperty("operation","").toString()
           =="NATIVE_SET_PROCESSING_FORMAT");
    NativeFormatProbe probe;
    assert(executeApprovedNativeControl(probe,result.translated));
    assert(probe.epoch==91 && probe.revision==25 && probe.rosterRevision==9);
    assert(probe.id==9007199254740993ULL);
    assert(probe.width==armix::mixer::ProcessingFormat::stereo);

    auto stale=juce::JSON::parse(juce::JSON::toString(intent,false));
    stale.getDynamicObject()->setProperty("baseRosterRevision","8");
    assert(!translateApprovedNativeControl(stale,accepted,installed).ok());
    auto wrong=juce::JSON::parse(juce::JSON::toString(intent,false));
    wrong.getProperty("payload",{}).getDynamicObject()
        ->setProperty("format","MONO");
    assert(translateApprovedNativeControl(wrong,accepted,installed).error
           =="INVALID_NATIVE_PROCESSING_FORMAT");
    auto main=juce::JSON::parse(juce::JSON::toString(intent,false));
    main.getDynamicObject()->setProperty("targetChannelId","18446744073709551615");
    assert(!translateApprovedNativeControl(main,accepted,installed).ok());
    auto noNative=juce::JSON::parse(juce::JSON::toString(installed,false));
    noNative.getDynamicObject()->setProperty(
        "supportedOps",juce::var(juce::Array<juce::var>{}));
    assert(!translateApprovedNativeControl(intent,accepted,noNative).ok());
}

static void approvedNativeGroupMuteSoloRegression() {
    auto installed=realBusReadbackFixture();
    auto group=std::make_unique<juce::DynamicObject>();
    group->setProperty("id","9007199254740994");
    group->setProperty("name","Bus A");
    group->setProperty("role","GROUP");
    group->setProperty("format","STEREO");
    group->setProperty("primaryOutput","0");
    group->setProperty("fader",1.0);
    group->setProperty("pan",0.0);
    group->setProperty("mute",false);
    group->setProperty("solo",false);
    group->setProperty("inserts",juce::var(juce::Array<juce::var>{}));
    installed.getProperty("channels",{}).getArray()->add(
        juce::var(group.release()));
    juce::Array<juce::var> nativeOps;nativeOps.add("NATIVE_SET_BUS_MUTE_SOLO");
    installed.getDynamicObject()->setProperty("supportedOps",juce::var(nativeOps));
    auto publicSnapshot=canonicalSnapshot();
    assert(projectApprovedNativeV8(publicSnapshot,installed,true));
    juce::Array<juce::var> publicOps;
    publicOps.add("SET_MUTE");publicOps.add("SET_SOLO");
    publicSnapshot.getProperty("graph",{}).getProperty("capabilities",{})
        .getDynamicObject()->setProperty("supportedOps",juce::var(publicOps));
    auto makeRequest=[&](const char* op,bool newValue,const char* id){
        auto payload=std::make_unique<juce::DynamicObject>();
        payload->setProperty("value",newValue);
        auto request=std::make_unique<juce::DynamicObject>();
        request->setProperty("requestId","group-control-1");
        request->setProperty("deviceEpoch","91");
        request->setProperty("baseGraphRevision","25");
        request->setProperty("baseRosterRevision","9");
        request->setProperty("targetChannelId",id);
        request->setProperty("operation",op);
        request->setProperty("payload",juce::var(payload.release()));
        return juce::var(request.release());
    };
    NativeControlProbe probe;
    const auto busId="9007199254740994";
    auto muted=translateApprovedNativeControl(
        makeRequest("SET_MUTE",true,busId),publicSnapshot,installed);
    assert(muted.ok());
    assert(muted.translated.getProperty("operation","").toString()==
           "NATIVE_SET_BUS_MUTE_SOLO");
    assert(executeApprovedNativeControl(probe,muted.translated));
    assert(probe.lastId==9007199254740994ULL&&probe.lastMute&&!probe.lastSolo);
    auto solo=translateApprovedNativeControl(
        makeRequest("SET_SOLO",true,busId),publicSnapshot,installed);
    assert(solo.ok());
    assert(executeApprovedNativeControl(probe,solo.translated));
    assert(probe.lastSolo&&!probe.lastMute);
    // MAIN remains immutable through the BUS-specific control gate.
    auto denied=translateApprovedNativeControl(
        makeRequest("SET_MUTE",true,"18446744073709551615"),
        publicSnapshot,installed);
    assert(!denied.ok());
}

static void approvedNativeVst3LoadContractRegression(
        const juce::var& canonical,const juce::var& producer) {
    // Native VST3 command admission is independent of a fabricated vendor
    // fixture. No processor is created here: the real JUCE host resolves
    // the opaque catalog key and the live publisher proves PCM after ACK.
    auto accepted=juce::JSON::parse(juce::JSON::toString(canonical,false));
    auto bus=juce::JSON::parse(juce::JSON::toString(producer,false));
    juce::Array<juce::var> pubOps,nativeOps;
    pubOps.add("LOAD_INSERT");
    nativeOps.add("NATIVE_LOAD_INSERT");
    accepted.getProperty("graph",{}).getProperty("capabilities",{})
        .getDynamicObject()->setProperty("supportedOps",juce::var(pubOps));
    bus.getDynamicObject()->setProperty("supportedOps",juce::var(nativeOps));
    auto payload=std::make_unique<juce::DynamicObject>();
    payload->setProperty("catalogId","test.vendor.vst3");
    payload->setProperty("slot",0);
    auto intent=std::make_unique<juce::DynamicObject>();
    intent->setProperty("operation","LOAD_INSERT");
    intent->setProperty("requestId","fx-canonical-test");
    intent->setProperty("targetChannelId","9007199254740993");
    intent->setProperty("deviceEpoch","91");
    intent->setProperty("baseGraphRevision","25");
    intent->setProperty("baseRosterRevision","9");
    intent->setProperty("payload",juce::var(payload.release()));
    const juce::var request(intent.release());
    const auto translated=translateApprovedNativeControl(request,accepted,bus);
    assert(translated.ok());
    assert(translated.translated.getProperty("operation","").toString()
           =="NATIVE_LOAD_INSERT");
    assert(translated.translated.getProperty("payload",{})
           .getProperty("catalogId","").toString()=="test.vendor.vst3");
    const auto noChange=[&](juce::var cmd,juce::var live,juce::var native){
        const auto before=juce::JSON::toString(native,false);
        const auto result=translateApprovedNativeControl(cmd,live,native);
        assert(!result.ok()&&!result.translated.isObject());
        assert(juce::JSON::toString(native,false)==before);
    };
    auto stale=juce::JSON::parse(juce::JSON::toString(request,false));
    stale.getDynamicObject()->setProperty("baseGraphRevision","24");
    noChange(stale,accepted,bus);
    auto gap=juce::JSON::parse(juce::JSON::toString(request,false));
    gap.getProperty("payload",{}).getDynamicObject()->setProperty("slot",1);
    noChange(gap,accepted,bus);
    auto empty=juce::JSON::parse(juce::JSON::toString(request,false));
    empty.getProperty("payload",{}).getDynamicObject()
        ->setProperty("catalogId","");
    noChange(empty,accepted,bus);
    auto unsupported=juce::JSON::parse(juce::JSON::toString(bus,false));
    unsupported.getDynamicObject()->setProperty(
        "supportedOps",juce::var(juce::Array<juce::var>{}));
    noChange(request,accepted,unsupported);
    auto falseRole=juce::JSON::parse(juce::JSON::toString(accepted,false));
    falseRole.getProperty("graph",{}).getProperty("channels",{})[0]
             .getDynamicObject()->setProperty("role","MAIN");
    noChange(request,falseRole,bus);
}
static void approvedWireTranslationRegression(const juce::var& canonical,
                                                const juce::var& producer) {
    auto accepted=juce::JSON::parse(juce::JSON::toString(canonical,false));
    auto bus=juce::JSON::parse(juce::JSON::toString(producer,false));
    juce::Array<juce::var> publicOps,nativeOps;
    publicOps.add("APPLY_WIRE_TRANSACTION");
    nativeOps.add("NATIVE_APPLY_WIRE_TRANSACTION");
    accepted.getProperty("graph",{}).getProperty("capabilities",{})
        .getDynamicObject()->setProperty("supportedOps",juce::var(publicOps));
    bus.getDynamicObject()->setProperty("supportedOps",juce::var(nativeOps));
    const juce::String id="9007199254740993";
    const juce::String edge="9223372036854775808";
    auto request=std::make_unique<juce::DynamicObject>();
    request->setProperty("requestId","p1-wire-control-test");
    request->setProperty("operation","APPLY_WIRE_TRANSACTION");
    request->setProperty("targetChannelId",id);
    request->setProperty("deviceEpoch","91");
    request->setProperty("baseGraphRevision","25");
    request->setProperty("baseRosterRevision","9");
    auto one=std::make_unique<juce::DynamicObject>();
    one->setProperty("op","REMOVE");
    one->setProperty("from",id);
    one->setProperty("edgeId",edge);
    juce::Array<juce::var> changes;changes.add(juce::var(one.release()));
    auto payload=std::make_unique<juce::DynamicObject>();
    payload->setProperty("changes",juce::var(changes));
    request->setProperty("payload",juce::var(payload.release()));
    const auto original=juce::var(request.release());
    const auto translated=translateApprovedNativeControl(original,accepted,bus);
    assert(translated.ok());
    assert(translated.translated.getProperty("operation","").toString()
           =="NATIVE_APPLY_WIRE_TRANSACTION");
    NativeWireProbe native;
    assert(executeApprovedNativeControl(native,translated.translated));
    assert(native.epoch==91&&native.pcmRevision==25&&native.rosterRevision==9);
    assert(native.applied.size()==1);
    assert(native.applied[0].kind==armix::mixer::WireChange::Kind::remove);
    assert(native.applied[0].from.value==9007199254740993ULL);
    assert(native.applied[0].edgeId==9223372036854775808ULL);
    // The accepted ChannelId MAIN is a valid ROUTE_ONLY target from a
    // real UI OUT gesture (even though the decoder formerly rejected it).
    auto reconnect=juce::JSON::parse(juce::JSON::toString(original,false));
    auto wire=std::make_unique<juce::DynamicObject>();
    wire->setProperty("op","ROUTE_ONLY");
    wire->setProperty("from",id);
    wire->setProperty("to","18446744073709551615");
    wire->setProperty("tap","POST_FADER");
    wire->setProperty("gainLinear",1.0);
    juce::Array<juce::var> reconnectChanges;
    reconnectChanges.add(juce::var(wire.release()));
    reconnect.getProperty("payload",{}).getDynamicObject()
        ->setProperty("changes",juce::var(reconnectChanges));
    auto back=translateApprovedNativeControl(reconnect,accepted,bus);
    assert(back.ok());
    assert(executeApprovedNativeControl(native,back.translated));
    assert(native.applied.size()==1);
    assert(native.applied.front().kind==armix::mixer::WireChange::Kind::routeOnly);
    assert(native.applied.front().to.value==18446744073709551615ULL);

    auto fail=[&](juce::var intent,juce::var acceptedState,juce::var nativeState){
        const auto before=juce::JSON::toString(nativeState,false);
        const auto result=translateApprovedNativeControl(intent,acceptedState,nativeState);
        assert(!result.ok());assert(!result.translated.isObject());
        assert(juce::JSON::toString(nativeState,false)==before);
        return result.error;
    };
    auto forged=juce::JSON::parse(juce::JSON::toString(original,false));
    forged.getProperty("payload",{}).getProperty("changes",{})[0]
          .getDynamicObject()->setProperty("edgeId","9223372036854775807");
    assert(fail(forged,accepted,bus)=="WIRE_STALE_EDGE");
    auto wrongSource=juce::JSON::parse(juce::JSON::toString(original,false));
    wrongSource.getProperty("payload",{}).getProperty("changes",{})[0]
          .getDynamicObject()->setProperty("from","18446744073709551615");
    assert(fail(wrongSource,accepted,bus)=="WIRE_SOURCE_OR_OPERATION_INVALID");
    auto stale=juce::JSON::parse(juce::JSON::toString(original,false));
    stale.getDynamicObject()->setProperty("baseGraphRevision","24");
    assert(fail(stale,accepted,bus)=="STALE_NATIVE_CONTROL_REVISION");
    auto noNative=juce::JSON::parse(juce::JSON::toString(bus,false));
    noNative.getDynamicObject()->setProperty("supportedOps",
                                              juce::var(juce::Array<juce::var>{}));
    assert(fail(original,accepted,noNative)=="NATIVE_WIRE_NOT_ADMITTED");
    auto fractionalMain=juce::JSON::parse(juce::JSON::toString(reconnect,false));
    fractionalMain.getProperty("payload",{}).getProperty("changes",{})[0]
        .getDynamicObject()->setProperty("gainLinear",0.8);
    assert(fail(fractionalMain,accepted,bus)=="WIRE_UNSUPPORTED_TAP_OR_GAIN");
    auto invalidTap=juce::JSON::parse(juce::JSON::toString(original,false));
    auto edit=invalidTap.getProperty("payload",{}).getProperty("changes",{})[0];
    edit.getDynamicObject()->setProperty("op","CONNECT");
    edit.getDynamicObject()->setProperty("to","18446744073709551615");
    edit.getDynamicObject()->setProperty("tap","PRE_FADER");
    edit.getDynamicObject()->setProperty("gainLinear",1.0);
    assert(fail(invalidTap,accepted,bus)=="WIRE_UNSUPPORTED_TAP_OR_GAIN");
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
    // The physical master is post-ChannelId host PCM, not the generic
    // roster's default 0 dB. Keep its accepted gain and mute across native
    // projection, even when the source input fader changes independently.
    live.getDynamicObject()->setProperty("masterDb",-13.5);
    live.getDynamicObject()->setProperty("masterMute",true);
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
    const auto projectedRows=snap.getProperty("graph",{}).getProperty("channels",{});
    const auto* projected=projectedRows.getArray();
    assert(projected && projected->size()==2);
    bool physicalMasterFound=false;
    for(const auto& row:*projected)
        if(row.getProperty("role",{}).toString()=="MAIN") {
            const auto ctl=row.getProperty("controls",{});
            assert(std::abs(static_cast<double>(ctl.getProperty("faderDb",{}))
                              -(-13.5))<0.00001);
            assert(static_cast<bool>(ctl.getProperty("mute",false)));
            physicalMasterFound=true;
        }
    assert(physicalMasterFound);
    publishAuthoritativeMixerSession(snap);
    const auto session=snap.getProperty("mixerSession",{});
    assert(session.getProperty("authority","").toString()=="CHANNEL_ID");
    assert(session.getProperty("deviceEpoch","").toString()=="91");
    assert(session.getProperty("routeRevision","").toString()=="25");
    assert(session.getProperty("channels",{}).getArray()->size()==2);
    assert(session.getProperty("channels",{})[0].getProperty("id","").toString()
           =="9007199254740993");
    assert(std::abs(static_cast<double>(session.getProperty("main",{})
        .getProperty("db",0.))-(-13.5))<0.00001);
    // The installed graph remains editable with playback and no capture pins.
    dev.getDynamicObject()->setProperty("enabledInputs",juce::var(juce::Array<juce::var>{}));
    assert(projectInstalledNativeChannelIdGraph(snap,live));
    publishAuthoritativeMixerSession(snap);
    assert(snap.getProperty("mixerSession",{}).getProperty("authority","").toString()=="CHANNEL_ID");
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
    publishAuthoritativeMixerSession(disconnected);
    assert(disconnected.getProperty("mixerSession",{})
        .getProperty("authority","").toString()=="NONE");
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
    // Only a real installed callback may advertise WIRE; G0 and producer
    // snapshots without the corresponding capability remain fail-closed.
    auto wiredBus=juce::JSON::parse(juce::JSON::toString(rawBus,false));
    juce::Array<juce::var> nativeWireOps;
    nativeWireOps.add("NATIVE_APPLY_WIRE_TRANSACTION");
    wiredBus.getDynamicObject()->setProperty(
        "supportedOps",juce::var(nativeWireOps));
    auto wiredLive=juce::JSON::parse(juce::JSON::toString(live,false));
    wiredLive.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("nativeBus",wiredBus);
    auto wiredSnap=canonicalSnapshot();
    auto* wiredDevice=wiredSnap.getProperty("device",{}).getDynamicObject();
    wiredDevice->setProperty("sampleRate",48000.0);
    wiredDevice->setProperty("bufferFrames",256);
    wiredDevice->setProperty("enabledInputs",juce::var(inputs));
    wiredDevice->setProperty("enabledOutputs",juce::var(outputs));
    assert(synchronizeApprovedRouteDeviceEpoch(wiredSnap,wiredLive));
    assert(projectInstalledNativeChannelIdGraph(wiredSnap,wiredLive));
    const auto wireCaps=wiredSnap.getProperty("graph",{})
        .getProperty("capabilities",{}).getProperty("supportedOps",{});
    assert(wireCaps.getArray()&&
           wireCaps.getArray()->contains("APPLY_WIRE_TRANSACTION"));
    nativeWireOps.add("NATIVE_CREATE_GROUP_SELECTED");
    wiredBus.getDynamicObject()->setProperty(
        "supportedOps",juce::var(nativeWireOps));
    assert(projectInstalledNativeChannelIdGraph(wiredSnap,wiredLive));
    assert(wiredSnap.getProperty("graph",{}).getProperty("capabilities",{})
        .getProperty("supportedOps",{}).getArray()->contains("CREATE_GROUP"));
    assert(!static_cast<bool>(wiredSnap.getProperty("productionCallbackLive",false)));
    assert(!static_cast<bool>(snap.getProperty("productionCallbackLive",false)));
}

// A prepared physical M4 mixer must allow the user to PATCH CH1 even when
// universal ChannelId PCM has not yet been admitted. This is device truth,
// not a test-only callback or optimistic GUI state.
static void firstSoundRouteAdmissionRegression() {
    auto snapshot=canonicalSnapshot();
    const auto device=snapshot.getProperty("device",{});
    auto* d=device.getDynamicObject();
    juce::Array<juce::var> inputs,outputs;
    inputs.add(0);outputs.add(0);outputs.add(1);
    d->setProperty("epoch","7");
    d->setProperty("sampleRate",48000.0);
    d->setProperty("bufferFrames",128);
    d->setProperty("enabledInputs",juce::var(inputs));
    d->setProperty("enabledOutputs",juce::var(outputs));
    const auto live=parse(R"JSON({
      "connected":true,"directRouteAuthorized":true,"adaptiveClockActive":false,
      "dryRoutingLab":{"active":true,"ready":true,"topologyFault":false,
        "hasAcceptedRoute":false,"epoch":7,"revision":12}
    })JSON");
    assert(publishVerifiedFirstSoundState(snapshot,live));
    auto m4=snapshot.getProperty("legacyM4",{});
    assert(static_cast<bool>(m4.getProperty("active",false)));
    assert(!static_cast<bool>(m4.getProperty("routeAccepted",true)));
    assert(m4.getProperty("routingRevision","").toString()=="12");
    assert(!static_cast<bool>(snapshot.getProperty("graph",{})
        .getProperty("capabilities",{}).getProperty("callbackOwnsChannelIds",false)));
    // After an actual source patch, ARM is enabled by existing JUCE native
    // preflight; no ChannelId capability or product LIVE is fabricated.
    live.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("hasAcceptedRoute",true);
    assert(publishVerifiedFirstSoundState(snapshot,live));
    m4=snapshot.getProperty("legacyM4",{});
    assert(static_cast<bool>(m4.getProperty("routeAccepted",false)));
    // First hardware choice may target CH05 (not hardcoded CH01). All ten
    // rows must project from native accepted data, never demo INPUT 1.
    juce::Array<juce::var> rows;
    for(int i=0;i<10;++i) {
        auto row=std::make_unique<juce::DynamicObject>();
        row->setProperty("mode",i==4?"MONO":"NONE");
        row->setProperty("left",i==4?0:-1);
        row->setProperty("right",-1);
        row->setProperty("main",true);
        rows.add(juce::var(row.release()));
    }
    auto accepted=std::make_unique<juce::DynamicObject>();
    accepted->setProperty("channels",juce::var(rows));
    live.getProperty("dryRoutingLab",{}).getDynamicObject()->setProperty(
        "acceptedRoute",juce::var(accepted.release()));
    assert(publishVerifiedFirstSoundState(snapshot,live));
    m4=snapshot.getProperty("legacyM4",{});
    const auto physicalRows=m4.getProperty("channels",{});
    assert(physicalRows.isArray());
    assert(physicalRows.getArray()->size()==10);
    assert(physicalRows.getArray()->getReference(4).getProperty("mode",{}).toString()=="MONO");
    assert(static_cast<int>(physicalRows.getArray()->getReference(4).getProperty("left",-1))==0);
    // One internal BUS must NOT invalidate the ten-lane native projection
    // or erase physical CH05 input/controls, which are still accepted PCM.
    auto withBus=juce::JSON::parse(juce::JSON::toString(live,false));
    withBus.getProperty("dryRoutingLab",{})
      .getProperty("acceptedRoute",{}).getProperty("channels",{})
      .getArray()->getReference(2).getDynamicObject()->setProperty("mode","BUS");
    assert(publishVerifiedFirstSoundState(snapshot,withBus));
    auto busReadback=snapshot.getProperty("legacyM4",{}).getProperty("channels",{});
    assert(busReadback.isArray()&&busReadback.getArray()->size()==10);
    assert(busReadback[2].getProperty("mode","").toString()=="BUS");
    assert(static_cast<int>(busReadback[2].getProperty("left",0))==-1);
    assert(busReadback[4].getProperty("mode","").toString()=="MONO");
    assert(static_cast<int>(busReadback[4].getProperty("left",-1))==0);
    auto invalidBus=juce::JSON::parse(juce::JSON::toString(withBus,false));
    invalidBus.getProperty("dryRoutingLab",{})
      .getProperty("acceptedRoute",{}).getProperty("channels",{})
      .getArray()->getReference(2).getDynamicObject()->setProperty("left",7);
    assert(publishVerifiedFirstSoundState(snapshot,invalidBus));
    assert(snapshot.getProperty("legacyM4",{})
        .getProperty("channels",{}).getArray()->isEmpty());
    assert(publishVerifiedFirstSoundState(snapshot,live));
    // Disabled hardware input is never presented as accepted physical PCM.
    auto badPins=juce::JSON::parse(juce::JSON::toString(live,false));
    badPins.getProperty("dryRoutingLab",{}).getProperty("acceptedRoute",{})
        .getProperty("channels",{}).getArray()->getReference(4)
        .getDynamicObject()->setProperty("left",7);
    assert(publishVerifiedFirstSoundState(snapshot,badPins));
    assert(snapshot.getProperty("legacyM4",{}).getProperty("channels",{}).getArray()->isEmpty());
    // Restore valid accepted rows for the stale epoch/fail-closed matrix.
    assert(publishVerifiedFirstSoundState(snapshot,live));
    auto reject=[&](const juce::var& degraded) {
        auto fresh=juce::JSON::parse(juce::JSON::toString(snapshot,false));
        assert(!publishVerifiedFirstSoundState(fresh,degraded));
        const auto disabled=fresh.getProperty("legacyM4",{});
        assert(disabled.isObject());
        assert(!static_cast<bool>(disabled.getProperty("active",true)));
        assert(!static_cast<bool>(disabled.getProperty("routeAccepted",true)));
        assert(disabled.getProperty("routingRevision","").toString()=="0");
    };
    auto bad=juce::JSON::parse(juce::JSON::toString(live,false));
    bad.getProperty("dryRoutingLab",{}).getDynamicObject()->setProperty("epoch",8);
    reject(bad); // ASIO re-opened, old physical pins must not be patchable
    bad=juce::JSON::parse(juce::JSON::toString(live,false));
    bad.getDynamicObject()->setProperty("directRouteAuthorized",false);
    reject(bad);
    bad=juce::JSON::parse(juce::JSON::toString(live,false));
    bad.getProperty("dryRoutingLab",{}).getDynamicObject()->setProperty("topologyFault",true);
    reject(bad);
    bad=juce::JSON::parse(juce::JSON::toString(live,false));
    bad.getProperty("dryRoutingLab",{}).getDynamicObject()->setProperty("revision",0);
    reject(bad);
    bad=juce::JSON::parse(juce::JSON::toString(live,false));
    bad.getDynamicObject()->setProperty("adaptiveClockActive",true);
    reject(bad);
    auto withoutInput=juce::JSON::parse(juce::JSON::toString(snapshot,false));
    withoutInput.getProperty("device",{}).getDynamicObject()
        ->setProperty("enabledInputs",juce::var(juce::Array<juce::var>{}));
    assert(publishVerifiedFirstSoundState(withoutInput,live));
    assert(withoutInput.getProperty("legacyM4",{})
        .getProperty("channels",{}).getArray()->isEmpty()); // stale MONO mapping rejected
    auto noCaptureRoute=juce::JSON::parse(juce::JSON::toString(live,false));
    auto noCaptureRows=noCaptureRoute.getProperty("dryRoutingLab",{})
        .getProperty("acceptedRoute",{}).getProperty("channels",{});
    auto* unpatched=noCaptureRows[4].getDynamicObject();
    unpatched->setProperty("mode","NONE");
    unpatched->setProperty("left",-1);
    assert(publishVerifiedFirstSoundState(withoutInput,noCaptureRoute));
    assert(withoutInput.getProperty("legacyM4",{})
        .getProperty("channels",{}).getArray()->size()==10);
}

// Control-only fixture: validates exclusive M4 session normalization,
// never claims that fixture data is a hardware PCM recording.
static void milestoneAM4SessionProjectionRegression() {
    auto snapshot=canonicalSnapshot();
    snapshot.getProperty("device",{}).getDynamicObject()->setProperty("epoch","33");
    auto m4=std::make_unique<juce::DynamicObject>();
    m4->setProperty("active",true);
    m4->setProperty("routeAccepted",true);
    m4->setProperty("nativeBusStructural",false);
    m4->setProperty("routingRevision","19");
    m4->setProperty("masterDb",-7.5);
    m4->setProperty("masterMute",false);
    juce::Array<juce::var> sources,controls,peaksL,peaksR,rackFlags;
    for(int i=0;i<10;++i) {
        auto src=std::make_unique<juce::DynamicObject>();
        src->setProperty("mode",i==4?"MONO":"NONE");
        src->setProperty("left",i==4?2:-1);
        src->setProperty("right",-1);
        src->setProperty("main",true);
        sources.add(juce::var(src.release()));
        auto ctl=std::make_unique<juce::DynamicObject>();
        ctl->setProperty("db",i==4?-6.:0.);
        ctl->setProperty("pan",i==4?25.:0.);
        ctl->setProperty("mute",false);
        ctl->setProperty("solo",false);
        controls.add(juce::var(ctl.release()));
        peaksL.add(i==4?0.125:0.0);
        peaksR.add(i==4?0.25:0.0);
        rackFlags.add(i==4);
    }
    m4->setProperty("channels",juce::var(sources));
    m4->setProperty("channelControls",juce::var(controls));
    m4->setProperty("channelPeaksL",juce::var(peaksL));
    m4->setProperty("channelPeaksR",juce::var(peaksR));
    m4->setProperty("fxRackBypassed",juce::var(rackFlags));
    snapshot.getDynamicObject()->setProperty("legacyM4",juce::var(m4.release()));
    publishAuthoritativeMixerSession(snapshot);
    const auto session=snapshot.getProperty("mixerSession",{});
    assert(session.getProperty("authority","").toString()=="M4");
    assert(session.getProperty("deviceEpoch","").toString()=="33");
    assert(session.getProperty("routeRevision","").toString()=="19");
    const auto rows=session.getProperty("channels",{});
    assert(rows.isArray()&&rows.getArray()->size()==10);
    assert(rows[4].getProperty("id","").toString()=="m4:05");
    assert(rows[4].getProperty("fxRackBypassed",{}).isBool());
    assert(static_cast<bool>(rows[4].getProperty("fxRackBypassed",false)));
    assert(std::abs(static_cast<double>(rows[4].getProperty("controls",{})
        .getProperty("faderDb",0.0))+6.)<0.00001);
    assert(std::abs(static_cast<double>(rows[4].getProperty("controls",{})
        .getProperty("pan",0.0))-.25)<0.00001);
    assert(std::abs(static_cast<double>(session.getProperty("main",{})
        .getProperty("db",0.0))+7.5)<0.00001);
    // Reject corrupt producer readback atomically, not just the bad strip.
    snapshot.getProperty("legacyM4",{}).getProperty("channelControls",{})
        .getArray()->getReference(4).getDynamicObject()->setProperty("db",55.);
    publishAuthoritativeMixerSession(snapshot);
    assert(snapshot.getProperty("mixerSession",{})
        .getProperty("authority","").toString()=="NONE");
}

static void firstSoundMeasuredSignalEvidenceRegression() {
    auto live=parse(R"JSON({
      "connected":true,"directRouteAuthorized":true,
      "adaptiveClockActive":false,"monitor":true,
      "captureObservationBlocks":19,
      "callbackInputChannels":2,"callbackOutputChannels":2,
      "inputPeak":0.35,"captureCallbackPeaks":[0.0,0.35],
      "peakLeft":0.08,"peakRight":0.12,
      "dryRoutingLab":{"active":true,"ready":true,"topologyFault":false,
        "hasAcceptedRoute":true}
    })JSON");
    const auto signal=[&](const juce::var& l,bool direct,bool arm,int ins=2,int outs=2) {
        return projectFirstSoundSignalEvidence(l,direct,arm,ins,outs);
    };
    const auto accepted=signal(live,true,true);
    assert(static_cast<bool>(accepted.getProperty("captureObserved",false)));
    assert(static_cast<bool>(accepted.getProperty("rawCapturePresent",false)));
    assert(static_cast<bool>(accepted.getProperty("mainPcmObserved",false)));
    assert(static_cast<bool>(accepted.getProperty("mainPcmPresent",false)));
    assert(std::abs(static_cast<double>(accepted.getProperty("rawCapturePeak",0.))-0.35)<1e-7);
    assert(std::abs(static_cast<double>(accepted.getProperty("mainPcmPeak",0.))-0.12)<1e-7);
    assert(accepted.getProperty("captureCallbackPeaks",{}).getArray()->size()==2);
    // An active unpatched graph still observes real input while correctly
    // hiding routed MAIN PCM. No physical output is ever auto-authorized.
    live.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("hasAcceptedRoute",false);
    auto unpatched=signal(live,true,true);
    assert(static_cast<bool>(unpatched.getProperty("captureObserved",false)));
    assert(!static_cast<bool>(unpatched.getProperty("mainPcmObserved",true)));
    live.getProperty("dryRoutingLab",{}).getDynamicObject()
        ->setProperty("hasAcceptedRoute",true);
    auto disarmed=signal(live,true,false);
    assert(static_cast<bool>(disarmed.getProperty("captureObserved",false)));
    assert(!static_cast<bool>(disarmed.getProperty("mainPcmObserved",true)));
    live.getDynamicObject()->setProperty("inputPeak",0.);
    live.getDynamicObject()->setProperty("captureCallbackPeaks",parse(
        R"JSON({"peaks":[0,0]})JSON").getProperty("peaks",{}));
    auto silence=signal(live,true,true);
    assert(static_cast<bool>(silence.getProperty("captureObserved",false)));
    assert(!static_cast<bool>(silence.getProperty("rawCapturePresent",true)));
    assert(!silence.getProperty("rawCapturePeak",{}).isVoid());
    live.getDynamicObject()->setProperty("inputPeak",0.35);
    // Driver callback never occurred (or is stale): evidence UNKNOWN rather
    // than a fabricated 0 dBFS or reported hardware source.
    live.getDynamicObject()->setProperty("captureObservationBlocks",0);
    auto none=signal(live,true,true);
    assert(!static_cast<bool>(none.getProperty("captureObserved",true)));
    assert(none.getProperty("rawCapturePeak",{}).isVoid());
    live.getDynamicObject()->setProperty("captureObservationBlocks",19);
    assert(!static_cast<bool>(signal(live,false,true).getProperty("captureObserved",true)));
    assert(!static_cast<bool>(signal(live,true,true,1,2)
        .getProperty("captureObserved",true)));
    live.getDynamicObject()->setProperty("directRouteAuthorized",false);
    assert(!static_cast<bool>(signal(live,true,true).getProperty("captureObserved",true)));
    live.getDynamicObject()->setProperty("directRouteAuthorized",true);
    live.getDynamicObject()->setProperty("captureCallbackPeaks",juce::var());
    assert(!static_cast<bool>(signal(live,true,true).getProperty("captureObserved",true)));
}
 
static void retainedDocumentDoesNotAdmitPcm() {
    auto snapshot=canonicalSnapshot();
    snapshot.getProperty("device",{}).getDynamicObject()
        ->setProperty("outputArmed",false);
    auto bus=realBusReadbackFixture();
    auto live=parse(R"JSON({
      "masterDb":-9.0,"masterMute":true,
      "dryRoutingLab":{"nativeChannelIdPcmActive":false}
    })JSON");
    auto* route=live.getProperty("dryRoutingLab",{}).getDynamicObject();
    route->setProperty("nativeBus",bus);
    assert(!projectRetainedNativeChannelIdGraph(snapshot,live));
    route->setProperty("retainedNativeBus",bus);
    assert(projectRetainedNativeChannelIdGraph(snapshot,live));
    publishAuthoritativeMixerSession(snapshot);
    const auto graph=snapshot.getProperty("graph",{});
    const auto caps=graph.getProperty("capabilities",{});
    assert(graph.getProperty("bindingState",{}).toString()=="NEEDS_REMAP");
    assert(graph.getProperty("channels",{})[0].getProperty("id",{}).toString()=="9007199254740993");
    assert(graph.getProperty("edges",{})[0].getProperty("edgeId",{}).toString()=="9223372036854775808");
    assert(!static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",true)));
    assert(caps.getProperty("supportedOps",{}).getArray()->isEmpty());
    assert(snapshot.getProperty("telemetry",{}).getProperty("channelMeterRmsPeak",{})
        .getDynamicObject()->getProperties().size()==0);
    const auto session=snapshot.getProperty("mixerSession",{});
    assert(session.getProperty("bindingState",{}).toString()=="NEEDS_REMAP");
    assert(static_cast<double>(session.getProperty("main",{}).getProperty("db",0.0))==-9.0);
    route->setProperty("nativeChannelIdPcmActive",true);
    assert(!projectRetainedNativeChannelIdGraph(snapshot,live));
}

int main() {
    retainedDocumentDoesNotAdmitPcm();
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
    nativeInsertCatalogInstanceProjectionRegression();
    approvedNativeMstTranslationRegression();
    approvedNativeGroupMuteSoloRegression();
    approvedNativeVst3LoadContractRegression(snap,bus);
    approvedWireTranslationRegression(snap,bus);
    installedCallbackAdmissionRegression(bus);
    firstSoundRouteAdmissionRegression();
    milestoneAM4SessionProjectionRegression();
    firstSoundMeasuredSignalEvidenceRegression();
    auto unadmitted=canonicalSnapshot();
    publishAuthoritativeMixerSession(unadmitted);
    assert(unadmitted.getProperty("mixerSession",{})
        .getProperty("authority","").toString()=="NONE");

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
