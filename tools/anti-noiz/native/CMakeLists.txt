# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.22)
project(AntiNoizNativeQualification VERSION 1.0.0 LANGUAGES C CXX)
include(CTest)
if(NOT DEFINED JUCE_ROOT)
  message(FATAL_ERROR "Pass pinned JUCE_ROOT")
endif()
add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce")
juce_add_console_app(AntiNoizNativeHostTests PRODUCT_NAME "Anti Noiz Native Host Tests")
target_sources(AntiNoizNativeHostTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../../tests/anti_noiz_native_host_test.cpp")
target_compile_features(AntiNoizNativeHostTests PRIVATE cxx_std_20)
target_compile_definitions(AntiNoizNativeHostTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=0)
# Registry instantiates all built-ins, including the pinned Reverb engine.
# Match the production dependency; do not mock or strip the real registry.
get_filename_component(ARMIX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
add_library(ARMixArVerbCore STATIC
    "${ARMIX_ROOT}/third_party/arsonverb/src/Reverb.cpp"
    "${ARMIX_ROOT}/third_party/arsonverb/src/Presets.cpp"
    "${ARMIX_ROOT}/third_party/arsonverb/src/arverb_c.cpp")
target_include_directories(ARMixArVerbCore PUBLIC "${ARMIX_ROOT}/third_party/arsonverb/include")
target_compile_features(ARMixArVerbCore PUBLIC cxx_std_17)
target_compile_definitions(ARMixArVerbCore PRIVATE ARVERB_BUILD_VERSION="1.0.1")
target_link_libraries(AntiNoizNativeHostTests PRIVATE ARMixArVerbCore juce::juce_audio_processors juce::juce_recommended_config_flags)
add_test(NAME AntiNoizNativeHost COMMAND AntiNoizNativeHostTests)
set_tests_properties(AntiNoizNativeHost PROPERTIES TIMEOUT 60)
