#!/usr/bin/env python3
"""Materialize approved ArSonMix icon package in Git, not just build output."""
from __future__ import annotations
import argparse
import filecmp
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/icon/brand-source-128.png"
ICON_DIR = ROOT / "assets/icon"
SITE_DIR = ROOT / "site"
SIZES = (16, 24, 32, 48, 64, 128, 180, 192, 256, 512, 1024)
WEB = ("favicon.ico", "favicon-16x16.png", "favicon-32x32.png",
       "apple-touch-icon.png", "android-chrome-192x192.png",
       "android-chrome-512x512.png", "site.webmanifest")
BUNDLE = tuple(f"arsonmix-{n}.png" for n in SIZES) + WEB

def run(check: bool) -> None:
    with tempfile.TemporaryDirectory(prefix="arsonmix-icon-sync-") as temp:
        stage = Path(temp)
        subprocess.run([sys.executable, str(ROOT / "scripts/generate_brand_icons.py"),
                        "--source", str(SOURCE), "--out", str(stage)], check=True)
        destinations = [(name, ICON_DIR / name) for name in BUNDLE]
        destinations += [("favicon.ico", ICON_DIR / "arsonmix-app.ico")]
        destinations += [(name, SITE_DIR / name) for name in WEB]
        problems = []
        for generated, destination in destinations:
            source = stage / generated
            if check:
                if not destination.is_file() or not filecmp.cmp(source, destination, shallow=False):
                    problems.append(str(destination.relative_to(ROOT)))
            else:
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
        if problems:
            raise SystemExit("Icon package missing or stale: " + ", ".join(problems))
        print("ARSONMIX_ICON_PACKAGE_" + ("VERIFIED" if check else "MATERIALIZED"),
              len(destinations), "artifacts")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    run(parser.parse_args().check)
