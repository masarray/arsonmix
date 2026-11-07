#!/usr/bin/env python3
"""Copy the verified product landing assets, without touching /preview/ build."""
from pathlib import Path
import shutil
import sys
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
source_media=ROOT/"site/media"
target_media=OUT/"media"
if target_media.exists(): shutil.rmtree(target_media)
target_media.mkdir()
for name in ("console-overview.jpg","console-routing.jpg","console-faders.jpg"):
    source=source_media/name
    if not source.is_file() or source.stat().st_size<12000:
        raise SystemExit("Missing/invalid official GUI screenshot "+name)
    shutil.copyfile(source,target_media/name)
print("ARSONMIX_LANDING_ASSETS_STAGED",sum(p.stat().st_size for p in target_media.iterdir()))
