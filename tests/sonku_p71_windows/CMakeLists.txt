cmake_minimum_required(VERSION 3.22)
project(SonKuP71FocusedNative LANGUAGES CXX)
# Independent Windows native C++ quality gate; no JUCE SDK/installer,
# WebView2 NuGet, full ArSonMix target or unrelated plugin dependencies.
enable_testing()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(SonKuP71XYZSource "${ROOT}/tests/sonku_p71_xyz_direct_test.cpp")
target_compile_definitions(SonKuP71XYZSource PRIVATE
  SONKU_P7_MEASURED_ELEVATION_RESEARCH=1
  SONKU_P71_DIRECT_XYZ_RESEARCH=1)
add_test(NAME SonKuP71TrueXYZNativeWin COMMAND SonKuP71XYZSource)
add_executable(SonKuP6DefaultRegression "${ROOT}/tests/sonku_p6_realtime_quality_test.cpp")
add_test(NAME SonKuP6NativeDefaultWin COMMAND SonKuP6DefaultRegression)
