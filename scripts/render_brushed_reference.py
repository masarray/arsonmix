#!/usr/bin/env python3
"""Rebuild the preserved Brushed Metal visual reference WITHOUT editing GUI source.

This script is NOT a runtime theme compiler. It overlays a frozen, historically
approved CSS adapter onto any canonical standalone HTML for visual comparison.
Production theme implementation must migrate material declarations into tokens.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "design-references/brushed-metal-v1.0/adapter.css"
DEFAULT_HTML = ROOT / "AR-MIX-Live-FX-Host-Standalone-v0.3.html"
DEFAULT_OUTPUT = ROOT / "dist-theme-previews/brushed-metal-reference.html"
CSS_SHA256 = "d4d3b13fa5ceba0f29665f902735ad0e48c4df44e4107cb00d2ff2f65ec678b6"
BASE_HTML_SHA256 = "d7ed37da6c52baaeef7c74cfb8be6388e523d491a18920b218d0e82ef8c55772"
WRAP_ID = "arsonmix-brushed-metal-adapter"


def overlay(html: str, css: str) -> str:
    marker = "</style></head>"
    if html.count(marker) != 1:
        raise ValueError("Standalone CSS closing marker not uniquely found; inspect build format.")
    if WRAP_ID in html:
        raise ValueError("Reference stylesheet already present; refusing duplicate injection.")
    return html.replace(
        marker,
        '</style><style id="' + WRAP_ID + '">\n' + css + '\n</style></head>',
        1,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=str, default=str(DEFAULT_HTML),
                        help="Canonical standalone HTML file, or - for stdin.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--strict-original", action="store_true",
                        help="Require the exact GitHub 3478823 standalone baseline.")
    args = parser.parse_args()

    css_bytes = REFERENCE.read_bytes()
    css_hash = hashlib.sha256(css_bytes).hexdigest()
    if css_hash != CSS_SHA256:
        raise SystemExit("Reference CSS changed or corrupted. SHA256: " + css_hash)

    source_bytes = (
        sys.stdin.buffer.read()
        if args.source == "-"
        else Path(args.source).read_bytes()
    )
    base_hash = hashlib.sha256(source_bytes).hexdigest()
    if args.strict_original and base_hash != BASE_HTML_SHA256:
        raise SystemExit("Historical source mismatch: use git show 3478823069d053db4f0d4c34786f444df3defac0")
    if base_hash != BASE_HTML_SHA256:
        print("NOTE: non-historical GUI source; output is a visual compatibility trial, "
              "NOT the approved v1.0 screenshot-equivalent artifact.", file=sys.stderr)

    result = overlay(source_bytes.decode("utf-8"), css_bytes.decode("utf-8"))
    dest = args.output.resolve()
    if args.source != "-" and dest == Path(args.source).resolve():
        raise SystemExit("Refusing to overwrite original standalone HTML.")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(result.encode("utf-8"))
    print(f"Brushed Metal reference: {dest}")
    print(f"Source SHA256: {base_hash}; CSS SHA256: {css_hash}")
    print("Layout and embedded JS unchanged. This is a reference overlay, NOT runtime implementation.")


if __name__ == "__main__":
    main()
