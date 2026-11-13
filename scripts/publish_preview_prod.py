"""Install a pinned PRE-v7 ArSonMix GUI into the public Pages archive route.

The *source* is built separately from the last approved commit before the v7 mixer key redesign.
This script never compiles that historical GUI with current CSS/JS: it only
copies a previously validated old /preview/ directory into /preview-prod/.
"""
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

PINNED_SHA = "09022ef0f227ca2a40a0c2f62c619a31395c8dac"
ARCHIVE_ROUTE = "/preview-prod/"
OLD_SPRITE = "mute-solo-pfl-56x37.png"
NEW_SPRITE = "mixer-keys-thin-flat-v9-approved-off-shadow-57x30.png"


def publish(frozen_preview: Path, pages_root: Path) -> Path:
    frozen_preview = frozen_preview.resolve(strict=True)
    pages_root = pages_root.resolve(strict=True)
    live = pages_root / "preview"
    dest = pages_root / "preview-prod"

    source_manifest = json.loads((frozen_preview / "build.json").read_text("utf-8"))
    if (source_manifest.get("sha") != PINNED_SHA or
            source_manifest.get("canonical_url") != "/preview/" or
            source_manifest.get("kind") != "APPROVED_SINGLE_GUI"):
        raise ValueError("Historical source is not exact pre-v7 approved /preview/")
    old_html = (frozen_preview / "index.html").read_text("utf-8")
    if ("console-body" not in old_html or 'data-hw-kind="cue"' not in old_html or
            'data-hw-kind="tap"' in old_html):
        raise ValueError("Historical PRE-v7 UI DOM is not the expected MUTE/SOLO/PFL version")
    if not (frozen_preview / "assets" / OLD_SPRITE).is_file():
        raise ValueError("Historical MUTE/SOLO/PFL PNG atlas missing")
    if any((frozen_preview / "assets").glob("*-thin-flat-v[789]-*.png")):
        raise ValueError("New mixer key sprites leaked into pre-v7 snapshot")
    if not (live / "index.html").is_file():
        raise ValueError("Canonical latest /preview/ must be built independently first")
    if not (live / "assets" / NEW_SPRITE).is_file():
        raise ValueError("Canonical latest /preview/ PNG asset is missing")

    if dest.exists():
        shutil.rmtree(dest)
    # Preserve every previous asset byte and browser-relative reference.
    shutil.copytree(frozen_preview, dest)
    source_manifest.update({
        "kind": "FROZEN_PRE_V7_GUI",
        "canonical_url": ARCHIVE_ROUTE,
        "archived_source_sha": PINNED_SHA,
        "frozen": True,
        "live_audio": False,
    })
    (dest / "build.json").write_text(
        json.dumps(source_manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if (dest / "index.html").read_bytes() != (frozen_preview / "index.html").read_bytes():
        raise AssertionError("Archived HTML changed during copy")
    return dest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old-preview", required=True, type=Path)
    parser.add_argument("--pages-root", required=True, type=Path)
    options = parser.parse_args()
    target = publish(options.old_preview, options.pages_root)
    print(f"PASS: published frozen {ARCHIVE_ROUTE} from {PINNED_SHA}: {target}")
