#!/usr/bin/env python3
"""Prevent new cross-owner CSS declaration stacking in the ONE approved /preview/.

This gate deliberately freezes the current debt, not the entire CSS file.
Deleting a duplicate is allowed. Adding an overlapping selector+property in a
new file, restoring an old owner, or adding another late stylesheet is blocked.
The lightweight leaf parser is deterministic and intentionally conservative.
Complex media queries/selector equivalence still require browser computed-style
and visual tests; this is NOT a complete CSS parser or proof of zero specificity
conflicts.
"""
from __future__ import annotations

from collections import defaultdict
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "site/preview-v2/index.html"
BASELINE = ROOT / "contracts/approved-preview-css-owner-baseline.json"
LINK = re.compile(r'<link\s+rel="stylesheet"\s+href="assets/([^"?]+\.css)(?:\?[^"]*)?"')
LEAF = re.compile(r"([^{}]+)\{([^{}]*)\}")
PROP = re.compile(r"(?:^|;)\s*([-\w]+)\s*:")
COMMENT = re.compile(r"/\*.*?\*/", re.S)


def stylesheet_names(html: str) -> list[str]:
    if re.search(r"<style(?:\s|>)", html, re.I):
        raise ValueError("Approved GUI must not regain late inline CSS override blocks")
    names = LINK.findall(html)
    if not names or len(names) != len(set(names)):
        raise ValueError("Duplicate or missing stylesheet links in approved GUI")
    return names


def source_path(name: str) -> Path:
    if name == "mixer-control-keys.css":
        # The approved hardware PNG buttons own this CSS, NOT base preview-v2.
        return ROOT / "site/preview-buttons/assets" / name
    return ROOT / "site/preview-v2/assets" / name


def declarations(css: str) -> set[str]:
    clean = COMMENT.sub("", css)
    found: set[str] = set()
    for match in LEAF.finditer(clean):
        selector = " ".join(match.group(1).split())
        if not selector or selector.startswith("@"):
            continue
        for property_match in PROP.finditer(match.group(2)):
            found.add(selector + "||" + property_match.group(1))
    return found


def inspect() -> tuple[list[str], dict[str, list[str]]]:
    names = stylesheet_names(PAGE.read_text(encoding="utf-8"))
    owner_map: dict[str, set[str]] = defaultdict(set)
    for name in names:
        for key in declarations(source_path(name).read_text(encoding="utf-8")):
            owner_map[key].add(name)
    overlap = {key: sorted(owners) for key, owners in owner_map.items() if len(owners) > 1}
    return names, dict(sorted(overlap.items()))


def verify() -> dict:
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    names, conflicts = inspect()
    if names != baseline["linkedStylesheets"]:
        raise ValueError("CSS source-owner order changed: review/update single-owner contract")
    allowed = baseline["crossOwners"]
    newly_stacked = {
        key: actual for key, actual in conflicts.items()
        if key not in allowed or not set(actual).issubset(allowed[key])
    }
    if newly_stacked:
        sample = list(newly_stacked.items())[:8]
        raise ValueError("NEW CROSS-OWNER CSS STACKING FORBIDDEN: " + repr(sample))
    return {
        "stylesheets": len(names),
        "crossOwnerBaseline": len(allowed),
        "crossOwnerCurrent": len(conflicts),
        "eliminated": len(allowed) - len(conflicts),
        "newCrossOwnerConflicts": 0,
    }


if __name__ == "__main__":
    print("APPROVED_CSS_OWNERSHIP_NO_NEW_STACKING", json.dumps(verify(), sort_keys=True))
