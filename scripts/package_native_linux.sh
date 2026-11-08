#!/usr/bin/env bash
# Exact-source, unsigned Linux x86_64 release package: NO CTest binary is eligible.
set -euo pipefail
expected="${1:?expected 40-char SHA}" tag="${2:?prerelease tag}" out="${3:-dist-linux}"
[[ "$expected" =~ ^[0-9a-f]{40}$ && "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$ ]] || exit 2
[[ "$(uname -s)" == "Linux" && "$(uname -m)" == "x86_64" ]] || { echo "Linux x86_64 required"; exit 2; }
[[ "$(git rev-parse HEAD)" == "$expected" ]] || { echo "Source SHA mismatch"; exit 2; }
exe=""
for candidate in "build/ARMixConsole_artefacts/Release/AR MIX Studio" "build/ARMixConsole_artefacts/AR MIX Studio"; do
  [[ ! -f "$candidate" ]] || { exe="$candidate"; break; }
done
[[ -n "$exe" && -x "$exe" ]] || { echo "Exact JUCE GUI executable missing"; exit 2; }
[[ "$(stat -c %s "$exe")" -ge 1048576 ]] || { echo "Native GUI too small"; exit 2; }
file -b "$exe" | grep -Eq '^ELF 64-bit.*x86-64' || { echo "Not ELF x86_64"; exit 2; }
readelf -h "$exe" | grep -q 'Machine:.*Advanced Micro Devices X86-64'
deps="$(ldd "$exe")"
if printf '%s\n' "$deps" | grep -q 'not found'; then echo "$deps"; exit 2; fi
mkdir -p "$out"; out="$(cd "$out" && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/ArSonMix"
cp "$exe" "$tmp/ArSonMix/ArSonMix"
cp LICENSE-NOTICE.txt "$tmp/ArSonMix/LICENSE-NOTICE.txt"
cat > "$tmp/ArSonMix/README-TEST.txt" <<'INFO'
ArSonMix native Linux alpha; UNSIGNED OPEN-SOURCE TEST BUILD.
Extract and run ./ArSonMix (chmod +x ArSonMix if needed).
Target: Ubuntu 24.04 x86_64 with GTK3, WebKitGTK 4.1, ALSA and JACK.
Audio via actual ALSA/JACK device, not ASIO/WASAPI.
Exact approved embedded /preview/ GUI with native JUCE mixer; no extra GUI.
External VST3 and optional FactoryFX are NOT bundled.
Back up Shows. Not certified for critical live performance.
INFO
asset="ArSonMix-${tag}-Linux-x86_64.tar.gz"
tar -czf "$out/$asset" -C "$tmp" ArSonMix
printf '%s  %s\n' "$(sha256sum "$out/$asset" | cut -d' ' -f1)" "$asset" > "$out/SHA256SUMS-Linux.txt"
EXPECTED="$expected" TAG="$tag" EXE="$exe" ASSET="$asset" OUTPUT="$out" python3 - <<'PY'
import hashlib,json,os,pathlib
out=pathlib.Path(os.environ["OUTPUT"]); exe=pathlib.Path(os.environ["EXE"])
asset=out/os.environ["ASSET"]
data={"schemaVersion":1,"platform":"linux","architecture":"x86_64",
      "sourceCommit":os.environ["EXPECTED"],"tag":os.environ["TAG"],
      "asset":asset.name,"assetSha256":hashlib.sha256(asset.read_bytes()).hexdigest(),
      "executableSha256":hashlib.sha256(exe.read_bytes()).hexdigest(),
      "format":"ELF-64 in tar.gz","signing":"none",
      "runtime":"Ubuntu 24.04 x86_64 WebKitGTK 4.1/ALSA/JACK",
      "guiAuthority":"/preview/","hardwareAudioFAT":"PENDING"}
(out/"LINUX_PROVENANCE.json").write_text(json.dumps(data,indent=2,sort_keys=True)+"\n")
PY
echo "ARSONMIX_LINUX_NATIVE_PACKAGE_VERIFIED=$asset SHA=$expected"
