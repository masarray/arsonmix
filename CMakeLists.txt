cmake_minimum_required(VERSION 3.22)
project(ARMixConsole VERSION 0.5.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT DEFINED JUCE_ROOT)
  message(FATAL_ERROR "Pass -DJUCE_ROOT=C:/path/to/JUCE (8.0.15+ required)")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# JUCE 8.0.15 WaveShell class indices move after setHostContext.
# Apply audited upstream 3fa28d0 ordering correction before compilation.
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/patch_juce_vst3_waveshell.py"
            --juce-root "${JUCE_ROOT}"
    RESULT_VARIABLE ARMIX_WAVESHELL_PATCH_RESULT
    OUTPUT_VARIABLE ARMIX_WAVESHELL_PATCH_MESSAGE
    ERROR_VARIABLE ARMIX_WAVESHELL_PATCH_ERROR)
if(NOT ARMIX_WAVESHELL_PATCH_RESULT EQUAL 0)
  message(FATAL_ERROR "JUCE WaveShell VST3 host fix not verified: ${ARMIX_WAVESHELL_PATCH_ERROR}")
endif()
message(STATUS "${ARMIX_WAVESHELL_PATCH_MESSAGE}")
add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce")
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
# Generate only the active, independent Guitar FX hardware layers before
# globs are evaluated. Never embed deprecated prototype pictures or pictures
# of non-playable modules (no chorus/reverb/etc in this native rig).
# All ON/OFF layers use identical geometry and are drawn inside fixed bounds.
find_program(ARMIX_GUITAR_NODE_EXECUTABLE NAMES node REQUIRED)
execute_process(
    COMMAND "${ARMIX_GUITAR_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_guitar_fx_layered_assets.mjs"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE ARMIX_GUITAR_ART_RESULT
    OUTPUT_VARIABLE ARMIX_GUITAR_ART_LOG
    ERROR_VARIABLE ARMIX_GUITAR_ART_ERROR)
if(NOT ARMIX_GUITAR_ART_RESULT EQUAL 0)
    message(FATAL_ERROR "Guitar FX layer generation failed: ${ARMIX_GUITAR_ART_ERROR}")
endif()
message(STATUS "${ARMIX_GUITAR_ART_LOG}")
# Optional hi-resolution artist layers are copied on top of the deterministic
# fallback *at configure time*, never loaded at runtime from user paths.
# Art approval adds the 12 PNGs to artist-source/; the source/audio contract
# remains unchanged. The shipped binary contains embedded PNG bytes only.
set(ARMIX_GUITAR_ART_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/design-references/guitar-fx/artist-source"
    CACHE PATH "Optional approved photo-real Guitar FX artist PNG source")
if(IS_DIRECTORY "${ARMIX_GUITAR_ART_SOURCE_DIR}")
    # Reject incomplete/misaligned artist exports before CMake embeds them.
    # A changed ON/OFF silhouette or outer frame would cause visible flicker.
    execute_process(
        COMMAND "${ARMIX_GUITAR_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/validate_guitar_fx_artist_source.mjs"
            "${ARMIX_GUITAR_ART_SOURCE_DIR}"
        RESULT_VARIABLE ARMIX_GUITAR_ART_CHECK_RESULT
        OUTPUT_VARIABLE ARMIX_GUITAR_ART_CHECK_LOG
        ERROR_VARIABLE ARMIX_GUITAR_ART_CHECK_ERROR)
    if(NOT ARMIX_GUITAR_ART_CHECK_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Invalid Guitar FX artist layers: ${ARMIX_GUITAR_ART_CHECK_ERROR}")
    endif()
    message(STATUS "${ARMIX_GUITAR_ART_CHECK_LOG}")
    foreach(ARMIX_GFX_ASSET
        gfx-amp-head.png gfx-cabinet.png gfx-pedal-gate-body.png
        gfx-pedal-drive-body.png gfx-knob.png gfx-switch-off.png
        gfx-switch-on.png gfx-button-off.png gfx-button-on.png
        gfx-panel.png gfx-stomp-off.png gfx-stomp-on.png)
        if(NOT EXISTS "${ARMIX_GUITAR_ART_SOURCE_DIR}/${ARMIX_GFX_ASSET}")
            message(FATAL_ERROR "Incomplete approved Guitar FX art source: ${ARMIX_GFX_ASSET}")
        endif()
        configure_file(
            "${ARMIX_GUITAR_ART_SOURCE_DIR}/${ARMIX_GFX_ASSET}"
            "${CMAKE_CURRENT_SOURCE_DIR}/design-references/guitar-fx/assets/${ARMIX_GFX_ASSET}"
            COPYONLY)
    endforeach()
    message(STATUS "Guitar FX approved layered artist art selected")
else()
    message(STATUS "Guitar FX deterministic layered fallback selected")
endif()
file(GLOB ARMIX_GUITAR_SKIN_PNGS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/design-references/guitar-fx/assets/gfx-*.png")
list(SORT ARMIX_GUITAR_SKIN_PNGS)
juce_add_binary_data(ARMixAssets SOURCES
    "${ARMIX_APPROVED_GUI_DIR}/index.html" ${ARMIX_APPROVED_GUI_BINARIES}
    ${ARMIX_GUITAR_SKIN_PNGS}
    "${ARMIX_BRAND_DIR}/arsonmix-256.png")
target_include_directories(ARMixConsole PRIVATE "${ARMIX_APPROVED_GUI_DIR}")

target_sources(ARMixConsole PRIVATE src/Main.cpp src/LiveAudioEngine.h engine/builtin_fx/HarmonyDsp.h engine/builtin_fx/HarmonyMusicKey.h engine/builtin_fx/ChannelHarmonyProcessor.h engine/builtin_fx/SonKuSpatialProcessor.h engine/builtin_fx/sonku/SonKuSpatialCore.h engine/builtin_fx/sonku/SonKuMeasuredHrir.h src/VstOrbitController.h engine/core/MixerRoutingPlan.h engine/core/ChannelRoutingContract.h engine/core/ChannelSendPlan.h engine/core/AuxBusPlan.h engine/core/PreparedMixerBlock.h engine/core/MasterOutputMonoPolicy.h engine/core/LiveChannelFxGraph.h engine/core/PreparedNativeMixerSession.h engine/core/NativeDryRouteCommand.h engine/core/RealtimeRoutePublisher.h engine/device/LiveInputPortRegistry.h src/LiveDryRoutingLab.h engine/core/OrbitPhase.h engine/core/OrbitControlPolicy.h engine/core/OrbitMotion.h engine/core/OrbitDistanceMapping.h engine/core/DearVrPolarPolicy.h src/PluginCatalog.h src/NativeFxParameterHotkey.h src/FactoryFxRegistry.h src/PluginVault.h src/PortableWorkspace.h src/StudioBackup.h src/DiagnosticsRecorder.h src/StereoPan.h src/NativePresetArchive.h src/NativeShowSchema.h src/NativeDryShowRecall.h src/VstBusLayout.h src/LiveGraphHandoff.h src/DiagnosticTone.h engine/core/RealtimeHealthHistogram.h engine/core/CallbackCadenceEvidence.h engine/diagnostics/CrackleEvidenceWindow.h engine/device/AdaptiveClockBridge.h engine/device/AudioProfilePolicy.h engine/device/PeriodCapabilityEvidence.h engine/device/SessionLifecycle.h engine/device/WasapiCapabilityProbe.h engine/device/JuceIndependentClockDevice.h engine/device/LiveSecondaryOutputDevice.h engine/core/LiveOutputRouteGate.h engine/device/ClockBridgeSizing.h engine/device/IndependentStreamFormatFence.h engine/device/DirectRestoreBackoff.h engine/device/IndependentStreamWatchdog.h engine/device/BridgeContinuityGate.h engine/device/EndpointIdentityPolicy.h engine/device/WasapiEndpointIdentityGuard.h engine/device/AsioStartupPolicy.h engine/device/AsioPriorityDeviceManager.h engine/device/JuceActivePortSnapshot.h engine/device/AsioRegistrationPolicy.h engine/device/AsioRegistryIdentityProbe.h engine/device/DirectRouteFence.h)
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
# Keep the native caption / About version aligned with the exact release
# artifact. The release controller validates and passes ARMIX_RELEASE_TAG
# BEFORE the tag is published; an existing checkout of a release tag is also
# supported. All other builds are visibly marked dev, never as a release.
set(ARMIX_RELEASE_TAG "" CACHE STRING "Verified release tag (vX.Y.Z-alpha.N)")
string(REPLACE "." "\\." ARMIX_ESCAPED_PROJECT_VERSION "${PROJECT_VERSION}")
set(ARMIX_RELEASE_TAG_PATTERN "^v${ARMIX_ESCAPED_PROJECT_VERSION}-alpha\\.[0-9]+$")
set(ARMIX_APP_VERSION "${PROJECT_VERSION}-dev")
if(ARMIX_RELEASE_TAG)
  if(NOT ARMIX_RELEASE_TAG MATCHES "${ARMIX_RELEASE_TAG_PATTERN}")
    message(FATAL_ERROR "ARMIX_RELEASE_TAG must match v${PROJECT_VERSION}-alpha.N")
  endif()
  string(SUBSTRING "${ARMIX_RELEASE_TAG}" 1 -1 ARMIX_APP_VERSION)
else()
  execute_process(
    COMMAND git describe --tags --exact-match --match "v${PROJECT_VERSION}-alpha.*" HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE ARMIX_EXACT_SOURCE_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE ARMIX_EXACT_TAG_STATUS ERROR_QUIET)
  if(ARMIX_EXACT_TAG_STATUS EQUAL 0 AND
     ARMIX_EXACT_SOURCE_TAG MATCHES "${ARMIX_RELEASE_TAG_PATTERN}")
    string(SUBSTRING "${ARMIX_EXACT_SOURCE_TAG}" 1 -1 ARMIX_APP_VERSION)
  elseif(ARMIX_BUILD_SOURCE_SHA MATCHES "^[0-9a-f]+$")
    string(SUBSTRING "${ARMIX_BUILD_SOURCE_SHA}" 0 8 ARMIX_BUILD_SHORT_SHA)
    set(ARMIX_APP_VERSION "${PROJECT_VERSION}-dev+g${ARMIX_BUILD_SHORT_SHA}")
  endif()
endif()
message(STATUS "ArSonMix executable version: ${ARMIX_APP_VERSION}")
# One JUCE mixer/PCM host on all three desktops. Device backends are OS owned.
target_compile_definitions(ARMixConsole PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_APPLICATION_NAME_STRING="AR MIX Studio"
    JUCE_APPLICATION_VERSION_STRING="${ARMIX_APP_VERSION}"
    ARMIX_BUILD_SOURCE_SHA="${ARMIX_BUILD_SOURCE_SHA}"
    ARMIX_GFX_EMBED_PNG_SKINS=1)
if(WIN32)
  # Windows retains its proven ASIO-first / WASAPI / static WebView2 distribution.
  # Never link Windows loader or sound APIs into macOS/Linux builds.
  target_compile_definitions(ARMixConsole PRIVATE
      JUCE_USE_WIN_WEBVIEW2=1
      JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
      JUCE_ASIO=1 JUCE_WASAPI=1 JUCE_DIRECTSOUND=1)
  # Dedicated INITGUID TU supplies PKEY_AudioEndpoint_FormFactor and
  # PKEY_AudioEngine_DeviceFormat to the Windows MMDevice discovery path.
  target_sources(ARMixConsole PRIVATE engine/device/WindowsAudioPropertyKeys.cpp)
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
  # LMS M2: portable native DSP kernel, prepared off-RT and exercised with
  # actual PCM blocks. This is NOT a second mixer callback or device graph.
  add_executable(ARMixSpeakerPcmTests tests/lms_prepared_speaker_pcm_test.cpp
      engine/lms/PreparedSpeakerSystem.h)
  target_compile_features(ARMixSpeakerPcmTests PRIVATE cxx_std_20)
  add_test(NAME LMSNativeLR4MultisectionVerifiedOutputsAndDelay
      COMMAND ARMixSpeakerPcmTests)
  add_executable(ARMixSpeakerDeviceBinderTests
      tests/lms_verified_device_binder_test.cpp
      engine/lms/NativeSpeakerDeviceBinder.h)
  target_compile_features(ARMixSpeakerDeviceBinderTests PRIVATE cxx_std_20)
  add_test(NAME LMSNativeExactJuceOutputInventoryDiscardBrowserOrdinals
      COMMAND ARMixSpeakerDeviceBinderTests)
  add_executable(ARMixSpeakerDraftAckReadbackTests
      tests/lms_native_draft_ack_readback_test.cpp
      engine/core/DynamicNativeSessionPublisher.h
      engine/lms/NativeSpeakerOutputReservation.h)
  target_compile_features(ARMixSpeakerDraftAckReadbackTests PRIVATE cxx_std_20)
  add_test(NAME LMSNativeSameGraphDesignAckReadbackAndStaleRevocation
      COMMAND ARMixSpeakerDraftAckReadbackTests)
  add_executable(ARMixSpeakerPostFxMainWitnessTests
      tests/lms_native_postfx_main_witness_test.cpp
      engine/lms/PreparedSpeakerSystem.h
      engine/core/DynamicNativeSessionPublisher.h)
  target_compile_features(ARMixSpeakerPostFxMainWitnessTests PRIVATE cxx_std_20)
  add_test(NAME LMSOneAcceptedNativeMainFxToEightWayShadowPcm
      COMMAND ARMixSpeakerPostFxMainWitnessTests)
  add_executable(ARMixSpeakerOutputReservationTests
      tests/lms_native_output_reservation_test.cpp
      engine/lms/NativeSpeakerOutputReservation.h
      engine/lms/PreparedSpeakerSystem.h)
  target_compile_features(ARMixSpeakerOutputReservationTests PRIVATE cxx_std_20)
  add_test(NAME LMSNativeAcceptedChannelIdExclusiveAsioOutputGuard
      COMMAND ARMixSpeakerOutputReservationTests)
  add_executable(ARMixSpeakerRealtimeTests tests/lms_synthetic_realtime_budget_test.cpp
      engine/lms/PreparedSpeakerSystem.h)
  target_compile_features(ARMixSpeakerRealtimeTests PRIVATE cxx_std_20)
  add_test(NAME LMSNativeFourWayStereoZeroCallbackHeap
      COMMAND ARMixSpeakerRealtimeTests)
  # LMS M6 JUCE WebView control ABI tests: native schema only, no ASIO
  # endpoint/driver opened, no second audio callback or render.
  juce_add_console_app(ARMixSpeakerWebIntentTests PRODUCT_NAME "ArSonMix LMS Offline WebView Native Contract")
  target_sources(ARMixSpeakerWebIntentTests PRIVATE tests/lms_juce_web_intent_test.cpp
      src/NativeLmsWebIntent.h)
  target_compile_features(ARMixSpeakerWebIntentTests PRIVATE cxx_std_20)
  target_link_libraries(ARMixSpeakerWebIntentTests PRIVATE
      juce::juce_core juce::juce_recommended_config_flags)
  add_test(NAME LMSNativeJuceIntentSchemaAndDisconnectedReadback
      COMMAND ARMixSpeakerWebIntentTests)
  juce_add_console_app(ARMixFactoryFxRegistryTests PRODUCT_NAME "ArSonMix Factory FX Integrity Tests")
  target_sources(ARMixFactoryFxRegistryTests PRIVATE tests/factory_fx_registry_test.cpp src/FactoryFxRegistry.h)
  target_link_libraries(ARMixFactoryFxRegistryTests PRIVATE juce::juce_core juce::juce_cryptography juce::juce_recommended_config_flags)
  target_compile_features(ARMixFactoryFxRegistryTests PRIVATE cxx_std_20)
  add_test(NAME FactoryFxVerifiedManifestAndNegativePathGate COMMAND ARMixFactoryFxRegistryTests)
  # M4: fixture processor goes through the SAME native FX descriptor path.
  # Remains part of the existing Windows native mixer qualification build.
  juce_add_console_app(ARMixNativeFxExtensionTests PRODUCT_NAME "ArSonMix Native FX Host Extension Tests")
  target_sources(ARMixNativeFxExtensionTests PRIVATE
      tests/native_fx_registry_extension_test.cpp src/NativeBuiltInFx.h)
  target_link_libraries(ARMixNativeFxExtensionTests PRIVATE
      juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixNativeFxExtensionTests PRIVATE cxx_std_20)
  add_test(NAME M4NativeFxCatalogExtensionAndStatePreparation
      COMMAND ARMixNativeFxExtensionTests)
  # One user-owned, effect-scoped library; not .smix or FX-chain .armix.
  # File IO is forbidden in the realtime PCM callback.
  juce_add_console_app(ARMixBuiltInFxPresetStoreTests PRODUCT_NAME "ArSonMix Native Built-in FX User Preset Store")
  target_sources(ARMixBuiltInFxPresetStoreTests PRIVATE
      tests/builtin_fx_user_preset_store_test.cpp src/BuiltInFxUserPresetStore.h)
  target_link_libraries(ARMixBuiltInFxPresetStoreTests PRIVATE
      juce::juce_core juce::juce_cryptography juce::juce_recommended_config_flags)
  target_compile_features(ARMixBuiltInFxPresetStoreTests PRIVATE cxx_std_20)
  add_test(NAME BuiltInFxUserPresetNativePortableSaveAndRecovery COMMAND ARMixBuiltInFxPresetStoreTests)
  # Real JUCE adapters for all four built-in FX: capture ONLY accepted
  # settings, validate portable types and re-open .armixfx in another store.
  # Never serialize from a published callback or an offline browser fake.
  juce_add_console_app(ARMixBuiltInFxAcceptedSnapshotTests PRODUCT_NAME
      "ArSonMix Native Built-in FX Accepted Preset Snapshot Tests")
  target_sources(ARMixBuiltInFxAcceptedSnapshotTests PRIVATE
      tests/builtin_fx_accepted_snapshot_test.cpp
      src/BuiltInFxAcceptedSnapshot.h)
  target_link_libraries(ARMixBuiltInFxAcceptedSnapshotTests PRIVATE
      juce::juce_audio_processors juce::juce_cryptography
      juce::juce_recommended_config_flags)
  target_compile_features(ARMixBuiltInFxAcceptedSnapshotTests PRIVATE cxx_std_20)
  add_test(NAME BuiltInFxUserPresetFourRealJuceAdaptersAndColdReopen
      COMMAND ARMixBuiltInFxAcceptedSnapshotTests)

  # Native 8D-SonKu non-JUCE measured-HRIR PCM and deterministic orbit fixture.
  add_executable(ARMixSonKuCoreTests tests/sonku_spatial_core_test.cpp)
  target_compile_features(ARMixSonKuCoreTests PRIVATE cxx_std_20)
  add_test(NAME NativeSonKuMeasuredBinauralDirectionalAndChunkParity COMMAND ARMixSonKuCoreTests)
  # Real JUCE processor: audio-applied ACK, v1/v2 state, corruption fences.
  juce_add_console_app(ARMixSonKuNativeStateTests PRODUCT_NAME "ArSonMix SonKu Native State Tests")
  target_sources(ARMixSonKuNativeStateTests PRIVATE tests/sonku_native_state_test.cpp)
  target_link_libraries(ARMixSonKuNativeStateTests PRIVATE
      juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixSonKuNativeStateTests PRIVATE cxx_std_20)
  add_test(NAME NativeSonKuSessionV1V2AndAudioAppliedAck
      COMMAND ARMixSonKuNativeStateTests)
  # Pure control-plane first-run routing selection: no JUCE, drivers,
  # WASAPI activation or callback dependencies. Full Windows CTest gate.
  add_executable(ARMixFirstRunEndpointPolicyTests
      tests/first_run_endpoint_policy_test.cpp
      engine/device/FirstRunEndpointPolicy.h)
  target_compile_features(ARMixFirstRunEndpointPolicyTests PRIVATE cxx_std_20)
  add_test(NAME NativeFirstRunVBCAblePhysicalOutputIdentityAndASIOPreservation
      COMMAND ARMixFirstRunEndpointPolicyTests)
  if(WIN32)
    add_executable(ARMixWindowsFirstRunReadOnlyTests
        tests/windows_first_run_endpoint_discovery_test.cpp
        engine/device/WindowsFirstRunEndpointDiscovery.h
        engine/device/WindowsAudioPropertyKeys.cpp)
    target_compile_features(ARMixWindowsFirstRunReadOnlyTests PRIVATE cxx_std_20)
    target_link_libraries(ARMixWindowsFirstRunReadOnlyTests PRIVATE ole32 uuid)
    add_test(NAME NativeWindowsFirstRunReadOnlyMMDeviceIdentityNoGuess
        COMMAND ARMixWindowsFirstRunReadOnlyTests)
  endif()
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
  # Explicit FX lease + ChannelId PCM regression: continuous Harmony DSP on
  # disabled insert/rack, exact dry PDC and state-identical wet reactivation.
  juce_add_console_app(ARMixHarmonyBypassPcmTests PRODUCT_NAME "ArSonMix Harmony Clickless Bypass PCM")
  target_sources(ARMixHarmonyBypassPcmTests PRIVATE tests/harmony_bypass_reliability_test.cpp)
  target_link_libraries(ARMixHarmonyBypassPcmTests PRIVATE
      juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixHarmonyBypassPcmTests PRIVATE cxx_std_20)
  add_test(NAME NativeHarmonyWarmBypassRackSlotAndLatencyParity COMMAND ARMixHarmonyBypassPcmTests)
  add_test(NAME ApprovedNativeHarmonyEditorAckAndSingleGui COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_harmony_assembly_test.py")
  add_executable(ARMixMixerRoutingTests tests/mixer_routing_plan_test.cpp)
  target_compile_features(ARMixMixerRoutingTests PRIVATE cxx_std_20)
  add_dependencies(ARMixMixerRoutingTests ARMixHarmonyDspTests ARMixHarmonyMusicGuideTests ARMixHarmonyChannelPcmTests ARMixHarmonyBypassPcmTests ARMixHarmonyCpuProfileTests)
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
  # O1/O2: integrated deterministic PCM on the existing Windows FAT CTest.
  # No additional workflow job or duplicate native build.
  add_executable(ARMixPhysicalExternalOutputTests tests/physical_external_output_pcm_test.cpp)
  target_compile_features(ARMixPhysicalExternalOutputTests PRIVATE cxx_std_20)
  add_test(NAME O1PhysicalExternalOutputNativePcmAndFailClosedRouting
           COMMAND ARMixPhysicalExternalOutputTests)
  add_dependencies(ARMixMixerRoutingTests ARMixPhysicalExternalOutputTests)
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
  add_dependencies(ARMixMixerRoutingTests ARMixNativeFxExtensionTests)
  juce_add_console_app(ARMixApprovedRoutingPcmTests PRODUCT_NAME "ArSonMix Approved WIRE PCM Tests")
  target_sources(ARMixApprovedRoutingPcmTests PRIVATE tests/approved_native_routing_pcm_test.cpp)
  target_link_libraries(ARMixApprovedRoutingPcmTests PRIVATE juce::juce_core juce::juce_recommended_config_flags)
  target_compile_features(ARMixApprovedRoutingPcmTests PRIVATE cxx_std_20)
  add_test(NAME P0ApprovedDecoderChannelToChannelAndSourceOnlyPCM COMMAND ARMixApprovedRoutingPcmTests)
  # M3a: actual ChannelId vs M4 production blocks before compatibility retirement.
  add_executable(ARMixProductionRenderAuditTests tests/production_render_audit_test.cpp)
  target_compile_features(ARMixProductionRenderAuditTests PRIVATE cxx_std_20)
  add_test(NAME M3ActualProductionRenderPathAndFailSilentCounters COMMAND ARMixProductionRenderAuditTests)
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
  # M2 control-thread device Apply owner: UI preflight, exact pin identity,
  # nested modal reentry, accepted readback and fail-closed rollback.
  juce_add_console_app(ARMixDeviceApplyCoordinatorTests PRODUCT_NAME "ArSonMix Device Apply Coordinator Tests")
  target_sources(ARMixDeviceApplyCoordinatorTests PRIVATE
      tests/native_device_apply_coordinator_test.cpp src/NativeDeviceApplyCoordinator.h)
  target_link_libraries(ARMixDeviceApplyCoordinatorTests PRIVATE
      juce::juce_audio_devices juce::juce_recommended_config_flags)
  target_compile_features(ARMixDeviceApplyCoordinatorTests PRIVATE cxx_std_20)
  add_test(NAME NativeDeviceApplyPinValidationAndRollbackEvidence COMMAND ARMixDeviceApplyCoordinatorTests)
  juce_add_console_app(ARMixVst3StartupSafetyTests PRODUCT_NAME "ArSonMix Waves VST3 Identity and Recovery Tests")
  target_sources(ARMixVst3StartupSafetyTests PRIVATE
      tests/vst3_waves_identity_startup_test.cpp
      src/Vst3DescriptionCache.h src/StartupVst3RecoveryGuard.h)
  target_link_libraries(ARMixVst3StartupSafetyTests PRIVATE
      juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixVst3StartupSafetyTests PRIVATE cxx_std_20)
  add_test(NAME WavesVst3IdentityAndStartupCrashRecovery COMMAND ARMixVst3StartupSafetyTests)
  add_test(NAME Vst3WaveShellHostContextOrderingFixture
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_waveshell_juce_patch.py")
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
  target_compile_definitions(ARMixDurableMixerSessionTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
  add_test(NAME P0DynamicMixerSessionDocumentAndCrashSafeRecovery COMMAND ARMixDurableMixerSessionTests)
  # H1 OS-global shortcut registration and stable identity are control-only.
  juce_add_console_app(ARMixNativeHotkeyTests PRODUCT_NAME "ArSonMix Native Hotkey Registry Tests")
  target_sources(ARMixNativeHotkeyTests PRIVATE tests/native_hotkey_registry_test.cpp
      src/NativeHotkeyRegistry.h)
  target_link_libraries(ARMixNativeHotkeyTests PRIVATE
      juce::juce_core juce::juce_recommended_config_flags)
  if(WIN32)
    target_link_libraries(ARMixNativeHotkeyTests PRIVATE user32 comctl32)
  endif()
  target_compile_features(ARMixNativeHotkeyTests PRIVATE cxx_std_20)
  add_test(NAME H1NativeGlobalShortcutRegistrationConflictAndPersistence
      COMMAND ARMixNativeHotkeyTests)
  # H2 hosted VST3 ParamID remapping / automation flags; no hardware needed.
  juce_add_console_app(ARMixFxParamHotkeyTests PRODUCT_NAME "ArSonMix FX Parameter Hotkey Tests")
  target_sources(ARMixFxParamHotkeyTests PRIVATE
      tests/native_fx_parameter_hotkey_test.cpp src/NativeFxParameterHotkey.h)
  target_link_libraries(ARMixFxParamHotkeyTests PRIVATE
      juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixFxParamHotkeyTests PRIVATE cxx_std_20)
  add_test(NAME H2HostedParamIdStableAndAutomationSafe COMMAND ARMixFxParamHotkeyTests)

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

  add_executable(ARMixNativeVocalThrowTests tests/builtin_vocal_throw_test.cpp
      engine/builtin_fx/VocalThrow.h)
  target_compile_features(ARMixNativeVocalThrowTests PRIVATE cxx_std_20)
  add_test(NAME NativeVocalThrowPhraseCaptureManualAutoAndNoHeap COMMAND ARMixNativeVocalThrowTests)
  add_dependencies(ARMixMixerRoutingTests ARMixNativeVocalThrowTests)
  # A stopped ASIO callback must not execute old, UI-rejected THROW cues
  # on resume. Exercise the real JUCE processor and its SPSC completion.
  juce_add_console_app(ARMixVocalThrowCancelTests PRODUCT_NAME
      "ArSonMix Vocal Delay Expired Cue Safety Tests")
  target_sources(ARMixVocalThrowCancelTests PRIVATE
      tests/native_vocal_throw_cancel_test.cpp
      engine/builtin_fx/VocalThrowProcessor.h)
  target_link_libraries(ARMixVocalThrowCancelTests PRIVATE
      juce::juce_audio_processors juce::juce_recommended_config_flags)
  target_compile_features(ARMixVocalThrowCancelTests PRIVATE cxx_std_20)
  add_test(NAME NativeVocalThrowExpiredCueCancellation COMMAND ARMixVocalThrowCancelTests)
  add_dependencies(ARMixMixerRoutingTests ARMixVocalThrowCancelTests)
  add_executable(ARMixNativeMusicTempoTests tests/native_music_tempo_test.cpp
      engine/core/NativeMusicTempoMonitor.h)
  target_compile_features(ARMixNativeMusicTempoTests PRIVATE cxx_std_20)
  add_test(NAME NativeMusicTempoSourceOnsetsAndFallback COMMAND ARMixNativeMusicTempoTests)
  add_dependencies(ARMixMixerRoutingTests ARMixNativeMusicTempoTests)


  add_executable(ARMixNativeMasterLoudnessTests tests/native_master_loudness_test.cpp engine/dsp/NativeMasterLoudness.h)
  target_compile_features(ARMixNativeMasterLoudnessTests PRIVATE cxx_std_20)
  add_test(NAME P1KMeasuredNativeBS1770StereoAnd4xTruePeak COMMAND ARMixNativeMasterLoudnessTests)

endif()
