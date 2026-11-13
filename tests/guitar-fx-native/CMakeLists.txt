cmake_minimum_required(VERSION 3.22)
project(ARMixGuitarEditorCapture LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED JUCE_ROOT)
  message(FATAL_ERROR "Pass -DJUCE_ROOT=<pinned JUCE 8 source directory>")
endif()
# This is deliberately independent of the full ArSonMix mixer CMake. The test
# still compiles the EXACT production editor and processor header, without a
# second audio engine or GUI mockup.
set(JUCE_WEB_BROWSER 0)
set(JUCE_USE_CURL 0)
add_subdirectory("${JUCE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/juce")
set(ARMIX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
file(GLOB ARMIX_GUITAR_SKIN_PNGS CONFIGURE_DEPENDS
    "${ARMIX_ROOT}/design-references/guitar-fx/assets/gfx-*.png")
list(LENGTH ARMIX_GUITAR_SKIN_PNGS ARMIX_GUITAR_SKIN_COUNT)
if(NOT ARMIX_GUITAR_SKIN_COUNT EQUAL 11)
  message(FATAL_ERROR "Native proof needs EXACTLY the eleven shipped geometry-v2 PNGs; got ${ARMIX_GUITAR_SKIN_COUNT}")
endif()
list(SORT ARMIX_GUITAR_SKIN_PNGS)
juce_add_binary_data(ARMixGuitarCaptureSkins SOURCES ${ARMIX_GUITAR_SKIN_PNGS})
juce_add_console_app(ARMixGuitarEditorCapture
    PRODUCT_NAME "ArSonMix Guitar FX Native Screenshot")
target_sources(ARMixGuitarEditorCapture PRIVATE
    "${ARMIX_ROOT}/tests/guitar_fx_native_capture.cpp")
target_compile_features(ARMixGuitarEditorCapture PRIVATE cxx_std_20)
target_compile_definitions(ARMixGuitarEditorCapture PRIVATE
    ARMIX_GFX_EMBED_PNG_SKINS=1 JUCE_WEB_BROWSER=0
    JUCE_PLUGINHOST_VST3=0 JUCE_USE_CURL=0)
target_link_libraries(ARMixGuitarEditorCapture PRIVATE
    ARMixGuitarCaptureSkins juce::juce_audio_processors
    juce::juce_gui_extra juce::juce_recommended_config_flags)
if(MSVC)
  target_compile_options(ARMixGuitarEditorCapture PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
endif()
