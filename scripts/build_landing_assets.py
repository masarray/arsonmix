#!/usr/bin/env python3
"""Copy the verified product landing assets, without touching /preview/ build."""
from pathlib import Path
import shutil
import sys
import json
from hashlib import sha256
ROOT = Path(__file__).resolve().parents[1]
OUT = Path(sys.argv[1] if len(sys.argv)>1 else "dist-pages").resolve()
if not (OUT/"index.html").is_file() or not (OUT/"preview/index.html").is_file():
    raise SystemExit("Build approved Pages GUI before staging marketing assets")
if OUT == ROOT or OUT == ROOT/"site":
    raise SystemExit("Unsafe landing asset output path")
for name in ("landing.css","downloads.js"):
    source=ROOT/"site"/name
    if not source.is_file(): raise SystemExit("Missing landing asset "+name)
    shutil.copyfile(source,OUT/name)
# Serve the exact AGPL-3.0 source license locally. Marketing pages do not
# navigate to GitHub UI, and the distributed license must be byte-identical
# to the authoritative repository root LICENSE.
license_source=ROOT/"LICENSE"
if not license_source.is_file() or license_source.stat().st_size<1000:
    raise SystemExit("Missing authoritative root AGPL license")
shutil.copyfile(license_source,OUT/"LICENSE.txt")
source_media=ROOT/"site/media"
target_media=OUT/"media"
if target_media.exists(): shutil.rmtree(target_media)
target_media.mkdir()
mixer = ("console-overview.jpg","console-routing.jpg","console-faders.jpg")
editors = ("fx-editor-8d-sonku.jpg","fx-editor-vocal-delay.jpg",
           "fx-editor-harmonice.jpg","fx-editor-channel-eq.jpg",
           "fx-editor-compressor.jpg","fx-editor-graphic-eq31.jpg")
provenance_path = source_media / "fx-gallery-provenance.json"
if not provenance_path.is_file():
    raise SystemExit("Missing FX editor screenshot provenance manifest")
provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
if (provenance.get("schemaVersion") != 1 or provenance.get("captureMode") !=
        "approved-native-webview-editor-chromium-offline-fixture"):
    raise SystemExit("Unrecognized FX editor screenshot source")
if (not isinstance(provenance.get("sourceCommit"), str) or
        len(provenance["sourceCommit"]) != 40):
    raise SystemExit("FX capture is missing its exact source commit")
listed = {image.get("file"):image for image in provenance.get("images",[])
          if isinstance(image,dict)}
if len(listed) != len(editors) or set(listed) != set(editors):
    raise SystemExit("FX gallery manifest does not match six approved editors")
for name in (*mixer,*editors):
    source = source_media / name
    if (not source.is_file() or source.stat().st_size < 12000 or
            source.read_bytes()[:3] != b"\xff\xd8\xff"):
        raise SystemExit("Missing/invalid original GUI screenshot: " + name)
    if name in listed:
        record = listed[name]
        if (record.get("actualWindowsScreenshot") is not False or
                record.get("audioOrHardwareVerifiedByImage") is not False):
            raise SystemExit("FX screenshot may not claim hardware test evidence")
        if (record.get("sizeBytes") != source.stat().st_size or
                record.get("sha256") != sha256(source.read_bytes()).hexdigest()):
            raise SystemExit("FX editor screenshot checksum/size mismatch: " + name)
    shutil.copyfile(source,target_media/name)
shutil.copyfile(provenance_path,target_media / provenance_path.name)
# Separate provenance: six Chromium WebView resources above are NOT Windows
# screenshots. The seven media files below are owner-supplied genuine Windows
# native JUCE windows, but they do NOT prove a qualified release/DSP signal.
native_files = (
    "fx-editor-immersib-front-row-power.jpg",
    "fx-editor-immersib-symphonic-palace.jpg",
    "fx-editor-immersib-velvet-cinema.jpg",
    "fx-editor-immersib-arena-thunder.jpg",
    "fx-editor-immersib-royal-concert-hall.jpg",
    "fx-editor-immersib-concert-front-row-xl.jpg",
    "fx-editor-guitar-rig-clean-studio.jpg",
)
native_manifest_file = "native-fx-capture-provenance.json"
native_manifest_path = source_media / native_manifest_file
if not native_manifest_path.is_file():
    raise SystemExit("Missing native Windows screenshot provenance manifest")
native_manifest = json.loads(native_manifest_path.read_text(encoding="utf-8"))
if (native_manifest.get("schemaVersion") != 1 or
        native_manifest.get("captureMode") != "owner-provided-windows-native-juce-screenshots" or
        native_manifest.get("verifiedReleaseTag") is not None or
        native_manifest.get("verifiedWindowsBinarySha") is not None):
    raise SystemExit("Unrecognized or overclaimed native screenshot provenance")
records = native_manifest.get("images")
if not isinstance(records, list) or len(records) != len(native_files):
    raise SystemExit("Native screenshot manifest must contain exactly seven captures")
native_by_name = {r.get("file"):r for r in records if isinstance(r, dict)}
if len(native_by_name) != len(native_files) or set(native_by_name) != set(native_files):
    raise SystemExit("Native Windows screenshot filename list mismatch")
for name in native_files:
    original = source_media / name
    rec = native_by_name[name]
    expected_size = (1062, 672) if "guitar" in name else (937, 610)
    if (rec.get("width"), rec.get("height")) != expected_size:
        raise SystemExit("Native screenshot dimension claim mismatch: " + name)
    if (rec.get("actualWindowsScreenshot") is not True or
            rec.get("releasedInstallerVersionVerified") is not False or
            rec.get("audioOrHardwareVerifiedByImage") is not False):
        raise SystemExit("Native screenshot must not claim verified release or DSP FAT")
    if "guitar" in name and "DSP OFFLINE" not in rec.get("capturedUiState", ""):
        raise SystemExit("Guitar screenshot must report the visible DSP OFFLINE status")
    source_digest = rec.get("originalSha256")
    if (not isinstance(source_digest, str) or len(source_digest) != 64 or
            any(c not in "0123456789abcdef" for c in source_digest)):
        raise SystemExit("Missing original Windows PNG checksum: " + name)
    if (not original.is_file() or original.stat().st_size < 50000 or
            original.read_bytes()[:3] != b"\xff\xd8\xff"):
        raise SystemExit("Missing/invalid native screenshot JPEG: " + name)
    if (rec.get("sizeBytes") != original.stat().st_size or
            rec.get("sha256") != sha256(original.read_bytes()).hexdigest()):
        raise SystemExit("Native screenshot checksum/size mismatch: " + name)
    shutil.copyfile(original, target_media / name)
shutil.copyfile(native_manifest_path, target_media / native_manifest_file)
print("ARSONMIX_LANDING_ASSETS_STAGED",
      sum(p.stat().st_size for p in target_media.iterdir()))
