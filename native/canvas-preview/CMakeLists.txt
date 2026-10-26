cmake_minimum_required(VERSION 3.22)
project(ARMixNativeMixerCanvasPreview VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT DEFINED JUCE_ROOT)
    message(FATAL_ERROR "Pass -DJUCE_ROOT=/path/to/JUCE (8.0.15 or newer)")
endif()

# Independent preview; leaves production WebView2 and real-time audio untouched.
add_subdirectory("${JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce")

juce_add_gui_app(ARMixNativeMixerCanvasPreview
    PRODUCT_NAME "ArSonMix Native Canvas Preview"
    COMPANY_NAME "ArSonMix"
    BUNDLE_ID "org.armix.nativecanvaspreview")

target_sources(ARMixNativeMixerCanvasPreview PRIVATE
    main.cpp NativeMixerCanvas.h)
target_link_libraries(ARMixNativeMixerCanvasPreview PRIVATE
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
target_compile_definitions(ARMixNativeMixerCanvasPreview PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
