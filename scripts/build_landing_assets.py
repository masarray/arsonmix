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
print("ARSONMIX_LANDING_ASSETS_STAGED",sum(p.stat().st_size for p in target_media.iterdir()))
