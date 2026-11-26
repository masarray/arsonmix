#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
cd "$(dirname "$0")/.."
juce_root="$(realpath "$1")"
cxx="${CXX:-clang++}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
flags=(-std=c++20 -O1 -g -pthread -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_USE_CURL=0 -I "$juce_root/modules")
for source in juce_core/juce_core.cpp juce_core/juce_core_CompilationTime.cpp juce_cryptography/juce_cryptography.cpp juce_events/juce_events.cpp; do
  "$cxx" "${flags[@]}" -fsanitize=fuzzer-no-link -c "$juce_root/modules/$source" -o "$work/$(basename "$source").o"
done
objects=("$work/"*.o)
for name in security_adversarial_input_test native_show_schema_test native_show_store_test native_preset_roundtrip_test portable_workspace_test runtime_vendor_crash_journal_test studio_backup_test; do
  "$cxx" "${flags[@]}" -fsanitize=fuzzer-no-link "tests/$name.cpp" "${objects[@]}" -ldl -lrt -o "$work/$name"
  timeout 45 "$work/$name"
  echo "ZBSH10_NATIVE_PASS $name"
done
"$cxx" "${flags[@]}" -fsanitize=fuzzer tests/security_show_codec_fuzz.cpp "${objects[@]}" -ldl -lrt -o "$work/fuzz"
mkdir "$work/corpus"
printf '%s' '{"schemaVersion":2,"name":"fixture","snapshotOrder":[]}' > "$work/corpus/show.json"
printf '%s' '{"schemaVersion":4,"strips":[],"aux":[],"masterGain":0.5}' > "$work/corpus/snapshot.json"
printf '%s' '{"a":[{},1,true,null,"text"],"b":"\uD83C\uDFB5"}' > "$work/corpus/json.json"
timeout 120 "$work/fuzz" "$work/corpus" -seed=61850 -runs=20000 -max_len=65536 -timeout=5 -rss_limit_mb=2048 -print_final_stats=1
echo 'ZBSH10_FUZZ_PASS 20000'
