#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Normalize ONLY the six approved photo venues to a 2200x922 concert viewport.

No artificial speaker sprites, no distortion, no black padding, no synthesis.
Original photography remains recoverable from Git history. Idempotent operation.
"""
from __future__ import annotations
import argparse
from pathlib import Path
from PIL import Image

CANVAS=(2200,922)
SCENE_TOP={
    "scene_live_concert.png":0.50,
    "scene_royal_concert_hall.png":0.20,
    "scene_arena_thunder.png":0.17,
    "scene_front_row_power.png":0.14,
    "scene_symphonic_palace.png":0.20,
    "scene_velvet_cinema.png":0.50,
}

def fit_photo(image: Image.Image, name: str) -> tuple[Image.Image,tuple[int,int,int,int]]:
    original=image.convert("RGB")
    sw,sh=original.size
    tw,th=CANVAS
    if sw*th>=sh*tw:
        cw,ch=max(1,sh*tw//th),sh
    else:
        cw,ch=sw,max(1,sw*th//tw)
    x0=(sw-cw)//2
    y0=round((sh-ch)*SCENE_TOP[name])
    y0=max(0,min(sh-ch,y0))
    crop_rect=(x0,y0,x0+cw,y0+ch)
    # LANCZOS is only resampling; never stretch independent X/Y geometries.
    return original.crop(crop_rect).resize(CANVAS,Image.Resampling.LANCZOS),crop_rect

def main()->int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--directory",type=Path,default=Path("assets/imersip/scenes"))
    parser.add_argument("--check",action="store_true")
    args=parser.parse_args()
    observed={p.name for p in args.directory.glob("*.png")}
    expected=set(SCENE_TOP)
    if observed!=expected:
        raise SystemExit(f"Unexpected photo files: missing={expected-observed} extra={observed-expected}")
    failures=[]
    for name in SCENE_TOP:
        path=args.directory/name
        with Image.open(path) as original:
            original.load()
            if original.size==CANVAS and original.mode in ("RGB","RGBA"):
                print(f"PASS {name}: {original.size} {original.mode}");continue
            if args.check:
                failures.append(f"{name}: {original.size} {original.mode}")
                continue
            image,box=fit_photo(original,name)
            print(f"NORMALIZE {name}: {original.size} -> {image.size} source_crop={box}")
            image.save(path,"PNG",optimize=True,compress_level=7)
    if failures:
        raise SystemExit("PANORAMA CONTRACT FAIL:\n"+"\n".join(failures))
    return 0
if __name__=="__main__":
    raise SystemExit(main())
