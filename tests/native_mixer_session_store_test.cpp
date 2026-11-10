// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeMixerSessionStore.h"
#include "../src/LiveDryRoutingLab.h"
#include "../src/NativeMixerSessionFxRestore.h"
#include "../src/NativeLogicalMixerControl.h"

using namespace armix::mixer;
using namespace armix::session;
using BindingMode=MixerDeviceBindingPolicy::Mode;
using armix::shows::BinaryState;

static MixerSessionDocument makeSession() {
    MixerSessionDocument d;
    d.id="11111111-2222-4333-8444-555555555555";
    d.savedMs=1762520000000LL;
    d.binding={"ASIO","Verified ASIO","Verified ASIO","3","3",48000.,256};
    d.outputs={2,0,1,-1,-1,false};
    d.request.physicalInputs=2;d.request.maxFrames=256;
    d.request.maxScratchBytes=32u*1024u*1024u;
    d.masterGain=.7f;d.masterMute=true;d.masterMono=false;
    std::vector<ChannelStripDefinition> rows;
    for(int i=1;i<=10;++i) {
        ChannelStripDefinition row;
        row.id={static_cast<std::uint64_t>(i)};
        row.role=ChannelRole::input;
        row.name="CH "+std::to_string(i);
        row.format=i==1?ProcessingFormat::mono:ProcessingFormat::stereo;
        if(i==1){
            row.sends.push_back({{16},1.f,false,0.f,true,100});
            row.sends.push_back({{11},.3f,true,-.25f,false,101});
        } else if(i==2)row.sends.push_back({{15},1.f,false,0.f,true,102});
        rows.push_back(std::move(row));
        d.legacyIds.inputs[i-1]={static_cast<std::uint64_t>(i)};
    }
    for(int i=0;i<4;++i) {
        ChannelStripDefinition aux;
        aux.id={static_cast<std::uint64_t>(11+i)};
        aux.role=ChannelRole::fxReturn;
        aux.format=ProcessingFormat::stereo;
        aux.name="AUX "+std::to_string(i+1);
        aux.sends.push_back({{15},1.f,false,0.f,true,
                             static_cast<std::uint64_t>(200+i)});
        d.legacyIds.aux[i]=aux.id;
        rows.push_back(std::move(aux));
    }
    ChannelStripDefinition group;
    group.id={16};group.role=ChannelRole::group;
    group.name="Vocal Group";group.format=ProcessingFormat::stereo;
    group.sends.push_back({{15},1.f,false,0.f,true,105});
    rows.push_back(std::move(group));
    ChannelStripDefinition main;
    main.id={15};main.role=ChannelRole::main;
    main.name="MAIN";main.format=ProcessingFormat::stereo;
    rows.push_back(std::move(main));
    d.legacyIds.main={15};
    d.acceptedRevision=52;
    assert(d.roster.restoreExact(d.acceptedRevision,rows));
    d.request.sources.push_back({{1},0,-1,false});
    d.rackBypassed.push_back({1});
    return d;
}
class FixtureDevice final : public juce::AudioIODevice {
public:
    FixtureDevice():AudioIODevice("Verified WASAPI fixture","Windows Audio") {
        inputMask.setRange(0,2,true);
        outputMask.setRange(0,2,true);
    }
    juce::StringArray getOutputChannelNames() override {return {"Main L","Main R"};}
    juce::StringArray getInputChannelNames() override {return {"Capture L","Capture R"};}
    juce::Array<double> getAvailableSampleRates() override {return {48000.,44100.};}
    juce::Array<int> getAvailableBufferSizes() override {return {480,512};}
    int getDefaultBufferSize() override {return 480;}
    juce::String open(const juce::BigInteger&,const juce::BigInteger&,double,int) override {return {};}
    void close() override {}
    bool isOpen() override {return true;}
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override {return true;}
    juce::String getLastError() override {return {};}
    int getCurrentBufferSizeSamples() override {return frames;}
    double getCurrentSampleRate() override {return rate;}
    int getCurrentBitDepth() override {return 32;}
    juce::BigInteger getActiveOutputChannels() const override {return outputMask;}
    juce::BigInteger getActiveInputChannels() const override {return inputMask;}
    int getOutputLatencyInSamples() override {return 0;}
    int getInputLatencyInSamples() override {return 0;}
    juce::BigInteger inputMask,outputMask;
    int frames=480;
    double rate=48000.;
};

static void outputOnlyNativeStartup() {
    // Real output-only callback: never invent a capture pin, but a silent
    // ChannelId session, its FX/WIRE bus and physical zero output are valid.
    FixtureDevice playback;
    playback.inputMask.clear();
    LiveDryRoutingLab mixer;
    juce::AudioPluginFormatManager formats;
    mixer.prepareDevice(&playback);
    assert(mixer.startUnpatchedSession(true,false,0,formats,48000.,480).isEmpty());
    assert(mixer.ready() && !mixer.hasExplicitRoute());
    // Legacy startup is silent before the one deliberate native migration.
    assert(!mixer.admitChannelIdFromAcceptedM4(0,480,true));
    assert(mixer.admitChannelIdFromAcceptedM4(0,480,true,{},true));
    assert(mixer.hasExplicitRoute() && !mixer.hasVerifiedNativeSources());
    assert(mixer.canReplacePristineStartupMixer());
    // Startup automatically restores an existing .smix on precisely the
    // same verified device. The saved Show must win over a never-edited
    // SOURCE NONE starter, without a second publisher/audio callback.
    {
        auto saved=makeSession();
        saved.binding={"Windows Audio","","Verified WASAPI fixture","0","3",48000.,480};
        saved.request.physicalInputs=0;
        saved.request.sources.clear();
        saved.request.maxFrames=480;
        saved.rackBypassed.clear();
        armix::device::JuceActivePortSnapshot pins;
        armix::device::JuceActivePortSnapshot::Fault fault;
        assert(armix::device::JuceActivePortSnapshot::capture(&playback,mixer.epoch(),pins,fault));
        std::shared_ptr<DynamicChannelFxLeaseBank> bank;
        const auto prepare=NativeMixerSessionFxRestore::prepare(saved,{},formats,
            48000.,480,pins.epoch,mixer.nativeGraphRevisionForRestore(),bank);
        assert(prepare.isEmpty() && bank);
        assert(mixer.restoreNativeMixerSession(saved,std::move(bank),pins,true,true).isEmpty());
        assert(!mixer.canReplacePristineStartupMixer()); // saved Show is now authored
        assert(mixer.hasExplicitRoute() && !mixer.hasVerifiedNativeSources());
    }
    const auto native=mixer.nativeBusStatus();
    assert(native.isObject());
    const auto graph=static_cast<std::uint64_t>(
        native.getProperty("graphRevision",{}).toString().getLargeIntValue());
    const auto roster=static_cast<std::uint64_t>(
        native.getProperty("rosterRevision",{}).toString().getLargeIntValue());
    const auto add=mixer.createNativeInputChannel(mixer.epoch(),graph,roster,
        "No Microphone Track",ProcessingFormat::stereo,true);
    assert(add.accepted && !mixer.hasVerifiedNativeSources());
    assert(!mixer.canReplacePristineStartupMixer()); // edited graph can never be replaced
    std::array<float,480> left{},right{};
    left.fill(1.f);right.fill(1.f); // must be cleared, not fake audio
    float* outputs[]={left.data(),right.data()};
    assert(mixer.process(nullptr,0,outputs,2,480,1.f,true,
        nullptr,nullptr,false,nullptr,nullptr,true));
    for(const auto s:left)assert(s==0.f);
    for(const auto s:right)assert(s==0.f);
}

static void nativeDeviceRecovery() {
    FixtureDevice device;
    auto saved=makeSession();
    saved.binding={"Windows Audio","VB-Cable","Realtek","3","3",48000.,480};
    saved.request.maxFrames=480;
    saved.request.sources={{ChannelId{1},0,1,true}};
    saved.rackBypassed.clear();
    auto rows=saved.roster.channels();
    rows.front().format=ProcessingFormat::stereo;
    rows.front().sends.push_back({{2},.15f,false,0.f,false,110});
    const auto eqId=armix::builtin_fx::ChannelEqProcessor::catalogIdentity;
    rows.front().insertCatalogIds.push_back(juce::String(eqId).toStdString());
    assert(saved.roster.restoreExact(saved.acceptedRevision,rows));
    BinaryState binary;
    armix::builtin_fx::ChannelEqProcessor eq;
    eq.prepareToPlay(48000.,480);
    eq.getStateInformation(binary.bytes);
    binary.sha256=juce::SHA256(binary.bytes).toHexString();
    saved.inserts.push_back({{1},0,eqId,{},binary.sha256,"EQ",0,false});
    // Validate the same durable document used at app startup.
    MixerSessionDocument verified;
    assert(Codec::decode(Codec::encode(saved),verified));
    std::vector<BinaryState> blobs{binary};
    juce::AudioPluginFormatManager formats;
    {
        LiveDryRoutingLab fresh;
        fresh.prepareDevice(&device);
        assert(fresh.startUnpatchedSession(true,false,0,formats,48000.,480).isEmpty());
        assert(fresh.ready() && !fresh.hasExplicitRoute());
        const auto epoch=fresh.epoch();
        assert(fresh.repatchPhysicalSource(0,epoch,fresh.revision(),"NONE",-1,-1,
            true,false,0,formats,48000.,480).isEmpty());
        assert(fresh.admitChannelIdFromAcceptedM4(0,480,true));
        assert(!fresh.hasVerifiedNativeSources());
        auto native=fresh.nativeBusStatus();
        const auto graph=static_cast<std::uint64_t>(
            native.getProperty("graphRevision",{}).toString().getLargeIntValue());
        const auto roster=static_cast<std::uint64_t>(
            native.getProperty("rosterRevision",{}).toString().getLargeIntValue());
        int publications=0;
        const auto publish=[&]{++publications;};
        assert(!fresh.createNativeInputChannel(epoch,graph+1,roster,"Stale",
            ProcessingFormat::stereo,true,publish).accepted);
        assert(publications==0);
        const auto added=fresh.createNativeInputChannel(epoch,graph,roster,"Track 11",
            ProcessingFormat::stereo,true,publish);
        assert(added.accepted && added.pcmRevision==graph+1 && publications==1);
        assert(!fresh.hasVerifiedNativeSources());
        native=fresh.nativeBusStatus();
        const auto channels=native.getProperty("channels",{});
        const auto* strips=channels.getArray();
        assert(strips && strips->size()==16);
        const auto& track=strips->getReference(14);
        assert(track.getProperty("id",{}).toString()==juce::String(added.created.value));
        assert(track.getProperty("name",{}).toString()=="Track 11");
        assert(track.getProperty("format",{}).toString()=="STEREO");
        assert(track.getProperty("primaryOutput",{}).toString()==
               strips->getReference(15).getProperty("id",{}).toString());
        const auto patched=fresh.patchNativeInputSource(epoch,added.pcmRevision,
            added.rosterRevision,added.created,0,1,true,true);
        assert(patched.accepted && fresh.hasVerifiedNativeSources());
        std::array<float,480> left,right,outLeft{},outRight{};
        left.fill(.2f);right.fill(.1f);
        const float* inputs[]={left.data(),right.data()};
        float* outputs[]={outLeft.data(),outRight.data()};
        assert(fresh.process(inputs,2,outputs,2,480,1.f,false,
            nullptr,nullptr,false,nullptr,nullptr,true));
        assert(outLeft.back()>0.f && outRight.back()>0.f);

    }

    LiveDryRoutingLab mixer;
    mixer.prepareDevice(&device);
    const auto restore=[&](const MixerSessionDocument& doc,
                           const std::vector<BinaryState>& states,bool disarmed,
                           bool replaceActive=false) {
        armix::device::JuceActivePortSnapshot pins;
        armix::device::JuceActivePortSnapshot::Fault fault;
        assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
        std::shared_ptr<DynamicChannelFxLeaseBank> bank;
        const auto preparation=NativeMixerSessionFxRestore::prepare(doc,states,formats,
            device.rate,doc.request.maxFrames,pins.epoch,mixer.nativeGraphRevisionForRestore(),bank);
        if(preparation.isNotEmpty())return preparation;
        return mixer.restoreNativeMixerSession(doc,std::move(bank),pins,disarmed,replaceActive);
    };
    assert(!mixer.ready() && mixer.revision()==0); // no compatibility PCM publisher
    assert(restore(verified,{},true).isNotEmpty());
    assert(restore(verified,blobs,false).isNotEmpty());
    assert(!mixer.channelIdPhysicalPcmActive());
    assert(restore(verified,blobs,true).isEmpty());
    const auto firstEpoch=mixer.epoch();
    assert(mixer.ready() && mixer.controlRevision()>0 && mixer.revision()==0);
    assert(mixer.status(&device).getProperty("mainOutputs",{}).getArray()->size()==2);

    std::array<float,480> inL,inR,outL,outR;
    inL.fill(.2f);inR.fill(.1f);
    const float* in[]={inL.data(),inR.data()};
    float* out[]={outL.data(),outR.data()};
    const auto process=[&](bool muted) {
        outL.fill(1.f);outR.fill(1.f);
        assert(mixer.process(in,device.inputMask.countNumberOfSetBits(),out,2,480,1.f,muted,
                             nullptr,nullptr,false,nullptr,nullptr,true));
    };
    process(true);
    assert(outL.back()==0.f && outR.back()==0.f && mixer.peakLeft()==0.f);
    auto graph=mixer.nativeBusStatus();
    const auto channels=graph.getProperty("channels",{});
    assert(static_cast<double>(channels.getArray()->getReference(0).getProperty("peakL",0.))>0.);
    process(false);
    assert(outL.back()>.05f && outR.back()>.025f);

    // Portable Open may replace a live session only on an explicit muted
    // publication. Invalid FX must leave the accepted graph and PCM intact.
    const auto liveRevision=mixer.controlRevision();
    assert(restore(verified,blobs,true).isNotEmpty());
    assert(restore(verified,{},true,true).isNotEmpty());
    assert(mixer.controlRevision()==liveRevision);
    process(false);
    assert(outL.back()>.05f && outR.back()>.025f);
    assert(restore(verified,blobs,true,true).isEmpty());
    assert(mixer.controlRevision()>liveRevision);
    process(false);
    assert(outL.back()>.05f && outR.back()>.025f);

    // Reopen repeatedly. The field failure had retained channels, FX and
    // WIRE, but no live legacy lease for the new 48 kHz/480 frame epoch.
    for(int attempt=0;attempt<3;++attempt) {
        const auto prior=mixer.epoch();
        mixer.deviceStopped();
        mixer.prepareDevice(&device);
        assert(!mixer.ready() && mixer.revision()==0);
        assert(mixer.status(&device).getProperty("retainedNativeBus",{}).isObject());
        process(true);
        assert(outL.back()==0.f && outR.back()==0.f);
        assert(mixer.rebindVerifiedDevice(prior,mixer.epoch(),480,false));
        assert(mixer.channelIdPhysicalPcmActive());
        assert(!mixer.setNativeInputMuteSolo(firstEpoch,mixer.controlRevision(),{1},true,false));
        process(true);
        assert(outL.back()==0.f && outR.back()==0.f);
        process(false);
        assert(outL.back()>.05f && outR.back()>.025f);
    }
    const auto ep=mixer.epoch();
    auto rev=mixer.controlRevision();
    assert(mixer.setNativeInputMuteSolo(ep,rev,{1},true,false));
    process(false);
    assert(outL.back()==0.f && outR.back()==0.f);
    assert(mixer.setNativeInputMuteSolo(ep,rev,{1},false,false));
    assert(mixer.setNativeInputMix(ep,rev,{1},.5f,0.f));
    process(false);
    assert(outL.back()>.02f && outL.back()<.19f);
    MixerSessionDocument current;
    std::vector<BinaryState> currentStates;
    assert(mixer.captureNativeMixerSession(current,currentStates).isEmpty());
    assert(current.roster.find({1})->sends[1].edgeId==101);
    assert(current.roster.find({1})->faderLinear==.5f);
    assert(current.inserts.size()==1 && current.inserts[0].catalogId==eqId);
    current.binding=saved.binding;

    // Render actual PCM after a sparse ASIO4ALL re-enumeration. Two compact
    // callbacks now address physical pins 2 and 3, with MAIN L at ordinal 1.
    auto aggregate=current;
    aggregate.binding={"ASIO","ASIO4ALL v2","ASIO4ALL v2","3","3",48000.,480,
        {"VB-Audio Point 1","VB-Audio Point 2"},{"2nd output with SST 1","2nd output with SST 2"}};
    auto reenumerated=aggregate.binding;
    reenumerated.inputMask=reenumerated.outputMask="6";
    reenumerated.inputNames.insert(0,"Other capture");
    reenumerated.outputNames={"Other playback","2nd output with SST 2","2nd output with SST 1"};
    const auto asioPair=MixerDeviceBindingPolicy::proposedOutputs(
        aggregate,reenumerated,2,BindingMode::verifiedAsioRestart);
    assert(MixerDeviceBindingPolicy::apply(aggregate,reenumerated,asioPair,2,
        BindingMode::verifiedAsioRestart).isEmpty());
    mixer.deviceStopped();
    device.inputMask.setBit(2);device.inputMask.clearBit(0);
    device.outputMask=device.inputMask;
    mixer.prepareDevice(&device);
    assert(restore(aggregate,currentStates,true).isEmpty());
    process(true);
    assert(outL.back()==0.f && outR.back()==0.f);
    process(false);
    assert(outL.back()>.02f && outR.back()>outL.back()+.01f);
    const auto reattachAsio=[&](const HardwareBinding& previous,const HardwareBinding& live) {
        mixer.deviceStopped();
        mixer.prepareDevice(&device);
        MixerSessionDocument draft;
        assert(mixer.describeRetainedAsioSession(draft));
        draft.binding=previous;
        const auto pair=MixerDeviceBindingPolicy::proposedOutputs(draft,live,2,
            BindingMode::verifiedAsioRestart);
        assert(MixerDeviceBindingPolicy::apply(draft,live,pair,
            device.inputMask.countNumberOfSetBits(),BindingMode::verifiedAsioRestart).isEmpty());
        armix::device::JuceActivePortSnapshot pins;
        armix::device::JuceActivePortSnapshot::Fault fault;
        assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
        assert(mixer.rebindRetainedAsioSession(std::move(draft),pins,480));
    };
    auto noCapture=reenumerated;
    noCapture.inputMask="0";
    device.inputMask.clear();
    reattachAsio(reenumerated,noCapture);
    assert(mixer.channelIdPhysicalPcmActive() && mixer.waitingAsioInputCount()==1);
    assert(mixer.setNativeInputMix(mixer.epoch(),mixer.controlRevision(),{1},.6f,0.f));
    process(false);
    assert(outL.back()==0.f && outR.back()==0.f);
    device.inputMask.setRange(1,2,true);
    reattachAsio(noCapture,reenumerated);
    assert(mixer.waitingAsioInputCount()==0);
    process(false);
    assert(outL.back()>.02f && outR.back()>outL.back()+.01f);
    device.inputMask.clearBit(2);device.inputMask.setBit(0);
    device.outputMask=device.inputMask;

    // The field failure switched Realtek playback to HDMI while keeping the
    // same VB-Cable source and a live FX rack. The new epoch has working PCM,
    // existing WIRE IDs and a repatchable ChannelId graph after one Apply.
    mixer.deviceStopped();
    device.rate=44100.;
    device.frames=512;
    mixer.prepareDevice(&device);
    auto hdmi=current.binding;
    hdmi.playback="LG TV";
    hdmi.rate=device.rate;hdmi.frames=device.frames;
    const auto hdmiPair=MixerDeviceBindingPolicy::proposedOutputs(
        current,hdmi,2,BindingMode::verifiedDeviceApply);
    assert(MixerDeviceBindingPolicy::apply(current,hdmi,hdmiPair,2,
        BindingMode::verifiedDeviceApply).isEmpty());
    assert(current.request.sources.size()==1 && current.request.sources[0].stereo);
    assert(current.roster.find({1})->sends[1].edgeId==101);
    current.request.maxFrames=512;
    assert(restore(current,currentStates,true).isEmpty());
    process(true);
    assert(outL.back()==0.f && outR.back()==0.f);
    assert(mixer.channelIdPhysicalPcmActive());
    process(false);
    assert(outL.back()>.02f && outR.back()>.02f);

    mixer.deviceStopped();
    device.inputMask.clearBit(1);
    device.rate=44100.;
    device.frames=512;
    mixer.prepareDevice(&device);
    auto nextBinding=current.binding;
    nextBinding.inputMask="1";nextBinding.rate=device.rate;nextBinding.frames=device.frames;
    const auto pair=MixerDeviceBindingPolicy::proposedOutputs(current,nextBinding,2);
    assert(MixerDeviceBindingPolicy::apply(current,nextBinding,pair,1,BindingMode::exact).isNotEmpty());
    assert(MixerDeviceBindingPolicy::apply(current,nextBinding,pair,1,BindingMode::verifiedDeviceApply).isEmpty());
    assert(current.request.sources.empty()); // lost stereo half is never borrowed
    assert(current.roster.find({1})->sends[1].edgeId==101);
    current.request.maxFrames=512;
    assert(restore(current,currentStates,true).isEmpty());
    process(false);
    assert(outL.back()==0.f && outR.back()==0.f);
    auto patch=mixer.patchNativeInputSource(mixer.epoch(),mixer.controlRevision(),
        current.roster.revision(),{1},0,-1,false,true);
    assert(patch.accepted);
    process(false);
    assert(outL.back()>.02f && outR.back()>.02f);
    const std::array<WireChange,3> sourceOnly{{
        {WireChange::Kind::remove,{1},{16},100},
        {WireChange::Kind::remove,{1},{11},101},
        {WireChange::Kind::remove,{1},{2},110}
    }};
    auto wire=mixer.applyNativeWireTransaction(mixer.epoch(),patch.pcmRevision,
                                               patch.rosterRevision,sourceOnly);
    assert(wire.accepted);
    process(false);
    assert(outL.back()==0.f && outR.back()==0.f);
    graph=mixer.nativeBusStatus();
    assert(static_cast<double>(graph.getProperty("channels",{}).getArray()->
        getReference(0).getProperty("peakL",0.))>0.);
    const std::array<WireChange,1> reconnect{{
        {WireChange::Kind::routeOnly,{1},{16},0,1.f}
    }};
    wire=mixer.applyNativeWireTransaction(mixer.epoch(),wire.pcmRevision,
                                         wire.rosterRevision,reconnect);
    assert(wire.accepted);
    process(false);
    assert(outL.back()>.02f && outR.back()>.02f);
    std::cout<<"Native callback cold restore, muted meters, repeated reopen, FX, sparse remap and PCM: PASS\n";
}

static void warmVst3Discovery() {
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixVst3Cache","",false);
    assert(root.createDirectory().wasOk());
    const auto module=root.getChildFile("Shell.vst3");
    assert(module.replaceWithText("module v1"));
    const auto cacheRoot=root.getChildFile("Cache");
    armix::vst::DescriptionCache::Descriptions scanned;
    for(int uid:{31,45,72}) {
        auto item=std::make_unique<juce::PluginDescription>();
        item->fileOrIdentifier=module.getFullPathName();
        item->pluginFormatName="VST3";
        item->name="Shell class "+juce::String(uid);
        item->uniqueId=uid;
        item->deprecatedUid=uid+1000;
        item->isInstrument=uid==45;
        scanned.add(item.release());
    }
    armix::vst::DescriptionCache(cacheRoot).remember(scanned);
    const armix::vst::DescriptionCache nextProcess(cacheRoot);
    const auto cached=nextProcess.find(module.getFullPathName(),72);
    assert(cached&&cached->name=="Shell class 72"&&cached->deprecatedUid==1072);
    assert(nextProcess.read(module.getFullPathName())[1]->uniqueId==45);
    assert(!nextProcess.find(module.getFullPathName(),45));
    assert(module.appendText("updated module binary"));
    assert(!nextProcess.find(module.getFullPathName(),72));
    assert(root.deleteRecursively());
}

// A music deck is an owned virtual source, not a physical ASIO pin. Its
// mixer/FX/WIRE topology survives Show save and a changed capture device.
static void verifyVirtualPlayerSessionSerialization() {
    auto document=makeSession();
    auto rows=document.roster.channels();
    ChannelStripDefinition player;
    player.id={17};player.role=ChannelRole::input;
    player.format=ProcessingFormat::stereo;
    player.name="MUSIC";
    player.sends.push_back({{15},1.f,false,0.f,true,106});
    rows.push_back(player);
    assert(document.roster.restoreExact(document.acceptedRevision,rows));
    document.request.sources.push_back(
        {{17},-1,-1,true,DynamicSourceKind::playerDeck,1});
    MixerSessionDocument decoded;
    const auto json=Codec::encode(document);
    assert(Codec::decode(json,decoded));
    assert(decoded.request.sources.size()==2);
    const auto& music=decoded.request.sources.back();
    assert(music.id.value==17 &&
           music.kind==DynamicSourceKind::playerDeck &&
           music.deckId==1 && music.inputL==-1 && music.inputR==-1 &&
           music.stereo);
    assert(Codec::encode(decoded)==json); // exact deterministic round trip

    auto damaged=juce::JSON::parse(json);
    auto values=damaged.getProperty("request",{})
        .getProperty("sources",{});
    values.getArray()->getReference(1).getDynamicObject()
        ->setProperty("deckId","0");
    assert(!Codec::decode(juce::JSON::toString(damaged,false),decoded));
    damaged=juce::JSON::parse(json);
    values=damaged.getProperty("request",{}).getProperty("sources",{});
    values.getArray()->getReference(1).getDynamicObject()
        ->setProperty("left",0); // never spoof pin 0 on a virtual source
    assert(!Codec::decode(juce::JSON::toString(damaged,false),decoded));

    auto changed=document.binding;
    changed.capture="Different capture interface";
    assert(MixerDeviceBindingPolicy::apply(document,changed,document.outputs,
        2,BindingMode::verifiedDeviceApply).isEmpty());
    assert(document.request.sources.size()==1);
    assert(document.request.sources.front().kind==
           DynamicSourceKind::playerDeck);
    assert(document.request.sources.front().deckId==1);
    assert(document.roster.find({17})->sends.front().destination.value==15);

    // Reconciled ASIO4ALL + ArSon Player: physically disconnected inputs
    // must NOT erase the mixer-owned virtual MUSIC source or its MAIN wire.
    auto virtualOnly=makeSession();
    auto channels=virtualOnly.roster.channels();
    channels.push_back(player);
    assert(virtualOnly.roster.restoreExact(virtualOnly.acceptedRevision,channels));
    virtualOnly.request.sources.clear();
    virtualOnly.request.sources.push_back(
        {{17},-1,-1,true,DynamicSourceKind::playerDeck,1});
    virtualOnly.request.physicalInputs=0;
    virtualOnly.binding.backend="ASIO";
    virtualOnly.binding.capture=virtualOnly.binding.playback="ASIO4ALL v2";
    virtualOnly.binding.inputMask="0";
    virtualOnly.binding.inputNames.clear();
    virtualOnly.binding.outputMask="3";
    virtualOnly.binding.outputNames={"Realtek Speakers 1","Realtek Speakers 2"};
    const auto onlyPlayer=Codec::encode(virtualOnly);
    MixerSessionDocument restoredPlayer;
    assert(Codec::decode(onlyPlayer,restoredPlayer));
    assert(restoredPlayer.request.physicalInputs==0 &&
           restoredPlayer.request.sources.size()==1 &&
           restoredPlayer.request.sources.front().kind==
               DynamicSourceKind::playerDeck);
    assert(Codec::encode(restoredPlayer)==onlyPlayer);
    auto returned=virtualOnly.binding;
    returned.inputMask="3";
    returned.inputNames={"VB-Audio Point 1","VB-Audio Point 2"};
    const auto pair=MixerDeviceBindingPolicy::proposedOutputs(
        restoredPlayer,returned,2,BindingMode::verifiedAsioRestart);
    assert(pair.valid());
    assert(MixerDeviceBindingPolicy::apply(restoredPlayer,returned,pair,2,
        BindingMode::verifiedAsioRestart).isEmpty());
    assert(restoredPlayer.request.physicalInputs==2 &&
           restoredPlayer.request.sources.size()==1 &&
           restoredPlayer.request.sources.front().kind==
               DynamicSourceKind::playerDeck);
    assert(restoredPlayer.roster.find({17})->sends.front()
           .destination.value==15);
    // The reverse transition 2-IN -> 0-IN must also retain the same deck.
    const auto absent=virtualOnly.binding;
    assert(MixerDeviceBindingPolicy::apply(restoredPlayer,absent,pair,0,
        BindingMode::verifiedAsioRestart).isEmpty());
    assert(restoredPlayer.request.sources.size()==1 &&
           restoredPlayer.request.sources.front().deckId==1);
    // Never relax zero-input validation for physical mic ordinals.
    auto bad=juce::JSON::parse(onlyPlayer);
    auto src=bad.getProperty("request",{}).getProperty("sources",{});
    src.getArray()->getReference(0).getDynamicObject()
       ->removeProperty("kind");
    assert(!Codec::decode(juce::JSON::toString(bad,false),restoredPlayer));
}

// Actual production callback fixture: a freshly verified mixer needs ZERO
// microphone patch, lab toggle, extra device or user routing confirmation.
// One native PLAY adds MUSIC -> existing MAIN in one published ChannelId pass.
static void verifyProductionPlayerFirstSoundWithSourceOnly() {
    FixtureDevice device;
    LiveDryRoutingLab mixer;
    juce::AudioPluginFormatManager formats;
    mixer.prepareDevice(&device);
    assert(mixer.startUnpatchedSession(true,false,0,formats,
                                       device.rate,device.frames).isEmpty());
    const auto ep=mixer.epoch();
    assert(mixer.repatchPhysicalSource(0,ep,mixer.revision(),
        "NONE",-1,-1,true,false,0,formats,device.rate,device.frames).isEmpty());
    assert(mixer.admitChannelIdFromAcceptedM4(0,device.frames,true));
    assert(!mixer.hasVerifiedNativeSources()); // player needs no microphone
    // Before admission, controlRevision may denote the legacy M4 revision;
    // once a native MUSIC strip is installed it switches to published PCM.
    // The invariant is +1 on accepted NATIVE graph and a matching control
    // revision AFTER publication, not equality to a prior M4 ordinal.
    const auto beforeGraph=mixer.nativeBusStatus().getProperty(
        "graphRevision","0").toString().getLargeIntValue();
    // New virtual MUSIC can publish directly against an ALREADY trusted
    // MAIN, including while the secondary stream copies MAIN. No physical
    // source pin, output relink, MONITOR ritual or manual arm.
    const auto added=mixer.ensureNativePlayerA(false,true);
    assert(added.accepted && added.created.value!=0);
    assert(mixer.nativePlayerSource()==added.created);
    assert(added.pcmRevision==static_cast<std::uint64_t>(beforeGraph)+1);
    assert(mixer.nativeBusStatus().getProperty("graphRevision","0")
            .toString()==juce::String(added.pcmRevision));
    assert(mixer.controlRevision()==added.pcmRevision);
    assert(mixer.playerDeckHasMainPath());
    // A second Play owns the exact existing source; no duplicate MAIN edge.
    const auto again=mixer.ensureNativePlayerA(false,true);
    assert(again.accepted && again.created==added.created);
    assert(again.pcmRevision==added.pcmRevision);
    const auto accepted=mixer.nativeBusStatus();
    assert(accepted.getProperty("graphRevision","0").toString()
           ==juce::String(added.pcmRevision));
    const auto* channels=accepted.getProperty("channels",{}).getArray();
    assert(channels);
    ChannelId main{};
    int playerCount=0;
    for(const auto& row:*channels) {
        if(row.getProperty("role","").toString()=="MAIN")
            main.value=static_cast<std::uint64_t>(
                row.getProperty("id","0").toString().getLargeIntValue());
        if(row.getProperty("id","").toString()==
           juce::String(static_cast<juce::int64>(added.created.value))) {
            ++playerCount;
            assert(row.getProperty("primaryOutput","0").toString()!="0");
        }
    }
    assert(main.value!=0 && playerCount==1);
    std::array<float,480> silence{},musicL{},musicR{},outL{},outR{};
    musicL.fill(.35f);musicR.fill(-.20f);
    const float* inputs[]={silence.data(),silence.data()};
    float* outputs[]={outL.data(),outR.data()};
    const std::array<DynamicPlayerPcmBlock,1> deck{{{1,musicL.data(),musicR.data()}}};
    auto render=[&] {
        assert(mixer.process(inputs,2,outputs,2,480,1.f,false,
                             nullptr,nullptr,false,nullptr,nullptr,true,deck));
    };
    render();
    assert(outL.back()>.10f && outR.back()<-.05f);
    const auto unplug=mixer.setNativePrimaryOutput(ep,added.pcmRevision,
        added.rosterRevision,added.created,{},true);
    assert(unplug.accepted && !mixer.playerDeckHasMainPath());
    render();
    assert(std::abs(outL.back())<1.e-6f && std::abs(outR.back())<1.e-6f);
    const auto repeated=mixer.ensureNativePlayerA(false,true);
    assert(repeated.accepted && repeated.created==added.created);
    assert(!mixer.playerDeckHasMainPath()); // NEVER undo SOURCE ONLY
    const auto reconnect=mixer.setNativePrimaryOutput(ep,unplug.pcmRevision,
        unplug.rosterRevision,added.created,main,true);
    assert(reconnect.accepted && mixer.playerDeckHasMainPath());
    render();
    assert(outL.back()>.10f && outR.back()<-.05f);
}

static void verifyOfflineLogicalSessionAndFile() {
    NativeLogicalMixerWorkspace workspace;
    assert(workspace.isLogicalOnly() && workspace.revision()==1);
    const auto initial=workspace.snapshot();
    assert(initial.getProperty("authority","").toString()=="LOGICAL_ONLY");
    assert(!static_cast<bool>(initial.getProperty("physicalOutputAdmitted",true)));
    assert(!static_cast<bool>(initial.getProperty("outputArmed",true)));
    const auto graph=initial.getProperty("graph",{});
    const auto caps=graph.getProperty("capabilities",{});
    assert(!static_cast<bool>(caps.getProperty("callbackOwnsChannelIds",true)));
    assert(graph.getProperty("channels",{}).getArray()->size()==15);
    auto command=[&](const char* operation,const juce::var& payload,
                     const juce::String& id=juce::String{}) {
        auto request=juce::var(new juce::DynamicObject());
        request.getDynamicObject()->setProperty("operation",operation);
        request.getDynamicObject()->setProperty("workspaceToken",workspace.workspaceToken());
        request.getDynamicObject()->setProperty("baseRevision",
            juce::String(std::to_string(workspace.revision())));
        request.getDynamicObject()->setProperty("channelId",id);
        request.getDynamicObject()->setProperty("payload",payload);
        return request;
    };
    auto name=juce::var(new juce::DynamicObject());
    name.getDynamicObject()->setProperty("name","Offline Backing");
    auto created=workspace.apply(command("CREATE_CHANNEL",name));
    assert(created.accepted && created.createdId.isNotEmpty());
    const auto track=created.createdId;
    assert(workspace.revision()==2);
    auto fx=juce::var(new juce::DynamicObject());
    fx.getDynamicObject()->setProperty("catalogId",
        "armix.builtin.channel-compressor.v1");
    assert(workspace.apply(command("ADD_FX_REFERENCE",fx,track)).accepted);
    auto rename=juce::var(new juce::DynamicObject());
    rename.getDynamicObject()->setProperty("name","Vocal Backing");
    assert(workspace.apply(command("RENAME_CHANNEL",rename,track)).accepted);
    // No fake callback/output admission is ever supplied by the GUI status.
    const auto projected=workspace.snapshot();
    assert(!static_cast<bool>(projected.getProperty("physicalOutputAdmitted",true)));
    const auto rows=projected.getProperty("graph",{}).getProperty("channels",{});
    bool found=false;
    for(const auto& row:*rows.getArray())
        if(row.getProperty("id","").toString()==track) {
            assert(row.getProperty("name","").toString()=="Vocal Backing");
            assert(row.getProperty("inserts",{}).getArray()->size()==1);
            found=true;
        }
    assert(found);
    // Replacing the document invalidates pending intents even if its exact
    // revision (or even the saved document ID) is the same.
    auto stale=command("CREATE_CHANNEL",name);
    auto replacement=workspace;
    assert(replacement.load(workspace.document(),workspace.states()));
    assert(replacement.revision()==workspace.revision());
    auto staleAck=replacement.apply(stale);
    assert(!staleAck.accepted && staleAck.error=="STALE_LOGICAL_REVISION");

    const auto json=Codec::encode(workspace.document());
    MixerSessionDocument restored;
    assert(Codec::decode(json,restored));
    assert(restored.logicalOnly);
    assert(restored.binding.backend.isEmpty() &&
           restored.binding.playback.isEmpty() &&
           restored.binding.outputMask=="0");
    assert(restored.outputs.activeOutputChannels==0);
    assert(restored.roster.find({static_cast<std::uint64_t>(track.getLargeIntValue())}));
    auto forged=juce::JSON::parse(json);
    forged.getProperty("binding",{}).getDynamicObject()->setProperty(
        "playback","Unverified Speaker");
    assert(!Codec::decode(juce::JSON::toString(forged,false),restored));
    forged=juce::JSON::parse(json);
    juce::Array<juce::var> fakePins;
    fakePins.add("Unverified mic");
    forged.getProperty("binding",{}).getDynamicObject()->setProperty(
        "inputNames",juce::var(fakePins));
    assert(!Codec::decode(juce::JSON::toString(forged,false),restored));
    forged=juce::JSON::parse(json);
    forged.getProperty("outputs",{}).getDynamicObject()->setProperty("active",2);
    assert(!Codec::decode(juce::JSON::toString(forged,false),restored));
    const auto folder=juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile("armix-offline-logical-","",false);
    assert(folder.createDirectory().wasOk());
    const auto file=folder.getChildFile("saved.smix");
    assert(NativeMixerSessionStore::writeFile(file,workspace.document(),{}).isEmpty());
    std::vector<BinaryState> states;
    assert(NativeMixerSessionStore::readFile(file,restored,states).isEmpty());
    assert(restored.logicalOnly && states.empty());
    // The automatic HEAD generation must accept the identical unbound
    // file; re-open cannot depend on an installed ASIO/WASAPI device.
    NativeMixerSessionStore liveStore(folder);
    assert(liveStore.commit(workspace.document(),{}).isEmpty());
    MixerSessionDocument verifiedHead;
    std::vector<BinaryState> headStates;
    assert(liveStore.load(verifiedHead,headStates).isEmpty());
    assert(verifiedHead.logicalOnly &&
           verifiedHead.roster.revision()==workspace.revision() &&
           verifiedHead.binding.playback.isEmpty() && headStates.empty());
    NativeLogicalMixerWorkspace reopened;
    assert(reopened.load(restored));
    assert(reopened.revision()==workspace.revision());
    assert(reopened.snapshot().getProperty("graph",{}).getProperty(
        "channels",{}).getArray()->size()==16);
    // A failed disk transaction must leave both the live snapshot and the
    // last verified on-disk generation unchanged.
    auto candidate=workspace;
    assert(candidate.apply(command("CREATE_CHANNEL",name)).accepted);
    const auto obstructed=folder.getChildFile("blocked");
    assert(obstructed.replaceWithText("regular file"));
    assert(NativeMixerSessionStore(obstructed).commit(candidate.document(),{}).isNotEmpty());
    assert(workspace.revision()==reopened.revision());
    assert(liveStore.load(verifiedHead,headStates).isEmpty() &&
           verifiedHead.acceptedRevision==workspace.revision());
    // Edit a later slot even when an earlier alpha.9 slot only has a
    // catalog reference. Persist the actual processor bytes without PCM.
    fx.getDynamicObject()->setProperty("catalogId",
        armix::builtin_fx::ChannelEqProcessor::catalogIdentity);
    assert(workspace.apply(command("ADD_FX_REFERENCE",fx,track)).accepted);
    const ChannelId trackId{static_cast<std::uint64_t>(track.getLargeIntValue())};
    armix::builtin_fx::ChannelEqProcessor eq;
    eq.prepareToPlay(48000.,256);
    auto band=eq.acceptedBands()[2];
    band.gainDb=5.f;
    assert(eq.queueBand(2,band,1));
    eq.consumeRealtimeEdits();
    armix::builtin_fx::ChannelEqProcessor::Completion applied{};
    assert(eq.pollCompletion(applied)&&applied.applied);
    juce::MemoryBlock bytes;
    eq.getStateInformation(bytes);
    StoredInsert insert;
    insert.channel=trackId;
    insert.slot=1;
    insert.catalogId=armix::builtin_fx::ChannelEqProcessor::catalogIdentity;
    insert.name=eq.getName();
    assert(workspace.saveFx(insert,std::move(bytes)));
    assert(liveStore.commit(workspace.document(),workspace.states()).isEmpty());
    assert(liveStore.load(verifiedHead,headStates).isEmpty()&&headStates.size()==1);
    assert(verifiedHead.logicalOnly&&verifiedHead.inserts.front().slot==1);
    armix::builtin_fx::ChannelEqProcessor reopenedEq;
    reopenedEq.prepareToPlay(48000.,256);
    assert(reopenedEq.restoreUnpublishedState(headStates.front().bytes.getData(),
        static_cast<int>(headStates.front().bytes.getSize())));
    assert(reopenedEq.acceptedBands()[2].gainDb==5.f);
    auto slot=juce::var(new juce::DynamicObject());
    slot.getDynamicObject()->setProperty("slot",0);
    assert(workspace.apply(command("REMOVE_FX_REFERENCE",slot,track)).accepted);
    assert(workspace.storedFx(trackId,0)&&workspace.states().size()==1);
    assert(workspace.apply(command("REMOVE_FX_REFERENCE",slot,track)).accepted);
    assert(!workspace.storedFx(trackId,0)&&workspace.states().empty());
    // No source comes from the borrowed MAIN pair. A deleted legacy strip
    // stays deleted, while its compatibility entry is explicitly absent.
    assert(workspace.apply(command("DELETE_CHANNEL",{},"1")).accepted);
    auto toBind=workspace.document();
    const auto binding=makeSession().binding;
    const VerifiedNativeOutputs outputs{2,0,1,-1,-1,false};
    assert(!Codec::bindLogicalToVerifiedMain(toBind,binding,{},2));
    assert(toBind.logicalOnly);
    assert(Codec::bindLogicalToVerifiedMain(toBind,binding,outputs,2));
    assert(!toBind.logicalOnly && toBind.request.sources.empty() &&
           toBind.request.physicalInputs==2 && toBind.legacyIds.inputs[0].value==0);
    assert(Codec::decode(Codec::encode(toBind),restored) && !restored.roster.find({1}));
    assert(folder.deleteRecursively());
}

static void verifyLegacyHarmoniceSessionName(){
    auto saved=makeSession();
    const juce::String id=
        armix::builtin_fx::ChannelHarmonyProcessor::catalogIdentity;
    auto rows=saved.roster.channels();
    rows.front().insertCatalogIds.push_back(id.toStdString());
    assert(saved.roster.restoreExact(saved.acceptedRevision,rows));
    armix::builtin_fx::ChannelHarmonyProcessor processor;
    processor.prepareToPlay(48000.,256);
    assert(processor.isReady());
    BinaryState state;
    processor.getStateInformation(state.bytes);
    state.sha256=juce::SHA256(state.bytes).toHexString();
    saved.inserts.push_back({{1},0,id,{},state.sha256,
                             "ArSon Harmony",0,false});
    juce::AudioPluginFormatManager formats;
    std::shared_ptr<DynamicChannelFxLeaseBank> restored;
    const auto error=NativeMixerSessionFxRestore::prepare(
        saved,std::vector<BinaryState>{state},formats,48000.,256,1,1,restored);
    assert(error.isEmpty() && restored);
    const auto* lease=restored->ownerForControl({1},0);
    assert(lease && *lease && (*lease)->name=="Harmonice");
    assert((*lease)->catalogId==id);
    assert(saved.inserts.front().name=="ArSon Harmony");
    assert((*lease)->processor->getLatencySamples()==1536);
}
static void verifyLogicalControlTransactions() {
    juce::ScopedJuceInitialiser_GUI juce;
    const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("armix-logical-control-","",false);
    assert(folder.createDirectory().wasOk());
    NativeMixerSessionSaveWorker worker;
    int commits=0;
    NativeLogicalMixerControl control(folder,worker,[&](const juce::String& id){
        assert(id.isNotEmpty());
        ++commits;
    });
    assert(control.ensureLoaded().error.isEmpty());
    auto command=[&](const char* requestId,const char* operation,
                     const juce::String& id={},juce::var payload={},juce::var value={}) {
        auto request=std::make_unique<juce::DynamicObject>();
        const auto before=control.readback();
        request->setProperty("requestId",requestId);
        request->setProperty("operation",operation);
        request->setProperty("channelId",id);
        request->setProperty("baseRevision",before.getProperty("revision",{}));
        request->setProperty("workspaceToken",before.getProperty("workspaceToken",{}));
        request->setProperty("payload",std::move(payload));
        request->setProperty("value",std::move(value));
        return juce::var(request.release());
    };
    juce::Array<juce::var> acks;
    const auto submit=[&](const juce::var& request,
                           NativeLogicalMixerControl::Availability access={false,true,false}) {
        control.handleIntent(request,access,
            [](const juce::String& id){return armix::builtin_fx::findNativeBuiltInFx(id)!=nullptr;},
            [](const auto& workspace,ChannelId channel,std::size_t slot,auto& insert,auto& processor){
                insert.channel=channel;
                insert.slot=slot;
                insert.catalogId=juce::String(workspace.document().roster.find(channel)
                    ->insertCatalogIds[slot]);
                juce::AudioPluginFormatManager formats;
                const auto error=NativeMixerSessionFxRestore::prepareProcessor(
                    insert,nullptr,formats,48000.,256,processor);
                if(error.isEmpty())insert.name=processor->getName();
                return error;
            },[&](juce::var ack){acks.add(std::move(ack));});
    };
    const auto waitForIO=[&]{
        const auto deadline=juce::Time::getMillisecondCounter()+10000;
        while(worker.busy() && juce::Time::getMillisecondCounter()<deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        assert(!worker.busy());
    };
    const auto reject=[&](const char* reason){
        const auto ack=acks.getLast();
        assert(ack.getProperty("status",{}).toString()=="REJECTED");
        assert(ack.getProperty("errorCode",{}).toString()==reason);
        assert(ack.getProperty("revision",{})==control.readback().getProperty("revision",{}));
    };
    const auto accept=[&](const juce::var& request){
        const auto before=control.readback().getProperty("revision",{});
        const int count=acks.size();
        submit(request);
        assert(acks.size()==count && worker.busy());
        assert(control.readback().getProperty("revision",{})==before);
        waitForIO();
        assert(acks.size()==count+1);
        const auto ack=acks.getLast();
        assert(ack.getProperty("requestId",{})==request.getProperty("requestId",{}));
        assert(ack.getProperty("status",{}).toString()=="ACCEPTED");
        const auto after=control.readback();
        assert(ack.getProperty("revision",{})==after.getProperty("revision",{}));
        assert(after.getProperty("revision",{})!=before);
        assert(after.getProperty("authority",{}).toString()=="LOGICAL_ONLY");
        assert(!static_cast<bool>(ack.getProperty("physicalOutputAdmitted",true)));
        assert(!static_cast<bool>(after.getProperty("graph",{}).getProperty("capabilities",{})
            .getProperty("callbackOwnsChannelIds",true)));
        return ack.getProperty("createdId",{}).toString();
    };
    auto create=command("logical-create","CREATE_CHANNEL",{},
        juce::JSON::parse(R"({"name":"Lead"})"));
    submit(create,{false,false,false});
    reject("PHYSICAL_PCM_AUTHORITY_ACTIVE");
    auto stale=command("logical-stale","CREATE_GROUP");
    const auto channel=accept(create);
    submit(create);
    reject("DUPLICATE_LOGICAL_REQUEST");
    submit(stale);
    reject("STALE_LOGICAL_REVISION");
    const auto bus=accept(command("logical-bus","CREATE_GROUP",{},
        juce::JSON::parse(R"({"name":"Vocals"})")));
    auto wire=juce::JSON::parse(R"({"changes":[{"op":"ROUTE_ONLY"}]})");
    auto* wireChange=wire.getProperty("changes",{})[0].getDynamicObject();
    wireChange->setProperty("from",channel);
    wireChange->setProperty("to",bus);
    accept(command("logical-wire","WIRE_TRANSACTION",channel,wire));
    accept(command("logical-rename","RENAME_CHANNEL",channel,
        juce::JSON::parse(R"({"name":"Lead Vox"})")));
    for(const auto& fx:armix::builtin_fx::nativeBuiltInFx) {
        auto payload=std::make_unique<juce::DynamicObject>();
        payload->setProperty("catalogId",fx.id);
        accept(command((juce::String("logical-fx-")+fx.id).toRawUTF8(),
            "ADD_FX_REFERENCE",bus,juce::var(payload.release())));
    }
    accept(command("logical-bypass","SET_FX_SLOT_BYPASS",bus,
        juce::JSON::parse(R"({"slot":1,"enabled":true})")));
    accept(command("logical-rack","SET_FX_RACK_BYPASS",bus,{},true));
    const auto file=folder.getChildFile("concert.smix");
    assert(control.exportTo(file,[](juce::String error){assert(error.isEmpty());}));
    waitForIO();
    MixerSessionDocument saved;
    std::vector<BinaryState> states;
    assert(NativeMixerSessionStore::readFile(file,saved,states).isEmpty());
    assert(saved.logicalOnly && saved.request.physicalInputs==0);
    const ChannelId trackId{static_cast<std::uint64_t>(channel.getLargeIntValue())};
    const ChannelId busId{static_cast<std::uint64_t>(bus.getLargeIntValue())};
    assert(saved.roster.find(trackId)->name=="Lead Vox");
    assert(saved.roster.find(trackId)->sends.front().destination==busId);
    assert(saved.inserts.size()==armix::builtin_fx::nativeBuiltInFx.size());
    assert(saved.inserts[1].bypass && saved.rackBypassed.front()==busId);
    juce::AudioPluginFormatManager formats;
    for(const auto& insert:saved.inserts) {
        const auto state=std::find_if(states.begin(),states.end(),[&](const auto& blob){
            return blob.sha256==insert.sha256;
        });
        assert(state!=states.end());
        std::unique_ptr<juce::AudioProcessor> restored;
        assert(NativeMixerSessionFxRestore::prepareProcessor(
            insert,&*state,formats,48000.,256,restored).isEmpty());
        assert(restored && restored->getName()==insert.name);
    }

    // A second request while an actual commit is queued sees the old model
    // and rejects. Force the next disk promotion to fail at PREVIOUS, while
    // HEAD still points to the last accepted and independently readable state.
    auto remove=command("logical-remove","REMOVE_FX_REFERENCE",bus,
        juce::JSON::parse(R"({"slot":0})"));
    submit(remove);
    submit(command("logical-busy","DELETE_CHANNEL",channel));
    reject("LOGICAL_EDITOR_BUSY");
    waitForIO();
    assert(acks.getLast().getProperty("status",{}).toString()=="ACCEPTED");
    const auto oldReadback=juce::JSON::toString(control.readback(),true);
    const int oldCommits=commits;
    const auto previous=folder.getChildFile("MixerSession").getChildFile("PREVIOUS");
    assert(previous.deleteFile());
    assert(previous.createDirectory().wasOk());
    assert(previous.getChildFile("sentinel").replaceWithText("force failed pointer write"));
    submit(command("logical-failed","DELETE_CHANNEL",channel));
    waitForIO();
    const auto failed=acks.getLast();
    assert(failed.getProperty("requestId",{}).toString()=="logical-failed");
    assert(failed.getProperty("status",{}).toString()=="REJECTED");
    assert(juce::JSON::toString(control.readback(),true)==oldReadback);
    assert(commits==oldCommits);
    MixerSessionDocument head;
    std::vector<BinaryState> headStates;
    assert(NativeMixerSessionStore(folder).load(head,headStates).isEmpty());
    assert(head.acceptedRevision==control.workspace().revision());
    assert(head.roster.find(trackId) && head.inserts.size()==saved.inserts.size()-1);
    assert(previous.deleteRecursively());

    accept(command("logical-delete","DELETE_CHANNEL",channel));
    accept(command("logical-remove-bus","REMOVE_BUS",bus));
    bool opened=false;
    assert(worker.load(file,[&](juce::String error,MixerSessionDocument document,
                                std::vector<BinaryState> binary){
        assert(error.isEmpty());
        assert(control.open(std::move(document),std::move(binary),[&](juce::String openError){
            assert(openError.isEmpty());
            opened=true;
        }));
    }));
    waitForIO();
    assert(opened);
    assert(control.workspace().document().roster.find(trackId));
    assert(control.workspace().document().roster.find(busId));
    assert(folder.deleteRecursively());
}

// H1: a saved VST3 Power mapping survives a real .smix Save/Open and the
// restored native FX lease keeps the EXACT token. A plugin replaced at that
// slot cannot inherit the shortcut. Legacy files omit instanceId completely.
static void verifyDurableNativeFxPowerIdentity() {
    using FxOwner=armix::mixer::LiveChannelFxGraph::FxInstance;
    auto doc=makeSession();
    auto rows=doc.roster.channels();
    for(auto& channel:rows)
        if(channel.id.value==5)
            channel.insertCatalogIds={"fixture.vst3.power"};
    assert(doc.roster.restoreExact(doc.acceptedRevision,rows));

    StoredInsert saved;
    saved.channel={5};saved.slot=0;
    saved.catalogId="fixture.vst3.power";
    saved.name="Power Fixture";
    saved.sha256=juce::String::repeatedString("0123456789abcdef",4);
    saved.bypass=true;
    // An ID comes from the native control owner, NOT the .smix file name,
    // catalog identity, user-facing FX label or slot ordinal.
    saved.instanceId="g5c-724";
    doc.inserts.push_back(saved);
    doc.hotkeys.push_back({"power-hotkey","SESSION",doc.id,"5",
        "SET_FX_SLOT_BYPASS",0x76,6,0,saved.instanceId});

    const auto durable=Codec::encode(doc);
    MixerSessionDocument reopened;
    assert(Codec::decode(durable,reopened));
    assert(reopened.inserts.size()==1&&reopened.hotkeys.size()==1);
    assert(reopened.inserts[0].instanceId=="g5c-724");
    assert(reopened.hotkeys[0].insertId==reopened.inserts[0].instanceId);
    assert(Codec::encode(reopened)==durable);
    // No vendor DSP is invoked in this fixture: construction and identity
    // reservation are CONTROL-thread state only.
    FxOwner restored(nullptr,"fixture.vst3.power","Power Fixture",64,
        reopened.inserts[0].instanceId);
    assert(restored.instanceId==reopened.hotkeys[0].insertId);
    FxOwner next(nullptr,"fixture.vst3.power","Next Insert",64);
    assert(next.instanceId.startsWith("g5c-"));
    assert(next.instanceId.substring(4).getLargeIntValue()>724);

    auto invalid=juce::JSON::parse(durable);
    auto inserts=invalid.getProperty("inserts",{});
    assert(inserts.isArray());
    inserts.getArray()->getReference(0).getDynamicObject()
        ->setProperty("instanceId","g5c-0");
    assert(!Codec::decode(juce::JSON::toString(invalid,false),reopened));
    invalid=juce::JSON::parse(durable);
    inserts=invalid.getProperty("inserts",{});
    inserts.getArray()->getReference(0).getDynamicObject()
        ->setProperty("instanceId","not-native-token");
    assert(!Codec::decode(juce::JSON::toString(invalid,false),reopened));
    invalid=juce::JSON::parse(durable);
    inserts=invalid.getProperty("inserts",{});
    inserts.getArray()->getReference(0).getDynamicObject()
        ->setProperty("instanceId",juce::var(24));
    assert(!Codec::decode(juce::JSON::toString(invalid,false),reopened));

    // A second channel must not claim the same native FX instance identity.
    auto duplicate=doc;
    auto second=saved;second.channel={6};
    duplicate.inserts.push_back(second);
    auto withSecond=duplicate.roster.channels();
    for(auto& channel:withSecond)
        if(channel.id.value==6)
            channel.insertCatalogIds={"fixture.vst3.power"};
    assert(duplicate.roster.restoreExact(duplicate.acceptedRevision,withSecond));
    assert(!Codec::decode(Codec::encode(duplicate),reopened));

    // Older .smix files containing vendor blobs but no native FX token decode
    // normally; adding durable identity does not force a schema bump.
    auto legacy=doc;
    legacy.inserts[0].instanceId.clear();
    legacy.hotkeys.clear();
    const auto old=Codec::encode(legacy);
    assert(Codec::decode(old,reopened));
    assert(reopened.inserts[0].instanceId.isEmpty());
}

static void verifyHotkeyDocumentIdentityAndMigration() {
    auto doc=makeSession();
    const auto legacy=Codec::encode(doc);
    MixerSessionDocument decoded;
    assert(Codec::decode(legacy,decoded) && decoded.hotkeys.empty());
    const juce::String session=doc.id;
    armix::hotkey::Mapping first{"native-ch05","SESSION",session,"5","SET_MUTE",0x77,6};
    armix::hotkey::Mapping second{"native-bus","SESSION",session,"16","SET_SOLO",0x78,6};
    armix::hotkey::Mapping rack{"fx-rack-05","SESSION",session,"5",
        "SET_FX_RACK_BYPASS",0x76,6};
    armix::hotkey::Mapping slot{"fx-slot-05","SESSION",session,"5",
        "SET_FX_SLOT_BYPASS",0x75,6,1,"plugin-instance-original"};
    doc.hotkeys={first,second,rack,slot};
    const auto stable=Codec::encode(doc);
    assert(Codec::decode(stable,decoded));
    assert(decoded.hotkeys.size()==4);
    assert(decoded.hotkeys[2].operation=="SET_FX_RACK_BYPASS");
    assert(decoded.hotkeys[3].slot==1 &&
           decoded.hotkeys[3].insertId=="plugin-instance-original");
    assert(decoded.hotkeys[0].channelId=="5"&&decoded.hotkeys[1].channelId=="16");
    // Rename or visual channel reorder cannot retarget numeric ChannelId.
    auto renamed=decoded.roster.channels();
    for(auto& row:renamed)if(row.id.value==5)row.name="New Singer Name";
    assert(decoded.roster.restoreExact(decoded.acceptedRevision,renamed));
    assert(Codec::decode(Codec::encode(decoded),doc));
    assert(doc.hotkeys[0].channelId=="5");
    auto damaged=juce::JSON::parse(stable);
    auto hotkeys=damaged.getProperty("hotkeys",{});
    auto* rows=hotkeys.getArray();
    assert(rows&&rows->size()==4);
    rows->getReference(0).getDynamicObject()->setProperty("channelId","999999");
    // Deleting a track does not make the entire mixer unsaveable. The
    // orphan shortcut remains visible but can never fire for another ID.
    assert(Codec::decode(juce::JSON::toString(damaged,false),decoded));
    assert(decoded.hotkeys[0].channelId=="999999");
    damaged=juce::JSON::parse(stable);
    hotkeys=damaged.getProperty("hotkeys",{});
    rows=hotkeys.getArray();
    rows->getReference(0).getDynamicObject()->setProperty("channelId","05");
    assert(!Codec::decode(juce::JSON::toString(damaged,false),decoded));
    damaged=juce::JSON::parse(stable);
    hotkeys=damaged.getProperty("hotkeys",{});
    rows=hotkeys.getArray();
    rows->getReference(1).getDynamicObject()->setProperty("vk",0x77);
    assert(!Codec::decode(juce::JSON::toString(damaged,false),decoded));
    damaged=juce::JSON::parse(stable);
    hotkeys=damaged.getProperty("hotkeys",{});
    rows=hotkeys.getArray();
    rows->getReference(0).getDynamicObject()->setProperty("sessionId","other");
    assert(!Codec::decode(juce::JSON::toString(damaged,false),decoded));
    damaged=juce::JSON::parse(stable);
    hotkeys=damaged.getProperty("hotkeys",{});
    hotkeys.getArray()->getReference(3).getDynamicObject()->setProperty("slot",8);
    assert(!Codec::decode(juce::JSON::toString(damaged,false),decoded));
    damaged=juce::JSON::parse(stable);
    hotkeys=damaged.getProperty("hotkeys",{});
    hotkeys.getArray()->getReference(3).getDynamicObject()->setProperty("insertId","");
    assert(!Codec::decode(juce::JSON::toString(damaged,false),decoded));
}

int main(){
    verifyDurableNativeFxPowerIdentity();
    verifyHotkeyDocumentIdentityAndMigration();
    verifyLogicalControlTransactions();
    verifyLegacyHarmoniceSessionName();
    verifyOfflineLogicalSessionAndFile();
    verifyProductionPlayerFirstSoundWithSourceOnly();
    verifyVirtualPlayerSessionSerialization();
    // Zero-input is a legitimate saved SOURCE NONE workspace, never a fake mic.
    {
        auto silent=makeSession();
        silent.binding={"Windows Audio","","Speakers","0","3",48000.,256};
        silent.request.sources.clear();silent.request.physicalInputs=0;
        MixerSessionDocument decoded;
        assert(Codec::decode(Codec::encode(silent),decoded));
        assert(decoded.request.physicalInputs==0 && decoded.request.sources.empty());
        assert(decoded.roster.revision()==silent.roster.revision());
        assert(MixerDeviceBindingPolicy::apply(decoded,decoded.binding,
            decoded.outputs,0,BindingMode::exact).isEmpty());
        auto malformed=juce::JSON::parse(Codec::encode(silent));
        malformed.getProperty("binding",{}).getDynamicObject()->setProperty("inputMask","3");
        assert(!Codec::decode(juce::JSON::toString(malformed,false),decoded));
    }
    outputOnlyNativeStartup();
    warmVst3Discovery();
    nativeDeviceRecovery();
    auto d=makeSession();
    const auto before=Codec::encode(d);
    MixerSessionDocument restored;
    assert(Codec::decode(before,restored));
    assert(restored.roster.size()==16);
    assert(restored.roster.revision()==52);
    assert(Codec::encode(restored)==before);
    const auto* vocal=restored.roster.find({1});
    assert(vocal&&vocal->sends.size()==2);
    assert(vocal->sends[0].edgeId==100&&vocal->sends[0].destination.value==16);
    assert(vocal->sends[1].preFader&&vocal->sends[1].gain==.3f);
    assert(restored.request.sources.size()==1);
    assert(restored.request.sources[0].inputL==0);
    assert(restored.binding.backend=="ASIO");
    assert(restored.outputs.mainR==1);
    // Refuse unconfirmed or incompatible device remaps without mutating
    // the saved ChannelId/EdgeId graph; accept only a verified live pair.
    auto remap=d;
    HardwareBinding replacement={"WASAPI","New capture","New playback",
                                 "5","c",44100.,512};
    VerifiedNativeOutputs verifiedPair={2,1,0,-1,-1,false};
    const auto graphBefore=Codec::encode(remap);
    auto refusal=MixerDeviceBindingPolicy::apply(remap,replacement,
                                                 verifiedPair,2,BindingMode::exact);
    assert(refusal.startsWith("NEEDS_REMAP"));
    assert(Codec::encode(remap)==graphBefore);
    refusal=MixerDeviceBindingPolicy::apply(remap,replacement,
                                            verifiedPair,1,BindingMode::verifiedDeviceApply);
    assert(refusal.startsWith("NEEDS_REMAP"));
    assert(Codec::encode(remap)==graphBefore);
    assert(MixerDeviceBindingPolicy::apply(remap,replacement,
                                           verifiedPair,2,BindingMode::verifiedDeviceApply).isEmpty());
    assert(remap.binding.backend=="WASAPI" && remap.outputs.mainL==1);
    assert(remap.roster.revision()==d.roster.revision());
    assert(remap.roster.find({1})->sends.size()==2 &&
           remap.roster.find({1})->sends[0].edgeId==100);
    auto captureChanged=d;
    auto identicalMasks=d.binding;
    identicalMasks.capture="Another capture";
    assert(MixerDeviceBindingPolicy::apply(captureChanged,identicalMasks,d.outputs,2,
        BindingMode::verifiedDeviceApply).isEmpty());
    assert(captureChanged.request.sources.empty());
    assert(captureChanged.roster.find({1})->sends[0].edgeId==100);

    // Output 3/4 was explicitly assigned to MAIN. Disabling those pins
    // must NEVER reassign MAIN to still-enabled 1/2 after Audio I/O Apply:
    // rejection keeps the original logical/physical saved session intact.
    auto main34=d;
    main34.binding.outputMask="f";
    main34.binding.outputNames={"OUT 1","OUT 2","OUT 3","OUT 4"};
    main34.outputs={4,2,3,-1,-1,false};
    const auto main34Before=Codec::encode(main34);
    auto only12=main34.binding;
    only12.outputMask="3";
    const auto missingPair=MixerDeviceBindingPolicy::proposedOutputs(
        main34,only12,2,BindingMode::verifiedDeviceApply);
    assert(!missingPair.valid()); // never silently return {2,0,1}
    assert(MixerDeviceBindingPolicy::apply(main34,only12,missingPair,2,
        BindingMode::verifiedDeviceApply).startsWith("NEEDS_REMAP"));
    // Even a caller supplying a syntactically valid but unauthorized MAIN
    // pair must not bypass the physical-pin identity check.
    const VerifiedNativeOutputs unauthorized12={2,0,1,-1,-1,false};
    assert(MixerDeviceBindingPolicy::apply(main34,only12,unauthorized12,2,
        BindingMode::verifiedDeviceApply).startsWith("NEEDS_REMAP"));
    assert(Codec::encode(main34)==main34Before);
    // Deliberate MAIN assignment through the verified current two-pin mask
    // is distinct from a normal Apply that must preserve the original pair.
    auto explicitlyRepatched=main34;
    const auto explicitPair=MixerDeviceBindingPolicy::proposedOutputs(
        main34,only12,2,BindingMode::verifiedMainPinApply);
    assert(explicitPair.valid() && explicitPair.mainL==0 && explicitPair.mainR==1);
    assert(MixerDeviceBindingPolicy::apply(explicitlyRepatched,only12,explicitPair,2,
        BindingMode::verifiedMainPinApply).isEmpty());
    assert(explicitlyRepatched.binding.outputMask=="3");
    assert(explicitlyRepatched.roster.revision()==main34.roster.revision());
    assert(!MixerDeviceBindingPolicy::proposedOutputs(main34,main34.binding,4,
        BindingMode::verifiedMainPinApply).valid());
    // If outputs 3/4 remain physically selected, ordinal 2/3 -> 0/1
    // is a legal SAME-PIN remap, not an automatic route to different jacks.
    auto only34=main34.binding;
    only34.outputMask="c";
    const auto preservedMain=MixerDeviceBindingPolicy::proposedOutputs(
        main34,only34,2,BindingMode::verifiedDeviceApply);
    assert(preservedMain.valid() && preservedMain.mainL==0 && preservedMain.mainR==1);
    auto main34Accepted=main34;
    assert(MixerDeviceBindingPolicy::apply(main34Accepted,only34,preservedMain,2,
        BindingMode::verifiedDeviceApply).isEmpty());
    assert(main34Accepted.outputs.mainL==0 && main34Accepted.outputs.mainR==1);
    assert(main34Accepted.binding.outputMask=="c");
    assert(main34Accepted.roster.revision()==main34.roster.revision());
    // Changed ASIO output identity (or unknown inventory with changed
    // mask) must fail closed until the user chooses an explicit MAIN pair.
    auto renamed34=main34.binding;
    renamed34.outputNames={"Other 1","Other 2","Other 3","Other 4"};
    assert(!MixerDeviceBindingPolicy::proposedOutputs(main34,renamed34,4,
        BindingMode::verifiedDeviceApply).valid());
    auto newOutputs=main34.binding;
    newOutputs.playback="Different playback endpoint";
    assert(!MixerDeviceBindingPolicy::proposedOutputs(main34,newOutputs,4,
        BindingMode::verifiedDeviceApply).valid());

    auto reordered=d;
    reordered.binding.inputNames={"In 1","In 2"};
    auto reorderedPins=reordered.binding;
    reorderedPins.inputNames={"In 2","In 1"};
    assert(MixerDeviceBindingPolicy::apply(reordered,reorderedPins,d.outputs,2,
        BindingMode::exact).isNotEmpty());
    assert(MixerDeviceBindingPolicy::apply(reordered,reorderedPins,d.outputs,2,
        BindingMode::verifiedDeviceApply).isEmpty());
    assert(reordered.request.sources.size()==d.request.sources.size());
    assert(reordered.request.sources.front().inputL==1);
    assert(reordered.request.sources.front().inputR==-1);

    // ASIO4ALL can reorder the VB-Cable input and the Realtek pair even
    // though JUCE still reports the same registered driver name.
    auto asio=d;
    asio.binding.backend="ASIO";
    asio.request.sources.front().stereo=true;
    asio.request.sources.front().inputR=1;
    asio.binding.capture=asio.binding.playback="ASIO4ALL v2";
    asio.binding.inputNames={"VB-Audio Point 1","VB-Audio Point 2"};
    asio.binding.outputNames={"2nd output with SST 1","2nd output with SST 2"};
    asio.binding.outputMask="3";
    asio.outputs={2,0,1,-1,-1,false};
    auto shifted=asio.binding;
    shifted.inputNames={"New input","VB-Audio Point 1","VB-Audio Point 2"};
    shifted.inputMask="6";
    shifted.outputNames={"New output","2nd output with SST 2","2nd output with SST 1"};
    shifted.outputMask="6";
    const auto shiftedPair=MixerDeviceBindingPolicy::proposedOutputs(asio,shifted,2,
        BindingMode::verifiedAsioRestart);
    assert(shiftedPair.valid() && shiftedPair.mainL==1 && shiftedPair.mainR==0);
    auto shiftedSession=asio;
    assert(MixerDeviceBindingPolicy::apply(shiftedSession,shifted,shiftedPair,2,
        BindingMode::verifiedAsioRestart).isEmpty());
    assert(shiftedSession.request.sources.front().inputL==0);
    assert(shiftedSession.request.sources.front().inputR==1);
    assert(shiftedSession.roster.revision()==asio.roster.revision());
    juce::BigInteger oldCapturePins;
    oldCapturePins.setRange(0,2,true);
    const auto translated=MixerDeviceBindingPolicy::translateNamedPins(
        oldCapturePins,asio.binding.inputNames,shifted.inputNames);
    assert(translated && translated->toString(16)=="6");
    auto temporarilyUnavailable=shifted;
    temporarilyUnavailable.inputMask="0";
    auto waiting=shiftedSession;
    assert(MixerDeviceBindingPolicy::apply(waiting,temporarilyUnavailable,shiftedPair,0,
        BindingMode::verifiedAsioRestart).isEmpty());
    assert(waiting.request.physicalInputs==0 && waiting.request.sources.empty());
    assert(waiting.waitingAsioInputs.size()==1);
    assert(waiting.roster.revision()==asio.roster.revision());
    assert(waiting.inserts.size()==asio.inserts.size());
    MixerSessionDocument reopenedWhileDisconnected;
    assert(Codec::decode(Codec::encode(waiting),reopenedWhileDisconnected));
    assert(reopenedWhileDisconnected.binding.outputNames==shifted.outputNames);
    assert(reopenedWhileDisconnected.waitingAsioInputs[0].leftName=="VB-Audio Point 1");
    juce::BigInteger returningMask;
    assert(!MixerDeviceBindingPolicy::addReturningCapturePins(
        reopenedWhileDisconnected.waitingAsioInputs,{"VB-Audio Point 1"},returningMask));
    assert(returningMask.isZero()); // a stereo half cannot open an unrelated capture
    assert(MixerDeviceBindingPolicy::addReturningCapturePins(
        reopenedWhileDisconnected.waitingAsioInputs,shifted.inputNames,returningMask));
    assert(returningMask.toString(16)=="6");

    auto nextSpeaker=shifted;
    nextSpeaker.outputMask="3";
    nextSpeaker.outputNames={"Realtek HD Audio output with SST 1","Realtek HD Audio output with SST 2"};
    const auto newlyApproved=MixerDeviceBindingPolicy::proposedOutputs(
        reopenedWhileDisconnected,nextSpeaker,2,BindingMode::verifiedAsioRestart);
    assert(newlyApproved.valid() && newlyApproved.mainL==0 && newlyApproved.mainR==1);
    assert(MixerDeviceBindingPolicy::apply(reopenedWhileDisconnected,nextSpeaker,newlyApproved,2,
        BindingMode::verifiedAsioRestart).isEmpty());
    assert(reopenedWhileDisconnected.waitingAsioInputs.empty());
    assert(reopenedWhileDisconnected.request.sources.front().inputL==0);
    assert(reopenedWhileDisconnected.request.sources.front().inputR==1);
    // ASIO4ALL often exposes VB-Cable and Realtek together: the physical
    // MAIN pair must be recovered at callback ordinals 2/3, not rejected
    // simply because four output pins are active.
    auto simultaneous=shifted;
    simultaneous.outputMask="f";
    simultaneous.outputNames={"VB-Audio Point 1","VB-Audio Point 2",
        "Realtek HD Audio output with SST 1","Realtek HD Audio output with SST 2"};
    const auto recoveredRealtek=MixerDeviceBindingPolicy::proposedOutputs(
        asio,simultaneous,4,BindingMode::verifiedAsioRestart);
    assert(recoveredRealtek.valid() && recoveredRealtek.activeOutputChannels==4 &&
           recoveredRealtek.mainL==2 && recoveredRealtek.mainR==3);
    auto multiOutputSession=asio;
    assert(MixerDeviceBindingPolicy::apply(multiOutputSession,simultaneous,
        recoveredRealtek,2,BindingMode::verifiedAsioRestart).isEmpty());
    assert(multiOutputSession.request.sources.size()==1 &&
           multiOutputSession.request.sources[0].inputL==0);
    // Multiple physical speaker pairs are ambiguous and must never
    // automatically redirect a live singer to the wrong output.
    auto ambiguous=simultaneous;
    ambiguous.outputNames={"Realtek Speakers A 1","Realtek Speakers A 2",
                           "Realtek Speakers B 1","Realtek Speakers B 2"};
    assert(!MixerDeviceBindingPolicy::proposedOutputs(
        asio,ambiguous,4,BindingMode::verifiedAsioRestart).valid());
    auto virtualCable=nextSpeaker;
    virtualCable.outputNames={"VB-Audio Point 1","VB-Audio Point 2"};
    assert(!MixerDeviceBindingPolicy::proposedOutputs(asio,virtualCable,2,
        BindingMode::verifiedDeviceApply).valid());
    auto duplicatePins=shifted;
    duplicatePins.outputNames.add("2nd output with SST 1");
    assert(!MixerDeviceBindingPolicy::proposedOutputs(asio,duplicatePins,2,
        BindingMode::verifiedAsioRestart).valid());
    // A third-party ASIO driver must rebind only the SAME registered host
    // and verified physical MAIN, never guess a new output or capture device.
    auto genericAsio=asio;
    genericAsio.binding.capture=genericAsio.binding.playback="MAONO PS22 ASIO";
    auto vendorCurrent=genericAsio.binding;
    const auto vendorMain=MixerDeviceBindingPolicy::proposedOutputs(
        genericAsio,vendorCurrent,2,BindingMode::verifiedAsioRestart);
    assert(vendorMain.valid() && vendorMain.mainL==genericAsio.outputs.mainL &&
           vendorMain.mainR==genericAsio.outputs.mainR);
    auto vendorRestored=genericAsio;
    assert(MixerDeviceBindingPolicy::apply(vendorRestored,vendorCurrent,vendorMain,2,
        BindingMode::verifiedAsioRestart).isEmpty());
    vendorCurrent.playback="Different ASIO interface";
    assert(!MixerDeviceBindingPolicy::proposedOutputs(
        genericAsio,vendorCurrent,2,BindingMode::verifiedAsioRestart).valid());
    vendorCurrent=genericAsio.binding;
    vendorCurrent.capture="Other capture";
    assert(!MixerDeviceBindingPolicy::proposedOutputs(
        genericAsio,vendorCurrent,2,BindingMode::verifiedAsioRestart).valid());
    auto lostCapture=genericAsio.binding;
    lostCapture.inputMask="0";
    auto unboundCapture=genericAsio;
    assert(!unboundCapture.request.sources.empty());
    assert(MixerDeviceBindingPolicy::apply(unboundCapture,lostCapture,vendorMain,0,
        BindingMode::verifiedAsioRestart).isNotEmpty());
    assert(Codec::encode(unboundCapture)==Codec::encode(genericAsio));
    auto exact=d;
    assert(MixerDeviceBindingPolicy::apply(exact,d.binding,d.outputs,2,BindingMode::exact)
           .isEmpty());
    assert(Codec::encode(exact)==graphBefore);
    ChannelEdit newChannel;
    newChannel.kind=ChannelEdit::Kind::add;
    newChannel.position=15;newChannel.role=ChannelRole::input;
    newChannel.format=ProcessingFormat::stereo;
    newChannel.text="Added after restart";
    const std::array<ChannelEdit,1> edits{newChannel};
    auto result=restored.roster.edit(restored.roster.revision(),edits);
    assert(result.ok()&&result.createdIds.size()==1&&result.createdIds[0].value==17);
    // Bad IDs, duplicate EdgeIDs or missing physical binding cannot be
    // imported into an accepted PCM graph.
    std::vector<ChannelStripDefinition> duplicate=d.roster.channels();
    duplicate[1].id=duplicate[0].id;
    DynamicChannelRoster invalid;
    assert(!invalid.restoreExact(1,duplicate));
    duplicate=d.roster.channels();
    duplicate[1].sends[0].edgeId=duplicate[0].sends[0].edgeId;
    assert(!invalid.restoreExact(1,duplicate));
    auto bad=juce::JSON::parse(before);
    bad.getDynamicObject()->setProperty("schema","untrusted.version");
    assert(!Codec::decode(juce::JSON::toString(bad,false),restored));
    bad=juce::JSON::parse(before);
    auto binds=bad.getProperty("binding",{});
    binds.getDynamicObject()->setProperty("rate",0.0);
    assert(!Codec::decode(juce::JSON::toString(bad,false),restored));

    // Immutable generation chain with a REAL vendor state hash.
    const auto base=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixDynamicSession","",false);
    const auto studio=base.getChildFile("Studio");
    assert(studio.createDirectory().wasOk());
    NativeMixerSessionStore store(studio);
    BinaryState binary;
    const char bytes[]={'V','S','T','3','-','S','T','A','T','E',0,1,2};
    binary.bytes.append(bytes,sizeof(bytes));
    binary.sha256=juce::SHA256(binary.bytes).toHexString();
    d.inserts.push_back({{1},0,"TestVendor|9|0","C:/FX/Test.vst3",
                         binary.sha256,"EQ",9,false});
    {
        auto installed=d.roster.channels();
        installed[0].insertCatalogIds.push_back("TestVendor|9|0");
        assert(d.roster.restoreExact(d.acceptedRevision,installed));
    }
    // Semantic corruption must be rejected, even if the surrounding JSON
    // is otherwise well-formed or would have a valid SHA-256 on disk.
    const auto validWithFx=Codec::encode(d);
    MixerSessionDocument rejected;
    auto damaged=juce::JSON::parse(validWithFx);
    auto channels=damaged.getProperty("channels",{});
    auto* entries=channels.getArray();
    assert(entries && !entries->isEmpty());
    auto catalog=entries->getReference(0).getProperty("catalogs",{});
    catalog.getArray()->clear();
    assert(!Codec::decode(juce::JSON::toString(damaged,false),rejected));
    // A ghost slot may not point past the one accepted catalog entry.
    damaged=juce::JSON::parse(validWithFx);
    auto inserts=damaged.getProperty("inserts",{});
    inserts.getArray()->getReference(0).getDynamicObject()->setProperty("slot",1);
    assert(!Codec::decode(juce::JSON::toString(damaged,false),rejected));
    // Wrong plugin on the same slot is not a valid ChannelId FX recovery.
    damaged=juce::JSON::parse(validWithFx);
    inserts=damaged.getProperty("inserts",{});
    inserts.getArray()->getReference(0).getDynamicObject()
        ->setProperty("catalogId","OtherVendor|99|0");
    assert(!Codec::decode(juce::JSON::toString(damaged,false),rejected));
    assert(store.commit(d,{binary}).isEmpty());
    MixerSessionDocument loaded;
    std::vector<BinaryState> readback;
    assert(store.load(loaded,readback).isEmpty());
    assert(readback.size()==1&&readback[0].bytes==binary.bytes);
    // Legacy JSON lacks a preference. Both explicit settings survive checkpoint
    // round trips, and portable files cannot request MAIN authorization.
    assert(!loaded.resumeMain.has_value());
    d.resumeMain=true;
    MixerSessionDocument roundTrip;
    assert(Codec::decode(Codec::encode(d),roundTrip)&&roundTrip.resumeMain==true);
    d.resumeMain=false;
    assert(Codec::decode(Codec::encode(d),roundTrip)&&roundTrip.resumeMain==false);
    const auto portable=studio.getChildFile("Live.smix");
    assert(NativeMixerSessionStore::writeFile(portable,d,{binary}).isEmpty());
    assert(NativeMixerSessionStore::readFile(portable,roundTrip,readback).isEmpty());
    assert(!roundTrip.resumeMain.has_value());
    assert(roundTrip.request.sources.size()==d.request.sources.size());
    assert(readback.size()==1&&readback[0].bytes==binary.bytes);
    auto tampered=juce::JSON::parse(portable.loadFileAsString());
    tampered.getProperty("pluginState",{}).getDynamicObject()->setProperty(binary.sha256,"AAAA");
    assert(portable.replaceWithText(juce::JSON::toString(tampered)));
    assert(NativeMixerSessionStore::readFile(portable,roundTrip,readback).isNotEmpty());
    assert(readback.size()==1&&readback[0].bytes==binary.bytes);
    const auto firstId=studio.getChildFile("MixerSession").getChildFile("HEAD")
        .loadFileAsString().trim();
    assert(armix::shows::detail::uuid(firstId));
    d.masterGain=.35f;d.savedMs+=1000;
    assert(store.commit(d,{binary}).isEmpty());
    assert(store.load(loaded,readback).isEmpty());
    assert(loaded.masterGain==.35f);
    assert(store.recoverPrevious().isNotEmpty()); // NEVER roll back valid HEAD
    const auto head=studio.getChildFile("MixerSession").getChildFile("HEAD");
    const auto secondId=head.loadFileAsString().trim();
    const auto vendor=studio.getChildFile("MixerSession").getChildFile("versions")
        .getChildFile(secondId).getChildFile("blobs")
        .getChildFile(binary.sha256+".bin");
    assert(vendor.appendText("CORRUPTED"));
    assert(store.load(loaded,readback).isNotEmpty());
    assert(loaded.masterGain==.35f); // rejection did not replace output
    assert(store.recoverPrevious().isEmpty());
    assert(store.load(loaded,readback).isEmpty());
    assert(loaded.masterGain==.7f);
    assert(readback[0].bytes==binary.bytes);
    assert(!store.recoverPrevious().isEmpty());
    assert(base.deleteRecursively());
    std::cout<<"PASS: dynamic ChannelId/EdgeId graph, persistent vendor blobs and"
                " explicit verified HEAD recovery\n";
    return 0;
}
