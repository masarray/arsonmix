# Optional JUCE-independent developer/CI gate for the built-in DSP module.
#   cmake -S engine/builtin_fx -B build/builtin_fx
#   cmake --build build/builtin_fx --config Release
#   ctest --test-dir build/builtin_fx -C Release --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(ArSonMixBuiltInFX LANGUAGES CXX)
enable_testing()
add_executable(ArSonMixBuiltInEQTests ../../tests/builtin_channel_eq_test.cpp)
target_compile_features(ArSonMixBuiltInEQTests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(ArSonMixBuiltInEQTests PRIVATE /W4)
else()
  target_compile_options(ArSonMixBuiltInEQTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME BuiltInChannelEqIndependentRealtimeDSP COMMAND ArSonMixBuiltInEQTests)

add_executable(ArSonMixBuiltInEQBankTests ../../tests/builtin_channel_eq_bank_test.cpp)
target_compile_features(ArSonMixBuiltInEQBankTests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(ArSonMixBuiltInEQBankTests PRIVATE /W4)
else()
  target_compile_options(ArSonMixBuiltInEQBankTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME BuiltInChannelEqFourteenChannelAudioOwnerAndAck COMMAND ArSonMixBuiltInEQBankTests)
