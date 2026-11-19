# IO-2 standalone native, no-JUCE / no-hardware CTest project.
# Deliberately NOT editing the shared root CMakeLists.txt while N-MOD PR #799
# owns Player native build/test wiring. Root integration is a later one-owner
# application seam after exact HEAD reconciliation.
cmake_minimum_required(VERSION 3.22)
project(ARMixIO2QualifiedInputMap LANGUAGES CXX)
include(CTest)
add_executable(ARMixIO2QualifiedInputMapTests
    ../multi_device_input_plan_test.cpp)
target_compile_features(ARMixIO2QualifiedInputMapTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(ARMixIO2QualifiedInputMapTests PRIVATE /W4 /WX)
else()
    target_compile_options(ARMixIO2QualifiedInputMapTests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME ArSonMixIO2QualifiedSourceIdentityAndPCMLease
    COMMAND ARMixIO2QualifiedInputMapTests)
