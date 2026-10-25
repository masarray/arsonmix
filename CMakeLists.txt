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

target_sources(ARMixConsole PRIVATE src/Main.cpp src/LiveAudioEngine.h src/PluginCatalog.h src/DiagnosticsRecorder.h)
target_compile_definitions(ARMixConsole PRIVATE
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2=1
    JUCE_USE_CURL=0
    JUCE_ASIO=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_APPLICATION_NAME_STRING="AR MIX Studio"
    JUCE_APPLICATION_VERSION_STRING="0.5.0-p2")
target_link_libraries(ARMixConsole PRIVATE
    ARMixAssets
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
  add_executable(ARMixRTTests tests/realtime_control_test.cpp)
  target_compile_features(ARMixRTTests PRIVATE cxx_std_20)
  add_test(NAME RealtimeControlPrimitives COMMAND ARMixRTTests)
endif()
