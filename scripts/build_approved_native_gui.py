#!/usr/bin/env python3
"""Stage the EXACT ONE approved /preview/ Pages GUI into native JUCE WebView2.

Calls scripts/build_pages.py to generate the *same* published index.html and
assets. There is no second HTML/CSS assembler, cloned GUI, or alternate skin.
A nonvisual bridge JS is the only native-specific addition.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CANONICAL = ROOT / "site" / "preview-v2"
NATIVE_ADAPTER = ROOT / "web" / "approved-native-bridge.js"
ADAPTER_MARKER = '<script defer src="assets/approved-native-bridge.js"></script>'
SCHEMA = "arsonmix.approved-native-pages-bundle.v2"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_public_pages(destination: Path) -> Path:
    """Call the ONE authoritative Pages assembler; never duplicate its CSS/JS rules."""
    subprocess.run(
        [sys.executable, str(ROOT / "scripts" / "build_pages.py"),
         "--out", str(destination), "--sha", "native-source-check",
         "--branch", "native-approved-gui"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    )
    public = destination / "preview"
    data = json.loads((public / "build.json").read_text("utf-8"))
    if data.get("kind") != "APPROVED_SINGLE_GUI" or data.get("canonical_url") != "/preview/":
        raise ValueError("Pages build is not the user-approved single /preview/ GUI")
    if not (public / "index.html").is_file():
        raise ValueError("Pages assembler did not generate the approved HTML")
    return public


def accepted_pages_html() -> str:
    with tempfile.TemporaryDirectory(prefix="armix-pages-source-") as tmp:
        return (build_public_pages(Path(tmp) / "public") / "index.html").read_text("utf-8")


def native_html(approved: str | None = None) -> str:
    html = accepted_pages_html() if approved is None else approved
    if html.count("</body>") != 1 or ADAPTER_MARKER in html:
        raise ValueError("Approved Pages must have exactly one body and no prior native injection")
    return html.replace("</body>", ADAPTER_MARKER + "\n</body>")


def safe_symbol(filename: str) -> str:
    # JUCE 8 BinaryData: 'mixer-control-keys.css' -> 'mixercontrolkeys_css'
    stem, ext = filename.rsplit(".", 1)
    return re.sub(r"[^A-Za-z0-9_]", "", stem).replace("_", "") + "_" + re.sub(r"[^A-Za-z0-9]", "", ext)


def resource_header(files: list[Path], root: Path) -> str:
    identifiers = {}
    lines = [
        "// Generated from scripts/build_pages.py /preview/. DO NOT EDIT.",
        "// Only the approved Pages renderer; NO legacy web/index or console-native UI.",
        "#pragma once",
        "inline bool resolveApprovedGuiResource(const juce::String& request,",
        "                                       const char*& data, int& size, juce::String& mime)",
        "{",
        '    const auto path = request.upToFirstOccurrenceOf("?", false, false);',
    ]
    for file in files:
        uri = "/" + file.relative_to(root).as_posix()
        sym = safe_symbol(file.name)
        if sym in identifiers:
            raise ValueError(f"JUCE symbol collision: {sym}: {identifiers[sym]}, {uri}")
        identifiers[sym] = uri
        mime = mimetypes.guess_type(file.name)[0] or "application/octet-stream"
        if file.suffix == ".js":
            mime = "text/javascript"
        elif file.suffix == ".css":
            mime = "text/css"
        condition = '(path == "/" || path == "/index.html")' if uri == "/index.html" else f"(path == {json.dumps(uri)})"
        lines.extend([
            f"    if {condition}",
            "    {",
            f"        data = BinaryData::{sym};",
            f"        size = BinaryData::{sym}Size;",
            f"        mime = {json.dumps(mime)};",
            "        return true;",
            "    }",
        ])
    return "\n".join([*lines, "    return false;", "}", ""])


def stage(out: Path) -> dict:
    out = out.resolve()
    if out in (ROOT, ROOT / "site", ROOT / "web", CANONICAL) or (out / ".git").exists():
        raise ValueError(f"Unsafe bundle destination: {out}")
    with tempfile.TemporaryDirectory(prefix="armix-native-pages-") as tmp:
        pages = build_public_pages(Path(tmp) / "public")
        approved = (pages / "index.html").read_bytes()
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        shutil.copytree(pages / "assets", out / "assets")
        shutil.copy2(NATIVE_ADAPTER, out / "assets" / "approved-native-bridge.js")
        native = native_html(approved.decode("utf-8")).encode("utf-8")
        (out / "index.html").write_bytes(native)
    files = [out / "index.html", *sorted(p for p in (out / "assets").rglob("*") if p.is_file())]
    if any(p.is_symlink() or p.relative_to(out).parts[0] not in ("index.html", "assets") for p in files):
        raise ValueError("Unsafe GUI resource input")
    if len(files) != len({f.name for f in files}):
        raise ValueError("Duplicate GUI asset basename would collide in JUCE BinaryData")
    (out / "ApprovedGuiResources.h").write_text(resource_header(files, out), encoding="utf-8")
    manifest = {
        "schema": SCHEMA,
        "authoritativePagesRoute": "/preview/",
        "authoritativePagesOutput": "preview/index.html",
        "canonicalSource": "site/preview-v2/index.html",
        "canonicalSourceSha256": sha256((CANONICAL / "index.html").read_bytes()),
        "approvedPagesHtmlSha256": sha256(approved),
        "nativeHtmlSha256": sha256(native),
        "adapter": "web/approved-native-bridge.js",
        "adapterState": "SCAFFOLD_ONLY__NOT_LIVE_PCM",
        "files": [{"path":p.relative_to(out).as_posix(), "sha256":sha256(p.read_bytes())} for p in files],
    }
    (out / "approved-gui-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def verify(out: Path) -> dict:
    if not out.is_dir():
        raise ValueError("Approved native bundle missing")
    before = (out / "approved-gui-manifest.json").read_bytes()
    existing = json.loads(before)
    with tempfile.TemporaryDirectory(prefix="armix-approved-check-") as tmp:
        expected_out = Path(tmp) / "bundle"
        expected = stage(expected_out)
        if existing != expected:
            raise ValueError("STALE OR WRONG APPROVED GUI BUNDLE: source/manifest changed")
        paths = {"index.html", "ApprovedGuiResources.h", "approved-gui-manifest.json"}
        paths.update(p["path"] for p in expected["files"])
        for path in sorted(paths):
            if (out / path).read_bytes() != (expected_out / path).read_bytes():
                raise ValueError(f"STALE OR WRONG APPROVED GUI BUNDLE: {path}")
    return existing


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--verify", action="store_true")
    args = p.parse_args()
    result = verify(args.out) if args.verify else stage(args.out)
    print("APPROVED_NATIVE_GUI_BUNDLE_OK", result["approvedPagesHtmlSha256"],
          len(result["files"]), "resources", result["adapterState"])


if __name__ == "__main__":
    main()
