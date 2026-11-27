// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
 #undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "../src/NativeMixerSessionStore.h"
#include "../src/NativeMixerSessionRecovery.h"
#include "../src/LiveDryRoutingLab.h"
#include "../src/NativeMixerSessionFxRestore.h"
#include "../src/NativeLogicalMixerControl.h"
#include "../engine/core/NativeOutputPairTap.h"

using namespace armix::mixer;
using namespace armix::session;
using BindingMode=MixerDeviceBindingPolicy::Mode;
using armix::shows::BinaryState;
using Recovery=NativeMixerSessionRecovery;

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

static void checkRecoveryPlan(const MixerSessionDocument& session) {
    auto doc=session;
    Recovery::Context context{BindingMode::exact,Recovery::Source::head,
        true,false,true,true,false};
    auto plan=Recovery::plan(doc,context);
    assert(plan.restoreAllowed && plan.replacePristineStartup && plan.resumeMain);
    assert(plan.bindingMode==BindingMode::exact);

    doc.binding.playback="ASIO4ALL v2";
    plan=Recovery::plan(doc,context);
    assert(plan.bindingMode==BindingMode::verifiedAsioRestart && plan.resumeMain);
    doc.resumeMain=false;
    assert(!Recovery::plan(doc,context).resumeMain);

    doc.resumeMain=true;
    context.source=Recovery::Source::previous;
    plan=Recovery::plan(doc,context);
    assert(plan.restoreAllowed && plan.replacePristineStartup && !plan.resumeMain);

    context.source=Recovery::Source::retained;
    plan=Recovery::plan(doc,context);
    assert(!plan.restoreAllowed && !plan.replacePristineStartup && !plan.resumeMain);
    context.nativeAlreadyActive=false;
    plan=Recovery::plan(doc,context);
    assert(plan.restoreAllowed && !plan.replacePristineStartup && !plan.resumeMain);

    context.source=Recovery::Source::head;
    context.nativeAlreadyActive=true;
    context.pristineStartup=false;
    plan=Recovery::plan(doc,context);
    assert(!plan.restoreAllowed && !plan.replacePristineStartup && !plan.resumeMain);

    context.pristineStartup=true;
    context.startupOnSavedDevice=false;
    context.bindingMode=BindingMode::verifiedDeviceApply;
    assert(!Recovery::plan(doc,context).restoreAllowed);
    context.recoveryPending=true;
    plan=Recovery::plan(doc,context);
    assert(plan.restoreAllowed && plan.replacePristineStartup && !plan.resumeMain);

    context.recoveryPending=false;
    context.bindingMode=BindingMode::exact;
    context.firstBinding=true;
    context.startupOnSavedDevice=true;
    plan=Recovery::plan(doc,context);
    assert(plan.restoreAllowed && plan.replacePristineStartup && !plan.resumeMain);
}


static void checkSessionControlPlans() {
    using Save=Recovery::SaveAction;
    using Open=Recovery::OpenAction;
    Recovery::SaveContext save;
    save.automatic=true;
    save.channelIdInstalled=true;
    save.physicalOpen=true;
    save.directAuthorized=true;
    assert(Recovery::planSave(save).action==Save::captureInstalled);
    save.workerBusy=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.workerBusy=false;
    save.deviceBusy=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.deviceBusy=false;
    save.vendorEditorOpen=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.vendorEditorOpen=false;
    save.readOnly=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.readOnly=false;
    save.channelIdInstalled=false;
    assert(Recovery::planSave(save).action==Save::unchanged);
    save.automatic=false;
    assert(Recovery::planSave(save).action==Save::exportLogical);
    save.retainedNative=true;
    assert(Recovery::planSave(save).action==Save::reject);
    // FUX-1: once native retained FX state is safely captured and
    // committed to a logical .smix, Save As must export the NEW FX blobs.
    // Retained physical/ASIO session is never recaptured or auto-armed.
    save.retainedOfflinePending=true;
    assert(Recovery::planSave(save).action==Save::exportLogical);
    save.channelIdInstalled=true; // Reconnect MUST NOT capture stale live DSP.
    assert(Recovery::planSave(save).action==Save::exportLogical);
    save.m4Installed=true;
    assert(Recovery::planSave(save).action==Save::exportLogical);
    save.secondaryArmed=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.secondaryArmed=false;
    save.vendorEditorOpen=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.vendorEditorOpen=false;
    save.workerBusy=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.workerBusy=false;
    save.automatic=true;
    assert(Recovery::planSave(save).action==Save::unchanged);
    save.automatic=false;
    save.retainedOfflinePending=false;
    save.channelIdInstalled=false;
    save.m4Installed=false;
    save.retainedNative=false;
    save.m4Installed=true; // Legacy M4 is installed, not an offline export.
    assert(Recovery::planSave(save).action==Save::captureInstalled);
    save.m4Installed=false;
    save.channelIdInstalled=true;
    save.automatic=true;
    save.recoveryPending=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.recoveryPending=false;
    save.physicalOpen=false;
    assert(Recovery::planSave(save).action==Save::reject);
    save.shutdownCheckpoint=true; // Explicit shutdown checkpoint exception.
    assert(Recovery::planSave(save).action==Save::captureInstalled);
    save.shutdownCheckpoint=false;
    save.physicalOpen=true;
    save.directAuthorized=false;
    assert(Recovery::planSave(save).action==Save::reject);
    save.directAuthorized=true;
    save.mainInterrupted=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.mainInterrupted=false;
    save.secondaryArmed=true;
    assert(Recovery::planSave(save).action==Save::reject);
    save.secondaryArmed=false;
    save.graphPending=true;
    assert(Recovery::planSave(save).action==Save::reject);

    Recovery::OpenContext open;
    assert(Recovery::planOpen(open).action==Open::openLogical);
    open.retainedNative=true;
    assert(Recovery::planOpen(open).action==Open::reject);
    open.channelIdInstalled=true;
    assert(Recovery::planOpen(open).action==Open::restoreInstalled);
    open.channelIdInstalled=false;
    open.m4Installed=true;
    assert(Recovery::planOpen(open).action==Open::restoreInstalled);
    open.m4Installed=false;
    open.retainedNative=false;
    open.secondaryArmed=true;
    assert(Recovery::planOpen(open).action==Open::reject);
    open.secondaryArmed=false;
    open.epochChanged=true;
    assert(Recovery::planOpen(open).action==Open::reject);
    open.epochChanged=false;
    open.operationBusy=true;
    assert(Recovery::planOpen(open).action==Open::reject);
    open.operationBusy=false;
    open.vendorEditorOpen=true;
    assert(Recovery::planOpen(open).action==Open::reject);

    // Save deduplication only follows a prior ACCEPTED fingerprint and
    // must not depend on wall clock; distinct edits always requalify.
    auto doc=makeSession();
    const auto first=Recovery::stageSave(doc,true,{},1762521000000LL);
    assert(first.error.isEmpty() && !first.unchanged);
    assert(first.fingerprint.isNotEmpty() && doc.savedMs==1762521000000LL);
    const auto again=Recovery::stageSave(doc,true,first.fingerprint,1762522000000LL);
    assert(again.error.isEmpty() && again.unchanged);
    const auto forced=Recovery::stageSave(doc,false,first.fingerprint,1762523000000LL);
    assert(forced.error.isEmpty() && !forced.unchanged);
    doc.masterGain=.4f;
    const auto changed=Recovery::stageSave(doc,true,first.fingerprint,1762524000000LL);
    assert(changed.error.isEmpty() && !changed.unchanged &&
           changed.fingerprint!=first.fingerprint);
    doc.id="UNTRUSTED";
    const auto invalid=Recovery::stageSave(doc,true,{},1762525000000LL);
    assert(invalid.error.isNotEmpty());

    Recovery::RestoreContext restore;
    restore.outputsDisarmed=true;
    assert(Recovery::restoreBlocker(restore)==nullptr);
    restore.workerBusy=true;
    assert(Recovery::restoreBlocker(restore)!=nullptr);
    restore.workerBusy=false;
    restore.vendorEditorOpen=true;
    assert(Recovery::restoreBlocker(restore)!=nullptr);
    restore.vendorEditorOpen=false;
    restore.graphPending=true;
    assert(Recovery::restoreBlocker(restore)!=nullptr);
    restore.graphPending=false;
    restore.outputsDisarmed=false;
    assert(Recovery::restoreBlocker(restore)!=nullptr);
}

class FixtureDevice final : public juce::AudioIODevice {
public:
    FixtureDevice(bool asio=false):AudioIODevice(asio?"Verified ASIO":"Verified WASAPI fixture",asio?"ASIO":"Windows Audio") {
        inputMask.setRange(0,2,true);
        outputMask.setRange(0,asio?6:2,true);
        outputNames=asio?juce::StringArray{"Main L","Main R","Amp L","Amp R","Spare L","Spare R"}:juce::StringArray{"Main L","Main R"};
    }
    juce::StringArray getOutputChannelNames() override {return outputNames;}
    juce::StringArray outputNames;
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

    // Operator-selectable Music Player INPUT: one decoder follows its new
    // ChannelId and existing FX/WIRE path without a second direct MAIN sum.
    const auto selected=mixer.createNativeInputChannel(ep,reconnect.pcmRevision,
        reconnect.rosterRevision,"MUSIC TARGET",ProcessingFormat::stereo,true);
    assert(selected.accepted && selected.created!=added.created);
    const auto moved=mixer.patchNativeInputSource(ep,selected.pcmRevision,
        selected.rosterRevision,selected.created,-1,-1,true,true,{},true);
    assert(moved.accepted && mixer.nativePlayerSource()==selected.created);
    assert(mixer.playerDeckHasMainPath());
    render();
    assert(outL.back()>.10f && outL.back()<.44f);
    assert(outR.back()<-.05f && outR.back()>-.27f);
    const auto acceptedAfterMove=mixer.nativeBusStatus();
    const auto* sourceRows=acceptedAfterMove.getProperty("channels",{}).getArray();
    assert(sourceRows);
    bool matched=false,oldSourceUnbound=false;
    for(const auto& row:*sourceRows) {
        if(row.getProperty("id","").toString()==
           juce::String(static_cast<juce::int64>(selected.created.value))) {
            const auto source=row.getProperty("source",{});
            assert(source.getProperty("sourceFormat","").toString()=="PLAYER");
            assert(source.getProperty("playerDeckId","").toString()=="1");
            matched=true;
        }
        if(row.getProperty("id","").toString()==
           juce::String(static_cast<juce::int64>(added.created.value)))
            oldSourceUnbound=!row.getProperty("source",{}).isObject();
    }
    assert(matched && oldSourceUnbound);
    // Disconnect the selected INPUT: playback must become silent, rather
    // than silently falling back to the old MUSIC→MAIN edge.
    const auto disconnected=mixer.patchNativeInputSource(ep,moved.pcmRevision,
        moved.rosterRevision,selected.created,-1,-1,false,true);
    assert(disconnected.accepted && !mixer.nativePlayerSource().value);
    assert(!mixer.playerDeckHasMainPath());
    render();
    assert(std::abs(outL.back())<1.e-6f &&
           std::abs(outR.back())<1.e-6f);
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
    // P0: an offline WIRE intent must not operate on a *different* channel
    // merely because its nested "from" points to an otherwise valid strip.
    // Test every structural operation against the exact saved revision and
    // byte-for-byte document. No device, callback or VST3 may be required.
    const ChannelId offlineTrackId{static_cast<std::uint64_t>(track.getLargeIntValue())};
    const auto primary=workspace.document().roster.find(offlineTrackId)->sends.front();
    auto wirePayload=[&](const juce::String& op,const juce::String& from,
                         const juce::String& to=juce::String{},std::uint64_t edge=0) {
        auto change=juce::var(new juce::DynamicObject());
        change.getDynamicObject()->setProperty("op",op);
        change.getDynamicObject()->setProperty("from",from);
        if(to.isNotEmpty())change.getDynamicObject()->setProperty("to",to);
        if(edge)change.getDynamicObject()->setProperty(
            "edgeId",juce::String(std::to_string(edge)));
        juce::Array<juce::var> changes;
        changes.add(change);
        auto payload=juce::var(new juce::DynamicObject());
        payload.getDynamicObject()->setProperty("changes",juce::var(changes));
        return payload;
    };
    for(const char* kind:{"DISCONNECT","REMOVE","RETARGET","CONNECT","ROUTE_ONLY"}) {
        const auto beforeRevision=workspace.revision();
        const auto beforeDocument=Codec::encode(workspace.document());
        const auto payload=wirePayload(kind,track,"11",primary.edgeId);
        const auto rejected=workspace.apply(command("WIRE_TRANSACTION",payload,"1"));
        assert(!rejected.accepted && rejected.error=="WIRE_CHANNEL_ID_MISMATCH");
        assert(rejected.revision==beforeRevision);
        assert(workspace.revision()==beforeRevision);
        assert(Codec::encode(workspace.document())==beforeDocument);
    }
    // Legitimate SOURCE ONLY still works on the exact ChannelId, and only
    // the explicit ROUTE_ONLY intent may restore the primary MAIN WIRE.
    assert(workspace.apply(command("WIRE_TRANSACTION",
        wirePayload("DISCONNECT",track),track)).accepted);
    assert(workspace.document().roster.find(offlineTrackId)->sends.empty());
    assert(workspace.apply(command("WIRE_TRANSACTION",
        wirePayload("ROUTE_ONLY",track,"15"),track)).accepted);
    assert(workspace.document().roster.find(offlineTrackId)->sends.size()==1);
    assert(workspace.document().roster.find(offlineTrackId)->sends.front().primaryOutput);
    // A saved parallel SEND supports offline PRE/POST and ON/OFF on exactly
    // one EdgeId, preserving the original MAIN cable and channel gain.
    assert(workspace.apply(command("WIRE_TRANSACTION",
        wirePayload("CONNECT",track,"11"),track)).accepted);
    const auto planned=workspace.document().roster.find(offlineTrackId)->sends;
    assert(planned.size()==2 && !planned.back().primaryOutput);
    const auto parallelEdge=planned.back().edgeId;
    auto tap=juce::var(new juce::DynamicObject());
    tap.getDynamicObject()->setProperty("edgeId",juce::String(std::to_string(parallelEdge)));
    tap.getDynamicObject()->setProperty("tap","PRE_FADER");
    assert(workspace.apply(command("SET_SEND_TAP",tap,track)).accepted);
    auto projectedWire=workspace.snapshot().getProperty("graph",{}).getProperty("edges",{});
    bool foundPre=false;
    for(const auto& edge:*projectedWire.getArray())
        if(edge.getProperty("edgeId",{}).toString()==juce::String(std::to_string(parallelEdge))) {
            assert(edge.getProperty("tapStage",{}).toString()=="PRE_FADER");
            assert(!static_cast<bool>(edge.getProperty("primaryOutput",true)));
            foundPre=true;
        }
    assert(foundPre);
    const auto* withPre=workspace.document().roster.find(offlineTrackId);
    assert(withPre->sends.size()==2 && withPre->sends.front().primaryOutput &&
           !withPre->sends.front().preFader && withPre->sends.back().preFader);
    auto enabled=juce::var(new juce::DynamicObject());
    enabled.getDynamicObject()->setProperty("edgeId",juce::String(std::to_string(parallelEdge)));
    enabled.getDynamicObject()->setProperty("enabled",false);
    assert(workspace.apply(command("SET_SEND_ENABLED",enabled,track)).accepted);
    assert(!workspace.document().roster.find(offlineTrackId)->sends.back().enabled);
    // Reopen must retain both OFF and PRE without inventing a LIVE PCM graph.
    NativeLogicalMixerWorkspace offlineCopy;
    assert(offlineCopy.load(workspace.document(),workspace.states()));
    const auto* copied=offlineCopy.document().roster.find(offlineTrackId);
    assert(copied && copied->sends.size()==2 && copied->sends.back().preFader &&
           !copied->sends.back().enabled && copied->sends.front().primaryOutput);
    assert(!static_cast<bool>(offlineCopy.snapshot().getProperty("physicalOutputAdmitted",true)));
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
    // Offline new return plans the same mandatory wet catalog, and cannot
    // lose it through the offline remove path even before a SEND exists.
    auto wetPayload=juce::var(new juce::DynamicObject());
    wetPayload.getDynamicObject()->setProperty("purpose","FX");
    wetPayload.getDynamicObject()->setProperty("name","Offline wet return");
    wetPayload.getDynamicObject()->setProperty("connectMain",true);
    const auto newReturn=workspace.apply(command("CREATE_GROUP",wetPayload));
    assert(newReturn.accepted);
    const ChannelId returnId{static_cast<std::uint64_t>(newReturn.createdId.getLargeIntValue())};
    const auto* wetRow=workspace.document().roster.find(returnId);
    assert(wetRow&&wetRow->role==ChannelRole::fxReturn&&wetRow->insertCatalogIds.size()==1&&
        isWetReturnCatalog(wetRow->insertCatalogIds.front()));
    const auto beforeRemove=Codec::encode(workspace.document());
    const auto rejectedReturn=workspace.apply(command("REMOVE_FX_REFERENCE",slot,newReturn.createdId));
    assert(!rejectedReturn.accepted&&rejectedReturn.error=="FX_RETURN_REQUIRES_WET_PROCESSOR");
    assert(Codec::encode(workspace.document())==beforeRemove);
    assert(liveStore.commit(workspace.document(),workspace.states()).isEmpty());
    assert(liveStore.load(verifiedHead,headStates).isEmpty());
    assert(verifiedHead.roster.find(returnId)->insertCatalogIds==wetRow->insertCatalogIds);
    assert(workspace.apply(command("REMOVE_BUS",{},newReturn.createdId)).accepted);
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
    using Harmony=armix::builtin_fx::ChannelHarmonyProcessor;
    auto saved=makeSession();
    const juce::String id=Harmony::catalogIdentity;
    auto rows=saved.roster.channels();
    rows.front().insertCatalogIds.push_back(id.toStdString());
    assert(saved.roster.restoreExact(saved.acceptedRevision,rows));
    // Create the actual historic wire layouts, not merely today's
    // state under yesterday's label. Latency belongs to the new device
    // lease, never to saved AHM1 bytes or a hard-coded 48k old delay.
    for(int version:{1,2,3}){
        BinaryState state;
        {
            juce::MemoryOutputStream stream(state.bytes,false);
            stream.writeInt(0x41484d31);stream.writeInt(version);
            stream.writeInt(7);stream.writeInt(1);
            stream.writeInt(version==3?2:5);stream.writeBool(false);
            stream.writeFloat(0.f);stream.writeFloat(.6f);
            stream.writeFloat(.75f);stream.writeFloat(0.f);
            if(version>=2){stream.writeInt(1);stream.writeInt64(2);}
            if(version==3)stream.writeInt(1);
        }
        const int bytes=version==1?37:version==2?49:53;
        assert(state.bytes.getSize()==static_cast<std::size_t>(bytes));
        state.sha256=juce::SHA256(state.bytes).toHexString();
        saved.inserts.clear();
        saved.inserts.push_back({{1},0,id,{},state.sha256,
                                 "ArSon Harmony",0,false});
        // Reject every truncated prefix and trailing byte transactionally,
        // before publication. Noncanonical bool bytes are corrupt too.
        {
            auto validator=std::make_unique<Harmony>();
            validator->prepareToPlay(48000.,64);
            assert(validator->restoreUnpublishedState(state.bytes.getData(),bytes));
            juce::MemoryBlock accepted;validator->getStateInformation(accepted);
            for(int n=1;n<bytes;++n)
                assert(!validator->restoreUnpublishedState(state.bytes.getData(),n));
            auto extra=state.bytes;const char zero=0;
            extra.append(&zero,1);
            assert(!validator->restoreUnpublishedState(extra.getData(),bytes+1));
            auto badBool=state.bytes;
            static_cast<unsigned char*>(badBool.getData())[20]=2;
            assert(!validator->restoreUnpublishedState(badBool.getData(),bytes));
            juce::MemoryBlock after;validator->getStateInformation(after);
            assert(accepted==after);
        }
        juce::AudioPluginFormatManager formats;
        for(double rate:{32000.,44100.,48000.,96000.,192000.}){
            constexpr int quantum=64;
            const int latency=static_cast<int>(std::lround(rate*.024));
            std::shared_ptr<DynamicChannelFxLeaseBank> restored;
            const auto error=NativeMixerSessionFxRestore::prepare(
                saved,std::vector<BinaryState>{state},formats,rate,quantum,
                1,1,restored);
            assert(error.isEmpty()&&restored);
            const auto* lease=restored->ownerForControl({1},0);
            assert(lease&&*lease&&(*lease)->name=="Harmonice");
            assert((*lease)->catalogId==id);
            assert(saved.inserts.front().name=="ArSon Harmony");
            auto* p=dynamic_cast<Harmony*>((*lease)->processor.get());
            assert(p&&p->isReady()&&p->getSampleRate()==rate);
            assert(p->getLatencySamples()==latency&&(*lease)->latencySamples==latency);
            assert((*lease)->continuousBypassProcessing);
            const auto s=p->acceptedSettings();
            assert(s.key==7&&s.scale==1&&s.voices==(version==3?2:5));
            assert(s.voiceMode==(version==3?1:0)&&s.wet==0.f&&!s.correction);
            assert(p->musicSourceChannel()==(version>=2?2u:0u));
            assert(p->effectiveKeyCode()==19);
            assert(!p->restoreUnpublishedState(state.bytes.getData(),bytes));
            // Exercise the actual JUCE lease at Wet=0: both enabled and
            // rack-bypassed paths must emit exactly the new delayed impulse.
            // The two impulses are separated by enough time to drain rings.
            for(bool bypass:{false,true}){
                const int length=latency+quantum*2;
                for(int at=0;at<length;at+=quantum){
                    std::array<float,quantum> l{},r{};
                    const int count=std::min(quantum,length-at);
                    if(at==0){l[0]=.5f;r[0]=-.25f;}
                    (*lease)->render(l.data(),r.data(),count,bypass);
                    for(int i=0;i<count;++i){
                        const bool hit=at+i==latency;
                        assert(std::abs(l[i]-(hit?.5f:0.f))<1.e-6f);
                        assert(std::abs(r[i]-(hit?-.25f:0.f))<1.e-6f);
                    }
                }
            }
            assert(!(*lease)->latencyChanged.load(std::memory_order_acquire));
            std::cout<<"H11_RESTORE version="<<version<<" rate="<<rate
                     <<" latency="<<latency<<" enabled/bypass PCM PASS\n";
        }
    }
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
        const auto actual=ack.getProperty("errorCode",{}).toString();
        if(actual!=reason)std::cerr<<"Logical rejection expected "<<reason
                                  <<" actual "<<actual.toStdString()<<std::endl;
        assert(actual==reason);
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
    // The full disk-committing control service must reject a target/source
    // mismatch *synchronously*, without scheduling a save or producing a
    // misleading ACCEPTED ACK. The exact saved readback is unchanged.
    const auto beforeWireMismatch=control.readback().getProperty("revision",{});
    submit(command("logical-wire-wrong-owner","WIRE_TRANSACTION","1",wire));
    reject("WIRE_CHANNEL_ID_MISMATCH");
    assert(!worker.busy());
    assert(control.readback().getProperty("revision",{})==beforeWireMismatch);
    accept(command("logical-rename","RENAME_CHANNEL",channel,
        juce::JSON::parse(R"({"name":"Lead Vox"})")));
    // The catalog can grow independently of the product's per-rack limit.
    // Exercise every registered built-in without requiring an illegal ninth
    // insert on one rack; still explicitly prove overflow is rejected.
    const auto capacity=armix::mixer::ChannelRosterBudget{}.maximumInsertSlotsPerChannel;
    assert(capacity>=2 && armix::builtin_fx::nativeBuiltInFx.size()<=2*capacity);
    std::size_t catalogIndex=0,wetCatalogCount=0;
    std::vector<const armix::builtin_fx::NativeBuiltInFx*> ordinaryCatalog;
    for(const auto& fx:armix::builtin_fx::nativeBuiltInFx) {
        auto payload=std::make_unique<juce::DynamicObject>();
        payload->setProperty("catalogId",fx.id);
        const juce::var value(payload.release());
        // Wet-return catalog identities are role-restricted. They are tested
        // on a real FX_RETURN in verifyDmxWetReturnSession, not an INPUT/GROUP.
        if(armix::mixer::isWetReturnCatalog(fx.id)){
            ++wetCatalogCount;
            const auto revision=control.readback().getProperty("revision",{});
            submit(command((juce::String("logical-wet-group-")+fx.id).toRawUTF8(),"ADD_FX_REFERENCE",bus,value));
            reject("WET_RETURN_REQUIRES_FX_RETURN_CHANNEL");
            submit(command((juce::String("logical-wet-input-")+fx.id).toRawUTF8(),"ADD_FX_REFERENCE",channel,value));
            reject("WET_RETURN_REQUIRES_FX_RETURN_CHANNEL");
            assert(!worker.busy() && control.readback().getProperty("revision",{})==revision);
            continue;
        }
        ordinaryCatalog.push_back(&fx);
        if(catalogIndex==capacity){
            const auto revision=control.readback().getProperty("revision",{});
            submit(command("logical-fx-overflow","ADD_FX_REFERENCE",bus,value));
            reject("LOGICAL_EDIT_REJECTED");
            assert(!worker.busy() && control.readback().getProperty("revision",{})==revision);
        }
        accept(command((juce::String("logical-fx-")+fx.id).toRawUTF8(),
            "ADD_FX_REFERENCE",catalogIndex<capacity?bus:channel,value));
        ++catalogIndex;
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
    assert(saved.inserts.size()==ordinaryCatalog.size());
    assert(wetCatalogCount==2&&ordinaryCatalog.size()+wetCatalogCount==armix::builtin_fx::nativeBuiltInFx.size());
    const auto bypassed=std::find_if(saved.inserts.begin(),saved.inserts.end(),[&](const auto& f){return f.channel==busId&&f.slot==1;});
    assert(bypassed!=saved.inserts.end()&&bypassed->bypass&&saved.rackBypassed.front()==busId);
    assert(saved.roster.find(busId)->insertCatalogIds.size()==std::min(capacity,catalogIndex));
    assert(saved.roster.find(trackId)->insertCatalogIds.size()==catalogIndex-std::min(capacity,catalogIndex));
    // Count alone could hide a missing FX replaced by a duplicate. Verify
    // each exact catalog ID at its intended channel and slot before restore.
    for(std::size_t i=0;i<catalogIndex;++i) {
        const auto expectedChannel=i<capacity?busId:trackId;
        const auto expectedSlot=i<capacity?i:i-capacity;
        const auto& fx=*ordinaryCatalog[i];
        const auto* row=saved.roster.find(expectedChannel);
        assert(row && row->insertCatalogIds[expectedSlot]==juce::String(fx.id).toStdString());
        assert(std::count_if(saved.inserts.begin(),saved.inserts.end(),[&](const auto& insert){
            return insert.channel==expectedChannel && insert.slot==expectedSlot &&
                   insert.catalogId==juce::String(fx.id);
        })==1);
    }
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
    const auto priorId=control.workspace().document().id;
    IoPortLayout resetDefaults;
    assert(resetDefaults.inputs.add(300,"Reset Vocal",IoPortLayout::I::Format::mono));
    assert(resetDefaults.outputs.add(400,"Reset Stage"));
    auto beforeReset=control.workspace().document();
    const auto oldToken=control.workspace().workspaceToken();
    const auto oldStateCount=control.workspace().states().size();
    assert(control.resetIo(oldToken,control.workspace().revision(),resetDefaults,
        [](juce::String error){if(error.isNotEmpty())std::cerr<<error.toStdString()<<'\n';assert(error.isEmpty());}));
    assert(worker.busy()&&control.workspace().workspaceToken()==oldToken);
    waitForIO();
    auto afterReset=control.workspace().document();
    assert(afterReset.id==priorId&&control.workspace().workspaceToken()!=oldToken);
    assert(afterReset.logicalInputs.find(300)&&afterReset.logicalOutputs.find(400));
    assert(control.workspace().states().size()==oldStateCount);
    afterReset.logicalInputs={};afterReset.logicalOutputs={};
    beforeReset.logicalInputs={};beforeReset.logicalOutputs={};
    assert(Codec::encode(beforeReset)==Codec::encode(afterReset));
    auto archivesAfterReset=folder.getChildFile("SessionArchive").findChildFiles(juce::File::findFiles,false,"*.smix");
    assert(archivesAfterReset.size()==1);
    const auto priorToken=control.workspace().workspaceToken();
    const auto priorRevision=control.workspace().revision();
    const auto beforeNew=juce::JSON::toString(control.readback(),true);
    bool rejected=false;
    assert(control.newSession("stale",priorRevision,nullptr,[&](juce::String error){
        rejected=error.isNotEmpty();
    }));
    assert(rejected&&!worker.busy()&&juce::JSON::toString(control.readback(),true)==beforeNew);
    IoPortLayout defaults;
    assert(defaults.inputs.add(100,"Vocal",IoPortLayout::I::Format::mono));
    assert(defaults.inputs.restoreIntent(100,{{"ASIO","Verified ASIO","Verified ASIO"},0,-1,"Mic",""}));
    assert(defaults.outputs.add(200,"Stage"));
    assert(defaults.outputs.restoreIntent(200,{{"ASIO","Verified ASIO"},2,3,"L","R"}));
    bool created=false;
    assert(control.newSession(priorToken,priorRevision,&defaults,[&](juce::String error){
        if(error.isNotEmpty())std::cerr<<error.toStdString()<<'\n';
        assert(error.isEmpty());created=true;
    }));
    assert(worker.busy()&&!created&&control.workspace().workspaceToken()==priorToken);
    waitForIO();
    const auto& fresh=control.workspace().document();
    assert(created&&fresh.id!=priorId&&control.workspace().workspaceToken()!=priorToken);
    assert(fresh.logicalOnly&&fresh.masterMute&&!fresh.resumeMain.value_or(true)&&fresh.inserts.empty());
    assert(fresh.logicalInputs.find(100)&&!fresh.logicalInputs.find(100)->binding);
    assert(fresh.logicalOutputs.find(200)&&!fresh.logicalOutputs.find(200)->binding);
    assert(fresh.logicalInputs.references().empty()&&fresh.logicalOutputs.references().empty());
    auto archived=folder.getChildFile("SessionArchive").findChildFiles(juce::File::findFiles,false,"*.smix");
    assert(archived.size()==2);
    MixerSessionDocument recoveredArchive;std::vector<BinaryState> archiveStates;
    assert(NativeMixerSessionStore::readFile(archivesAfterReset[0],recoveredArchive,archiveStates).isEmpty());
    assert(recoveredArchive.id==priorId&&recoveredArchive.inserts.size()==saved.inserts.size());
    assert(archiveStates.size()==states.size());
    // A later generation must not remove the separately archived old show.
    const auto newToken=control.workspace().workspaceToken();
    assert(control.newSession(newToken,control.workspace().revision(),nullptr,
        [](juce::String error){assert(error.isEmpty());}));
    waitForIO();
    assert(control.workspace().document().logicalInputs.empty());
    assert(control.workspace().document().logicalOutputs.empty());
    assert(archived[0].existsAsFile());
    const auto beforeArchiveId=control.workspace().document().id;
    assert(control.open(std::move(recoveredArchive),std::move(archiveStates),
        [](juce::String error){assert(error.isEmpty());},true));
    waitForIO();
    assert(control.workspace().document().id==priorId);
    assert(control.workspace().document().roster.find(trackId));
    assert(control.workspace().states().size()==states.size());
    auto allArchives=folder.getChildFile("SessionArchive").findChildFiles(juce::File::findFiles,false,"*.smix");
    assert(allArchives.size()==4);
    bool backupFound=false;
    for(const auto& archive:allArchives){
        MixerSessionDocument copy;std::vector<BinaryState> blobs;
        assert(NativeMixerSessionStore::readFile(archive,copy,blobs).isEmpty());
        if(copy.id==beforeArchiveId)backupFound=true;
    }
    assert(backupFound);
    // Close immediately after a confirmed New: the worker may have
    // committed HEAD but its queued message-thread callback has not run.
    const auto beforeShutdown=control.workspace().document().id;
    bool shutdownAdopted=false;
    assert(control.newSession(control.workspace().workspaceToken(),control.workspace().revision(),
        nullptr,[&](juce::String error){assert(error.isEmpty());shutdownAdopted=true;}));
    worker.finishForShutdown();
    assert(shutdownAdopted&&!worker.busy());
    assert(control.workspace().document().id!=beforeShutdown);
    MixerSessionDocument shutdownDoc;std::vector<BinaryState> shutdownStates;
    assert(NativeMixerSessionStore(folder).load(shutdownDoc,shutdownStates).isEmpty());
    assert(shutdownDoc.id==control.workspace().document().id);
    assert(shutdownStates.empty());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
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

    // H2 .smix preserves the exact vendor parameter ID and class identity,
    // even if FX slot order changes. Missing host parameter -> inert mapping.
    auto withParam=doc;
    withParam.hotkeys.push_back({"p-vendor-1","SESSION",session,"5",
        "SET_FX_PARAM_TOGGLE",0x73,6,1,"g5c-724",
        "verified-vst3-class-id","vendor-param-203","Stereo Width"});
    const auto saved=Codec::encode(withParam);
    assert(Codec::decode(saved,decoded));
    assert(decoded.hotkeys.size()==5);
    assert(decoded.hotkeys.back().paramId=="vendor-param-203");
    assert(decoded.hotkeys.back().catalogId=="verified-vst3-class-id");
    assert(Codec::encode(decoded)==saved);
    auto unsafe=juce::JSON::parse(saved);
    auto entries=unsafe.getProperty("hotkeys",{}).getArray();
    assert(entries&&entries->size()==5);
    entries->getReference(4).getDynamicObject()->setProperty("paramId","");
    assert(!Codec::decode(juce::JSON::toString(unsafe,false),decoded));
    unsafe=juce::JSON::parse(saved);
    unsafe.getProperty("hotkeys",{}).getArray()->getReference(4)
        .getDynamicObject()->setProperty("catalogId","");
    assert(!Codec::decode(juce::JSON::toString(unsafe,false),decoded));
}

static void verifyIndependentAsioExternalRestoreProvenance() {
    auto doc=makeSession();
    doc.binding.outputMask="ff";
    doc.binding.outputNames={"ASIO OUT 1","ASIO OUT 2",
        "ASIO OUT 3","ASIO OUT 4","ASIO OUT 5","ASIO OUT 6",
        "ASIO OUT 7","ASIO OUT 8"};
    doc.outputs={8,0,1,-1,-1,false};
    doc.externalOutputs.push_back({{16},2,3,"ASIO OUT 3","ASIO OUT 4",
        "Verified ASIO"});
    doc.externalOutputs.push_back({{12},4,5,"ASIO OUT 5","ASIO OUT 6",
        "Verified ASIO"});
    const auto saved=Codec::encode(doc);
    MixerSessionDocument loaded;
    assert(Codec::decode(saved,loaded));
    assert(loaded.externalOutputs.size()==2);
    assert(loaded.externalOutputs[0].playbackDevice=="Verified ASIO");
    const std::array<int,8> full{{0,1,2,3,4,5,6,7}};
    const auto same=[&](const StoredExternalOutput& p) {
        return MixerDeviceBindingPolicy::canRestoreExternalOutput(
            p,"Verified ASIO",full,doc.binding.outputNames,doc.outputs);
    };
    assert(same(loaded.externalOutputs[0]));
    assert(same(loaded.externalOutputs[1]));
    auto ambiguous=doc.binding.outputNames;
    ambiguous.set(7,ambiguous[2]);
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        loaded.externalOutputs[0],"Verified ASIO",full,ambiguous,doc.outputs));
    const std::array<int,8> repeated{{0,1,2,3,4,5,6,6}};
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        loaded.externalOutputs[0],"Verified ASIO",repeated,doc.binding.outputNames,doc.outputs));
    // A new ASIO device uses identical generic labels. A verified device
    // APPLY must not silently transfer saved amp ownership to the new DAC.
    auto newDevice=doc.binding;
    newDevice.capture=newDevice.playback="Different ASIO";
    assert(MixerDeviceBindingPolicy::apply(loaded,newDevice,doc.outputs,
        2,BindingMode::verifiedDeviceApply).isEmpty());
    assert(loaded.binding.playback=="Different ASIO");
    assert(loaded.externalOutputs[0].playbackDevice=="Verified ASIO");
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        loaded.externalOutputs[0],"Different ASIO",full,
        newDevice.outputNames,doc.outputs));
    MixerSessionDocument reopened;
    assert(Codec::decode(Codec::encode(loaded),reopened));
    assert(reopened.externalOutputs[0].playbackDevice=="Verified ASIO");
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        reopened.externalOutputs[0],"Different ASIO",full,
        newDevice.outputNames,doc.outputs));
    // Even when the driver itself returns, a 6-pin enabled mask must
    // authorize 3/4 independently and keep missing 5/6 as NEEDS_REMAP.
    const std::array<int,6> sparse{{0,1,2,3,6,7}};
    VerifiedNativeOutputs sparseHw{6,0,1,-1,-1,false};
    assert(MixerDeviceBindingPolicy::canRestoreExternalOutput(
        doc.externalOutputs[0],"Verified ASIO",sparse,
        doc.binding.outputNames,sparseHw));
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        doc.externalOutputs[1],"Verified ASIO",sparse,
        doc.binding.outputNames,sparseHw));
    // Another active stereo pair may only be restored by exact original
    // driver and without collisions against MAIN or MONITOR.
    auto hw=doc.outputs;hw.mainL=2;hw.mainR=3;
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        doc.externalOutputs[0],"Verified ASIO",full,
        doc.binding.outputNames,hw));
    hw=doc.outputs;hw.monitorEnabled=true;hw.monitorL=4;hw.monitorR=5;
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        doc.externalOutputs[1],"Verified ASIO",full,
        doc.binding.outputNames,hw));
    auto renamed=doc.binding.outputNames;
    renamed.set(2,"MOVED AMP");
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        doc.externalOutputs[0],"Verified ASIO",full,renamed,doc.outputs));
    // Migration: an O1-v1 .smix with no playbackDevice property inherits
    // only its ORIGINAL saved binding. It cannot be claimed by another ASIO.
    auto legacyJson=juce::JSON::parse(saved);
    auto* legacy=legacyJson.getProperty("externalOutputs",{}).getArray();
    assert(legacy&&legacy->size()==2);
    for(auto& item:*legacy)item.getDynamicObject()->removeProperty("playbackDevice");
    assert(Codec::decode(juce::JSON::toString(legacyJson,false),reopened));
    assert(reopened.externalOutputs[0].playbackDevice=="Verified ASIO");
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        reopened.externalOutputs[0],"Different ASIO",full,
        doc.binding.outputNames,doc.outputs));
    // A bogus explicit provenance cannot bypass the decoder.
    auto bad=juce::JSON::parse(saved);
    bad.getProperty("externalOutputs",{}).getArray()->getReference(0)
        .getDynamicObject()->setProperty("playbackDevice",juce::var(15));
    assert(!Codec::decode(juce::JSON::toString(bad,false),reopened));
}
static void verifyExternalOutputPortableIntent() {
    auto doc=makeSession();
    // Two additional amplifier pairs are physical intent, not armed state.
    doc.binding.outputMask="ff";
    doc.binding.outputNames={"ASIO OUT 1","ASIO OUT 2",
        "ASIO OUT 3","ASIO OUT 4","ASIO OUT 5","ASIO OUT 6",
        "ASIO OUT 7","ASIO OUT 8"};
    doc.outputs.activeOutputChannels=8;
    doc.externalOutputs.push_back({{16},2,3,"ASIO OUT 3","ASIO OUT 4"});
    doc.externalOutputs.push_back({{12},4,5,"ASIO OUT 5","ASIO OUT 6"});
    const auto encoded=Codec::encode(doc);
    MixerSessionDocument loaded;
    assert(Codec::decode(encoded,loaded));
    assert(loaded.externalOutputs.size()==2);
    assert(loaded.externalOutputs[0].channel.value==16);
    assert(loaded.externalOutputs[0].left==2);
    assert(loaded.externalOutputs[0].right==3);
    assert(loaded.externalOutputs[0].leftName=="ASIO OUT 3");
    assert(loaded.externalOutputs[1].channel.value==12);
    // Legacy sessions without the optional field retain their native mixer.
    doc.externalOutputs.clear();
    assert(Codec::decode(Codec::encode(doc),loaded));
    assert(loaded.externalOutputs.empty());
    // Corrupted pair, duplicate ChannelId or missing pin name fail closed.
    doc.externalOutputs.push_back({{16},3,4,"ASIO OUT 4","ASIO OUT 5"});
    assert(!Codec::decode(Codec::encode(doc),loaded));
    doc.externalOutputs[0]={ {16},2,3,"ASIO OUT 3","ASIO OUT 4" };
    doc.externalOutputs.push_back(doc.externalOutputs[0]);
    assert(!Codec::decode(Codec::encode(doc),loaded));
    doc.externalOutputs.pop_back();
    doc.externalOutputs[0].leftName.clear();
    assert(!Codec::decode(Codec::encode(doc),loaded));
}

static void verifyChannelInputGainSessionRoundtrip() {
    auto session=makeSession();
    auto rows=session.roster.channels();
    rows[0].inputGainLinear=2.f;
    assert(session.roster.restoreExact(session.acceptedRevision,rows));
    const auto json=Codec::encode(session);
    MixerSessionDocument restored;
    assert(Codec::decode(json,restored));
    assert(std::abs(restored.roster.find({1})->inputGainLinear-2.f)<1.e-6f);
    assert(std::abs(restored.roster.find({1})->faderLinear-1.f)<1.e-6f);
    assert(restored.roster.find({1})->sends[0].edgeId==100);
    assert(Codec::encode(restored)==json);
    // Pre-GAIN v1 sessions have no inputGain property; absence means unity
    // without inventing a new schema or erasing WIRE/VST references.
    auto old=juce::JSON::parse(json);
    auto channels=old.getProperty("channels",{});
    for(auto& item:*channels.getArray())
        item.getDynamicObject()->removeProperty("inputGain");
    assert(Codec::decode(juce::JSON::toString(old,false),restored));
    assert(restored.roster.find({1})->inputGainLinear==1.f);
    // Present but malformed/off-role input gain cannot be normalized.
    auto invalid=juce::JSON::parse(json);
    auto values=invalid.getProperty("channels",{});
    values.getArray()->getReference(0).getDynamicObject()
        ->setProperty("inputGain",juce::String("NaN"));
    assert(!Codec::decode(juce::JSON::toString(invalid,false),restored));
    invalid=juce::JSON::parse(json);
    values=invalid.getProperty("channels",{});
    values.getArray()->getReference(values.getArray()->size()-1)
        .getDynamicObject()->setProperty("inputGain",2.0);
    assert(!Codec::decode(juce::JSON::toString(invalid,false),restored));
}

static void checkLogicalInputSession(){
    using R=armix::device::LogicalInputRegistry;
    auto d=makeSession();
    const auto old=Codec::encode(d);
    MixerSessionDocument decoded;
    assert(Codec::decode(old,decoded)&&decoded.logicalInputs.empty());
    assert(Codec::encode(decoded)==old);
    const std::uint64_t port=9007199254740993ULL;
    assert(d.logicalInputs.add(port,"Lead Vocal",R::Format::mono));
    assert(d.logicalInputs.assign(1,port)&&d.logicalInputs.assign(2,port));
    d.binding.inputNames={"Mic 1","Mic 2"};
    const R::Device device{"ASIO","Verified ASIO","Verified ASIO"};
    const std::array<R::Pin,2> pins{{{0,0,"Mic 1"},{1,1,"Mic 2"}}};
    const R::Inventory live{device,7,pins,true};
    assert(d.logicalInputs.bind(port,{device,0,-1,"Mic 1",""},live,7));
    const auto encoded=Codec::encode(d);
    assert(encoded.contains("arsonmix.mixer-session.v2"));
    assert(Codec::decode(encoded,decoded));
    assert(decoded.request.sources.empty()); // no persisted callback projection
    assert(decoded.logicalInputs.assignedPort(1)==port&&decoded.logicalInputs.assignedPort(2)==port);
    assert(Codec::encode(decoded)==encoded);
    assert(LogicalInputProjection::prepare(decoded,live));
    assert(decoded.request.sources.size()==2&&decoded.request.sources[0].inputL==0&&decoded.request.sources[1].inputL==0);
    assert(Codec::encode(decoded)==encoded);
    LogicalInputProjection::strip(decoded);
    auto other=live;other.device.capture="Different interface";
    assert(LogicalInputProjection::prepare(decoded,other)&&decoded.request.sources.empty());
    assert(decoded.logicalInputs.assignedPort(1)==port);
    assert(LogicalInputProjection::prepare(decoded,live)&&decoded.request.sources.size()==2);
    using P=LogicalInputProjection;
    bool graphChanged=false;
    P::Edit edit;edit.operation=P::Operation::rename;edit.port=port;edit.name="Studio Vocal";
    assert(P::edit(decoded,edit,live,graphChanged)&&!graphChanged&&decoded.request.sources.size()==2);
    edit.operation=P::Operation::remove;
    assert(!P::edit(decoded,edit,live,graphChanged));
    edit.operation=P::Operation::bind;edit.binding={device,1,-1,"Mic 2",""};
    assert(P::edit(decoded,edit,live,graphChanged)&&graphChanged);
    assert(decoded.request.sources.size()==2&&decoded.request.sources[0].inputL==1&&decoded.request.sources[1].inputL==1);
    edit.binding={device,0,-1,"Wrong label",""};
    assert(!P::edit(decoded,edit,live,graphChanged)&&decoded.request.sources[0].inputL==1);
    edit.operation=P::Operation::unbind;
    assert(P::edit(decoded,edit,live,graphChanged)&&graphChanged&&decoded.request.sources.empty());
    assert(decoded.logicalInputs.assignedPort(1)==port&&decoded.logicalInputs.assignedPort(2)==port);
    edit.operation=P::Operation::clear;edit.channel=1;
    assert(P::edit(decoded,edit,live,graphChanged)&&graphChanged&&!decoded.logicalInputs.assignedPort(1));
    auto bad=juce::JSON::parse(encoded);
    bad.getDynamicObject()->setProperty("schema","arsonmix.mixer-session.v1");
    assert(!Codec::decode(juce::JSON::toString(bad,false),decoded));
    bad=juce::JSON::parse(encoded);
    auto logical=bad.getProperty("logicalInputs",{});
    auto refs=logical.getProperty("references",{});
    refs.getArray()->add((*refs.getArray())[0]);
    assert(!Codec::decode(juce::JSON::toString(bad,false),decoded));
    bad=juce::JSON::parse(encoded);
    logical=bad.getProperty("logicalInputs",{});
    refs=logical.getProperty("references",{});
    (*refs.getArray())[0].getDynamicObject()->setProperty("portId","999");
    assert(!Codec::decode(juce::JSON::toString(bad,false),decoded));
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixLogicalInputSession","",false);
    assert(dir.createDirectory().wasOk());
    const auto file=dir.getChildFile("logical.smix");
    assert(NativeMixerSessionStore::writeFile(file,d,{}).isEmpty());
    std::vector<armix::shows::BinaryState> states;
    assert(NativeMixerSessionStore::readFile(file,decoded,states).isEmpty());
    assert(Codec::encode(decoded)==encoded&&states.empty());
    assert(dir.deleteRecursively());
}
// Exercise the actual accepted JUCE/ChannelId renderer, not only a JSON
// or planner simulation. No physical interface is opened by this fixture.
static void verifyLogicalInputNativePcm(){
    using R=armix::device::LogicalInputRegistry;
    using P=LogicalInputProjection;
    FixtureDevice device;
    LiveDryRoutingLab mixer;
    mixer.prepareDevice(&device);
    armix::device::JuceActivePortSnapshot pins;
    armix::device::JuceActivePortSnapshot::Fault fault{};
    assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
    auto d=makeSession();
    d.binding={"Windows Audio","Fixture Capture","Verified WASAPI fixture","3","3",48000.,480,
        device.getInputChannelNames(),device.getOutputChannelNames()};
    d.request.maxFrames=480;d.request.sources.clear();d.rackBypassed.clear();
    const R::Device identity{"Windows Audio","Fixture Capture","Verified WASAPI fixture"};
    const std::array<R::Pin,2> inputPins{{{0,0,"Capture L"},{1,1,"Capture R"}}};
    const R::Inventory live{identity,pins.epoch,inputPins,true};
    const std::uint64_t port=9007199254740993ULL;
    assert(d.logicalInputs.add(port,"Vocal",R::Format::mono));
    assert(d.logicalInputs.bind(port,{identity,0,-1,"Capture L",""},live,pins.epoch));
    assert(d.logicalInputs.assign(1,port)&&d.logicalInputs.assign(2,port));
    MixerSessionDocument decoded;
    assert(Codec::decode(Codec::encode(d),decoded));
    juce::AudioPluginFormatManager formats;
    std::shared_ptr<DynamicChannelFxLeaseBank> bank;
    assert(NativeMixerSessionFxRestore::prepare(decoded,{},formats,48000.,480,pins.epoch,
        mixer.nativeGraphRevisionForRestore(),bank).isEmpty());
    assert(mixer.restoreNativeMixerSession(decoded,std::move(bank),pins,true).isEmpty());
    assert(mixer.hasVerifiedNativeSources());
    std::array<float,480> inputL,inputR,outL{},outR{};
    inputL.fill(.15f);inputR.fill(-.15f);
    const float* in[]={inputL.data(),inputR.data()};
    float* out[]={outL.data(),outR.data()};
    auto render=[&](){
        for(int i=0;i<2;++i)assert(mixer.process(in,2,out,2,480,1.f,false,
            nullptr,nullptr,false,nullptr,nullptr,true));
    };
    render();
    assert(outL.back()>.01f&&outR.back()>.01f);
    auto state=mixer.nativeBusStatus();
    std::uint64_t revision=0,roster=0;
    assert(parseDecimal(state.getProperty("graphRevision",{}),revision));
    assert(parseDecimal(state.getProperty("rosterRevision",{}),roster));
    int publications=0;
    const auto publish=[&]{++publications;};
    P::Edit edit;edit.port=port;edit.operation=P::Operation::rename;edit.name="Studio Vocal";
    const auto logical=mixer.logicalInputRevision();
    auto changed=mixer.editNativeLogicalInput(pins.epoch,revision,roster,logical,edit,d.binding,pins,publish);
    assert(changed.accepted&&changed.pcmRevision==revision&&publications==0);
    assert(mixer.logicalInputRevision()==logical+1);
    edit.operation=P::Operation::bind;edit.binding={identity,1,-1,"Capture R",""};
    assert(!mixer.editNativeLogicalInput(pins.epoch,revision,roster,logical,edit,d.binding,pins,publish).accepted);
    assert(publications==0);
    changed=mixer.editNativeLogicalInput(pins.epoch,revision,roster,logical+1,edit,d.binding,pins,publish);
    assert(changed.accepted&&changed.pcmRevision==revision+1&&publications==1);
    render();
    assert(outL.back()<-.01f&&outR.back()<-.01f);
    state=mixer.nativeBusStatus();
    const auto data=state.getProperty("logicalInputRegistry",{});
    const auto refs=data.getProperty("references",{});
    assert(refs.getArray()->size()==2);
    for(const auto& ref:*refs.getArray()){
        assert(ref.getProperty("portId","").toString()==decimal(port));
        assert(ref.getProperty("state","").toString()=="RESOLVED");
    }
    MixerSessionDocument captured;std::vector<BinaryState> blobs;
    assert(mixer.captureNativeMixerSession(captured,blobs).isEmpty());
    captured.id=d.id;captured.savedMs=d.savedMs;captured.binding=d.binding;
    assert(Codec::decode(Codec::encode(captured),decoded));
    assert(decoded.request.sources.empty()&&decoded.logicalInputs.assignedPort(1)==port&&
           decoded.logicalInputs.assignedPort(2)==port);
    const auto* saved=decoded.logicalInputs.find(port);
    assert(saved&&saved->name=="Studio Vocal"&&saved->binding&&saved->binding->left==1);
    edit.operation=P::Operation::unbind;
    changed=mixer.editNativeLogicalInput(pins.epoch,changed.pcmRevision,roster,
        mixer.logicalInputRevision(),edit,d.binding,pins,publish);
    assert(changed.accepted&&publications==2&&!mixer.hasVerifiedNativeSources());
    render();
    assert(std::abs(outL.back())<1.e-6f&&std::abs(outR.back())<1.e-6f);
    state=mixer.nativeBusStatus();
    const auto missing=state.getProperty("logicalInputRegistry",{}).getProperty("references",{});
    assert(missing.getArray()->size()==2);
    for(const auto& ref:*missing.getArray())assert(ref.getProperty("state","").toString()=="NEEDS_REMAP");
}
static void checkLogicalOutputSession(){
    using R=armix::device::LogicalOutputRegistry;
    auto d=makeSession();d.binding.outputMask="ff";
    d.binding.outputNames={"Main L","Main R","Amp L","Amp R","Spare L","Spare R","Direct L","Direct R"};
    d.outputs={8,0,1,-1,-1,false};
    const R::Device device{"ASIO","Verified ASIO"};
    const std::array<R::Pin,8> pins{{{0,0,"Main L"},{1,1,"Main R"},{2,2,"Amp L"},{3,3,"Amp R"},
        {4,4,"Spare L"},{5,5,"Spare R"},{6,6,"Direct L"},{7,7,"Direct R"}}};
    const R::Inventory live{device,7,pins,{},true};
    const std::uint64_t port=9007199254740993ULL;
    assert(d.logicalOutputs.add(port,"Stage amp")&&d.logicalOutputs.add(20,"Studio main"));
    assert(d.logicalOutputs.bind(port,{device,2,3,"Amp L","Amp R"},live,7,true));
    assert(d.logicalOutputs.bind(20,{device,0,1,"Main L","Main R"},live,7,true));
    assert(d.logicalOutputs.assign(16,port,R::Role::external)&&d.logicalOutputs.assign(12,port,R::Role::external));
    assert(d.logicalOutputs.assign(15,20,R::Role::main));
    d.externalOutputs.push_back({{11},6,7,"Direct L","Direct R","Verified ASIO"});
    const auto encoded=Codec::encode(d);
    MixerSessionDocument loaded;
    assert(encoded.contains("arsonmix.mixer-session.v3")&&Codec::decode(encoded,loaded));
    assert(loaded.logicalOutputs.assignedPort(16)==port&&loaded.externalOutputs.size()==1);
    assert(Codec::encode(loaded)==encoded);
    assert(LogicalOutputProjection::prepare(loaded,live)&&loaded.externalOutputs.size()==3);
    assert(loaded.externalOutputs[1].left==2&&loaded.externalOutputs[2].left==2);
    assert(Codec::encode(loaded)==encoded);
    LogicalOutputProjection::strip(loaded);
    auto other=live;other.device.playback="Other";
    assert(!LogicalOutputProjection::prepare(loaded,other)&&loaded.externalOutputs.size()==1);
    assert(loaded.logicalOutputs.clear(15));
    assert(LogicalOutputProjection::prepare(loaded,other)&&loaded.externalOutputs.size()==1);
    assert(loaded.logicalOutputs.assignedPort(16)==port);
    auto missing=pins;missing[2].name="Renamed";
    auto unavailable=live;unavailable.pins=missing;
    assert(LogicalOutputProjection::prepare(loaded,unavailable)&&loaded.externalOutputs.size()==1);
    auto bad=juce::JSON::parse(encoded);
    bad.getDynamicObject()->setProperty("schema","arsonmix.mixer-session.v2");
    assert(!Codec::decode(juce::JSON::toString(bad,false),loaded));
    bad=juce::JSON::parse(encoded);
    auto refs=bad.getProperty("logicalOutputs",{}).getProperty("references",{});
    refs.getArray()->add((*refs.getArray())[0]);
    assert(!Codec::decode(juce::JSON::toString(bad,false),loaded));
    bad=juce::JSON::parse(encoded);
    refs=bad.getProperty("logicalOutputs",{}).getProperty("references",{});
    (*refs.getArray())[0].getDynamicObject()->setProperty("role","MAIN");
    assert(!Codec::decode(juce::JSON::toString(bad,false),loaded));
    bad=juce::JSON::parse(encoded);
    refs=bad.getProperty("logicalOutputs",{}).getProperty("references",{});
    (*refs.getArray())[0].getDynamicObject()->setProperty("channelId","11");
    assert(!Codec::decode(juce::JSON::toString(bad,false),loaded));
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixLogicalOutputSession","",false);
    assert(dir.createDirectory().wasOk());
    const auto file=dir.getChildFile("output.smix");
    assert(NativeMixerSessionStore::writeFile(file,d,{}).isEmpty());
    std::vector<BinaryState> states;
    assert(NativeMixerSessionStore::readFile(file,loaded,states).isEmpty());
    assert(Codec::encode(loaded)==encoded&&states.empty());
    assert(dir.deleteRecursively());
}
// Exercise actual JUCE ChannelId render with two active ASIO output pins.
static void verifyDmxSplitNativePcm(){
    FixtureDevice device(true);device.outputMask.clear();device.outputMask.setRange(0,2,true);
    LiveDryRoutingLab mixer;mixer.prepareDevice(&device);
    armix::device::JuceActivePortSnapshot pins;
    armix::device::JuceActivePortSnapshot::Fault fault{};
    assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
    auto d=makeSession();
    d.binding={"ASIO","Verified ASIO","Verified ASIO","3","3",48000.,480,
        device.getInputChannelNames(),device.getOutputChannelNames()};
    d.request.maxFrames=480;d.rackBypassed.clear();
    juce::AudioPluginFormatManager formats;std::shared_ptr<DynamicChannelFxLeaseBank> bank;
    assert(NativeMixerSessionFxRestore::prepare(d,{},formats,48000.,480,pins.epoch,
        mixer.nativeGraphRevisionForRestore(),bank).isEmpty());
    assert(mixer.restoreNativeMixerSession(d,std::move(bank),pins,true).isEmpty());
    std::array<float,480> inputL{},inputR{},outL{},outR{};inputL.fill(.2f);
    const float* in[]={inputL.data(),inputR.data()};float* out[]={outL.data(),outR.data()};
    std::array<float*,2> tap{};std::array<int,2> physicalMain{},physicalMonitor{};
    auto render=[&](bool muted){
        assert(mixer.process(in,2,out,2,480,1.f,muted,nullptr,nullptr,false,
            &physicalMain,&physicalMonitor,true,{},false,nullptr,false,&tap));
    };
    auto revisions=[&](){
        auto state=mixer.nativeBusStatus();std::uint64_t rev=0,roster=0;
        assert(parseDecimal(state.getProperty("graphRevision",{}),rev));
        assert(parseDecimal(state.getProperty("rosterRevision",{}),roster));
        return std::array<std::uint64_t,2>{rev,roster};
    };
    render(true);
    auto rev=revisions();
    assert(mixer.patchDmxSplitOutput(pins.epoch,rev[0],rev[1],true,pins,false).isNotEmpty());
    assert(mixer.patchDmxSplitOutput(pins.epoch,rev[0]+1,rev[1],true,pins,true).isNotEmpty());
    assert(mixer.patchDmxSplitOutput(pins.epoch,rev[0],rev[1],true,pins,true).isEmpty());
    assert(mixer.nativeBusPatchAwaitingCallback());render(true);
    assert(!mixer.nativeBusPatchAwaitingCallback()&&!tap[0]&&!tap[1]);
    render(false);
    assert(tap[0]&&tap[1]&&tap[0]!=out[0]&&tap[1]!=out[1]);
    assert((physicalMain==std::array<int,2>{-1,-1}));
    assert((physicalMonitor==std::array<int,2>{0,1}));
    assert(tap[0][479]>.05f&&outL.back()==0.f&&outR.back()==0.f);
    MixerSessionDocument saved;std::vector<BinaryState> blobs;
    assert(mixer.captureNativeMixerSession(saved,blobs).isEmpty());
    const auto monitor=saved.legacyIds.monitor;assert(monitor.value);
    rev=revisions();
    const std::array<WireChange,1> wire{{{WireChange::Kind::connect,{1},monitor,0,.25f}}};
    assert(mixer.applyNativeWireTransaction(pins.epoch,rev[0],rev[1],wire).accepted);
    render(false);render(false);
    assert(outL.back()>.01f&&outL.back()<tap[0][479]);
    const float separateHeadphone=outL.back(),separateMain=tap[0][479];
    rev=revisions();
    // Switching destinations is now reversible, but still requires explicit disarm.
    assert(mixer.patchDmxSplitOutput(pins.epoch,rev[0],rev[1],false,pins,false).isNotEmpty());
    assert(mixer.patchDmxSplitOutput(pins.epoch,rev[0]+1,rev[1],false,pins,true).isNotEmpty());
    render(false);assert(outL.back()>.01f);
    assert(mixer.captureNativeMixerSession(saved,blobs).isEmpty());
    saved.id=d.id;saved.savedMs=d.savedMs;saved.binding=d.binding;
    MixerSessionDocument decoded;assert(Codec::decode(Codec::encode(saved),decoded));
    assert(decoded.outputs.secondaryMain()&&decoded.legacyIds.monitor==monitor);
    auto changed=decoded.binding;changed.outputNames.set(0,"Wrong pin");
    assert(!MixerDeviceBindingPolicy::proposedOutputs(decoded,changed,2).valid());
    assert(!MixerDeviceBindingPolicy::proposedOutputs(decoded,decoded.binding,2,BindingMode::verifiedMainPinApply).valid());
    assert(MixerDeviceBindingPolicy::proposedOutputs(decoded,decoded.binding,2).secondaryMain());
    Recovery::Context context{BindingMode::exact,Recovery::Source::head,true,false,true,true,false};
    assert(!Recovery::plan(decoded,context).resumeMain);
    const auto monitorEdge=[&](const MixerSessionDocument& doc){
        const auto* row=doc.roster.find({1});assert(row);
        const auto it=std::find_if(row->sends.begin(),row->sends.end(),
            [&](const auto& edge){return edge.destination==monitor&&!edge.primaryOutput;});
        assert(it!=row->sends.end());return *it;
    };
    const auto originalEdge=monitorEdge(decoded);
    render(true);assert(!tap[0]&&!tap[1]&&outL.back()==0.f&&outR.back()==0.f);
    rev=revisions();
    assert(mixer.patchDmxSplitOutput(pins.epoch,rev[0],rev[1],false,pins,true).isEmpty());
    assert(mixer.nativeBusPatchAwaitingCallback());
    render(true);
    assert(!mixer.nativeBusPatchAwaitingCallback()&&!tap[0]&&!tap[1]&&outL.back()==0.f&&outR.back()==0.f);
    render(false);render(false);
    assert(!tap[0]&&!tap[1]);
    assert((physicalMain==std::array<int,2>{0,1}));
    assert((physicalMonitor==std::array<int,2>{-1,-1}));
    assert(std::abs(outL.back()-separateMain)<1.e-5f);
    assert(mixer.captureNativeMixerSession(saved,blobs).isEmpty());
    saved.id=d.id;saved.savedMs=d.savedMs;saved.binding=d.binding;
    MixerSessionDocument dormant;assert(Codec::decode(Codec::encode(saved),dormant));
    assert(!dormant.outputs.secondaryMain()&&!dormant.outputs.monitorEnabled&&
        dormant.legacyIds.monitor==monitor);
    const auto retainedEdge=monitorEdge(dormant);
    assert(retainedEdge.edgeId==originalEdge.edgeId&&retainedEdge.gain==originalEdge.gain&&
        retainedEdge.preFader==originalEdge.preFader&&retainedEdge.enabled==originalEdge.enabled);
    // Re-enter Separate without replacing the authored monitor send.
    render(true);rev=revisions();
    assert(mixer.patchDmxSplitOutput(pins.epoch,rev[0],rev[1],true,pins,true,true).isEmpty());
    render(true);assert(!mixer.nativeBusPatchAwaitingCallback());
    render(false);render(false);
    assert(tap[0]&&tap[1]&&std::abs(tap[0][479]-separateMain)<1.e-5f);
    assert(std::abs(outL.back()-separateHeadphone)<1.e-5f);
    assert(mixer.captureNativeMixerSession(saved,blobs).isEmpty());
    assert(monitorEdge(saved).edgeId==originalEdge.edgeId&&saved.legacyIds.monitor==monitor);
    render(true);assert(!tap[0]&&!tap[1]&&outL.back()==0.f&&outR.back()==0.f);
    std::cout<<"DMX_SPLIT_NATIVE_PCM_AND_BINDING_PASS\n";
    std::cout<<"DMX_REVERSIBLE_MONITOR_DORMANT_PCM_ROUNDTRIP_PASS\n";
}
static void verifyLogicalOutputNativePcm(){
    using R=armix::device::LogicalOutputRegistry;
    using P=LogicalOutputProjection;
    FixtureDevice device(true);
    LiveDryRoutingLab mixer;mixer.prepareDevice(&device);
    armix::device::JuceActivePortSnapshot pins;
    armix::device::JuceActivePortSnapshot::Fault fault{};
    assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
    auto d=makeSession();
    d.binding={"ASIO","Verified ASIO","Verified ASIO","3","3f",48000.,480,
        device.getInputChannelNames(),device.getOutputChannelNames()};
    d.outputs={6,0,1,-1,-1,false};d.request.maxFrames=480;d.rackBypassed.clear();
    const std::uint64_t port=9007199254740993ULL;
    assert(d.logicalOutputs.add(port,"Stage amp"));
    assert(d.logicalOutputs.restoreIntent(port,{{"ASIO","Verified ASIO"},2,3,"Amp L","Amp R"}));
    assert(d.logicalOutputs.assign(16,port,R::Role::external));
    MixerSessionDocument decoded;assert(Codec::decode(Codec::encode(d),decoded));
    juce::AudioPluginFormatManager formats;std::shared_ptr<DynamicChannelFxLeaseBank> bank;
    assert(NativeMixerSessionFxRestore::prepare(decoded,{},formats,48000.,480,pins.epoch,
        mixer.nativeGraphRevisionForRestore(),bank).isEmpty());
    assert(mixer.restoreNativeMixerSession(decoded,std::move(bank),pins,true).isEmpty());
    std::array<float,480> inputL{},inputR{};inputL.fill(.15f);
    const float* in[]={inputL.data(),inputR.data()};
    std::array<std::array<float,480>,6> buffers{};std::array<float*,6> out{};
    for(std::size_t i=0;i<out.size();++i)out[i]=buffers[i].data();
    auto render=[&](){for(int i=0;i<2;++i)assert(mixer.process(in,2,out.data(),6,480,1.f,false,
        nullptr,nullptr,false,nullptr,nullptr,true));};
    render();assert(buffers[2].back()>.01f&&buffers[3].back()>.01f);
    auto state=mixer.nativeBusStatus();std::uint64_t revision=0,roster=0;
    assert(parseDecimal(state.getProperty("graphRevision",{}),revision));
    assert(parseDecimal(state.getProperty("rosterRevision",{}),roster));
    const auto logical=mixer.logicalOutputRevision();
    P::Edit e;e.port=port;e.operation=P::Operation::bind;e.confirmed=true;
    e.binding={{"ASIO","Verified ASIO"},4,5,"Spare L","Spare R"};
    assert(!mixer.editNativeLogicalOutput(pins.epoch,revision,roster,logical,e,pins,false).accepted);
    auto changed=mixer.editNativeLogicalOutput(pins.epoch,revision,roster,logical,e,pins,true);
    assert(changed.accepted&&changed.pcmRevision==revision+1);
    render();
    assert(std::abs(buffers[2].back())<1.e-6f&&std::abs(buffers[3].back())<1.e-6f);
    assert(buffers[4].back()>.01f&&buffers[5].back()>.01f);
    assert(!mixer.editNativeLogicalOutput(pins.epoch,changed.pcmRevision,roster,logical,e,pins,true).accepted);
    MixerSessionDocument saved;std::vector<BinaryState> blobs;
    assert(mixer.captureNativeMixerSession(saved,blobs).isEmpty());
    saved.id=d.id;saved.savedMs=d.savedMs;saved.binding=d.binding;
    assert(Codec::decode(Codec::encode(saved),decoded)&&decoded.externalOutputs.empty());
    assert(decoded.logicalOutputs.find(port)->binding->left==4);
    e.operation=P::Operation::unbind;
    changed=mixer.editNativeLogicalOutput(pins.epoch,changed.pcmRevision,roster,
        mixer.logicalOutputRevision(),e,pins,true);
    assert(changed.accepted);render();
    assert(std::abs(buffers[4].back())<1.e-6f&&std::abs(buffers[5].back())<1.e-6f);
    state=mixer.nativeBusStatus();
    const auto refs=state.getProperty("logicalOutputRegistry",{}).getProperty("references",{});
    assert(refs.getArray()->size()==1&&(*refs.getArray())[0].getProperty("state","").toString()=="NEEDS_REMAP");
}
static void verifyIoDefaults(){
    using namespace armix::session;
    using I=IoPortLayout::I;using O=IoPortLayout::O;
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("ArSonMixIoDefaults","",false);
    assert(dir.createDirectory().wasOk());
    I inputs;O outputs;
    const std::uint64_t id=9007199254740993ULL;
    assert(inputs.add(id,"Vocal",I::Format::mono));
    assert(inputs.restoreIntent(id,{{"ASIO","Verified ASIO","Verified ASIO"},0,-1,"Capture L",""}));
    assert(inputs.assign(1,id));
    assert(outputs.add(20,"Stage amp")&&outputs.restoreIntent(20,{{"ASIO","Verified ASIO"},2,3,"Amp L","Amp R"}));
    assert(outputs.assign(16,20,O::Role::external));
    auto layout=IoPortLayout::capture(inputs,outputs);
    assert(layout.definitionsOnly()&&layout.inputs.find(id)&&layout.outputs.find(20));
    NativeIoDefaults store(dir);
    assert(store.revision()==0&&!store.readOnly()&&store.layout().empty());
    const auto saveError=store.save(0,layout);
    if(saveError.isNotEmpty())std::cerr<<"I/O defaults first save: "<<saveError.toStdString()<<'\n';
    assert(saveError.isEmpty()&&store.revision()==1);
    const auto file=dir.getChildFile("io-defaults.v1.json");
    const auto canonical=file.loadFileAsString();
    const auto parsed=juce::JSON::parse(canonical);
    assert(parsed.isObject()&&juce::JSON::toString(parsed,false)==canonical);
    assert(!store.save(1,IoPortLayout{inputs,outputs}).isEmpty());
    assert(store.revision()==1&&file.loadFileAsString()==canonical);
    // Canonical unknown fields cannot smuggle routing or ARM into a default.
    auto tampered=juce::JSON::parse(canonical);
    tampered.getDynamicObject()->setProperty("armed",true);
    assert(file.replaceWithText(juce::JSON::toString(tampered,false),false,false,nullptr));
    NativeIoDefaults rejected(dir);
    assert(rejected.readOnly()&&rejected.save(0,layout).isNotEmpty());
    assert(file.loadFileAsString()==juce::JSON::toString(tampered,false));
    assert(file.replaceWithText(canonical,false,false,nullptr));
    assert(store.save(0,{}).isNotEmpty()&&store.revision()==1);
    NativeIoDefaults reopened(dir);
    assert(!reopened.readOnly()&&reopened.revision()==1&&reopened.layout().inputs.find(id));
    assert(reopened.layout().definitionsOnly());
    FixtureDevice device(true);LiveDryRoutingLab mixer;mixer.prepareDevice(&device);
    juce::AudioPluginFormatManager formats;
    assert(mixer.startUnpatchedSession(true,false,0,formats,48000.,480).isEmpty());
    assert(mixer.admitChannelIdFromAcceptedM4(0,480,true,{},true));
    armix::device::JuceActivePortSnapshot pins;
    armix::device::JuceActivePortSnapshot::Fault fault{};
    assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
    HardwareBinding binding{"ASIO","Verified ASIO","Verified ASIO","3","3f",48000.,480,
        device.getInputChannelNames(),device.getOutputChannelNames()};
    auto wrong=binding;wrong.capture="Other capture";
    auto qualified=layout.qualify(wrong,pins);
    assert(!qualified.inputs.find(id)->binding&&qualified.outputs.find(20)->binding);
    auto wrongPins=pins;wrongPins.outputNames.set(2,"Renamed");
    qualified=layout.qualify(binding,wrongPins);
    assert(qualified.inputs.find(id)->binding&&!qualified.outputs.find(20)->binding);
    assert(!mixer.installIoPortDefaults(layout,binding,pins,false));
    assert(mixer.installIoPortDefaults(layout,binding,pins,true));
    assert(!mixer.installIoPortDefaults(layout,binding,pins,true));
    assert(!mixer.hasVerifiedNativeSources());
    const auto status=mixer.nativeBusStatus();
    assert(status.getProperty("logicalInputRegistry",{}).getProperty("references",{}).getArray()->isEmpty());
    assert(status.getProperty("logicalOutputRegistry",{}).getProperty("references",{}).getArray()->isEmpty());
    assert(reopened.save(1,{}).isEmpty()&&reopened.revision()==2);
    assert(file.appendText("damaged"));
    NativeIoDefaults damaged(dir);
    assert(damaged.readOnly()&&damaged.save(0,layout).isNotEmpty());
    assert(file.loadFileAsString().endsWith("damaged"));
    assert(dir.deleteRecursively());
}
// Exact durable AVRB state and independent PRE EdgeId survive .smix.
static void verifyDmxWetReturnSession(const armix::builtin_fx::NativeBuiltInFx& fx){
    auto d=makeSession();
    auto rows=d.roster.channels();
    auto* wet=&*std::find_if(rows.begin(),rows.end(),[](const auto& row){return row.id.value==11;});
    wet->insertCatalogIds.push_back(fx.id);
    // Existing legacy source -> AUX PRE edge stays exact. Return -> MAIN
    // and preserved physical binding use the original .smix schema.
    assert(d.roster.restoreExact(d.acceptedRevision,rows));
    auto processor=fx.prepare(48000.,256,nullptr,0);
    assert(processor);
    BinaryState state;processor->getStateInformation(state.bytes);
    assert(!state.bytes.isEmpty());
    state.sha256=juce::SHA256(state.bytes).toHexString();
    d.inserts.push_back({{11},0,fx.id,{},state.sha256,
                         fx.name,0,true,"g5c-96011"});
    MixerSessionDocument reopened;
    const auto json=Codec::encode(d);
    assert(Codec::decode(json,reopened)&&Codec::encode(reopened)==json);
    assert(reopened.roster.find({1})->sends[1].edgeId==101);
    assert(reopened.roster.find({11})->insertCatalogIds[0]==fx.id);
    assert(reopened.inserts.front().instanceId=="g5c-96011"&&reopened.inserts.front().bypass);
    juce::AudioPluginFormatManager formats;
    std::shared_ptr<DynamicChannelFxLeaseBank> bank;
    assert(NativeMixerSessionFxRestore::prepare(reopened,{state},formats,48000.,256,7,1,bank).isEmpty());
    const auto* lease=bank->ownerForControl({11},0);
    assert(lease&&*lease&&(*lease)->silenceBypass&&(*lease)->bypassRequested.load());
    std::array<float,256> l{},r{};
    for(int b=0;b<8;++b){
        l.fill(.2f);r.fill(-.1f);(*bank)({11},l.data(),r.data(),256);
    }
    assert(std::all_of(l.begin(),l.end(),[](float v){return v==0.f;}));
    auto bad=reopened;auto invalidRows=bad.roster.channels();
    auto first=std::find_if(invalidRows.begin(),invalidRows.end(),[](const auto& row){return row.id.value==1;});
    auto aux=std::find_if(invalidRows.begin(),invalidRows.end(),[](const auto& row){return row.id.value==11;});
    first->insertCatalogIds=aux->insertCatalogIds;aux->insertCatalogIds.clear();
    bad.inserts.front().channel={1};
    assert(bad.roster.restoreExact(bad.acceptedRevision,invalidRows));
    MixerSessionDocument rejected;
    assert(!Codec::decode(Codec::encode(bad),rejected));
    assert(!NativeMixerSessionFxRestore::prepare(bad,{state},formats,48000.,256,7,2,bank).isEmpty());
    assert(bank->ownerForControl({11},0));
    std::cout<<"DMX_RETURN_SMIX_ROUNDTRIP_PASS\n";
}
// Product path: create a fresh owned wet return on the actual native lab,
// patch the exact EdgeId, measure dry vs wet PCM, save and reopen the real
// .smix with a fresh processor bank and verify the same identities.
static void verifyCreatedWetReturnPcmAndRecovery(){
    using namespace armix::mixer;
    FixtureDevice device(true);device.outputMask.clear();device.outputMask.setRange(0,2,true);
    auto mixerStorage=std::make_unique<LiveDryRoutingLab>();
    auto& mixer=*mixerStorage;mixer.prepareDevice(&device);
    armix::device::JuceActivePortSnapshot pins;
    armix::device::JuceActivePortSnapshot::Fault fault{};
    assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
    auto d=makeSession();
    d.binding={"ASIO","Verified ASIO","Verified ASIO","3","3",48000.,480,
        device.getInputChannelNames(),device.getOutputChannelNames()};
    d.request.maxFrames=480;d.rackBypassed.clear();
    juce::AudioPluginFormatManager formats;
    std::shared_ptr<DynamicChannelFxLeaseBank> bank;
    assert(NativeMixerSessionFxRestore::prepare(d,{},formats,48000.,480,pins.epoch,
        mixer.nativeGraphRevisionForRestore(),bank).isEmpty());
    assert(mixer.restoreNativeMixerSession(d,std::move(bank),pins,true).isEmpty());
    std::array<float,480> inL{},inR{},outL{},outR{};
    const float* in[]={inL.data(),inR.data()};float* out[]={outL.data(),outR.data()};
    auto render=[&](LiveDryRoutingLab& lab,bool muted=false){
        assert(lab.process(in,2,out,2,480,1.f,muted));
    };
    auto revisions=[&](LiveDryRoutingLab& lab){
        auto state=lab.nativeBusStatus();std::uint64_t rev=0,roster=0;
        assert(parseDecimal(state.getProperty("graphRevision",{}),rev));
        assert(parseDecimal(state.getProperty("rosterRevision",{}),roster));
        return std::array<std::uint64_t,2>{rev,roster};
    };
    inL.fill(.1f);
    render(mixer);render(mixer);
    const float dry=outL.back();assert(dry>.01f);
    auto rev=revisions(mixer);
    assert(!mixer.editNativeBus(pins.epoch,rev[0],rev[1],ChannelRole::fxReturn,{},"Empty",0.f,{},true).accepted);
    assert(revisions(mixer)==rev);
    auto processor=armix::builtin_fx::prepareNativeVocalReverbReturn(48000.,480,nullptr,0);
    assert(processor);
    auto owner=std::make_shared<LiveChannelFxGraph::FxInstance>(std::move(processor),
        armix::builtin_fx::VocalReverbProcessor::wetReturnIdentity,"Reverb-Sis Return",
        LiveDryRoutingLab::Block::hardFrameLimit);
    const auto token=owner->instanceId;
    auto created=mixer.editNativeBus(pins.epoch,rev[0],rev[1],ChannelRole::fxReturn,
        {},"New wet return",0.f,{},true,{},true,std::move(owner));
    assert(created.accepted&&created.created.value);
    const auto wet=created.created;
    render(mixer);render(mixer);
    assert(std::abs(outL.back()-dry)<1.e-5f);
    rev=revisions(mixer);
    // Never allow the last wet processor to disappear even before assignment.
    assert(!mixer.editNativeFxRack(pins.epoch,rev[0],rev[1],wet,0,token,{},0).accepted);
    assert(revisions(mixer)==rev);
    WireChange send{WireChange::Kind::connect,{1},wet,0,0.f};
    assert(mixer.applyNativeWireTransaction(pins.epoch,rev[0],rev[1],
        std::span<const WireChange>{&send,1}).accepted);
    render(mixer);render(mixer);
    assert(std::abs(outL.back()-dry)<1.e-5f);
    MixerSessionDocument saved;std::vector<BinaryState> blobs;
    assert(mixer.captureNativeMixerSession(saved,blobs).isEmpty());
    const auto* voice=saved.roster.find({1});assert(voice);
    const auto edge=std::find_if(voice->sends.begin(),voice->sends.end(),
        [&](const auto& e){return e.destination==wet;});
    assert(edge!=voice->sends.end()&&edge->edgeId&&edge->gain==0.f&&!edge->primaryOutput);
    const auto edgeId=edge->edgeId;
    rev=revisions(mixer);
    assert(mixer.setNativeWireGain(pins.epoch,rev[0],rev[1],{1},edgeId,1.f));
    for(int i=0;i<30;++i)render(mixer);
    inL.fill(0.f);
    double wetEnergy=0.;
    for(int b=0;b<100;++b){
        render(mixer);
        for(float v:outL){assert(std::isfinite(v));wetEnergy+=double(v)*v;}
    }
    assert(wetEnergy>1.e-8);
    assert(mixer.bypassNativeInsert(pins.epoch,rev[0],wet,0,token,true));
    inL.fill(.1f);
    for(int b=0;b<8;++b)render(mixer);
    assert(std::abs(outL.back()-dry)<1.e-5f);
    render(mixer,true);
    assert(mixer.captureNativeMixerSession(saved,blobs).isEmpty());
    saved.id=d.id;saved.savedMs=d.savedMs;saved.binding=d.binding;
    saved.masterMute=true;saved.resumeMain=false;
    // Device loss must preserve the actual leased processor state, not a stale disk mirror.
    const auto oldEpoch=mixer.epoch();
    mixer.deviceStopped();
    assert(!mixer.channelIdPhysicalPcmActive() && mixer.hasRetainedNativeSession());
    LiveDryRoutingLab::RetainedMixerCapture detached;
    assert(mixer.beginRetainedMixerCapture(detached).isEmpty());
    assert(detached.ownerEpoch==oldEpoch && detached.observedEpoch!=oldEpoch);
    bool done=false;
    for(unsigned i=0;i<1024 && !done;++i)
        assert(mixer.stepRetainedMixerCapture(detached,done).isEmpty());
    assert(done);
    detached.document.id=saved.id;detached.document.savedMs=saved.savedMs;
    detached.document.binding=saved.binding;
    detached.document.masterGain=saved.masterGain;
    detached.document.masterMute=true;detached.document.resumeMain=false;
    detached.document.masterMono=saved.masterMono;
    NativeLogicalMixerWorkspace offline;
    assert(offline.load(std::move(detached.document),std::move(detached.states)));
    const auto* prior=offline.storedFx(wet,0);
    assert(prior && prior->instanceId==token && prior->bypass);
    const auto hash=prior->sha256;
    juce::var edit(new juce::DynamicObject());
    edit.getDynamicObject()->setProperty("workspaceToken",offline.workspaceToken());
    edit.getDynamicObject()->setProperty("baseRevision",juce::String(std::to_string(offline.revision())));
    edit.getDynamicObject()->setProperty("operation","SET_FX_RACK_BYPASS");
    edit.getDynamicObject()->setProperty("channelId",juce::String(std::to_string(wet.value)));
    edit.getDynamicObject()->setProperty("value",true);
    assert(offline.apply(edit).accepted);
    assert(offline.storedFx(wet,0)->sha256==hash);
    assert(!static_cast<bool>(offline.snapshot().getProperty("physicalOutputAdmitted",true)));
    saved=offline.document();blobs=offline.states();
    assert(std::find(saved.rackBypassed.begin(),saved.rackBypassed.end(),wet)!=saved.rackBypassed.end());
    const auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("armix-created-wet-","",false);
    assert(folder.createDirectory().wasOk());
    const auto file=folder.getChildFile("wet.smix");
    assert(NativeMixerSessionStore::writeFile(file,saved,blobs).isEmpty());
    MixerSessionDocument reopened;std::vector<BinaryState> states;
    assert(NativeMixerSessionStore::readFile(file,reopened,states).isEmpty());
    const auto* row=reopened.roster.find(wet);
    assert(row&&row->role==ChannelRole::fxReturn&&row->insertCatalogIds.size()==1&&
        isWetReturnCatalog(row->insertCatalogIds.front()));
    const auto* restoredVoice=reopened.roster.find({1});assert(restoredVoice);
    const auto restoredEdge=std::find_if(restoredVoice->sends.begin(),restoredVoice->sends.end(),
        [&](const auto& e){return e.edgeId==edgeId;});
    assert(restoredEdge!=restoredVoice->sends.end()&&restoredEdge->destination==wet&&restoredEdge->gain==1.f);
    FixtureDevice replacement(true);replacement.outputMask.clear();replacement.outputMask.setRange(0,2,true);
    auto recoveredStorage=std::make_unique<LiveDryRoutingLab>();
    auto& recovered=*recoveredStorage;recovered.prepareDevice(&replacement);
    armix::device::JuceActivePortSnapshot newPins;
    assert(armix::device::JuceActivePortSnapshot::capture(&replacement,recovered.epoch(),newPins,fault));
    assert(NativeMixerSessionFxRestore::prepare(reopened,states,formats,48000.,480,newPins.epoch,
        recovered.nativeGraphRevisionForRestore(),bank).isEmpty());
    assert(bank->rackBypassed(wet));
    const auto* lease=bank->ownerForControl(wet,0);
    assert(lease&&*lease&&(*lease)->instanceId==token&&(*lease)->silenceBypass&&(*lease)->bypassRequested.load());
    assert(recovered.restoreNativeMixerSession(reopened,std::move(bank),newPins,true).isEmpty());
    render(recovered,true);assert(outL.back()==0.f&&outR.back()==0.f);
    for(int i=0;i<8;++i)render(recovered);
    assert(std::abs(outL.back()-dry)<1.e-5f);
    assert(folder.deleteRecursively());
    std::cout<<"UNIFIED_CREATED_WET_PCM_SMIX_RECOVERY_PASS\n";
}

// A real native EQ command is queued immediately before device STOP,
// with no subsequent PCM callback to consume it. The detached capture
// must serialize the new accepted value and not fake physical admission.
static void verifyDetachedPendingEqCapture(){
    using Eq=armix::builtin_fx::ChannelEqProcessor;
    FixtureDevice device(true);device.outputMask.clear();device.outputMask.setRange(0,2,true);
    auto storage=std::make_unique<LiveDryRoutingLab>();
    auto& mixer=*storage;mixer.prepareDevice(&device);
    armix::device::JuceActivePortSnapshot pins;
    armix::device::JuceActivePortSnapshot::Fault fault{};
    assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
    auto d=makeSession();
    d.binding={"ASIO","Verified ASIO","Verified ASIO","3","3",48000.,480,
        device.getInputChannelNames(),device.getOutputChannelNames()};
    d.request.maxFrames=480;
    auto rows=d.roster.channels();
    for(auto& row:rows)if(row.id.value==5)row.insertCatalogIds={Eq::catalogIdentity};
    assert(d.roster.restoreExact(d.acceptedRevision,rows));
    Eq initial;initial.prepareToPlay(48000.,480);
    BinaryState binary;initial.getStateInformation(binary.bytes);
    binary.sha256=juce::SHA256(binary.bytes).toHexString();
    StoredInsert insert;insert.channel={5};insert.slot=0;insert.catalogId=Eq::catalogIdentity;
    insert.name=initial.getName();insert.sha256=binary.sha256;
    d.inserts.push_back(insert);
    juce::AudioPluginFormatManager formats;
    std::shared_ptr<DynamicChannelFxLeaseBank> bank;
    assert(NativeMixerSessionFxRestore::prepare(d,{binary},formats,48000.,480,pins.epoch,
        mixer.nativeGraphRevisionForRestore(),bank).isEmpty());
    auto owner=*bank->ownerForControl({5},0);
    auto* eq=dynamic_cast<Eq*>(owner->processor.get());assert(eq);
    assert(mixer.restoreNativeMixerSession(d,std::move(bank),pins,true).isEmpty());
    auto band=eq->acceptedBands()[2];band.gainDb=6.f;
    assert(eq->queueBand(2,band,77));
    assert(!eq->stateSnapshotQuiescent());
    mixer.deviceStopped();
    assert(mixer.hasRetainedNativeSession()&&!mixer.channelIdPhysicalPcmActive());
    LiveDryRoutingLab::RetainedMixerCapture detached;
    assert(mixer.beginRetainedMixerCapture(detached).isEmpty());
    bool done=false;
    {
        armix::rt::RealtimeRenderPermit held(owner->processing);
        assert(held);
        assert(mixer.stepRetainedMixerCapture(detached,done).isEmpty());
        assert(!done && !eq->stateSnapshotQuiescent());
    }
    for(unsigned i=0;i<32&&!done;++i)
        assert(mixer.stepRetainedMixerCapture(detached,done).isEmpty());
    assert(done&&eq->stateSnapshotQuiescent());
    assert(eq->acceptedBands()[2].gainDb==6.f);
    assert(!mixer.channelIdPhysicalPcmActive());
    assert(detached.document.inserts.size()==1&&detached.states.size()==1);
    assert(detached.document.inserts.front().instanceId==owner->instanceId);
    Eq reopened;reopened.prepareToPlay(48000.,480);
    const auto& bytes=detached.states.front().bytes;
    assert(reopened.restoreUnpublishedState(bytes.getData(),static_cast<int>(bytes.getSize())));
    assert(reopened.acceptedBands()[2].gainDb==6.f);
    std::cout<<"DETACHED_PENDING_EQ_CAPTURE_PASS\n";
}

// FIELD 7bbb3d6: ordinary inserts must work on an armed aligned MAIN
// graph while its post-FX PCM is also exposed to the secondary sink.
// No actual driver or WASAPI endpoint is opened by this fixture.
static void verifyLiveInsertWithSecondaryMainTap(){
    using namespace armix::mixer;
    FixtureDevice device(true);device.outputMask.clear();device.outputMask.setRange(0,2,true);
    auto storage=std::make_unique<LiveDryRoutingLab>();
    auto& mixer=*storage;mixer.prepareDevice(&device);
    armix::device::JuceActivePortSnapshot pins;
    armix::device::JuceActivePortSnapshot::Fault fault{};
    assert(armix::device::JuceActivePortSnapshot::capture(&device,mixer.epoch(),pins,fault));
    auto doc=makeSession();
    doc.binding={"ASIO","Verified ASIO","Verified ASIO","3","3",48000.,480,
        device.getInputChannelNames(),device.getOutputChannelNames()};
    doc.request.maxFrames=480;doc.rackBypassed.clear();
    juce::AudioPluginFormatManager formats;
    std::shared_ptr<DynamicChannelFxLeaseBank> bank;
    assert(NativeMixerSessionFxRestore::prepare(doc,{},formats,48000.,480,pins.epoch,
        mixer.nativeGraphRevisionForRestore(),bank).isEmpty());
    assert(mixer.restoreNativeMixerSession(doc,std::move(bank),pins,true).isEmpty());
    std::array<float,480> inL{},inR{},outL{},outR{};
    inL.fill(.1f);inR.fill(.1f);
    const float* in[]={inL.data(),inR.data()};float* out[]={outL.data(),outR.data()};
    auto render=[&]{
        std::array<float*,2> splitTap{};
        std::array<int,2> mainPair{};
        const auto lease=mixer.captureNativeOutputLease();
        assert(mixer.process(in,2,out,2,480,1.f,false,nullptr,nullptr,false,
            &mainPair,nullptr,false,{},false,&lease,false,&splitTap));
        // FIELD is aligned MAIN, not separate headphone/stream mode.
        // Match the production callback's NativeOutputPairTap fallback.
        assert(!splitTap[0]&&!splitTap[1]);
        assert((mainPair==std::array<int,2>{0,1}));
        const auto tap=NativeOutputPairTap::select(out,2,mainPair);
        assert(tap[0]==outL.data()&&tap[1]==outR.data());
        for(int i=0;i<480;++i)
            assert(std::isfinite(tap[0][i])&&std::isfinite(tap[1][i]));
        return outL.back();
    };
    render();const float baseline=render();assert(baseline>.01f);
    auto revisions=[&]{
        const auto state=mixer.nativeBusStatus();std::uint64_t rev=0,roster=0;
        assert(parseDecimal(state.getProperty("graphRevision",{}),rev));
        assert(parseDecimal(state.getProperty("rosterRevision",{}),roster));
        return std::array<std::uint64_t,2>{rev,roster};
    };
    auto add=[&](const char* id,ChannelId target,std::array<std::uint64_t,2> base){
        const auto* fx=armix::builtin_fx::findNativeBuiltInFx(id);assert(fx);
        auto proc=fx->prepare(48000.,480,nullptr,0);assert(proc);
        auto owner=std::make_shared<LiveChannelFxGraph::FxInstance>(std::move(proc),
            fx->id,fx->name,LiveDryRoutingLab::Block::hardFrameLimit);
        // Explicitly not disarmed: use the real live internal-FX publication.
        return mixer.attachNativeInsert(pins.epoch,base[0],base[1],target,
            fx->id,std::move(owner),true,false);
    };
    const auto before=revisions();
    auto ducker=add("armix.builtin.auto-ducker.v1",{16},before);
    assert(ducker.accepted&&ducker.pcmRevision==before[0]+1);
    render();assert(render()>.01f);
    const auto after=revisions();
    assert(!add("armix.builtin.channel-eq.v1",{16},before).accepted);
    assert(revisions()==after); // stale attempt cannot replace accepted rack
    auto eq=add("armix.builtin.channel-eq.v1",{16},after);
    assert(eq.accepted&&eq.pcmRevision==after[0]+1);
    render();assert(render()>.01f);
    const auto state=mixer.nativeBusStatus();
    const auto channels=state.getProperty("channels",{});assert(channels.isArray());
    bool found=false;
    for(const auto& row:*channels.getArray())if(row.getProperty("id","").toString()=="16"){
        const auto rack=row.getProperty("inserts",{});assert(rack.isArray()&&rack.size()==2);
        assert(rack[0].getProperty("id",{}).toString()=="armix.builtin.auto-ducker.v1");
        assert(rack[1].getProperty("id",{}).toString()=="armix.builtin.channel-eq.v1");
        found=true;
    }
    assert(found);
    std::cout<<"LIVE_INSERT_SECONDARY_MAIN_TAP_PASS\n";
}
int main(){
    verifyLiveInsertWithSecondaryMainTap();
    verifyDetachedPendingEqCapture();
    verifyCreatedWetReturnPcmAndRecovery();
    for(const auto& fx:armix::builtin_fx::nativeBuiltInFx)
        if(armix::mixer::isWetReturnCatalog(fx.id))verifyDmxWetReturnSession(fx);
    verifyIoDefaults();
    checkLogicalOutputSession();
    verifyDmxSplitNativePcm();
    verifyLogicalOutputNativePcm();
    checkLogicalInputSession();
    verifyLogicalInputNativePcm();
    verifyChannelInputGainSessionRoundtrip();
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
    checkRecoveryPlan(d);
    checkSessionControlPlans();
    verifyExternalOutputPortableIntent();
    verifyIndependentAsioExternalRestoreProvenance();
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

    // Dedicated single-ASIO 2x2 -> 8x8: expanding enabled channels
    // preserves the microphone and MAIN, then VC can be patched independently.
    // This is synthetic vendor-neutral evidence, not a PS22 hardware claim.
    auto expanded=d;
    expanded.binding.inputNames={"MIC 1","MIC 2","VC 1","VC 2",
        "VC 3","VC 4","Loopback 1","Loopback 2"};
    expanded.binding.outputNames={"HP 1","HP 2","VC OUT 1","VC OUT 2",
        "VC OUT 3","VC OUT 4","Other 1","Other 2"};
    auto full=expanded.binding;
    full.inputMask="ff";full.outputMask="ff";
    const auto fullPair=MixerDeviceBindingPolicy::proposedOutputs(
        expanded,full,8,BindingMode::verifiedDeviceApply);
    assert(fullPair.valid() && fullPair.mainL==0 && fullPair.mainR==1);
    assert(MixerDeviceBindingPolicy::apply(expanded,full,fullPair,8,
        BindingMode::verifiedDeviceApply).isEmpty());
    assert(expanded.request.physicalInputs==8);
    assert(expanded.request.sources.front().inputL==0);
    assert(expanded.roster.revision()==d.roster.revision());
    assert(expanded.binding.inputMask=="ff" && expanded.binding.outputMask=="ff");
    const std::array<int,8> fullPins{{0,1,2,3,4,5,6,7}};
    StoredExternalOutput vc{{16},2,3,"VC OUT 1","VC OUT 2",full.playback};
    assert(MixerDeviceBindingPolicy::canRestoreExternalOutput(
        vc,full.playback,fullPins,full.outputNames,fullPair));
    assert(!MixerDeviceBindingPolicy::canRestoreExternalOutput(
        vc,full.playback,fullPins,juce::StringArray{},fullPair));

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
    MixerSessionDocument loaded;
    std::vector<BinaryState> readback;
    int retainedReads=0;
    const auto failedRetainedRead=[&](MixerSessionDocument&,std::vector<BinaryState>&) {
        ++retainedReads;
        return juce::String("Retained graph unavailable");
    };
    auto checkpoint=Recovery::read(store,false,loaded,readback,failedRetainedRead);
    assert(checkpoint.source==Recovery::Source::missing && checkpoint.error.isNotEmpty());
    assert(retainedReads==0);
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
    checkpoint=Recovery::read(store,false,loaded,readback,failedRetainedRead);
    assert(checkpoint.source==Recovery::Source::head && checkpoint.error.isEmpty());
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
    // P0-A: a real .smix file containing non-default controls on different
    // channel banks, MASTER, and existing FX/WIRE state. The codec is not
    // permitted to restore every control at the minimum or unity.
    {
        auto varied=d;
        auto rows=varied.roster.channels();
        for(std::size_t i=0;i<rows.size();++i){
            if(rows[i].role!=ChannelRole::input)continue;
            rows[i].faderLinear=.08f+.07f*static_cast<float>(i);
            rows[i].pan=(i%2==0)?-.35f:.6f;
            rows[i].mute=i%3==0;rows[i].solo=i==8;
        }
        assert(varied.roster.restoreExact(varied.acceptedRevision,rows));
        varied.masterGain=.43f;varied.masterMute=false;
        const auto project=studio.getChildFile("RestoreControls.smix");
        assert(NativeMixerSessionStore::writeFile(project,varied,{binary}).isEmpty());
        MixerSessionDocument reopened;
        std::vector<BinaryState> restoredStates;
        assert(NativeMixerSessionStore::readFile(project,reopened,restoredStates).isEmpty());
        assert(std::abs(reopened.masterGain-varied.masterGain)<1.e-6f);
        assert(reopened.masterMute==varied.masterMute);
        for(const auto& row:rows){
            const auto* actual=reopened.roster.find(row.id);
            assert(actual);
            assert(std::abs(actual->faderLinear-row.faderLinear)<1.e-6f);
            assert(std::abs(actual->pan-row.pan)<1.e-6f);
            assert(actual->mute==row.mute&&actual->solo==row.solo);
            assert(actual->insertCatalogIds==row.insertCatalogIds);
            assert(actual->sends.size()==row.sends.size());
        }
        assert(restoredStates.size()==1&&restoredStates[0].bytes==binary.bytes);
        const auto prior=Codec::encode(reopened);
        assert(project.replaceWithText("{broken"));
        assert(NativeMixerSessionStore::readFile(project,reopened,restoredStates).isNotEmpty());
        assert(Codec::encode(reopened)==prior);
        assert(restoredStates.size()==1&&restoredStates[0].bytes==binary.bytes);
    }
    auto tampered=juce::JSON::parse(portable.loadFileAsString());
    tampered.getProperty("pluginState",{}).getDynamicObject()->setProperty(binary.sha256,"AAAA");
    assert(portable.replaceWithText(juce::JSON::toString(tampered)));
    assert(NativeMixerSessionStore::readFile(portable,roundTrip,readback).isNotEmpty());
    assert(readback.size()==1&&readback[0].bytes==binary.bytes);
    // Bounded input preflight rejects recursive JSON before JUCE parsing,
    // with the caller's previous graph and vendor data preserved.
    juce::String deep;
    for(int i=0;i<65;++i)deep+="[";
    for(int i=0;i<65;++i)deep+="]";
    assert(portable.replaceWithText(deep));
    assert(NativeMixerSessionStore::readFile(portable,roundTrip,readback).isNotEmpty());
    assert(readback.size()==1&&readback[0].bytes==binary.bytes);
    assert(!Codec::decode(deep,roundTrip));
    const auto firstId=studio.getChildFile("MixerSession").getChildFile("HEAD")
        .loadFileAsString().trim();
    assert(armix::shows::detail::uuid(firstId));
    d.masterGain=.35f;d.savedMs+=1000;
    assert(store.commit(d,{binary}).isEmpty());
    assert(store.load(loaded,readback).isEmpty());
    assert(loaded.masterGain==.35f);
    assert(store.recoverPrevious().isNotEmpty()); // NEVER roll back valid HEAD
    auto rejectedRecovery=makeSession();
    rejectedRecovery.masterGain=.12f;
    std::vector<BinaryState> rejectedStates{binary};
    assert(store.recoverPrevious(rejectedRecovery,rejectedStates).isNotEmpty());
    assert(rejectedRecovery.masterGain==.12f &&
           rejectedStates.size()==1 && rejectedStates[0].bytes==binary.bytes);
    const auto head=studio.getChildFile("MixerSession").getChildFile("HEAD");
    const auto secondId=head.loadFileAsString().trim();
    // Missing HEAD must not be mistaken for a fresh mixer or overwrite
    // PREVIOUS. Verify actual recovery and recreate the second checkpoint.
    assert(head.deleteFile());
    assert(store.exists());
    assert(store.commit(d,{binary}).isNotEmpty());
    assert(!head.exists());
    checkpoint=Recovery::read(store,false,loaded,readback,failedRetainedRead);
    assert(checkpoint.source==Recovery::Source::previous&&checkpoint.error.isEmpty());
    assert(head.loadFileAsString().trim()==firstId);
    assert(head.replaceWithText(secondId+"\n")); // restore original test pointer
    assert(store.load(loaded,readback).isEmpty()&&loaded.masterGain==.35f);
    const auto vendor=studio.getChildFile("MixerSession").getChildFile("versions")
        .getChildFile(secondId).getChildFile("blobs")
        .getChildFile(binary.sha256+".bin");
    assert(vendor.appendText("CORRUPTED"));
    assert(store.load(loaded,readback).isNotEmpty());
    assert(loaded.masterGain==.35f); // rejection did not replace output
    checkpoint=Recovery::read(store,true,loaded,readback,failedRetainedRead);
    assert(checkpoint.source==Recovery::Source::retained &&
           checkpoint.error=="Retained graph unavailable");
    assert(retainedReads==1 && head.loadFileAsString().trim()==secondId);
    assert(loaded.masterGain==.35f);
    checkpoint=Recovery::read(store,false,loaded,readback,failedRetainedRead);
    assert(checkpoint.source==Recovery::Source::previous && checkpoint.error.isEmpty());
    assert(retainedReads==1 && head.loadFileAsString().trim()==firstId);
    assert(loaded.masterGain==.7f);
    assert(readback[0].bytes==binary.bytes);
    assert(!store.recoverPrevious().isEmpty());
    // A broken PREVIOUS can neither publish HEAD nor replace caller state.
    const auto oldVendor=studio.getChildFile("MixerSession").getChildFile("versions")
        .getChildFile(firstId).getChildFile("blobs").getChildFile(binary.sha256+".bin");
    assert(oldVendor.appendText("CORRUPTED"));
    checkpoint=Recovery::read(store,false,rejectedRecovery,rejectedStates,failedRetainedRead);
    assert(checkpoint.source==Recovery::Source::head && checkpoint.error.isNotEmpty());
    assert(rejectedRecovery.masterGain==.12f &&
           rejectedStates.size()==1 && rejectedStates[0].bytes==binary.bytes);
    assert(head.loadFileAsString().trim()==firstId);
    // Preserve complete damaged journal, not just HEAD, and unrelated data.
    const auto keep=studio.getChildFile("unrelated-user-preset");
    assert(keep.replaceWithText("preserve"));
    juce::File archived;
    assert(store.quarantineForStartupRecovery(archived).isEmpty());
    assert(archived.isDirectory()&&!store.exists());
    assert(archived.getChildFile("HEAD").loadFileAsString().trim()==firstId);
    assert(archived.getChildFile("PREVIOUS").existsAsFile());
    assert(archived.getChildFile("versions").getChildFile(firstId).getChildFile("blobs")
        .getChildFile(binary.sha256+".bin").existsAsFile());
    assert(keep.loadFileAsString()=="preserve");
    juce::File again;
    assert(store.quarantineForStartupRecovery(again).isEmpty()&&again==juce::File{});
    assert(archived.isDirectory());
    assert(base.deleteRecursively());
    std::cout<<"PASS: dynamic ChannelId/EdgeId graph, persistent vendor blobs and"
                " explicit verified HEAD recovery\n";
    return 0;
}
