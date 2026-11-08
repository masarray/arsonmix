#!/usr/bin/env bash
# FREE ad-hoc signature, unnotarized macOS native test DMG.
set -euo pipefail
expected="${1:?expected SHA}" tag="${2:?prerelease tag}" out="${3:-dist-macos}"
[[ "$expected" =~ ^[0-9a-f]{40}$ && "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$ ]] || exit 2
[[ "$(uname -s)" == "Darwin" ]] || { echo "macOS runner required"; exit 2; }
[[ "$(git rev-parse HEAD)" == "$expected" ]] || { echo "Source SHA mismatch"; exit 2; }
case "$(uname -m)" in x86_64) arch=x64;; arm64) arch=arm64;; *) echo "Unsupported Mac architecture"; exit 2;; esac
app=""
for candidate in "build/ARMixConsole_artefacts/Release/AR MIX Studio.app" "build/ARMixConsole_artefacts/AR MIX Studio.app"; do
  [[ ! -d "$candidate" ]] || { app="$candidate"; break; }
done
[[ -n "$app" && -f "$app/Contents/Info.plist" ]] || { echo "Exact JUCE GUI app missing"; exit 2; }
plutil -lint "$app/Contents/Info.plist" >/dev/null
bundle_exe="$(/usr/libexec/PlistBuddy -c 'Print CFBundleExecutable' "$app/Contents/Info.plist")"
[[ -f "$app/Contents/MacOS/$bundle_exe" ]] || { echo "Mac bundle executable missing"; exit 2; }
[[ "$(stat -f %z "$app/Contents/MacOS/$bundle_exe")" -ge 1048576 ]] || { echo "Native GUI too small"; exit 2; }
file -b "$app/Contents/MacOS/$bundle_exe" | grep -q 'Mach-O 64-bit' || { echo "Not Mach-O app"; exit 2; }
mkdir -p "$out"; out="$(cd "$out" && pwd)"
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/ArSonMix"
cp -R "$app" "$tmp/ArSonMix/ArSonMix.app"
cp LICENSE-NOTICE.txt "$tmp/ArSonMix/LICENSE-NOTICE.txt"
cat > "$tmp/ArSonMix/README-TEST.txt" <<'INFO'
ArSonMix native macOS alpha; COMMUNITY TEST / NO PAID SIGNING.
This app has free ad-hoc code signing, not Apple Developer ID/notarization.
Drag ArSonMix.app to Applications; verify the published SHA256 checksum.
macOS Gatekeeper may block first launch. Try opening the app, then use
System Settings > Privacy & Security > Open Anyway only if you trust it.
NEVER disable Gatekeeper globally. Audio uses CoreAudio, not ASIO/WASAPI.
FactoryFX and licensed third-party VST3 are not bundled.
Back up Shows. Not certified for critical live performance.
INFO
ln -s /Applications "$tmp/ArSonMix/Applications"
codesign --force --deep --sign - --timestamp=none "$tmp/ArSonMix/ArSonMix.app"
codesign --verify --deep --strict "$tmp/ArSonMix/ArSonMix.app"
asset="ArSonMix-${tag}-macOS-${arch}.dmg"
hdiutil create -quiet -ov -format UDZO -fs HFS+ -volname "ArSonMix Test" -srcfolder "$tmp/ArSonMix" "$out/$asset"
hdiutil verify "$out/$asset" >/dev/null
printf '%s  %s\n' "$(shasum -a 256 "$out/$asset" | cut -d' ' -f1)" "$asset" > "$out/SHA256SUMS-macOS.txt"
EXPECTED="$expected" TAG="$tag" ARCH="$arch" ASSET="$asset" OUTPUT="$out" python3 - <<'PY'
import hashlib,json,os,pathlib
out=pathlib.Path(os.environ["OUTPUT"]); asset=out/os.environ["ASSET"]
data={"schemaVersion":1,"platform":"macos","architecture":os.environ["ARCH"],
      "sourceCommit":os.environ["EXPECTED"],"tag":os.environ["TAG"],
      "asset":asset.name,"assetSha256":hashlib.sha256(asset.read_bytes()).hexdigest(),
      "format":"native Mach-O .app in DMG","signing":"ad-hoc (free; NOT Developer ID)",
      "notarized":False,"guiAuthority":"/preview/",
      "audioBackend":"CoreAudio","hardwareAudioFAT":"PENDING"}
(out/"MACOS_PROVENANCE.json").write_text(json.dumps(data,indent=2,sort_keys=True)+"\n")
PY
echo "ARSONMIX_MACOS_NATIVE_PACKAGE_VERIFIED=$asset SHA=$expected"
