// SPDX-License-Identifier: AGPL-3.0-only
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
const read=p=>readFileSync(new URL('../'+p,import.meta.url),'utf8');
const main=read('src/Main.cpp');
const engine=read('src/LiveAudioEngine.h');
const policy=read('engine/device/AudioContinuityPolicy.h');
const coordinator=read('src/NativeDeviceApplyCoordinator.h');
const sessionRecovery=read('src/NativeMixerSessionRecovery.h');
const cmake=read('CMakeLists.txt');

test('display sleep never changes logical mixer intent or device configuration',()=>{
  assert.match(main,/WM_POWERBROADCAST/);
  assert.match(main,/PBT_APMSUSPEND/);
  assert.match(main,/PBT_APMRESUMEAUTOMATIC/);
  assert.match(main,/Screen\/monitor power events are NOT a system suspend/);
  assert.match(policy,/displayOff, displayOn, systemSuspend, systemResume/);
  assert.match(policy,/changeMixerIntent\(NativePowerEvent\)/);
  assert.match(engine,/mainOutputIntent_\.operatorRequested\(value\)/);
  assert.match(engine,/mainOutputIntent_\.deviceInterrupted\(\)/);
  const listener=main.split('void changeListenerCallback (juce::ChangeBroadcaster*) override')[1]
    .split('void timerCallback() override')[0];
  assert.doesNotMatch(listener,/deviceSettingsDirty_\s*=\s*true/);
});

test('reconnect is to the exact previously selected endpoint without silent fallback',()=>{
  assert.match(main,/rememberSavedWasapiProfile\(saved.get\(\)\)/);
  assert.match(main,/verifiedWasapiSetup_/);
  assert.match(main,/serviceWasapiDeviceContinuity\(\)/);
  assert.match(main,/finishAudioSetupAndRecoverMixer\(\)/);
  assert.match(main,/!device \|\| !device->isOpen\(\) \|\| directCallbackStalled_/);
  assert.match(main,/actual\.outputDeviceName!=wanted\.outputDeviceName/);
  assert.match(main,/actual\.inputDeviceName!=wanted\.inputDeviceName/);
  assert.match(coordinator,/wasapiRecovery/);
  assert.match(coordinator,/setup\.inputDeviceName\.isEmpty\(\)\?0:2/);
  assert.match(main,/if\(powerNoticeHwnd_\)/);
  assert.doesNotMatch(main,/new juce::AudioDeviceManager/);
});

test('master mute is only automatic for a positively identified feedback self loop',()=>{
  const protector=main.split('void protectKnownFeedbackLoop(const juce::String& fault)')[1]
    .split('void changeListenerCallback')[0];
  assert.match(protector,/VB-Cable capture\/playback self-loop/);
  assert.match(protector,/engine_\.setMasterMuted\(true\)/);
  assert.doesNotMatch(protector,/engine_\.setOutputMuted\(true\)/);
  const stopped=engine.split('void audioDeviceStopped() override')[1]
    .split('static void renderOriginalChannelOneGraph')[0];
  assert.doesNotMatch(stopped,/setMasterMuted/);
  assert.match(engine,/mainOutputIntent_\.deviceInterrupted\(\)/);
});


test('WASAPI recovery fails closed against substituted endpoint names or pin masks',()=>{
  const comparison=main.split('static bool matchesPinnedWasapiReadback(')[1]
      .split('void rememberVerifiedWasapiSetup()')[0];
  assert.match(comparison,/actual\.inputDeviceName==wanted\.inputDeviceName/);
  assert.match(comparison,/actual\.outputDeviceName==wanted\.outputDeviceName/);
  assert.match(comparison,/getActiveInputChannels\(\)==wanted\.inputChannels/);
  assert.match(comparison,/getActiveOutputChannels\(\)==wanted\.outputChannels/);
  const reconnect=main.split('void serviceWasapiDeviceContinuity()')[1]
      .split('void protectKnownFeedbackLoop(')[0];
  assert.match(reconnect,/matchesPinnedWasapiReadback\(\*verifiedWasapiSetup_,live,device\)/);
  assert.match(reconnect,/matchesPinnedWasapiReadback\(wanted,actual,opened\)/);
  const remember=main.split('void rememberVerifiedWasapiSetup()')[1]
      .split('void observeNativeCallbackHeartbeat()')[0];
  assert.match(remember,/pinned\.useDefaultOutputChannels=false/);
  assert.match(remember,/pinned\.useDefaultInputChannels=false/);
});

test('session capture and shutdown keep requested MAIN through a device STOP',()=>{
  const capture=main.split('juce::String saveMixerSession(bool resumeMain')[1]
      .split('const auto pendingId=doc.id;')[0];
  assert.match(capture,/if\(monitoring\)engine_\.setOutputMuted\(true\)/);
  assert.match(capture,/engine_\.setOutputMuted\(false\)/);
  assert.doesNotMatch(capture,/engine_\.setMonitorEnabled\(/);
  const shutdown=main.split('~ConsoleWindow() override')[1]
      .split('engine_.stop();')[0];
  assert.match(shutdown,/engine_\.wantsMainContinuation\(\) && !operatorOutputMuted_/);
  assert.match(shutdown,/saveMixerSession\(resumeMain,\{\},true\)/);
});

test('automatic checkpoints cannot overwrite saved MAIN and device binding during STOP',()=>{
  const save=main.split('juce::String saveMixerSession(bool resumeMain')[1]
      .split('void openMixerFile(')[0];
  // Recovery is the single SAVE admission owner; Main provides physical
  // readback evidence. STOP must never be an implicit checkpoint success.
  const admit=sessionRecovery.split('static SavePlan planSave(')[1]
      .split('enum class OpenAction')[0];
  assert.match(save,/audio!=nullptr && audio->isOpen\(\)/);
  assert.match(save,/engine_\.mainInterruptedByDevice\(\)/);
  assert.match(save,/Recovery::planSave\(/);
  assert.match(admit,/c\.automatic && !c\.shutdownCheckpoint/);
  assert.match(admit,/!c\.physicalOpen \|\| !c\.directAuthorized \|\| c\.mainInterrupted/);
  assert.match(admit,/SaveAction::reject/);
  assert.match(save,/doc\.resumeMain=resumeMain \|\|/);
  assert.match(save,/engine_\.wantsMainContinuation\(\) && !operatorOutputMuted_/);
  const persistence=main.split('if (deviceSettingsDirty_)')[1]
      .split('if (timerTicks_')[0];
  assert.match(persistence,/activeDevice != nullptr && activeDevice->isOpen\(\)/);
  assert.match(persistence,/deviceSettingsDirty_ = false/);
  assert.ok(persistence.indexOf('deviceSettingsDirty_ = false')>
            persistence.indexOf('activeDevice->isOpen()'),
            'Do not consume a pending device profile while its driver is absent');
});

test('bounded retry + callback watchdog cannot spin in realtime PCM',()=>{
  assert.match(policy,/30000ull/);
  assert.match(main,/callbackWatchTickMs_/);
  assert.match(main,/now-callbackProgressMs_>=3500/);
  assert.match(main,/now-callbackWatchTickMs_>1500/);
  assert.match(main,/AudioContinuityRetry wasapiRecovery_/);
  assert.match(cmake,/P0DisplayIdlePreservesAudioAndDeviceRecoveryIntent/);
});

test('approved machine profile remains pending on IO failure and flushes on normal exit',()=>{
  const persistence=main.split('void persistApprovedAudioSettings(bool shutdown)')[1]
    .split('void timerCallback() override')[0];
  assert.match(persistence,/if\(!shutdown&&now<deviceSettingsRetryAt_\)return/);
  assert.match(persistence,/deviceSettingsRetryAt_=now\+5000/);
  assert.ok(persistence.indexOf('return; // keep dirty')<persistence.indexOf('deviceSettingsDirty_ = false'));
  assert.ok(persistence.indexOf('lastAudioXml_=text')<persistence.indexOf('deviceSettingsDirty_ = false'));
  assert.match(main,/if\(!updaterProbeMode_\)persistApprovedAudioSettings\(true\)/);
  assert.match(main,/mixerSessionWorker_\.finishForShutdown\(true\);\s*engine_\.stop\(\)/);
});

test('cold MON resumes saved playback intent only after verified restored MAIN',()=>{
 const s=main.split('void serviceStartupSecondaryPreparation()')[1].split('// Same-session, same-graph recovery only')[0];
 assert.match(s,/mixerSessionRecoveryPending_\|\|startupFxGuard_/);
 assert.match(s,/!engine_\.secondaryResumeReady\(\)/);
 assert.match(s,/!engine_\.hasBoundMixerRoute\(\)/);
 assert.match(s,/current.selection.id!=wanted.id/);
 assert.match(s,/engine_\.connectSecondaryOutput\(wanted.name,wanted.id\)/);
 assert.match(s,/resume=wanted\.autoResume&&!operatorOutputMuted_/);
 assert.match(s,/if\(resume&&!engine_\.isMonitorEnabled\(\)\)return/);
 assert.match(s,/resume\?engine_\.enableSecondaryOutput\(wanted.name,wanted.id\)/);
 assert.doesNotMatch(s,/setMonitorEnabled/);
 assert.match(s,/generation!=startupSecondaryRetry_\.generation\(\)/);
 assert.match(s,/epoch!=engine_\.dryRoutingLabEpoch\(\)/);
 assert.match(s,/startupSecondaryRetry_\.failed\(now\)/);
 assert.match(main,/cancelSecondaryContinuity\(\);\s*cancelStartupSecondaryPreparation\(\)/);
 assert.match(main,/serviceSecondaryContinuity\(\);\s*serviceStartupSecondaryPreparation\(\)/);
});

test('saved ASIO cold-open retries are bounded, exact-registration and single-callback',()=>{
 const s=main.split('void serviceStartupAsioRetry()')[1].split('void rememberSavedWasapiProfile(')[0];
 assert.match(s,/engine_\.start\(&settings,true\)/);
 assert.match(s,/startupAsioRetry_\.attempts\(\)>=3/);
 assert.match(s,/generation!=startupAsioRetry_\.generation\(\)/);
 assert.match(s,/!engine_\.savedAsioStartupRetryAvailable\(\)/);
 assert.match(engine,/if\(!attached_\)\s*\{\s*deviceManager_\.addAudioCallback \(this\)/);
 assert.match(engine,/startupAsioRetryIdentity_->sameDriverClass\(\*savedAsioPreflight\)/);
 assert.match(main,/!mixerSessionStartupInspected_ && !pendingStartupAsioSettings_/);
});
test('last saved MAIN mute and previous-generation recovery remain muted after restore',()=>{
 const s=main.split('juce::String restoreSavedMixerSession(')[1].split('juce::String saveMixerSession(')[0];
 assert.match(s,/const bool holdMain=usedPrevious\|\|\(doc.resumeMain.has_value\(\)&&!doc.resumeMain.value\(\)\)/);
 assert.match(s,/if\(holdMain\)\s*\{\s*setOutputHoldReason\(usedPrevious\?"CHECKPOINT_RECOVERY":"SAVED_MUTED"\);\s*autoMainEpoch_=0;\s*engine_\.setMonitorEnabled\(false\)/);
 assert.match(main,/if\(mixerSessionRecoveryPending_\)return; \/\/ saved session has not been restored/);
});

test('smart idle checkpoint observes actual controls, debounces, and never captures armed audio',()=>{
 const h=read('src/SessionCheckpointCadence.h');
 const roster=read('engine/core/DynamicChannelRoster.h');
 assert.match(h,/if\(!disarmed\) \{wasDisarmed_=false;return false;\}/);
 assert.match(h,/quietMs=2000, retryMs=5000, refreshMs=60000/);
 assert.match(h,/stamp!=stamp_/);
 assert.match(h,/now-lastAttempt_<refreshMs/);
 assert.match(roster,/if\(row.faderLinear!=gain\|\|row.pan!=pan\)\+\+liveStateRevision_/);
 assert.match(engine,/dryRoutingLab_\.sessionLiveControlRevision\(\)/);
 assert.match(main,/sessionCheckpointCadence_\.observe\(checkpointNow,engine_\.allOutputsDisarmed\(\)/);
 assert.match(main,/sessionCheckpointCadence_\.failed\(monotonicMilliseconds\(\)\)/);
 assert.doesNotMatch(main,/timerTicks_%720==0/);
});
test('recovered checkpoint is durably muted before atomic HEAD publication',()=>{
 const s=read('src/NativeMixerSessionStore.h');
 const recover=s.split('juce::String recoverPrevious(MixerSessionDocument& out')[1].split('private:')[0];
 assert.ok(recover.indexOf('writePointer(marker,pointer)')<recover.indexOf('writePointer(head,pointer)'));
 assert.match(recover,/verified.resumeMain=false/);
 assert.match(s,/if\(recovered.isEmpty\(\)\|\|recovered==pointer\)doc.resumeMain=false/);
});

test('late WASAPI cold startup binds the device before one guarded FX restore',()=>{
 const s=main.split('void serviceWasapiDeviceContinuity()')[1].split('void protectKnownFeedbackLoop(')[0];
 assert.match(s,/else if\(pendingStartupWasapi_\)\s*\{[\s\S]*?if\(!engine_\.finishManualAudioSetup\(\)\)/);
 assert.match(s,/pendingStartupWasapi_=false;\s*startupError_\.clear\(\)/);
 assert.match(main,/!pendingStartupAsioSettings_ && !pendingStartupWasapi_ && !catalog_\.busy\(\)/);
 assert.match(main,/pendingStartupWasapi_=saved&&startupError_\.isNotEmpty\(\)/);
});
test('MON persistence retries newest accepted intent without reusing stale preference',()=>{
 const p=main.split('bool persistSecondaryPreference(')[1].split('bool forgetSecondaryStartupSelection(')[0];
 assert.match(p,/if\(!immediate&&now<secondaryPreferenceRetryAt_\)return false/);
 assert.match(p,/secondaryPreferenceRetryAt_=now\+5000/);
 assert.ok(p.indexOf('secondaryPreference_=std::move(next)')<p.indexOf('secondaryPreferenceDirty_=true'));
 assert.match(p,/return persistSecondaryPreference\(true\)/);
 assert.match(main,/if\(!updaterProbeMode_\)\(void\)persistSecondaryPreference\(true\)/);
 assert.match(main,/serviceSecondaryInventory\(\);\s*\(void\)persistSecondaryPreference\(\)/);
});

test('live checkpoint uses accepted metadata and immutable FX identity without muting or vendor capture',()=>{
 const lab=read('src/LiveDryRoutingLab.h');
 const capture=lab.split('juce::String captureLiveMixerControls(')[1].split('juce::String captureNativeMixerSession(')[0];
 assert.match(capture,/nativeAccepted_\.matches\(nativePublisher_,ep,rev\)/);
 assert.match(capture,/\(\*owner\)->instanceId/);
 assert.doesNotMatch(capture,/tryCaptureState|RealtimeRenderPermit|Thread::sleep|getStateInformation/);
 const host=main.split('juce::String saveLiveMixerControls()')[1].split('juce::String saveMixerSession(')[0];
 assert.doesNotMatch(host,/setOutputMuted|setMonitorEnabled|captureNativeMixerSession/);
 assert.match(host,/engine_\.captureLiveMixerControls\(doc\)/);
 assert.match(host,/lastMixerSessionFingerprint_\.clear\(\)/);
 assert.match(main,/liveCheckpointCadence_\.observeLive\(checkpointNow,!engine_\.allOutputsDisarmed\(\)/);
 const worker=read('src/NativeMixerSessionSaveWorker.h');
 assert.match(worker,/store\.commitLiveControls\(std::move\(doc_\)\)/);
 const store=read('src/NativeMixerSessionStore.h');
 assert.match(store,/if\(bound!=generation\)return OverlayResult::absent/);
 assert.match(store,/if\(base.logicalOnly\|\|draft.logicalOnly\|\|base.id!=draft.id\)return false/);
 assert.match(store,/old.catalogId!=fx.catalogId\|\|old.path!=fx.path\|\|old.uid!=fx.uid/);
 assert.match(store,/staging.overwriteTargetFileWithTemporary\(\)/);
});

test('ASIO vendor panel preserves operator desire and only resumes the same verified driver',()=>{
 const p=main.split('void showAsioControlPanel(')[1].split('void chooseStudioBackup()')[0];
 assert.match(p,/const bool resume=engine_\.wantsMainContinuation\(\)&&!operatorOutputMuted_/);
 assert.match(p,/engine_\.beginManualAudioSetup\(true\)/);
 assert.match(p,/returned->getName\(\)==driverName/);
 assert.match(p,/sameDriver && finishAudioSetupAndRecoverMixer\(\)/);
 assert.match(p,/resume&&!operatorOutputMuted_&&!panelError\)requestAutomaticMain\(false\)/);
 assert.match(p,/ASIO_PANEL_BEGIN/);
 assert.doesNotMatch(p,/setOutputHoldReason|operatorOutputMuted_\s*=|setMonitorEnabled\(true\)/);
 const restore=main.split('juce::String restoreSavedMixerSession(')[1].split('juce::String saveMixerSession(')[0];
 assert.match(restore,/if\(operatorOutputMuted_\)retainedDoc\.resumeMain=false/);
 assert.match(restore,/else if\(engine_\.wantsMainContinuation\(\)\)retainedDoc\.resumeMain=true/);
 assert.match(restore,/else retainedDoc\.resumeMain\.reset\(\)/);
 assert.match(restore,/const bool holdMain=usedPrevious/);
});
test('nested device transaction permits explicit STOP but rejects ARM with native ACK',()=>{
 const a=main.split('void onAction (const juce::var& args)')[1];
 assert.match(a,/const bool outputIntent=action=="monitor"&&args\.getProperty\("value",\{\}\)\.isBool\(\)/);
 assert.match(a,/action!="audioDeviceApply" && !outputIntent/);
 const monitor=a.split('else if (action == "monitor" && value.isBool())')[1].split('else if (action == "dryTest"')[0];
 assert.match(monitor,/if \(arm && deviceApply_\.busy\(\)\)/);
 assert.match(monitor,/if \(fault.isEmpty\(\)\) \{\s*setOutputHoldReason\(arm\?nullptr:"OPERATOR_STOP"\)/);
 assert.match(monitor,/arMixAudioArmAck/);
});

test('smart bus remap uses native temporary stop and verified continuation, not an operator latch on success',()=>{
 const p=main.split('void handlePhysicalBusOutput(')[1].split('void serviceSecondaryContinuity()')[0];
 assert.match(p,/const bool resumeMain=engine_\.isMonitorEnabled\(\)&&!operatorOutputMuted_/);
 assert.match(p,/engine_\.captureSecondaryResumeIntent\(\)/);
 assert.ok(p.indexOf('engine_.setMonitorEnabled(false)')<p.indexOf('engine_.patchPhysicalBusOutput('));
 assert.ok(p.indexOf('engine_.disarmSecondaryOutput()')<p.indexOf('engine_.patchPhysicalBusOutput('));
 assert.match(p,/if\(resumeMain&&!operatorOutputMuted_\) \{[\s\S]*?engine_\.retainMainIntentAfterBusPatch\(\);\s*requestAutomaticMain\(false\)/);
 assert.match(engine,/void retainMainIntentAfterBusPatch\(\) noexcept \{mainOutputIntent_\.operatorRequested\(true\);\}/);
 assert.match(p,/if\(stream\)pendingVendorStream_=PendingVendorStream/);
 assert.match(p,/setOutputHoldReason\("BUS_REMAP_FAILED"\);\s*diagnostics_\.record\("OUTPUT_MUTE","ON \| bus remap rejected"\)/);
 const panic=main.split('if(mapping.operation=="PANIC_MUTE")')[1].split('// Existing control-thread guards')[0];
 assert.match(panic,/pendingVendorStream_\.reset\(\)/);
 assert.match(panic,/cancelStartupSecondaryPreparation\(\)/);
});

test('physical bus remap waits for AUDIO fade receipt with bounded CONTROL lease',()=>{
 const p=main.split('void servicePendingPhysicalBusPatch()')[1].split('void serviceSecondaryContinuity()')[0];
 assert.match(p,/monotonicMilliseconds\(\)\)>=p.deadline/);
 assert.match(p,/if\(!engine_\.physicalBusPatchFadeReady\(\)\)return/);
 assert.match(p,/handlePhysicalBusOutput\(args,true\)/);
 assert.match(p,/engine_\.beginPhysicalBusPatchFade\(\)/);
 assert.match(p,/Purpose::busOutputPatch/);
 assert.match(engine,/busPatchEnvelope_\.process\(outputs,outputChannels/);
 assert.ok(engine.indexOf('busPatchEnvelope_.process(outputs,outputChannels')<engine.indexOf('secondaryOutput_.publish(secondaryTicket,nativeMain[0],nativeMain[1],samples)'));
 assert.ok(engine.indexOf('secondaryOutput_.publish(secondaryTicket,nativeMain[0],nativeMain[1],samples)')<engine.indexOf('busPatchEnvelope_.acknowledge(patchFade)'));
 // Service both transitions before the busy-lease return.
 assert.match(main,/servicePendingPhysicalBusPatch\(\);\s*serviceRackTransition\(\);\s*if\(deviceApply_\.busy\(\)\)return/);
});

test('integrated FIELD serializes physical bus remap with FX rack file operations',()=>{
 const rack=main.split('bool rackFileOperationBusy() const')[1].split('void chooseNamedIoProfile(')[0];
 assert.match(rack,/deviceApply_\.busy\(\)/);
 assert.match(rack,/if\(!current\(\)\)return finish\("FAILED"/);
 assert.match(rack,/safe->engine_\.rackFileTargetAvailable\(ep,rev,roster,channel\)/);
 const bus=main.split('void handlePhysicalBusOutput(')[1].split('void serviceSecondaryContinuity()')[0];
 assert.match(bus,/Purpose::busOutputPatch/);
 assert.match(bus,/!deviceApply_\.busy\(\)/);
 const actions=main.split('void onAction (const juce::var& args)')[1];
 assert.match(actions,/if\(action=="fxRackFileReadback"\)return readFxRackFileResult\(args\)/);
 assert.match(actions,/cancelPendingPhysicalBusPatch\("BUS OUTPUT: superseded by operator action"\)/);
});

test('FX file transition preserves the operator gates and the callback verifies committed PCM',()=>{
 const t=main.split('void finishRackTransition(')[1].split('void chooseFxRackFile(')[0];
 assert.match(t,/nativeTopologyMatches\(job.epoch,job.revision\)/);
 assert.match(t,/monotonicMilliseconds\(\)\)>=job.deadline/);
 assert.ok(t.indexOf('if(!engine_.physicalBusPatchFadeReady())return;')<t.indexOf('error=job.operation();'));
 assert.match(t,/rackOutputRevisionObserved\(job.epoch,job.revision\)/);
 assert.match(t,/finishPhysicalBusPatchFade\(!engine_.allOutputsDisarmed\(\)\)/);
 const capture=engine.split('juce::String captureRackFile(')[1].split('struct PreparedRackFile')[0];
 assert.match(capture,/rackSupportsLiveSnapshot/);
 assert.match(capture,/\(!live&&!rackFileAllowed/);
 const choose=main.split('void chooseFxRackFile(')[1].split('void chooseNamedIoProfile(')[0];
 assert.doesNotMatch(choose,/setOutputHoldReason|operatorOutputMuted_\s*=|setMonitorEnabled\(/);
 assert.match(choose,/rackArchive\(\[file,rack\]\{return Archive::save/);
});


test('session capture cannot reopen MAIN after a newer operator STOP or device/graph change',()=>{
 const s=main.split('juce::String saveMixerSession(bool resumeMain')[1].split('const auto pendingId=doc.id;')[0];
 assert.match(s,/Purpose::mixerSessionCapture/);
 assert.ok(s.indexOf('Purpose::mixerSessionCapture')<s.indexOf('engine_.captureNativeMixerSession(doc,states)'));
 assert.match(s,/const auto revision=engine_\.outputArmRevision\(\)/);
 assert.match(s,/mayResumeCapturedMain\(monitoring,epoch,revision,operatorOutputMuted_\)/);
 assert.match(s,/if\(mayResume\(\) && engine_\.liveOutputArmFault\(false\)\.isEmpty\(\) && mayResume\(\)\)/);
 assert.match(policy,/return wasArmed && sameRoute && !operatorMuted && desired\(\) && !interrupted\(\)/);
 const capture=engine.split('juce::String captureNativeMixerSession(')[1].split('struct MixerBindingInspection')[0];
 assert.match(capture,/captureEpoch!=dryRoutingLabEpoch\(\)/);
 assert.match(capture,/current=deviceManager_\.getCurrentAudioDevice\(\)/);
 assert.match(capture,/current!=device \|\| !current->isOpen\(\)/);
 assert.match(capture,/document\.binding=readMixerBinding\(\*current\)/);
 assert.doesNotMatch(capture,/readMixerBinding\(\*device\)/);
});


test('authorized MAIN rollback and saved explicit resume do not depend on capture pins',()=>{
 const rollback=main.split('case Apply::Outcome::recoveryRequired: {')[1].split('// A Windows native Explorer picker')[0];
 assert.match(rollback,/if\(restored&&previousMonitor\)requestAutomaticMain\(false\)/);
 assert.match(rollback,/if\(restored\)resumeStream\(true\)/);
 const start=main.indexOf('const bool holdMain=usedPrevious');
 const end=main.indexOf('mixerSessionStatus_=engine_.isMonitorEnabled()',start);
 const restore=main.slice(start,end);
 assert.match(restore,/const bool savedMainRequested=doc\.resumeMain\.value_or\(false\)/);
 assert.ok(restore.indexOf('savedMainRequested=')<restore.indexOf('std::move(doc)'));
 assert.match(restore,/if\(plan.resumeMain\)\s*requestAutomaticMain\(!savedMainRequested && plan.bindingMode!=MixerBindingMode::verifiedAsioRestart\)/);
 assert.match(restore,/if\(holdMain\)\s*\{[\s\S]*?engine_\.setMonitorEnabled\(false\)/);
 const auto=main.split('void serviceAutomaticMain()')[1].split('void requestAutomaticMain')[0];
 assert.match(auto,/if\(operatorOutputMuted_\)\{autoMainEpoch_=0;return;\}/);
 assert.match(auto,/autoMainEpoch_!=engine_\.dryRoutingLabEpoch\(\)/);
 assert.match(auto,/engine_\.liveOutputArmFault\(requireInput\)/);
});

test('silent track and standalone AUX creation keep both output gates and existing secondary recovery lease',()=>{
 const wrapper=main.split('LiveDryRoutingLab::NativeBusResult withNativeTopologyContinuity(')[1].split('struct PendingPhysicalBusPatch')[0];
 const silent=wrapper.split('if(silentAddition){')[1].split('return result;')[0];
 assert.match(silent,/result=edit\(\)/);
 assert.match(silent,/pendingVendorStream_->revision=after/);
 assert.match(silent,/secondaryContinuity_->revision=after/);
 assert.doesNotMatch(silent,/disarmSecondaryOutput|setOutputMuted|setMonitorEnabled|requestAutomaticMain|cancelSecondaryContinuity/);
 const track=engine.split('LiveDryRoutingLab::NativeBusResult createNativeInputChannel(')[1].split('LiveDryRoutingLab::NativeBusResult removeNativeInputChannel(')[0];
 assert.match(track,/if\(silentAddition&&!allOutputsDisarmed\(\)\)/);
 assert.match(track,/dormantAdditionReady\(epoch,pcmRevision\)/);
 assert.match(track,/name,format,false,\{\},0,false,true/);
 const core=read('engine/core/DynamicPreparedMixerBlock.h');
 assert.match(core,/renderOrder_\.size\(\)!=old\.renderOrder_\.size\(\)/);
 assert.match(core,/renderOrder_\[k\]=controlIndex_\.at\(id\.value\)/);
 assert.match(core,/channelScheduled\(n\.id\)\|\|n\.inputL>=0\|\|n\.playerDeckId/);
});

test('secondary PCM uses a render-start generation and reset waits for SC reader reservations',()=>{
 const device=read('engine/device/LiveSecondaryOutputDevice.h');
 const start=engine.indexOf('const auto secondaryTicket=secondaryOutput_.captureProducerTicket()');
 const render=engine.indexOf('const bool routed=dryRoutingLab_.process(',start);
 const publish=engine.indexOf('secondaryOutput_.publish(secondaryTicket,nativeMain[0],nativeMain[1],samples)',render);
 assert.ok(start>=0&&render>start&&publish>render);
 assert.match(device,/const auto ticket=admission_\.beginRearm\(\)/);
 assert.match(device,/while\(publishers_\.load\(\)!=0 \|\| consumers_\.load\(\)!=0\)/);
 assert.match(device,/publishers_\.fetch_add\(1\);\s*if\(ticketValid\(ticket\)\)/);
 assert.match(device,/consumers_\.fetch_add\(1\);\s*if\(owner\.ticketValid\(ticket\)\)/);
 assert.match(device,/owner\.bridge_\.pull\(output,2,frames\);\s*[\s\S]*?if\(!owner\.ticketValid\(ticket\)\)/);
 assert.match(device,/if\(owner\.expectedRunning_\.load/);
 assert.match(policy,/authorized_\.load\(\)==ticket&&generation_\.load\(\)==ticket/);
});


test('silent removal is native-verified before avoiding secondary disarm; active deletion retains ordinary path',()=>{
 const lab=read('src/LiveDryRoutingLab.h');
 const core=read('engine/core/DynamicPreparedMixerBlock.h');
 const publisher=read('engine/core/DynamicNativeSessionPublisher.h');
 assert.match(core,/old\.channelScheduled\(removed\)\|\|retired\.inputL>=0/);
 assert.match(publisher,/!dormantRosterEdit&&retiring\.value&&previous/);
 assert.match(lab,/nativePublisher_\.externalOutputDormant\(ep,rev,id\)/);
 assert.match(engine,/if\(silentRemoval&&!allOutputsDisarmed\(\)\)/);
 assert.match(main,/engine_\.removeNativeInputChannel\(ep,rev,roster,target,true\)/);
 assert.match(main,/target,true,false,true\)/);
 assert.match(main,/SILENT_REMOVE_CONTINUITY_NOT_VERIFIED/);
});

test('authorized secondary callback without a complete native tap emits clocked silence, not stale PCM',()=>{
 const device=read('engine/device/LiveSecondaryOutputDevice.h');
 const lab=read('src/LiveDryRoutingLab.h');
 const bridge=read('engine/device/AdaptiveClockBridge.h');
 assert.match(engine,/if\(outputGate\.secondary\)\s*\{[\s\S]*?if\(routed&&nativeMain\[0\]&&nativeMain\[1\]\)[\s\S]*?secondaryOutput_\.publish\(secondaryTicket,nullptr,nullptr,samples\)/);
 assert.match(device,/if\(ticketValid\(ticket\)\)\s*\{\s*if\(left&&right\)/);
 assert.match(device,/bridge_\.pushSilence\(frames\);\s*substitutedFrames_\.fetch_add/);
 assert.match(device,/bridge_\.resetStopped\(\);[^\n]*\n\s*substitutedFrames_\.store\(0/);
 assert.match(bridge,/int pushSilence\(int frames\)[\s\S]*?return push\(zero,1,frames\)/);
 assert.match(lab,/const auto silence=\[&\]\(\) noexcept \{[\s\S]*?if\(actualOutputPair\)\*actualOutputPair=\{\{-1,-1\}\};[\s\S]*?if\(secondaryMainTap\)\*secondaryMainTap=\{\{nullptr,nullptr\}\};/);
 assert.match(engine,/"secondaryOutputSubstitutedFrames"/);
 const stop=device.split('void stop() {')[1].split('bool arm(bool requested)')[0];
 assert.match(stop,/admission_\.revoke\(\);[\s\S]*?while\(publishers_\.load\(\)!=0\)[\s\S]*?substitutedFrames_\.store\(0/);
 assert.match(main,/"secondaryOutputPriming", "secondaryOutputSubstitutedFrames"/);
});

test('same-format physical source retarget preserves output generation and requires callback receipt',()=>{
 const core=read('engine/core/DynamicPreparedMixerBlock.h');
 const lab=read('src/LiveDryRoutingLab.h');
 const vm=read('web/approved-channel-strip-view-model.js');
 assert.match(core,/const auto target=\(sequence<<32\)\|packPhysical/);
 assert.match(core,/sourceAppliedTargets_\[i\]\.store\(target,std::memory_order_release\)/);
 assert.match(lab,/nativePublisher_\.setPhysicalSource\(ep,rev,id,[\s\S]*?&reply.sourceTicket/);
 assert.match(lab,/rememberLiveSourceChange\(\)/);
 assert.match(main,/if\(liveSwitch\)return engine_\.switchNativePhysicalSource\([\s\S]*?\},liveSwitch\);/);
 assert.match(main,/"sourceSwitchTicket"/);
 assert.match(vm,/channel.source\?\.appliedSwitchTicket===pending.sourceSwitchTicket/);
});


test('custom secondary programme preserves generation, whole callback, and independent MAIN',()=>{
 const start=engine.indexOf('const auto secondaryTicket=secondaryOutput_.captureProducerTicket()');
 const render=engine.indexOf('const bool routed=dryRoutingLab_.process(',start);
 const envelope=engine.indexOf('busPatchEnvelope_.process(outputs,outputChannels',render);
 const publish=engine.indexOf('secondaryOutput_.publish(secondaryTicket,programmePcm[0],programmePcm[1],samples)',envelope);
 const acknowledge=engine.indexOf('busPatchEnvelope_.acknowledge(patchFade)',publish);
 assert.ok(start>=0&&render>start&&envelope>render&&publish>envelope&&acknowledge>publish);
 const selection=engine.split('juce::String selectSecondaryProgramme(')[1].split('juce::String armSecondaryOutput(')[0];
 assert.ok(selection.indexOf('secondaryOutput_.disarm()')<selection.indexOf('secondaryProgrammeSource_.store'));
 assert.match(selection,/if\(armed\)return armSecondaryOutput\(true\)/);
 const callback=engine.split('if(outputGate.secondary) {').at(-1).split('secondaryProgrammeAppliedTicket_.store')[0];
 assert.match(callback,/if\(programmeId\)[\s\S]*?if\(valid\)[\s\S]*?publish\(secondaryTicket,programmePcm\[0\],programmePcm\[1\],samples\)[\s\S]*?else[\s\S]*?publish\(secondaryTicket,nullptr,nullptr,samples\)/);
 assert.match(callback,/else if\(routed&&nativeMain\[0\]&&nativeMain\[1\]\)/);
 assert.match(main,/if\(engine_\.hasSecondaryProgrammeSource\(\)\)autoResume=false/);
});
