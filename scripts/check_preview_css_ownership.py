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
import sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
from compile_approved_css import _declarations

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


def same_context_repeat_count(css: str) -> int:
    """Reuse the publisher parser: media/support/layer scopes are distinct keys."""
    keys = [declaration.key for declaration in _declarations(css, "ownership")]
    return len(keys) - len(set(keys))


def count_same_file_repeated_declarations(names: list[str]) -> dict[str, int]:
    """Reject repeated declarations within the same selector and CSS context.

    Responsive variants are separate conditions, not late unconditional stacks.
    Existing caps remain upper bounds; no baseline debt is increased.
    """
    return {name: same_context_repeat_count(source_path(name).read_text(encoding="utf-8"))
            for name in names}


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
    # Stage B: forbid growing same-owner selector/property layering too.
    # V28 historical source byte checks remain valid; no stylesheet changed.
    same_file = count_same_file_repeated_declarations(names)
    caps = baseline["sameFileRepeatedDeclarationCaps"]
    if set(caps) != set(names):
        raise ValueError("Missing/stale same-file CSS ownership baseline")
    new_stacking = {
        name: (count, caps[name])
        for name, count in same_file.items() if count > caps[name]
    }
    if new_stacking:
        raise ValueError("NEW SAME-OWNER CSS STACKING FORBIDDEN: " + repr(new_stacking))
    return {
        "stylesheets": len(names),
        "crossOwnerBaseline": len(allowed),
        "crossOwnerCurrent": len(conflicts),
        "eliminated": len(allowed) - len(conflicts),
        "newCrossOwnerConflicts": 0,
        "sameOwnerRepeatedBaseline": sum(caps.values()),
        "sameOwnerRepeatedCurrent": sum(same_file.values()),
        "newSameOwnerStacking": 0,
    }


if __name__ == "__main__":
    print("APPROVED_CSS_OWNERSHIP_NO_NEW_STACKING", json.dumps(verify(), sort_keys=True))
