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
CONTAINER = re.compile(r"^@(media|supports|container|layer|scope)(?=\s|\()", re.I)
# Keep older declarations as browser/undefined-variable fallbacks when the
# chosen modern value may be unsupported. P0 WebView2 runtime versions vary.
FALLBACK_SENSITIVE = re.compile(r"\b(?:var|env|color-mix|image-set|paint|oklch|oklab|lab|lch|light-dark|color)\s*\(|-webkit-|-moz-", re.I)
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
    """Strip CSS comments, never quoted strings or CSS escapes."""
    result = []
    quote = ""
    i = 0
    while i < len(text):
        c = text[i]
        if quote:
            if c == "\\" and i + 1 < len(text):
                result.extend((c, text[i + 1]))
                i += 2
                continue
            result.append(c)
            if c == quote:
                quote = ""
        elif text.startswith("/*", i):
            stop = text.find("*/", i + 2)
            if stop < 0:
                raise ValueError("unterminated CSS comment")
            i = stop + 2
            continue
        else:
            result.append(c)
            if c in "'\"":
                quote = c
        i += 1
    return "".join(result).strip()


def _normalize_header(text: str):
    """Normalize whitespace only OUTSIDE quoted selector/at-rule strings."""
    parts = []
    quote = ""
    escaped = False
    pending_space = False
    for c in text:
        if quote:
            parts.append(c)
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == quote:
                quote = ""
        elif c.isspace():
            pending_space = True
        else:
            if pending_space and parts:
                parts.append(" ")
            pending_space = False
            parts.append(c)
            if c in "'\"":
                quote = c
    return "".join(parts).strip()


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
                normalized = _normalize_header(header)
                if CONTAINER.match(normalized):
                    walk(at + 1, close, context + (normalized,))
                cursor = close + 1
                continue
            if header and not _seek(source, at + 1, close, "{}")[1]:
                # Never optimize nested-rule bodies (e.g. modern nesting).
                p = at + 1
                selector = _normalize_header(header)
                while p < close:
                    stop, divider = _seek(source, p, close, ";")
                    piece = source[p:stop]
                    visible = _visible(piece)
                    match = PROPERTY.match(visible)
                    if match and visible[match.end():].strip():
                        raw_property = match.group(1)
                        # Custom property names are CASE SENSITIVE in CSS.
                        prop = (raw_property if raw_property.startswith("--")
                                else raw_property.lower())
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
    source_by_owner = dict(stylesheets)
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
    fallback = 0
    across = 0
    for declarations in groups.values():
        if len(declarations) < 2:
            continue
        potential += len(declarations) - 1
        important = [d for d in declarations if d.important]
        winner = important[-1] if important else declarations[-1]
        # A later var()/color-mix()/vendor declaration may be invalid in an
        # older WebView2 or when a custom property is unset. Its earlier
        # declaration is a genuine CSS fallback, not dead CSS.
        winner_text = _visible(
            source_by_owner[winner.filename][winner.start:winner.end])
        preserve_fallbacks = bool(FALLBACK_SENSITIVE.search(winner_text))
        for item in declarations:
            if item is winner:
                continue
            if preserve_fallbacks:
                fallback += 1
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
        "preservedFallbackDeclarations": fallback,
        "sourceStylesheetCount": len(stylesheets),
        "generatedStylesheetCount": len(compiled),
        "addedOverrideStylesheets": 0,
    }
    if stats["removedDeclarations"] + locked + fallback != potential:
        raise ValueError("CSS loser accounting does not balance")
    return compiled, stats


# Stage D: one runtime CSSOM owner; the 18 historical sources and their
# original cascade order remain independently maintainable.
RUNTIME_CSS = "arsonmix-approved-runtime.css"


def compile_runtime_bundle(stylesheets: list[tuple[str, str]]) -> str:
    """Concatenate source-ordered *compiled* CSS, without changing selector scope.

    Every owner lives in /assets/: url(...) resolution is unchanged. Fail
    closed on top-level CSS at-rules whose semantics depend on file location.
    Do not minify/rewrite original selectors or reorder @media/@font-face.
    """
    if not stylesheets or len({name for name, _ in stylesheets}) != len(stylesheets):
        raise ValueError("Runtime CSS requires unique, ordered approved owners")
    if any(name == RUNTIME_CSS for name, _ in stylesheets):
        raise ValueError("Runtime CSS output must not be an authored owner")
    parts = ["/* ArSonMix approved runtime CSS: generated; edit source owners only. */\n"]
    for name, css in stylesheets:
        # Remove only comments for detecting forbidden at-rules, not for output.
        plain = re.sub(r"/\*[\s\S]*?\*/", "", css)
        if re.search(r"@(?:charset|import|namespace)\b", plain, re.I):
            raise ValueError("CSS owner cannot be bundled with @import/charset/namespace: " + name)
        if not re.fullmatch(r"[a-zA-Z0-9._-]+\.css", name):
            raise ValueError("Unsafe runtime CSS owner: " + name)
        # Separators prevent neighboring tokens merging; comments document
        # provenance but are NOT a second override/cascade layer.
        parts.append("\n/* BEGIN approved owner " + name + " */\n")
        parts.append(css)
        parts.append("\n/* END approved owner " + name + " */\n")
    return "".join(parts)


def link_runtime_bundle(html: str, css_sha: str) -> str:
    """Replace exactly the approved CSS owner links with ONE runtime link."""
    if not re.fullmatch(r"[a-f0-9]{16}", css_sha):
        raise ValueError("Invalid generated runtime CSS digest")
    original = list(LINK.finditer(html))
    if len(original) != 18 or len({m.group(2) for m in original}) != 18:
        raise ValueError("Approved GUI must list its 18 unique CSS owners")
    if RUNTIME_CSS in (m.group(2) for m in original):
        raise ValueError("Runtime CSS would recursively import itself")
    count = 0
    def rewrite(match):
        nonlocal count
        count += 1
        if count == 1:
            return ('<link rel="stylesheet" href="assets/' + RUNTIME_CSS +
                    '?sha=' + css_sha + '">')
        return ""
    rewritten = LINK.sub(rewrite, html)
    if count != len(original) or rewritten.count('href="assets/' + RUNTIME_CSS) != 1:
        raise ValueError("CSS runtime bundling failed closed")
    return rewritten


def optimize_assets(html: str, assets: Path, *, runtime_bundle: bool = False):
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
    if not runtime_bundle:
        # Low-level optimizer retains its original per-owner API for tools,
        # unit fixtures and source migration; only the official assembler
        # opts into Stage D's single runtime CSSOM.
        return rendered, stats
    # Keep all per-owner compiled CSS copies as inspectable provenance.
    # The browser/native WebView2 now *request just one* generated CSS file.
    # Packaging may discard redundant unreferenced owner copies separately
    # after the established exact-approved-PNG and WIRE provenance checks.
    bundled = compile_runtime_bundle([(name, optimized[name]) for name, _ in files])
    digest = hashlib.sha256(bundled.encode("utf-8")).hexdigest()[:16]
    (assets / RUNTIME_CSS).write_text(bundled, encoding="utf-8", newline="")
    rendered = link_runtime_bundle(rendered, digest)
    stats["runtimeStylesheetCount"] = 1
    stats["removedStylesheetRequests"] = len(files) - 1
    stats["runtimeBundleCssSHA256"] = digest
    stats["runtimeBundleBytes"] = len(bundled.encode("utf-8"))
    stats["compiledOwnerBytes"] = sum(len(optimized[name].encode("utf-8")) for name, _ in files)
    return rendered, stats
