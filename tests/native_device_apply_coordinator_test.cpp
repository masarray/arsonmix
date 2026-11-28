// SPDX-License-Identifier: AGPL-3.0-only
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/NativeDeviceApplyCoordinator.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>

using Coordinator=armix::device::NativeDeviceApplyCoordinator;
using Setup=juce::AudioDeviceManager::AudioDeviceSetup;

static juce::var config(const juce::String& json) {
    auto v=juce::JSON::parse(json);
    assert(v.isObject());
    return v;
}

static void verifyTransactionOutcomes() {
    using Outcome=Coordinator::Outcome;
    int bindCalls=0,rollbackCalls=0;
    Coordinator::BindResult binding;
    bool canRestore=true;
    const auto bind=[&] {
        ++bindCalls;
        return binding;
    };
    const auto rollback=[&] {
        ++rollbackCalls;
        return canRestore;
    };

    auto result=Coordinator::complete({},false,bind,rollback);
    assert(result.state==Outcome::accepted && bindCalls==1 && rollbackCalls==0);

    binding={"Retained pin mapping requires confirmation",true,true};
    result=Coordinator::complete({},false,bind,rollback);
    assert(result.state==Outcome::needsRemap && result.canReusePins &&
           result.error==binding.error && bindCalls==2 && rollbackCalls==0);

    result=Coordinator::complete("ASIO pin names changed",true,bind,rollback);
    assert(result.state==Outcome::topologyChanged && bindCalls==2 && rollbackCalls==0);

    result=Coordinator::complete("Driver readback mismatch",false,bind,rollback);
    assert(result.state==Outcome::restored && result.error=="Driver readback mismatch" &&
           bindCalls==2 && rollbackCalls==1);

    binding={"Mixer preparation was rejected"};
    canRestore=false;
    result=Coordinator::complete({},false,bind,rollback);
    assert(result.state==Outcome::recoveryRequired && result.error==binding.error &&
           bindCalls==3 && rollbackCalls==2);

    juce::BigInteger selected;
    selected.setBit(0);
    selected.setBit(1);
    const auto moved=armix::device::translateNamedPins(selected,{"Left","Right"},
        {"Unused","Right","Left"});
    assert(moved && !(*moved)[0] && (*moved)[1] && (*moved)[2]);
    assert(!armix::device::translateNamedPins(selected,{"Left","Right"},
        {"Left","Left","Right"}));
    assert(!armix::device::translateNamedPins(selected,{"Left","Right"},{"Left"}));
}

static void verifyBusyCommandReceipts() {
    Coordinator owner;
    const auto args=config(R"({"requestId":"out-o-42","channelId":"7","deviceEpoch":"13","revision":"8","value":true,"channel":2})");
    assert(!owner.busyCommandReceipt("routingExternalOutput",args).event);
    for(auto purpose : {Coordinator::Purpose::explicitApply,Coordinator::Purpose::setupDialog,
            Coordinator::Purpose::asioRecovery,Coordinator::Purpose::wasapiRecovery,
            Coordinator::Purpose::offlineFxCapture,Coordinator::Purpose::secondaryOutput,
            Coordinator::Purpose::busOutputPatch,Coordinator::Purpose::fxRackFile,
            Coordinator::Purpose::mixerSessionCapture}){
        auto lease=owner.tryBegin(purpose);
        assert(lease);
        const std::pair<const char*,const char*> expected[]={
            {"routingExternalOutput","arMixExternalOutputAck"},
            {"routingMonitorTiming","arMixMonitorTimingAck"},
            {"routingLogicalInputEdit","arMixLogicalInputAck"},
            {"routingLogicalOutputEdit","arMixLogicalOutputAck"},
            {"audioIoDefaults","arMixIoDefaultsAck"},
            {"audioNamedPatchEdit","arMixNamedPatchAck"},
            {"routingNativeFxRackBypass","arMixNativeFxPowerAck"},
            {"routingNativeFxInsertBypass","arMixNativeFxPowerAck"},
            {"routingM4Control","arMixM4ControlAck"},
            {"routingChannelPatch","arMixChannelPatchAck"},
            {"routingSourceSwitch","arMixSourceSwitchAck"},
            {"routingFxInsert","arMixRoutingFxInsertAck"},
            {"routingFxBypass","arMixRoutingFxBypassAck"},
            {"routingFxRackBypass","arMixRoutingFxRackAck"},
            {"routingGlobalFxBypass","arMixGlobalFxAck"},
            {"audioNamedProfileFile","arMixNamedProfileFileResult"},
            {"routingDryShowSave","arMixNativeShowSaveAck"},
            {"routingDryShowList","arMixNativeShowListAck"},
            {"routingDryShowRecall","arMixNativeShowRecallAck"},
            {"routingDryShowRecover","arMixNativeShowRecoverAck"},
        };
        for(const auto& [action,event]:expected){
            const auto rejected=owner.busyCommandReceipt(action,args);
            assert(rejected.event&&juce::String(rejected.event)==event);
            assert(rejected.payload.getProperty("requestId",{})==args.getProperty("requestId",{}));
            assert(rejected.payload.getProperty("ok",{}).isBool()&&!bool(rejected.payload.getProperty("ok",{})));
            assert(rejected.payload.getProperty("error",{}).toString().isNotEmpty());
            assert(owner.activePurpose()==purpose); // no mutation/release
        }
        const auto ext=owner.busyCommandReceipt("routingExternalOutput",args).payload;
        assert(ext.getProperty("channelId",{}).toString()=="7");
        assert(ext.getProperty("deviceEpoch",{}).toString()=="13");
        assert(ext.getProperty("graphRevision",{}).toString()=="8");
        const auto fx=owner.busyCommandReceipt("routingNativeFxInsertBypass",args).payload;
        assert(fx.getProperty("channelId",{}).toString()=="7"&&bool(fx.getProperty("bypassed",{})));
        const auto numeric=config(R"({"requestId":42,"channel":2})");
        const auto patch=owner.busyCommandReceipt("routingChannelPatch",numeric).payload;
        assert(patch.getProperty("requestId",{}).isInt()&&int(patch.getProperty("requestId",{}))==42);
        assert(int(patch.getProperty("channel",{}))==2);
        const auto slotRequest=config(R"({"requestId":"fx-1","channel":2,"slot":3})");
        const auto slot=owner.busyCommandReceipt("routingFxBypass",slotRequest).payload;
        assert(int(slot.getProperty("channel",{}))==2&&int(slot.getProperty("slot",{}))==3);
        for(const auto* operation:{"OPEN","EXPORT"}){
            auto file=config(R"({"requestId":"io-file-9"})");
            file.getDynamicObject()->setProperty("operation",operation);
            const auto rejected=owner.busyCommandReceipt("audioNamedProfileFile",file);
            assert(rejected.payload.getProperty("requestId",{}).toString()=="io-file-9");
            assert(rejected.payload.getProperty("operation",{}).toString()==operation);
        }
        assert(!owner.busyCommandReceipt("monitor",args).event);
        assert(!owner.busyCommandReceipt("secondaryOutputStop",args).event);
        assert(!owner.busyCommandReceipt("audioDeviceApply",args).event);
        assert(!owner.busyCommandReceipt("unknown",args).event);
    }
    assert(!owner.busy()&&!owner.busyCommandReceipt("routingExternalOutput",args).event);
}

int main() {
    verifyTransactionOutcomes();
    verifyBusyCommandReceipts();
    // Nested JUCE driver modal loops must reject another concurrent APPLY
    // and release exactly once, including moved lease ownership.
    Coordinator owner;
    assert(!owner.busy());
    {
        auto outer=owner.tryBegin();
        assert(outer && owner.busy());
        assert(!owner.tryBegin());
        auto moved=std::move(outer);
        assert(!outer && moved);
        assert(!owner.tryBegin());
    }
    assert(!owner.busy());
    auto reacquired=owner.tryBegin();
    assert(reacquired && owner.busy());
    assert(owner.activePurpose()==Coordinator::Purpose::explicitApply);
    // A JUCE modal dialog must reserve the SAME coordinator for its entire
    // visible lifecycle, not just the showDeviceSetup call stack.
    Coordinator modalOwner;
    std::optional<Coordinator::Lease> visibleSetup;
    auto lease=modalOwner.tryBegin(Coordinator::Purpose::setupDialog);
    assert(lease);
    visibleSetup.emplace(std::move(lease));
    assert(modalOwner.busy() &&
           modalOwner.activePurpose()==Coordinator::Purpose::setupDialog);
    assert(!modalOwner.tryBegin(Coordinator::Purpose::explicitApply));
    assert(!modalOwner.tryBegin(Coordinator::Purpose::asioRecovery));
    assert(!modalOwner.tryBegin(Coordinator::Purpose::setupDialog));
    // During the close callback move out the lease. New actions must remain
    // rejected until the final hardware readback/rebind finishes.
    {
        auto closing=std::move(*visibleSetup);
        visibleSetup.reset();
        assert(modalOwner.busy());
        assert(!modalOwner.tryBegin(Coordinator::Purpose::explicitApply));
    }
    assert(!modalOwner.busy() &&
           modalOwner.activePurpose()==Coordinator::Purpose::none);
    assert(!modalOwner.tryBegin(Coordinator::Purpose::none));
    {
        auto restart=modalOwner.tryBegin(Coordinator::Purpose::asioRecovery);
        assert(restart && !modalOwner.tryBegin(Coordinator::Purpose::setupDialog));
    }
    assert(!modalOwner.busy());

    const auto valid=config(R"({"backend":"ASIO","captureDevice":"ASIO Test",
        "playbackDevice":"ASIO Test","sampleRate":48000,"bufferSize":256,
        "assignSelectedPlaybackPinsToMain":true,
        "confirmRetainedPinMapping":false})");
    const auto parsed=Coordinator::parse(valid);
    assert(parsed);
    const auto& request=parsed.value;
    assert(request.backend=="ASIO" && request.rate==48000. &&
           request.block==256. && request.assignMain);
    assert(Coordinator::validateNames(request,{"ASIO Test"},{"ASIO Test"},false).isEmpty());
    assert(Coordinator::validateNames(request,{"ASIO Test"},{"Other"},false).isNotEmpty());
    assert(Coordinator::validateNames(request,{"Different"},{"ASIO Test"},false).isNotEmpty());
    auto outputOnly=config(R"({"backend":"Windows Audio",
        "captureDevice":"","playbackDevice":"Speakers",
        "sampleRate":0,"bufferSize":0})");
    auto output=Coordinator::parse(outputOnly);
    assert(output && output.value.rate==0. && output.value.block==0.);
    // Capture name is optional only when it is genuinely absent or empty.
    const auto explicitEmpty=config(R"({"backend":"Windows Audio",
        "captureDevice":"","playbackDevice":"Speakers","sampleRate":0,"bufferSize":0})");
    const auto outputParsed=Coordinator::parse(explicitEmpty);
    assert(outputParsed && outputParsed.value.capture.isEmpty());
    assert(Coordinator::validateNames(output.value,{},{"Speakers"},true).isEmpty());
    assert(Coordinator::validateNames(output.value,{},{"Speakers"},false).isNotEmpty());
    for(const auto& invalid : {
        "{}", R"({"backend":42,"playbackDevice":"X","sampleRate":48000,"bufferSize":256})",
        R"({"backend":true,"playbackDevice":"X","sampleRate":48000,"bufferSize":256})",
        R"({"backend":"ASIO","playbackDevice":19,"sampleRate":48000,"bufferSize":256})",
        R"({"backend":"ASIO","playbackDevice":true,"sampleRate":48000,"bufferSize":256})",
        R"({"backend":"ASIO","captureDevice":false,"playbackDevice":"X","sampleRate":48000,"bufferSize":256})",
        R"({"backend":"ASIO","captureDevice":72,"playbackDevice":"X","sampleRate":48000,"bufferSize":256})",
        R"({"backend":"ASIO","playbackDevice":["X"],"sampleRate":48000,"bufferSize":256})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":"48000","bufferSize":256})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":4000,"bufferSize":256})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":48000,"bufferSize":1.5})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":48000,"bufferSize":8193})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":48000,"bufferSize":256,"assignSelectedPlaybackPinsToMain":"yes"})",
        R"({"backend":"ASIO","playbackDevice":"X","sampleRate":48000,"bufferSize":256,"confirmRetainedPinMapping":1})"
    }) {
        assert(!Coordinator::parse(juce::JSON::parse(juce::String(invalid))));
    }

    auto input=juce::JSON::parse("[0,2]");
    auto outputs=juce::JSON::parse("[1,3]");
    juce::BigInteger inMask,outMask;
    assert(Coordinator::parsePinMasks(input,outputs,4,4,false,inMask,outMask));
    assert(inMask[0] && inMask[2] && outMask[1] && outMask[3]);
    assert(!Coordinator::parsePinMasks(input,outputs,4,4,true,inMask,outMask));
    assert(Coordinator::parsePinMasks(juce::JSON::parse("[]"),outputs,4,4,true,inMask,outMask));
    assert(inMask.isZero());
    assert(!Coordinator::parsePinMasks(input,juce::JSON::parse("[0]"),4,4,false,inMask,outMask));
    assert(!Coordinator::parsePinMasks(input,juce::JSON::parse("[3,3]"),4,4,false,inMask,outMask));
    assert(!Coordinator::parsePinMasks(input,juce::JSON::parse("[1,-1]"),4,4,false,inMask,outMask));
    assert(!Coordinator::parsePinMasks(input,juce::JSON::parse("[0,5]"),4,4,false,inMask,outMask));
    juce::Array<juce::var> huge;
    huge.add(juce::var(static_cast<juce::int64>(1ull<<33)));
    huge.add(juce::var(1));
    assert(!Coordinator::parsePinMasks(input,juce::var(huge),4,4,false,inMask,outMask));
    // An invalid request never partially overwrites the previous masks.
    assert(inMask.isZero() && outMask[1] && outMask[3]);

    Setup candidate;
    candidate.inputDeviceName="ASIO Test";
    candidate.outputDeviceName="ASIO Test";
    candidate.sampleRate=48000;
    candidate.bufferSize=256;
    candidate.useDefaultInputChannels=false;
    candidate.useDefaultOutputChannels=false;
    candidate.inputChannels.setBit(0);
    candidate.outputChannels.setBit(0);
    candidate.outputChannels.setBit(1);
    Coordinator::AppliedReadback observed;
    observed.backend="ASIO";
    observed.actual=candidate;
    observed.deviceName="ASIO Test";
    observed.deviceType="ASIO";
    observed.open=true;
    observed.activeInputs=candidate.inputChannels;
    observed.activeOutputs=candidate.outputChannels;
    observed.deviceRate=48000.;
    observed.deviceBuffer=256;
    assert(Coordinator::matchesApplied(request,candidate,observed));
    observed.deviceName="ASIO Different";
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.deviceName="ASIO Test";
    observed.deviceType="Windows Audio";
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.deviceType="ASIO";
    observed.actual.outputChannels.clearBit(1);
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.actual=candidate;
    observed.deviceBuffer=128;
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.deviceBuffer=256;
    observed.deviceRate=0.;
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.deviceRate=48000.;
    observed.deviceBuffer=0;
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.deviceBuffer=256;
    observed.backend="Windows Audio";
    assert(!Coordinator::matchesApplied(request,candidate,observed));
    observed.backend="ASIO";
    observed.open=false;
    assert(!Coordinator::matchesApplied(request,candidate,observed));

    Coordinator::RollbackReadback rollback;
    rollback.open=true;
    rollback.backend="ASIO";
    rollback.actual=candidate;
    rollback.deviceName="ASIO Test";
    rollback.deviceType="ASIO";
    rollback.activeInputs=candidate.inputChannels;
    rollback.activeOutputs=candidate.outputChannels;
    rollback.deviceRate=48000.;
    rollback.deviceBuffer=256;
    rollback.inputNames={"Mic 1","Mic 2"};
    rollback.outputNames={"Monitor L","Monitor R"};
    assert(Coordinator::matchesRollback(candidate,"ASIO",rollback.inputNames,
                                        rollback.outputNames,rollback));
    rollback.deviceName="Other ASIO";
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        rollback.inputNames,rollback.outputNames,rollback));
    rollback.deviceName="ASIO Test";
    rollback.activeOutputs.clearBit(1);
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        rollback.inputNames,rollback.outputNames,rollback));
    rollback.activeOutputs=candidate.outputChannels;
    rollback.deviceBuffer=512;
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        rollback.inputNames,rollback.outputNames,rollback));
    rollback.deviceBuffer=256;
    rollback.deviceRate=0.;
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        rollback.inputNames,rollback.outputNames,rollback));
    rollback.deviceRate=48000.;
    rollback.deviceBuffer=0;
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        rollback.inputNames,rollback.outputNames,rollback));
    rollback.deviceBuffer=256;
    rollback.outputNames={"Monitor R","Monitor L"};
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        {"Mic 1","Mic 2"},{"Monitor L","Monitor R"},rollback));
    rollback.outputNames={"Monitor L","Monitor R"};
    rollback.open=false;
    assert(!Coordinator::matchesRollback(candidate,"ASIO",
        rollback.inputNames,rollback.outputNames,rollback));

    // M2c accepted session/rebind policy must be independent of a device
    // dialog, DSP and any filesystem writes. Rejections never clear .smix.
    using Rebind=Coordinator::MixerRebindAction;
    const auto decide=Coordinator::decideMixerRebind;
    Coordinator::MixerRebindEvidence e;
    assert(decide(e)==Rebind::reject);
    e.installedChannelPcm=true;
    e.storedCheckpoint=true;
    e.retainedNativeSession=true;
    assert(decide(e)==Rebind::reject); // physical fence always wins
    e.physicalAuthorized=true;
    assert(decide(e)==Rebind::retainInstalled); // no-change dialog
    e.pendingStartupRestore=true;
    e.pristineStartupMixer=true;
    assert(decide(e)==Rebind::restoreRetained); // must restore accepted Show
    e.storedCheckpoint=false;
    e.retainedNativeSession=false;
    assert(decide(e)==Rebind::retainInstalled); // no file, installed graph valid
    e.installedChannelPcm=false;
    assert(decide(e)==Rebind::reject); // no fabricated fresh PCM admission
    e.freshBound=true;
    assert(decide(e)==Rebind::acceptFreshBound);
    e.retainedNativeSession=true;
    assert(decide(e)==Rebind::restoreRetained);
    e.retainedNativeSession=false;
    e.storedCheckpoint=true;
    assert(decide(e)==Rebind::restoreRetained);
    e.physicalAuthorized=false;
    assert(decide(e)==Rebind::reject); // stale device can't replay document

    std::cout<<"Native device Apply preflight/rollback: PASS\n";
}
