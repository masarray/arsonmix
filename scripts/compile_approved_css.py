#!/usr/bin/env python3
"""Stage C: deterministic, source-preserving CSS cascade pruning for APPROVED /preview/.

Keep the historical CSS authoring and V28 exact-source restoration intact.
Only the copied Pages/WebView2 assets are optimized. Never add an override
stylesheet, rewrite selectors, change stylesheet order or move asset URLs.

This intentionally removes only provably shadowed *same selector, same
property, same priority origin, same at-rule context* declarations. Identical
selectors in distinct @media/@supports/@container scopes are not collapsed.
Unrecognized/nested rule syntax and @font-face/@keyframes are preserved.
"""
from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re

LINK = re.compile(r'(<link\s+rel="stylesheet"\s+href="assets/)([^"?]+\.css)(\?[^"]*)?(")', re.I)
PROPERTY = re.compile(r"^\s*(--[A-Za-z0-9_-]+|[-A-Za-z_][-A-Za-z0-9_]*)\s*:", re.S)
IMPORTANT = re.compile(r"!\s*important\s*$", re.I)
CONTAINER = re.compile(r"^@(media|supports|container|layer|scope)(?=\\s|\\()", re.I)
# Explicit byte parity with the owner-approved PNG buttons and VIRE historical
# socket source: these 2 assets participate as winners but never change bytes.
LOCKED_ASSETS = frozenset({"mixer-control-keys.css", "mixer-wire-dots.css"})


@dataclass(frozen=True)
class Declaration:
    filename: str
    context: tuple[str, ...]
    selector: str
    property: str
    important: bool
    start: int
    end: int

    @property
    def key(self):
        return self.context, self.selector, self.property


def _seek(source: str, begin: int, end: int, stops: str):
    """Find delimiter outside CSS comments, strings, functions and brackets."""
    i, parens, brackets = begin, 0, 0
    quote = ""
    while i < end:
        c = source[i]
        if quote:
            if c == "\\":
                i += 2
                continue
            if c == quote:
                quote = ""
        elif source.startswith("/*", i):
            close = source.find("*/", i + 2, end)
            i = end if close < 0 else close + 2
            continue
        elif c in "'\"":
            quote = c
        elif c == "(":
            parens += 1
        elif c == ")" and parens:
            parens -= 1
        elif c == "[":
            brackets += 1
        elif c == "]" and brackets:
            brackets -= 1
        elif parens == 0 and brackets == 0 and c in stops:
            return i, c
        i += 1
    return end, ""


def _close_brace(source: str, open_pos: int, end: int):
    depth, pos = 1, open_pos + 1
    while pos < end:
        at, delimiter = _seek(source, pos, end, "{}")
        if not delimiter:
            return -1
        depth += 1 if delimiter == "{" else -1
        if depth == 0:
            return at
        pos = at + 1
    return -1


def _visible(text: str):
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S).strip()


def _declarations(source: str, filename: str):
    result = []

    def walk(begin: int, end: int, context: tuple[str, ...]):
        cursor = begin
        while cursor < end:
            at, delimiter = _seek(source, cursor, end, ";{}")
            if not delimiter:
                break
            if delimiter != "{":
                cursor = at + 1
                continue
            header = _visible(source[cursor:at])
            close = _close_brace(source, at, end)
            if close < 0:
                raise ValueError(f"{filename}: unmatched CSS brace near {at}")
            if header.startswith("@"):
                normalized = " ".join(header.split())
                if CONTAINER.match(normalized):
                    walk(at + 1, close, context + (normalized,))
                cursor = close + 1
                continue
            if header and not _seek(source, at + 1, close, "{}")[1]:
                # Never optimize nested-rule bodies (e.g. modern nesting).
                p = at + 1
                selector = " ".join(header.split())
                while p < close:
                    stop, divider = _seek(source, p, close, ";")
                    piece = source[p:stop]
                    visible = _visible(piece)
                    match = PROPERTY.match(visible)
                    if match and visible[match.end():].strip():
                        prop = match.group(1).lower()
                        important = bool(IMPORTANT.search(visible))
                        result.append(Declaration(filename, context, selector, prop,
                                                  important, p, stop + int(divider == ";")))
                    p = stop + 1
            cursor = close + 1

    walk(0, len(source), ())
    return result


def optimize_css(stylesheets: list[tuple[str, str]]):
    """Return optimized byte-preserved-per-owner CSS + auditable counters.

    "Last important" wins whenever a key contains important declarations;
    otherwise "last normal" wins, with exact selector/context identity.
    Important and normal declarations are never confused. Locked sources may
    retain duplicate declarations; no winner is deleted without a later
    equivalent-context winner and an explicit scoped proof.
    """
    entries = []
    for filename, css in stylesheets:
        for record in _declarations(css, filename):
            entries.append(record)
    groups = defaultdict(list)
    for item in entries:
        groups[item.key].append(item)
    removals = defaultdict(list)
    potential = 0
    locked = 0
    across = 0
    for declarations in groups.values():
        if len(declarations) < 2:
            continue
        potential += len(declarations) - 1
        important = [d for d in declarations if d.important]
        winner = important[-1] if important else declarations[-1]
        for item in declarations:
            if item is winner:
                continue
            if item.filename in LOCKED_ASSETS:
                locked += 1
                continue
            removals[item.filename].append((item.start, item.end))
            if item.filename != winner.filename:
                across += 1
    compiled = {}
    for filename, css in stylesheets:
        spans = sorted(removals[filename])
        # No overlapping declarations: each is a uniquely parsed source span.
        if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
            raise ValueError("CSS optimization spans overlap: " + filename)
        for a, b in reversed(spans):
            css = css[:a] + css[b:]
        compiled[filename] = css
    stats = {
        "sourceDeclarations": len(entries),
        "candidateRedundantDeclarations": potential,
        "removedDeclarations": sum(map(len, removals.values())),
        "removedCrossFileDeclarations": across,
        "lockedCandidateDeclarations": locked,
        "sourceStylesheetCount": len(stylesheets),
        "generatedStylesheetCount": len(compiled),
        "addedOverrideStylesheets": 0,
    }
    if stats["removedDeclarations"] + locked != potential:
        raise ValueError("CSS loser accounting does not balance")
    return compiled, stats


def optimize_assets(html: str, assets: Path):
    """Change only copied CSS asset bytes and add content-addressing to URLs."""
    names = [match.group(2) for match in LINK.finditer(html)]
    if not names or len(set(names)) != len(names):
        raise ValueError("Approved preview has duplicate/missing CSS links")
    files = [(name, (assets / name).read_text(encoding="utf-8")) for name in names]
    optimized, stats = optimize_css(files)
    revisions = {}
    for name, old in files:
        new = optimized[name]
        if name in LOCKED_ASSETS and new != old:
            raise ValueError("Locked native/PNG CSS must remain byte identical")
        if name not in LOCKED_ASSETS:
            (assets / name).write_text(new, encoding="utf-8", newline="")
        revisions[name] = hashlib.sha256(new.encode("utf-8")).hexdigest()[:16]

    def link(match):
        prefix, name, query, trailing = match.groups()
        # Stable existing CSS ownership/cache keys stay intact. One additional
        # deterministic hash makes the ACTUAL compiled CSS cache-safe.
        base = query or ""
        if "armixc=" in base:
            raise ValueError("Stage C URL compiled twice")
        delim = "&" if base else "?"
        return prefix + name + base + delim + "armixc=" + revisions[name] + trailing

    rendered = LINK.sub(link, html)
    stats["shaByCssOwner"] = revisions
    return rendered, stats
