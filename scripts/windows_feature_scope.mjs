// SPDX-License-Identifier: AGPL-3.0-only
// Audited developer-scope only. The full Windows FAT/installer is unmodified.
const groups=[
 {id:'audio-io',
  owned:[/^engine\/device\//,/^tests\/(?:asio_|device_|io[01]_|bridge_continuity|direct_restore|endpoint_|independent_stream|stream_watchdog|clock_bridge|adaptive_clock)[^/]*\.cpp$/],
  targets:['ARMixAsioRestartGateTests','ARMixDeviceApplyCoordinatorTests','ARMixRecoveryTests','ARMixIo1CaptureClockTests','ARMixBridgeContinuityTests'],
  tests:['Asio4AllWdmSettleBoundedDriverTransactionsAndDeferredRebind','NativeDeviceApplyPinValidationAndRollbackEvidence','BoundedDirectDeviceRecovery','IO1SecondaryCaptureDeviceLeaseEpochAndNineClocks','SustainedAsrcContinuityFailback']},
 {id:'mixer-routing',
  owned:[/^tests\/(?:channel_routing_contract|channel_send_plan|prepared_mixer_block|mixer_routing_plan|physical_external_output_pcm|native_mixer_session)[^/]*\.cpp$/],
  targets:['ARMixChannelRoutingContractTests','ARMixChannelSendTests','ARMixPreparedMixerBlockTests','ARMixNativeMixerSessionTests'],
  tests:['MonoStereoSparsePhysicalPortRouting','TenChannelAcyclicPrePostSends','TenChannelEndToEndPreparedAudioRender','P1FPhysicalPortToNativeMasterAndEpochGuard']},
 {id:'graphic-eq',
  owned:[/^engine\/builtin_fx\/(?:ChannelEq|GraphicEq31)[^/]*\.h$/,/^tests\/(?:graphic_eq31|builtin_channel_eq)[^/]*\.cpp$/],
  targets:['ARMixNativeChannelEqTests','ARMixGraphicEq31CoreTests','ARMixGraphicEq31WebViewTests'],
  tests:['NativePEQEightBandsRTAndFrequencyResponse','GraphicEq31FrequencyResponseAndRealtimeStability','GraphicEq31OriginalSvgLayersAndDynamicThumb','GraphicEq31WebViewAudioAckAndAtomic31Bands']},
 {id:'compressor',
  owned:[/^engine\/builtin_fx\/(?:ChannelCompressor|CompressorMeterMath)[^/]*\.h$/,/^tests\/builtin_channel_compressor[^/]*\.cpp$/],
  targets:['ARMixNativeCompressorTests','ARMixNativeCompressorLifecycleTests'],
  tests:['NativeCompressorZeroLatencyAndLinkedStereoPCM','NativeCompressorDeviceRestartAckAndShowPCM']},
 {id:'player',
  owned:[/^engine\/player\//,/^tests\/player_[^/]*\.cpp$/],
  targets:['ARMixPlayerVirtualSourceTests','ARMixPlayerPcmRingTests','ARMixPlayerDecodeTests','ARMixPlayerPitchTempoTests'],
  tests:['ArSonPlayerVirtualPcmFirstSoundAndSourceOnly','ArSonPlayerWorkerToRealtimeStereoRing','ArSonPlayerRealWavDecodeResampleAndTransport','ArSonPlayerM2IndependentKeyTempoStereoPCM']},
 {id:'vst-host',
  owned:[/^tests\/(?:vst3_|vst_host_|vst_startup_|vault_)[^/]*\.cpp$/],
  targets:['ARMixVst3StartupSafetyTests','ARMixVaultTests','ARMixLiveChannelFxTests'],
  tests:['WavesVst3IdentityAndStartupCrashRecovery','SelectiveVst3BundleVaultImport','P1HRealNativePerChannelFxAndSafeRetirement']},
 {id:'vocal-fx',
  owned:[/^engine\/builtin_fx\/(?:Harmony[^/]*|ChannelHarmonyProcessor)\.h$/,/^tests\/harmony_[^/]*\.cpp$/,/^tools\/harmony_[^/]*\.cpp$/],
  targets:['ARMixHarmonyDspTests','ARMixHarmonyPitchSyncTests','ARMixHarmonySpatialTests','ARMixHarmonyCpuProfileTests'],
  tests:['NativeHarmonyMultivoicePitchAndRealtimeFinitePCM','NativeHarmonyPitchSynchronousFormantCandidateAndBlockParity','NativeHarmonyBackingStageSpatialDepthAndMonoPDC','NativeHarmonySyntheticCallbackCpuProfile']},
 {id:'spatial',
  owned:[/^engine\/builtin_fx\/SonKuSpatialProcessor\.h$/,/^engine\/builtin_fx\/sonku\//,/^tests\/sonku_[^/]*\.cpp$/],
  targets:['ARMixSonKuCoreTests','ARMixSonKuNativeStateTests'],
  tests:['NativeSonKuMeasuredBinauralDirectionalAndChunkParity','NativeSonKuSessionV1V2AndAudioAppliedAck']},
 {id:'session',
  owned:[/^tests\/(?:durable_mixer_session|show_schema|show_store|native_v8_projection)[^/]*\.cpp$/],
  targets:['ARMixDurableMixerSessionTests','ARMixShowSchemaTests','ARMixShowStoreTests','ARMixNativeV8ProjectionTests'],
  tests:['P0DynamicMixerSessionDocumentAndCrashSafeRecovery','P4NativeShowSnapshotV2WireRoundTrip','P4ImmutableNativeShowsAndVendorStateRoundTrip','M3BNativeV8StableIdReadbackAndFailClosedProjection']}
];
const documentation=f=>/^docs\/[^/]+(?:\/[^/]+)*\.md$/.test(f);
for(const g of groups){
 if(!/^[a-z][a-z0-9-]*$/.test(g.id)||!g.owned.length||!g.targets.length||
    !g.tests.length||new Set(g.targets).size!==g.targets.length||
    new Set(g.tests).size!==g.tests.length)throw Error('FEATURE_REGISTRY_INVALID');
}
export function featureById(id){return groups.find(g=>g.id===id)||null;}
export function availableFeatureIds(){return groups.map(g=>g.id);}
export function selectWindowsFeatureScope(files){
 if(!Array.isArray(files)||!files.length||
    files.some(f=>typeof f!=='string'||f.includes('..')||f.startsWith('/')))return null;
 const families=new Set();let substantive=0;
 for(const file of files){
   if(documentation(file))continue;
   const matches=groups.filter(g=>g.owned.some(re=>re.test(file)));
   if(matches.length!==1)return null;
   ++substantive;families.add(matches[0].id);
   if(families.size!==1)return null; // shared/mixed needs full
 }
 return substantive?featureById([...families][0]):null;
}
export function validateWindowsFeaturePlan(plan,cmakeSource,ctestNames){
 if(!plan||typeof cmakeSource!=='string'||!Array.isArray(ctestNames))
   throw Error('FEATURE_VALIDATION_INVALID');
 for(const target of plan.targets){
   if(!/^[A-Za-z][A-Za-z0-9_]*$/.test(target))throw Error('FEATURE_TARGET_BAD_NAME');
   if(!new RegExp('^\\s*(?:add_executable|juce_add_console_app|juce_add_gui_app)\\(\\s*'+target+'\\b','m').test(cmakeSource))
      throw Error('FEATURE_UNDECLARED_CMAKE_TARGET_'+target);
 }
 for(const name of plan.tests)
   if(!ctestNames.includes(name))throw Error('FEATURE_MISSING_CTEST_'+name);
 return true;
}
if(process.argv[1]?.endsWith('windows_feature_scope.mjs')){
 try{
   if(process.argv.length!==3)throw Error('FEATURE_USAGE_SCOPE_REQUIRED');
   const scope=featureById(process.argv[2]);
   if(!scope)throw Error('FEATURE_UNKNOWN_SCOPE');
   console.log(JSON.stringify({id:scope.id,targets:scope.targets,tests:scope.tests}));
 }catch(e){console.error(e.message);process.exitCode=1;}
}
