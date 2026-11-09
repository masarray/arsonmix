// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeMixerSessionStore.h"
#include "../src/LiveDryRoutingLab.h"
#include "../src/NativeMixerSessionFxRestore.h"

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
}

int main(){
    verifyVirtualPlayerSessionSerialization();
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
    assert(reordered.request.sources.empty());
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
