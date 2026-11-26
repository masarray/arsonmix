#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
# No JUCE, no compiler flags leak into production. Fail on first finding.
set -euo pipefail
mode="${1:-asan}"
cxx="${CXX:-clang++}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
common=(-std=c++20 -O1 -g -fno-omit-frame-pointer -pthread)
case "$mode" in
  asan)
    flags=(-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all)
    export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
    export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
    tests=(native_memory_safety_test live_port_epoch_publisher_test live_fx_structural_handoff_test realtime_control_test adaptive_clock_bridge_test secondary_capture_clock_stream_test)
    ;;
  tsan)
    flags=(-fsanitize=thread -fno-sanitize-recover=all)
    export TSAN_OPTIONS=halt_on_error=1:exitcode=66
    tests=(native_memory_safety_test live_port_epoch_publisher_test realtime_control_test)
    ;;
  *) echo 'Expected asan or tsan' >&2; exit 2;;
esac
"$cxx" --version
for name in "${tests[@]}"; do
    echo "ZBSH07 $mode $name"
    "$cxx" "${common[@]}" "${flags[@]}" "tests/$name.cpp" -o "$work/$name"
    timeout 120 "$work/$name"
done
echo "ZBSH07_SANITIZER_PASS $mode ${#tests[@]} tests"
