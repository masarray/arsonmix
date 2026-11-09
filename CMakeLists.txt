cmake_minimum_required(VERSION 3.22)
project(ARMixConsole VERSION 0.5.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT DEFINED JUCE_ROOT)
  message(FATAL_ERROR "Pass -DJUCE_ROOT=C:/path/to/JUCE (8.0.15+ required)")
endif()
add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
# The independent exact-byte verifier must use the SAME Python interpreter
# selected by CMake to generate compressed favicon/PNG resources (Windows
# runners can have several Python versions with different zlib builds).
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/approved-gui-python.txt" "${Python3_EXECUTABLE}\n")
set(ARMIX_BRAND_DIR "${CMAKE_CURRENT_BINARY_DIR}/brand_icons")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_brand_icons.py"
            --source "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/brand-source-128.png"
            --out "${ARMIX_BRAND_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE ARMIX_BRAND_STATUS)
if (NOT ARMIX_BRAND_STATUS EQUAL 0)
    message(FATAL_ERROR "ArSonMix branding resource generation failed")
endif()

juce_add_gui_app(ARMixConsole
    PRODUCT_NAME "AR MIX Studio"
    ICON_BIG "${ARMIX_BRAND_DIR}/arsonmix-256.png"
    ICON_SMALL "${ARMIX_BRAND_DIR}/arsonmix-32.png"
    COMPANY_NAME "AR MIX Open Source"
    BUNDLE_ID "org.armix.studio"
    # macOS TCC: CoreAudio input must display a valid user-facing privacy prompt.
    # This is a FREE Info.plist attribute, not a paid Apple signing entitlement.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Allow ArSonMix to receive audio from microphone and connected audio interfaces."
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 ${WIN32}
    NEEDS_CURL FALSE)

# The approved operator GUI is generated from the EXACT Pages sources at
# configure time. Never compile legacy web/index.html or console-native.html.
# User-approved /preview/ iframe uses preview-v2 DOM + PNG-button variant.
set(ARMIX_APPROVED_GUI_DIR "${CMAKE_CURRENT_BINARY_DIR}/approved_gui")
file(GLOB_RECURSE ARMIX_APPROVED_GUI_SOURCE_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/site/preview-v2/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/site/preview-buttons/assets/*")
list(APPEND ARMIX_APPROVED_GUI_SOURCE_INPUTS
    "${CMAKE_CURRENT_SOURCE_DIR}/web/approved-native-bridge.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/approved-native-context-menu.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_approved_native_gui.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_pages.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compose_native_peq.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARMIX_APPROVED_GUI_SOURCE_INPUTS})
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_approved_native_gui.py"
            --out "${ARMIX_APPROVED_GUI_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE ARMIX_GUI_BUNDLE_STATUS
    OUTPUT_VARIABLE ARMIX_GUI_BUNDLE_LOG
    ERROR_VARIABLE ARMIX_GUI_BUNDLE_ERROR)
if (NOT ARMIX_GUI_BUNDLE_STATUS EQUAL 0)
    message(FATAL_ERROR "Approved Pages GUI native bundle failed: ${ARMIX_GUI_BUNDLE_ERROR}")
endif()
message(STATUS "${ARMIX_GUI_BUNDLE_LOG}")
file(GLOB_RECURSE ARMIX_APPROVED_GUI_BINARIES CONFIGURE_DEPENDS
    "${ARMIX_APPROVED_GUI_DIR}/assets/*")
list(SORT ARMIX_APPROVED_GUI_BINARIES)
juce_add_binary_data(ARMixAssets SOURCES
    "${ARMIX_APPROVED_GUI_DIR}/index.html" ${ARMIX_APPROVED_GUI_BINARIES}
    "${ARMIX_BRAND_DIR}/arsonmix-256.png")
target_include_directories(ARMixConsole PRIVATE "${ARMIX_APPROVED_GUI_DIR}")

target_sources(ARMixConsole PRIVATE src/Main.cpp src/LiveAudioEngine.h engine/builtin_fx/HarmonyDsp.h engine/builtin_fx/HarmonyMusicKey.h engine/builtin_fx/ChannelHarmonyProcessor.h src/VstOrbitController.h engine/core/MixerRoutingPlan.h engine/core/ChannelRoutingContract.h engine/core/ChannelSendPlan.h engine/core/AuxBusPlan.h engine/core/PreparedMixerBlock.h engine/core/MasterOutputMonoPolicy.h engine/core/LiveChannelFxGraph.h engine/core/PreparedNativeMixerSession.h engine/core/NativeDryRouteCommand.h engine/core/RealtimeRoutePublisher.h engine/device/LiveInputPortRegistry.h src/LiveDryRoutingLab.h engine/core/OrbitPhase.h engine/core/OrbitControlPolicy.h engine/core/OrbitMotion.h engine/core/OrbitDistanceMapping.h engine/core/DearVrPolarPolicy.h src/PluginCatalog.h src/FactoryFxRegistry.h src/PluginVault.h src/PortableWorkspace.h src/StudioBackup.h src/DiagnosticsRecorder.h src/StereoPan.h src/NativePresetArchive.h src/NativeShowSchema.h src/NativeDryShowRecall.h src/VstBusLayout.h src/LiveGraphHandoff.h src/DiagnosticTone.h engine/core/RealtimeHealthHistogram.h engine/core/CallbackCadenceEvidence.h engine/diagnostics/CrackleEvidenceWindow.h engine/device/AdaptiveClockBridge.h engine/device/AudioProfilePolicy.h engine/device/PeriodCapabilityEvidence.h engine/device/SessionLifecycle.h engine/device/WasapiCapabilityProbe.h engine/device/JuceIndependentClockDevice.h engine/device/LiveSecondaryOutputDevice.h engine/core/LiveOutputRouteGate.h engine/device/ClockBridgeSizing.h engine/device/IndependentStreamFormatFence.h engine/device/DirectRestoreBackoff.h engine/device/IndependentStreamWatchdog.h engine/device/BridgeContinuityGate.h engine/device/EndpointIdentityPolicy.h engine/device/WasapiEndpointIdentityGuard.h engine/device/AsioStartupPolicy.h engine/device/AsioPriorityDeviceManager.h engine/device/JuceActivePortSnapshot.h engine/device/AsioRegistrationPolicy.h engine/device/AsioRegistryIdentityProbe.h engine/device/DirectRouteFence.h)
# Explicit UTF-8 source decoding: native channel icons are Unicode, and
# MSVC's machine-specific ANSI source charset corrupts emoji literals.
# Limit the flag to C++ (not Windows .rc/resource compilation).
if(MSVC)
  target_compile_options(ARMixConsole PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
endif()
# Stamp the build source in Copy Audio Report, including Windows field trials.
execute_process(COMMAND git rev-parse --verify HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE ARMIX_BUILD_SOURCE_SHA OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE ARMIX_BUILD_SOURCE_STATUS ERROR_QUIET)
if(NOT ARMIX_BUILD_SOURCE_STATUS EQUAL 0 OR NOT ARMIX_BUILD_SOURCE_SHA MATCHES "^[0-9a-f]+$")
  set(ARMIX_BUILD_SOURCE_SHA "unavailable")
endif()
# One JUCE mixer/PCM host on all three desktops. Device backends are OS owned.
target_compile_definitions(ARMixConsole PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_APPLICATION_NAME_STRING="AR MIX Studio"
    JUCE_APPLICATION_VERSION_STRING="0.5.0-p2"
    ARMIX_BUILD_SOURCE_SHA="${ARMIX_BUILD_SOURCE_SHA}")
if(WIN32)
  # Windows retains its proven ASIO-first / WASAPI / static WebView2 distribution.
  # Never link Windows loader or sound APIs into macOS/Linux builds.
  target_compile_definitions(ARMixConsole PRIVATE
      JUCE_USE_WIN_WEBVIEW2=1
      JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
      JUCE_ASIO=1 JUCE_WASAPI=1 JUCE_DIRECTSOUND=1)
  target_link_libraries(ARMixConsole PRIVATE ole32 uuid advapi32)
endif()

target_link_libraries(ARMixConsole PRIVATE
    ARMixAssets
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_cryptography
    juce::juce_audio_devices
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
  juce_add_console_app(ARMixFactoryFxRegistryTests PRODUCT_NAME "ArSonMix Factory FX Integrity Tests")
  target_sources(ARMixFactoryFxRegistryTests PRIVATE tests/factory_fx_registry_test.cpp src/FactoryFxRegistry.h)
  target_link_libraries(ARMixFactoryFxRegistryTests PRIVATE juce::juce_core juce::juce_cryptography juce::juce_recommended_config_flags)
  target_compile_features(ARMixFactoryFxRegistryTests PRIVATE cxx_std_20)
  add_test(NAME FactoryFxVerifiedManifestAndNegativePathGate COMMAND ARMixFactoryFxRegistryTests)
  add_executable(ARMixFxStructuralHandoffTests tests/live_fx_structural_handoff_test.cpp)
  target_compile_features(ARMixFxStructuralHandoffTests PRIVATE cxx_std_20)
  add_test(NAME R0BStructuralFxAckAndControlThreadRetirement COMMAND ARMixFxStructuralHandoffTests)
  add_executable(ARMixHarmonyOfflineAb tools/harmony_offline_ab.cpp)
  target_compile_features(ARMixHarmonyOfflineAb PRIVATE cxx_std_20)
  add_test(NAME NativeHarmonyOfflineDryAndABRenderHarness COMMAND ARMixHarmonyOfflineAb --self-test)
  add_executable(ARMixHarmonyPitchSyncTests tests/harmony_pitch_synchronous_test.cpp)
  target_compile_features(ARMixHarmonyPitchSyncTests PRIVATE cxx_std_20)
  add_test(NAME NativeHarmonyPitchSynchronousFormantCandidateAndBlockParity COMMAND ARMixHarmonyPitchSyncTests)
  add_executable(ARMixHarmonySpatialTests tests/harmony_stage_spatial_test.cpp)
  target_compile_features(ARMixHarmonySpatialTests PRIVATE cxx_std_20)
  add_test(NAME NativeHarmonyBackingStageSpatialDepthAndMonoPDC COMMAND ARMixHarmonySpatialTests)
  add_executable(ARMixHarmonyDspTests tests/harmony_dsp_test.cpp)
  target_compile_features(ARMixHarmonyDspTests PRIVATE cxx_std_20)
  add_test(NAME NativeHarmonyMultivoicePitchAndRealtimeFinitePCM COMMAND ARMixHarmonyDspTests)
  # Non-gating synthetic engine-only CPU profile (not ASIO certification).
  add_executable(ARMixHarmonyCpuProfileTests tests/harmony_cpu_profile_test.cpp)
  target_compile_features(ARMixHarmonyCpuProfileTests PRIVATE cxx_std_20)
  add_test(NAME NativeHarmonySyntheticCallbackCpuProfile COMMAND ARMixHarmonyCpuProfileTests)
  add_executable(ARMixHarmonyMusicGuideTests tests/harmony_music_key_test.cpp)
  target_compile_features(ARMixHarmonyMusicGuideTests PRIVATE cxx_std_20)
  add_test(NAME NativeHarmonyCrossChannelMusicKeyAndManualFallback COMMAND ARMixHarmonyMusicGuideTests)
  juce_add_console_app(ARMixHarmonyChannelPcmTests PRODUCT_NAME "ArSonMix Native Harmony Music Channel PCM")
  target_sources(ARMixHarmonyChannelPcmTests PRIVATE tests/harmony_music_bank_test.cpp)
  target_link_libraries(ARMixHarmonyChannelPcmTests PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixHarmonyChannelPcmTests PRIVATE cxx_std_20)
  add_test(NAME NativeHarmonyMusicFromOtherChannelIdAndStateMigration COMMAND ARMixHarmonyChannelPcmTests)
  add_test(NAME ApprovedNativeHarmonyEditorAckAndSingleGui COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_harmony_assembly_test.py")
  add_executable(ARMixMixerRoutingTests tests/mixer_routing_plan_test.cpp)
  target_compile_features(ARMixMixerRoutingTests PRIVATE cxx_std_20)
  add_dependencies(ARMixMixerRoutingTests ARMixHarmonyDspTests ARMixHarmonyMusicGuideTests ARMixHarmonyChannelPcmTests ARMixHarmonyCpuProfileTests)
  add_test(NAME TenStripStaticRoutingAndMaster COMMAND ARMixMixerRoutingTests)
  # M3D atomic native Session: the accepted Console roster cannot advance
  # without a successful epoch/revision-gated PCM publication.
  add_executable(ARMixDynamicAcceptedSessionTests tests/dynamic_accepted_session_test.cpp)
  target_compile_features(ARMixDynamicAcceptedSessionTests PRIVATE cxx_std_20)
  add_test(NAME M3DAtomicStudioBusFxChannelSessionRollback COMMAND ARMixDynamicAcceptedSessionTests)
  # ArSon Player M0: virtual stereo PCM source + trusted-MAIN first-sound
  # planner + SOURCE ONLY and shared-deck fan-out, with zero RT allocations.
  add_executable(ARMixPlayerVirtualSourceTests tests/player_virtual_source_pcm_test.cpp)
  target_compile_features(ARMixPlayerVirtualSourceTests PRIVATE cxx_std_20)
  add_test(NAME ArSonPlayerVirtualPcmFirstSoundAndSourceOnly COMMAND ARMixPlayerVirtualSourceTests)
  add_dependencies(ARMixMixerRoutingTests ARMixPlayerVirtualSourceTests)
  find_package(Threads REQUIRED)
  add_executable(ARMixPlayerPcmRingTests tests/player_pcm_ring_test.cpp)
  target_compile_features(ARMixPlayerPcmRingTests PRIVATE cxx_std_20)
  target_link_libraries(ARMixPlayerPcmRingTests PRIVATE Threads::Threads)
  add_test(NAME ArSonPlayerWorkerToRealtimeStereoRing COMMAND ARMixPlayerPcmRingTests)
  add_dependencies(ARMixMixerRoutingTests ARMixPlayerPcmRingTests)
  juce_add_console_app(ARMixPlayerDecodeTests PRODUCT_NAME "ArSon Player Native Decoder Integration")
  target_sources(ARMixPlayerDecodeTests PRIVATE tests/player_native_decode_test.cpp)
  target_link_libraries(ARMixPlayerDecodeTests PRIVATE
      juce::juce_audio_formats juce::juce_recommended_config_flags)
  target_compile_features(ARMixPlayerDecodeTests PRIVATE cxx_std_20)
  add_test(NAME ArSonPlayerRealWavDecodeResampleAndTransport COMMAND ARMixPlayerDecodeTests)
  add_dependencies(ARMixMixerRoutingTests ARMixPlayerDecodeTests)
  # M2: independent stereo key/tempo, documented MIT-vendored Signalsmith.
  # Build under the existing Windows mixer test target; no new CI lane.
  add_executable(ARMixPlayerPitchTempoTests tests/player_pitch_tempo_test.cpp)
  target_include_directories(ARMixPlayerPitchTempoTests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith")
  target_compile_features(ARMixPlayerPitchTempoTests PRIVATE cxx_std_20)
  add_test(NAME ArSonPlayerM2IndependentKeyTempoStereoPCM
      COMMAND ARMixPlayerPitchTempoTests)
  add_dependencies(ARMixMixerRoutingTests ARMixPlayerPitchTempoTests)
  # Windows release workflow builds ARMixMixerRoutingTests explicitly, then runs
  # all CTests. Make its native session regression a build dependency so the
  # new executable is actually present (without editing the CI target list).
  add_dependencies(ARMixMixerRoutingTests ARMixDynamicAcceptedSessionTests)
  # Integration-only producer/consumer: accepted ChannelId readback + real
  # offline PCM with CH07/CH02, GROUP, wet FX RETURN and MONITOR. No new host.
  add_executable(ARMixConsoleBridgeV6Tests tests/console_native_integration_test.cpp
      integration/ConsoleNativeContractV6.h)
  target_compile_features(ARMixConsoleBridgeV6Tests PRIVATE cxx_std_20)
  add_test(NAME ConsoleV6NativeAcceptedSnapshotAndGoldenPCM COMMAND ARMixConsoleBridgeV6Tests)
  # Existing Windows build target pulls in this test without a second workflow.
  add_dependencies(ARMixMixerRoutingTests ARMixConsoleBridgeV6Tests)
  # M3-B INTEGRATOR-only readback serialization of real ChannelId PCM.
  # Never installs a second callback, renderer or fake live source.
  juce_add_console_app(ARMixNativeV8ProjectionTests PRODUCT_NAME "ArSonMix Native V8 Readback Projection Tests")
  target_sources(ARMixNativeV8ProjectionTests PRIVATE tests/approved_native_v8_projection_test.cpp integration/ApprovedNativeV8Projection.h integration/ApprovedNativeControlIntent.h integration/ApprovedNativeRuntimeSnapshot.h)
  target_link_libraries(ARMixNativeV8ProjectionTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
  target_compile_features(ARMixNativeV8ProjectionTests PRIVATE cxx_std_20)
  add_test(NAME M3BNativeV8StableIdReadbackAndFailClosedProjection COMMAND ARMixNativeV8ProjectionTests)
  add_dependencies(ARMixMixerRoutingTests ARMixNativeV8ProjectionTests)
  juce_add_console_app(ARMixApprovedRoutingPcmTests PRODUCT_NAME "ArSonMix Approved WIRE PCM Tests")
  target_sources(ARMixApprovedRoutingPcmTests PRIVATE tests/approved_native_routing_pcm_test.cpp)
  target_link_libraries(ARMixApprovedRoutingPcmTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
  target_compile_features(ARMixApprovedRoutingPcmTests PRIVATE cxx_std_20)
  add_test(NAME P0ApprovedDecoderChannelToChannelAndSourceOnlyPCM COMMAND ARMixApprovedRoutingPcmTests)
  add_dependencies(ARMixMixerRoutingTests ARMixApprovedRoutingPcmTests)
  add_executable(ARMixLatencyCompensationTests tests/dynamic_latency_compensation_test.cpp)
  target_compile_features(ARMixLatencyCompensationTests PRIVATE cxx_std_20)
  add_test(NAME NativeGraphLatencyAlignedFanInAndHardwarePCM COMMAND ARMixLatencyCompensationTests)
  add_dependencies(ARMixMixerRoutingTests ARMixLatencyCompensationTests)
  add_executable(ARMixChannelRoutingContractTests tests/channel_routing_contract_test.cpp)
  target_compile_features(ARMixChannelRoutingContractTests PRIVATE cxx_std_20)
  add_test(NAME MonoStereoSparsePhysicalPortRouting COMMAND ARMixChannelRoutingContractTests)
  add_executable(ARMixChannelSendTests tests/channel_send_plan_test.cpp)
  target_compile_features(ARMixChannelSendTests PRIVATE cxx_std_20)
  add_test(NAME TenChannelAcyclicPrePostSends COMMAND ARMixChannelSendTests)
  add_executable(ARMixPreparedMixerBlockTests tests/prepared_mixer_block_test.cpp)
  target_compile_features(ARMixPreparedMixerBlockTests PRIVATE cxx_std_20)
  add_test(NAME TenChannelEndToEndPreparedAudioRender COMMAND ARMixPreparedMixerBlockTests)
  juce_add_console_app(ARMixJucePortMaskTests PRODUCT_NAME "ArSonMix JUCE Port Mask Snapshot Tests")
  target_sources(ARMixJucePortMaskTests PRIVATE tests/juce_active_port_snapshot_test.cpp engine/device/JuceActivePortSnapshot.h)
  target_link_libraries(ARMixJucePortMaskTests PRIVATE juce::juce_audio_devices juce::juce_recommended_config_flags)
  target_compile_features(ARMixJucePortMaskTests PRIVATE cxx_std_20)
  add_test(NAME JuceSparseActiveChannelMaskAndEpoch COMMAND ARMixJucePortMaskTests)
  juce_add_console_app(ARMixNativeMixerSessionTests PRODUCT_NAME "ArSonMix Bound Native Routing Tests")
  target_sources(ARMixNativeMixerSessionTests PRIVATE tests/prepared_native_mixer_session_test.cpp engine/core/PreparedNativeMixerSession.h)
  target_link_libraries(ARMixNativeMixerSessionTests PRIVATE juce::juce_audio_devices juce::juce_recommended_config_flags)
  target_compile_features(ARMixNativeMixerSessionTests PRIVATE cxx_std_20)
  add_test(NAME P1FPhysicalPortToNativeMasterAndEpochGuard COMMAND ARMixNativeMixerSessionTests)
  add_executable(ARMixTenChannelLiveControlTests tests/realtime_ten_channel_controls_test.cpp)
  target_compile_features(ARMixTenChannelLiveControlTests PRIVATE cxx_std_20)
  add_test(NAME P1ITenChannelLiveFaderPanMuteSoloAndStableFx COMMAND ARMixTenChannelLiveControlTests)
  add_executable(ARMixDedicatedAuxBusTests tests/dedicated_aux_bus_test.cpp)
  target_compile_features(ARMixDedicatedAuxBusTests PRIVATE cxx_std_20)
  add_test(NAME P1KTenIndependentInputsAndFourDedicatedAuxReturns COMMAND ARMixDedicatedAuxBusTests)
  juce_add_console_app(ARMixLiveChannelFxTests PRODUCT_NAME "ArSonMix P1H Independent Channel FX Tests")
  target_sources(ARMixLiveChannelFxTests PRIVATE tests/live_channel_fx_graph_test.cpp engine/core/LiveChannelFxGraph.h)
  target_link_libraries(ARMixLiveChannelFxTests PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixLiveChannelFxTests PRIVATE cxx_std_20)
  add_test(NAME P1HRealNativePerChannelFxAndSafeRetirement COMMAND ARMixLiveChannelFxTests)
  juce_add_console_app(ARMixNativeDrySendTests PRODUCT_NAME "ArSonMix Live Dry Send and BUS Tests")
  target_sources(ARMixNativeDrySendTests PRIVATE tests/native_dry_route_command_test.cpp engine/core/NativeDryRouteCommand.h)
  target_link_libraries(ARMixNativeDrySendTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
  target_compile_features(ARMixNativeDrySendTests PRIVATE cxx_std_20)
  add_test(NAME LiveNativePrePostSendAndBusContract COMMAND ARMixNativeDrySendTests)
  # Device reopen must not silently replace an authored ChannelId mixer.
  add_executable(ARMixDeviceSessionTransitionTests tests/device_session_transition_policy_test.cpp engine/device/DeviceSessionTransitionPolicy.h)
  target_compile_features(ARMixDeviceSessionTransitionTests PRIVATE cxx_std_20)
  add_test(NAME NonDestructiveAudioSetupRetainsMixerDocument COMMAND ARMixDeviceSessionTransitionTests)
  add_executable(ARMixLiveRoutingEpochTests tests/live_port_epoch_publisher_test.cpp)
  target_compile_features(ARMixLiveRoutingEpochTests PRIVATE cxx_std_20)
  add_test(NAME P1ELivePortGenerationRevisionAndRealtimeRetirement COMMAND ARMixLiveRoutingEpochTests)
  add_executable(ARMixOrbitPhaseTests tests/orbit_phase_test.cpp)
  target_compile_features(ARMixOrbitPhaseTests PRIVATE cxx_std_20)
  add_test(NAME VstAzimuthOrbitRamp COMMAND ARMixOrbitPhaseTests)
  add_executable(ARMixOrbitMotionTests tests/orbit_motion_test.cpp)
  target_compile_features(ARMixOrbitMotionTests PRIVATE cxx_std_20)
  add_test(NAME VstThreeAxisMotionPaths COMMAND ARMixOrbitMotionTests)
  add_executable(ARMixOrbitDistanceTests tests/orbit_distance_mapping_test.cpp)
  target_compile_features(ARMixOrbitDistanceTests PRIVATE cxx_std_20)
  add_test(NAME VstOrbitVendorDistanceMapping COMMAND ARMixOrbitDistanceTests)
  add_executable(ARMixDearVrPolarPolicyTests tests/dearvr_polar_policy_test.cpp)
  target_compile_features(ARMixDearVrPolarPolicyTests PRIVATE cxx_std_20)
  add_test(NAME VstDearVrPolarPolicy COMMAND ARMixDearVrPolarPolicyTests)
  add_executable(ARMixOrbitControlTests tests/orbit_control_policy_test.cpp)
  target_compile_features(ARMixOrbitControlTests PRIVATE cxx_std_20)
  add_test(NAME VstOrbitControlRatePolicy COMMAND ARMixOrbitControlTests)
  add_executable(ARMixAsioRegistryIdentityTests tests/asio_registry_identity_test.cpp)
  target_compile_features(ARMixAsioRegistryIdentityTests PRIVATE cxx_std_20)
  if(WIN32)
    target_link_libraries(ARMixAsioRegistryIdentityTests PRIVATE advapi32)
  endif()
  add_test(NAME AsioDriverClassRegistryAttestation COMMAND ARMixAsioRegistryIdentityTests)
  add_executable(ARMixDirectFenceTests tests/direct_route_fence_test.cpp)
  target_compile_features(ARMixDirectFenceTests PRIVATE cxx_std_20)
  add_test(NAME P0AsioLiveDirectRouteFence COMMAND ARMixDirectFenceTests)
  add_executable(ARMixAsioPolicyTests tests/asio_startup_policy_test.cpp)
  target_compile_features(ARMixAsioPolicyTests PRIVATE cxx_std_20)
  add_test(NAME AsioFirstBootPriorityPolicy COMMAND ARMixAsioPolicyTests)
  add_executable(ARMixEndpointPolicyTests tests/endpoint_identity_policy_test.cpp)
  target_compile_features(ARMixEndpointPolicyTests PRIVATE cxx_std_20)
  add_test(NAME UniqueWindowsEndpointIdentity COMMAND ARMixEndpointPolicyTests)
  add_executable(ARMixBridgeContinuityTests tests/bridge_continuity_gate_test.cpp)
  target_compile_features(ARMixBridgeContinuityTests PRIVATE cxx_std_20)
  add_test(NAME SustainedAsrcContinuityFailback COMMAND ARMixBridgeContinuityTests)
  add_executable(ARMixStreamWatchdogTests tests/independent_stream_watchdog_test.cpp)
  target_compile_features(ARMixStreamWatchdogTests PRIVATE cxx_std_20)
  add_test(NAME IndependentStreamSilenceRecovery COMMAND ARMixStreamWatchdogTests)
  add_executable(ARMixAsioRestartGateTests tests/asio_restart_recovery_gate_test.cpp
    engine/device/AsioRestartRecoveryGate.h)
  target_compile_features(ARMixAsioRestartGateTests PRIVATE cxx_std_20)
  add_test(NAME Asio4AllWdmSettleBoundedDriverTransactionsAndDeferredRebind COMMAND ARMixAsioRestartGateTests)
  add_executable(ARMixRecoveryTests tests/direct_restore_backoff_test.cpp)
  target_compile_features(ARMixRecoveryTests PRIVATE cxx_std_20)
  add_test(NAME BoundedDirectDeviceRecovery COMMAND ARMixRecoveryTests)
  add_executable(ARMixClockSizingTests tests/clock_bridge_sizing_test.cpp)
  target_compile_features(ARMixClockSizingTests PRIVATE cxx_std_20)
  add_test(NAME AdaptiveClockPeriodHeadroom COMMAND ARMixClockSizingTests)
  add_executable(ARMixIndependentFormatTests tests/independent_stream_format_fence_test.cpp)
  target_compile_features(ARMixIndependentFormatTests PRIVATE cxx_std_20)
  add_test(NAME IndependentStreamNegotiatedFormatFence COMMAND ARMixIndependentFormatTests)
  if(WIN32)
    juce_add_console_app(ARMixAsioBackendTests PRODUCT_NAME "ArSonMix ASIO Backend Registration Tests")
    target_sources(ARMixAsioBackendTests PRIVATE tests/asio_backend_registration_test.cpp engine/device/AsioPriorityDeviceManager.h)
    target_compile_definitions(ARMixAsioBackendTests PRIVATE JUCE_ASIO=1)
    target_link_libraries(ARMixAsioBackendTests PRIVATE juce::juce_audio_devices juce::juce_recommended_config_flags)
    target_compile_features(ARMixAsioBackendTests PRIVATE cxx_std_20)
    add_test(NAME AsioNativeBackendRegistered COMMAND ARMixAsioBackendTests)
    juce_add_console_app(ARMixDualJuceTests PRODUCT_NAME "ArSonMix Independent JUCE Device Tests")
    target_sources(ARMixDualJuceTests PRIVATE tests/juce_independent_device_lifecycle_test.cpp engine/device/JuceIndependentClockDevice.h)
    target_link_libraries(ARMixDualJuceTests PRIVATE juce::juce_audio_devices juce::juce_audio_basics juce::juce_recommended_config_flags)
    target_compile_features(ARMixDualJuceTests PRIVATE cxx_std_20)
    add_test(NAME IndependentJuceDeviceLifecycle COMMAND ARMixDualJuceTests)
    juce_add_console_app(ARMixWasapiProbeTests PRODUCT_NAME "ArSonMix WASAPI Probe Tests")
    target_sources(ARMixWasapiProbeTests PRIVATE tests/wasapi_capability_probe_test.cpp engine/device/WasapiCapabilityProbe.h)
    target_link_libraries(ARMixWasapiProbeTests PRIVATE juce::juce_core ole32 uuid juce::juce_recommended_config_flags)
    target_compile_features(ARMixWasapiProbeTests PRIVATE cxx_std_20)
    add_test(NAME WasapiCapabilityProbe COMMAND ARMixWasapiProbeTests)
    juce_add_console_app(ARMixEndpointGuardTests PRODUCT_NAME "ArSonMix Windows Endpoint Identity Guard Tests")
    target_sources(ARMixEndpointGuardTests PRIVATE tests/wasapi_endpoint_identity_guard_test.cpp engine/device/WasapiEndpointIdentityGuard.h)
    target_link_libraries(ARMixEndpointGuardTests PRIVATE juce::juce_core ole32 uuid juce::juce_recommended_config_flags)
    target_compile_features(ARMixEndpointGuardTests PRIVATE cxx_std_20)
    add_test(NAME WindowsEndpointIdentityGuard COMMAND ARMixEndpointGuardTests)
  endif()
  add_executable(ARMixLifecycleTests tests/session_lifecycle_test.cpp)
  target_compile_features(ARMixLifecycleTests PRIVATE cxx_std_20)
  add_test(NAME DeviceSessionLifecycle COMMAND ARMixLifecycleTests)
  add_executable(ARMixPolicyTests tests/audio_profile_policy_test.cpp)
  target_compile_features(ARMixPolicyTests PRIVATE cxx_std_20)
  add_test(NAME CapabilityAudioProfilePolicy COMMAND ARMixPolicyTests)
  add_executable(ARMixPeriodEvidenceTests tests/period_capability_evidence_test.cpp)
  target_compile_features(ARMixPeriodEvidenceTests PRIVATE cxx_std_20)
  add_test(NAME NativeDriverPeriodEvidence COMMAND ARMixPeriodEvidenceTests)
  add_executable(ARMixClockBridgeTests tests/adaptive_clock_bridge_test.cpp)
  target_compile_features(ARMixClockBridgeTests PRIVATE cxx_std_20)
  add_test(NAME AdaptiveDualClockBridge COMMAND ARMixClockBridgeTests)
  add_executable(ARMixVirtualClockSoakTests tests/adaptive_clock_virtual_soak_test.cpp)
  target_compile_features(ARMixVirtualClockSoakTests PRIVATE cxx_std_20)
  add_test(NAME IndependentClockASRCDriftJitterSoak COMMAND ARMixVirtualClockSoakTests)
  set_tests_properties(IndependentClockASRCDriftJitterSoak PROPERTIES TIMEOUT 30)
  # Live-safe logical FX move: deterministic x64 atomic order + output de-click.
  add_executable(ARMixFxOrderTests tests/fx_order_atomic_test.cpp)
  target_compile_features(ARMixFxOrderTests PRIVATE cxx_std_20)
  add_test(NAME FxAtomicOrderAndOutputRamp COMMAND ARMixFxOrderTests)
  add_executable(ARMixFxBypassEnvelopeTests tests/fx_bypass_envelope_test.cpp)
  target_compile_features(ARMixFxBypassEnvelopeTests PRIVATE cxx_std_20)
  add_test(NAME P0FXBypassSampleAccurateDeClick COMMAND ARMixFxBypassEnvelopeTests)
  add_executable(ARMixCrackleWindowTests tests/crackle_evidence_window_test.cpp)
  target_compile_features(ARMixCrackleWindowTests PRIVATE cxx_std_20)
  add_test(NAME P0PreMarkerFiveSecondEvidenceWindow COMMAND ARMixCrackleWindowTests)
  add_executable(ARMixCaptureBoundaryTests tests/capture_boundary_evidence_test.cpp)
  target_compile_features(ARMixCaptureBoundaryTests PRIVATE cxx_std_20)
  add_test(NAME P0CaptureZeroHeadBoundaryCandidates COMMAND ARMixCaptureBoundaryTests)
  add_executable(ARMixCadenceTests tests/callback_cadence_evidence_test.cpp)
  target_compile_features(ARMixCadenceTests PRIVATE cxx_std_20)
  add_test(NAME P0RenderCallbackVariableQuantumCadence COMMAND ARMixCadenceTests)
  add_executable(ARMixHealthTests tests/realtime_health_histogram_test.cpp)
  target_compile_features(ARMixHealthTests PRIVATE cxx_std_20)
  add_test(NAME RealtimeHealthHistogram COMMAND ARMixHealthTests)
  add_executable(ARMixDiagnosticTests tests/diagnostic_tone_test.cpp)
  target_compile_features(ARMixDiagnosticTests PRIVATE cxx_std_20)
  add_test(NAME P0DiagnosticTone COMMAND ARMixDiagnosticTests)
  juce_add_console_app(ARMixVaultTests PRODUCT_NAME "ArSonMix Selective VST3 Vault Tests")
  target_sources(ARMixVaultTests PRIVATE tests/plugin_vault_test.cpp src/PluginVault.h src/PortableWorkspace.h)
  target_link_libraries(ARMixVaultTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
  target_compile_features(ARMixVaultTests PRIVATE cxx_std_20)
  add_test(NAME SelectiveVst3BundleVaultImport COMMAND ARMixVaultTests)
  juce_add_console_app(ARMixBackupTests PRODUCT_NAME "ArSonMix Verified Native Studio Backup Tests")
  target_sources(ARMixBackupTests PRIVATE tests/studio_backup_test.cpp src/StudioBackup.h src/PortableWorkspace.h)
  target_link_libraries(ARMixBackupTests PRIVATE juce::juce_core juce::juce_cryptography
      juce::juce_events juce::juce_recommended_config_flags)
  target_compile_features(ARMixBackupTests PRIVATE cxx_std_20)
  add_test(NAME VerifiedPortableStudioBackup COMMAND ARMixBackupTests)
  juce_add_console_app(ARMixWorkspaceTests PRODUCT_NAME "ArSonMix Portable Workspace Tests")
  target_sources(ARMixWorkspaceTests PRIVATE tests/portable_workspace_test.cpp src/PortableWorkspace.h src/NativePresetArchive.h)
  target_link_libraries(ARMixWorkspaceTests PRIVATE juce::juce_core juce::juce_cryptography
      juce::juce_recommended_config_flags)
  target_compile_features(ARMixWorkspaceTests PRIVATE cxx_std_20)
  add_test(NAME PortableStudioUsbRelocation COMMAND ARMixWorkspaceTests)
  add_executable(ARMixRTTests tests/realtime_control_test.cpp)
  target_compile_features(ARMixRTTests PRIVATE cxx_std_20)
  add_test(NAME RealtimeControlPrimitives COMMAND ARMixRTTests)
  juce_add_console_app(ARMixPresetTests PRODUCT_NAME "ArSonMix Native Preset Tests")
  target_sources(ARMixPresetTests PRIVATE tests/native_preset_roundtrip_test.cpp src/NativePresetArchive.h)
  target_link_libraries(ARMixPresetTests PRIVATE juce::juce_core juce::juce_cryptography
      juce::juce_recommended_config_flags)
  target_compile_features(ARMixPresetTests PRIVATE cxx_std_20)
  add_test(NAME NativePresetZipRoundTrip COMMAND ARMixPresetTests)
  juce_add_console_app(ARMixShowSchemaTests PRODUCT_NAME "ArSonMix Show V2 Wire Schema Tests")
  target_sources(ARMixShowSchemaTests PRIVATE tests/native_show_schema_test.cpp src/NativeShowSchema.h)
  target_link_libraries(ARMixShowSchemaTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
  target_compile_features(ARMixShowSchemaTests PRIVATE cxx_std_20)
  add_test(NAME P4NativeShowSnapshotV2WireRoundTrip COMMAND ARMixShowSchemaTests)
  juce_add_console_app(ARMixLiveShowCaptureTests PRODUCT_NAME "ArSonMix Live Native Dry Show Capture Tests")
  target_sources(ARMixLiveShowCaptureTests PRIVATE tests/native_live_show_capture_test.cpp src/NativeLiveShowCapture.h src/NativeDryShowRecall.h src/NativeShowStore.h)
  target_link_libraries(ARMixLiveShowCaptureTests PRIVATE juce::juce_core juce::juce_cryptography juce::juce_recommended_config_flags)
  target_compile_features(ARMixLiveShowCaptureTests PRIVATE cxx_std_20)
  add_test(NAME P4NativeAcknowledgedDryShowCaptureAndStorage COMMAND ARMixLiveShowCaptureTests)

  juce_add_console_app(ARMixShowStoreTests PRODUCT_NAME "ArSonMix Native Studio Shows Generation Tests")
  target_sources(ARMixShowStoreTests PRIVATE tests/native_show_store_test.cpp src/NativeShowStore.h src/NativeShowSchema.h)
  target_link_libraries(ARMixShowStoreTests PRIVATE juce::juce_core juce::juce_cryptography juce::juce_recommended_config_flags)
  target_compile_features(ARMixShowStoreTests PRIVATE cxx_std_20)
  add_test(NAME P4ImmutableNativeShowsAndVendorStateRoundTrip COMMAND ARMixShowStoreTests)
  # Full-size stable ChannelId MixerSession document (never the fixed ShowV2).
  juce_add_console_app(ARMixDurableMixerSessionTests PRODUCT_NAME "ArSonMix Durable Dynamic Mixer Session Tests")
  target_sources(ARMixDurableMixerSessionTests PRIVATE tests/native_mixer_session_store_test.cpp
      src/NativeMixerSessionDocument.h src/NativeMixerSessionStore.h)
  target_link_libraries(ARMixDurableMixerSessionTests PRIVATE
      juce::juce_audio_devices juce::juce_audio_processors juce::juce_cryptography juce::juce_recommended_config_flags)
  target_compile_features(ARMixDurableMixerSessionTests PRIVATE cxx_std_20)
  add_test(NAME P0DynamicMixerSessionDocumentAndCrashSafeRecovery COMMAND ARMixDurableMixerSessionTests)
  # Presentation-only native identity; no audio-thread access.
  juce_add_console_app(ARMixChannelAppearanceTests PRODUCT_NAME "ArSonMix Channel Appearance Tests")
  target_sources(ARMixChannelAppearanceTests PRIVATE
      tests/native_channel_appearance_store_test.cpp src/NativeChannelAppearanceStore.h)
  target_link_libraries(ARMixChannelAppearanceTests PRIVATE
      juce::juce_core juce::juce_recommended_config_flags)
  target_compile_features(ARMixChannelAppearanceTests PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(ARMixChannelAppearanceTests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
  endif()
  add_test(NAME NativeChannelIdentityDiskAckAndExactId COMMAND ARMixChannelAppearanceTests)

  # Eight-band native PEQ: deterministic sweep, stereo isolation, RT no-heap,
  # smoothing and Nyquist guards. Built with the existing Windows native lane.
  add_executable(ARMixNativeChannelEqTests tests/builtin_channel_eq_test.cpp
      engine/builtin_fx/BuiltInFxCatalog.h engine/builtin_fx/ChannelEq.h)
  target_compile_features(ARMixNativeChannelEqTests PRIVATE cxx_std_20)
  add_test(NAME NativePEQEightBandsRTAndFrequencyResponse COMMAND ARMixNativeChannelEqTests)
  add_dependencies(ARMixMixerRoutingTests ARMixNativeChannelEqTests)

  # Zero-lookahead native compressor DSP: zero PDC, stereo-linked peak,
  # soft knee, dry/wet, parameter validation and variable sample rates.
  add_executable(ARMixNativeCompressorTests tests/builtin_channel_compressor_test.cpp
      engine/builtin_fx/ChannelCompressor.h)
  target_compile_features(ARMixNativeCompressorTests PRIVATE cxx_std_20)
  add_test(NAME NativeCompressorZeroLatencyAndLinkedStereoPCM COMMAND ARMixNativeCompressorTests)
  add_dependencies(ARMixMixerRoutingTests ARMixNativeCompressorTests)
  # Real JUCE FX processor lifecycle: retain last AUDIO-applied Compressor
  # settings across rate/buffer restarts even when CONTROL ACK is unpolled.
  juce_add_console_app(ARMixNativeCompressorLifecycleTests PRODUCT_NAME
      "ArSonMix Native Compressor Device Restart Tests")
  target_sources(ARMixNativeCompressorLifecycleTests PRIVATE
      tests/native_compressor_processor_lifecycle_test.cpp
      engine/builtin_fx/ChannelCompressorProcessor.h)
  target_link_libraries(ARMixNativeCompressorLifecycleTests PRIVATE
      juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixNativeCompressorLifecycleTests PRIVATE cxx_std_20)
  add_test(NAME NativeCompressorDeviceRestartAckAndShowPCM
      COMMAND ARMixNativeCompressorLifecycleTests)
  add_dependencies(ARMixMixerRoutingTests ARMixNativeCompressorLifecycleTests)

  add_executable(ARMixNativeMasterLoudnessTests tests/native_master_loudness_test.cpp engine/dsp/NativeMasterLoudness.h)
  target_compile_features(ARMixNativeMasterLoudnessTests PRIVATE cxx_std_20)
  add_test(NAME P1KMeasuredNativeBS1770StereoAnd4xTruePeak COMMAND ARMixNativeMasterLoudnessTests)

endif()
