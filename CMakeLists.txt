cmake_minimum_required(VERSION 3.22)
project(ARMixConsole VERSION 0.5.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT DEFINED JUCE_ROOT)
  message(FATAL_ERROR "Pass -DJUCE_ROOT=C:/path/to/JUCE (8.0.15+ required)")
endif()
add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce")

juce_add_gui_app(ARMixConsole
    PRODUCT_NAME "AR MIX Studio"
    COMPANY_NAME "AR MIX Open Source"
    BUNDLE_ID "org.armix.studio"
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    NEEDS_CURL FALSE)

juce_add_binary_data(ARMixAssets SOURCES
    web/index.html web/styles.css web/app.js web/bridge.js web/monitor.js)

target_sources(ARMixConsole PRIVATE src/Main.cpp src/LiveAudioEngine.h src/PluginCatalog.h src/DiagnosticsRecorder.h src/StereoPan.h src/NativePresetArchive.h src/VstBusLayout.h src/LiveGraphHandoff.h src/DiagnosticTone.h engine/core/RealtimeHealthHistogram.h engine/device/AdaptiveClockBridge.h engine/device/AudioProfilePolicy.h engine/device/SessionLifecycle.h engine/device/WasapiCapabilityProbe.h engine/device/JuceIndependentClockDevice.h engine/device/ClockBridgeSizing.h engine/device/DirectRestoreBackoff.h)
target_compile_definitions(ARMixConsole PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_CURL=0
    JUCE_ASIO=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_APPLICATION_NAME_STRING="AR MIX Studio"
    JUCE_APPLICATION_VERSION_STRING="0.5.0-p2")
if(WIN32)
  target_link_libraries(ARMixConsole PRIVATE ole32 uuid)
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
  add_executable(ARMixRecoveryTests tests/direct_restore_backoff_test.cpp)
  target_compile_features(ARMixRecoveryTests PRIVATE cxx_std_20)
  add_test(NAME BoundedDirectDeviceRecovery COMMAND ARMixRecoveryTests)
  add_executable(ARMixClockSizingTests tests/clock_bridge_sizing_test.cpp)
  target_compile_features(ARMixClockSizingTests PRIVATE cxx_std_20)
  add_test(NAME AdaptiveClockPeriodHeadroom COMMAND ARMixClockSizingTests)
  if(WIN32)
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
  endif()
  add_executable(ARMixLifecycleTests tests/session_lifecycle_test.cpp)
  target_compile_features(ARMixLifecycleTests PRIVATE cxx_std_20)
  add_test(NAME DeviceSessionLifecycle COMMAND ARMixLifecycleTests)
  add_executable(ARMixPolicyTests tests/audio_profile_policy_test.cpp)
  target_compile_features(ARMixPolicyTests PRIVATE cxx_std_20)
  add_test(NAME CapabilityAudioProfilePolicy COMMAND ARMixPolicyTests)
  add_executable(ARMixClockBridgeTests tests/adaptive_clock_bridge_test.cpp)
  target_compile_features(ARMixClockBridgeTests PRIVATE cxx_std_20)
  add_test(NAME AdaptiveDualClockBridge COMMAND ARMixClockBridgeTests)
  add_executable(ARMixHealthTests tests/realtime_health_histogram_test.cpp)
  target_compile_features(ARMixHealthTests PRIVATE cxx_std_20)
  add_test(NAME RealtimeHealthHistogram COMMAND ARMixHealthTests)
  add_executable(ARMixDiagnosticTests tests/diagnostic_tone_test.cpp)
  target_compile_features(ARMixDiagnosticTests PRIVATE cxx_std_20)
  add_test(NAME P0DiagnosticTone COMMAND ARMixDiagnosticTests)
  add_executable(ARMixRTTests tests/realtime_control_test.cpp)
  target_compile_features(ARMixRTTests PRIVATE cxx_std_20)
  add_test(NAME RealtimeControlPrimitives COMMAND ARMixRTTests)
  juce_add_console_app(ARMixPresetTests PRODUCT_NAME "ArSonMix Native Preset Tests")
  target_sources(ARMixPresetTests PRIVATE tests/native_preset_roundtrip_test.cpp src/NativePresetArchive.h)
  target_link_libraries(ARMixPresetTests PRIVATE juce::juce_core juce::juce_cryptography
      juce::juce_recommended_config_flags)
  target_compile_features(ARMixPresetTests PRIVATE cxx_std_20)
  add_test(NAME NativePresetZipRoundTrip COMMAND ARMixPresetTests)
endif()
