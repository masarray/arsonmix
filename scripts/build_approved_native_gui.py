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
ADAPTER_MARKER = ('<script defer src="assets/approved-native-contract.js"></script>\n'
                  '<script defer src="assets/approved-native-bridge.js"></script>')
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


# Native-only event seam: the original PUBLIC Pages HTML is byte-for-byte
# unchanged. Insert five small control hooks ONLY into the generated WebView2
# HTML at native build time. Every anchor must occur exactly once: no silent
# breakage if Pages GUI source evolves. No extra layout or simulated PCM.
NATIVE_EVENT_HOOKS = (
    ("function wireGraphCommit(ops,{record=true}={}){\n",
     "function wireGraphCommit(ops,{record=true}={}){\n"
     " if(window.__JUCE__?.backend){window.__ARMIX_NATIVE_M2__?.consumeWire?.(ops);return false;}\n"),
    ("function wireWsUndo(){\n",
     "function wireWsUndo(){\n"
     " if(window.__JUCE__?.backend){window.__ARMIX_NATIVE_M2__?.consumeUndo?.();return false;}\n"),
    ("function handleField(event){\n",
     "function handleField(event){\n"
     " if(window.__JUCE__?.backend){window.__ARMIX_NATIVE_M2__?.consumeField?.(event);return;}\n"),
    (" if(a==='wireExpand'){document.querySelector('.section-rail",
     " if(window.__JUCE__?.backend&&!window.__ARMIX_NATIVE_M2__?.enabled)return;\n"
     " if(window.__ARMIX_NATIVE_M2__?.consumeAction?.(t,a,btn))return;\n"
     " if(a==='wireExpand'){document.querySelector('.section-rail"),
    ("document.querySelector('.console-body').addEventListener('dblclick',e=>{\n"
     " const t=findTrack(e.target);if(!t)return;\n",
     "document.querySelector('.console-body').addEventListener('dblclick',e=>{\n"
     " const t=findTrack(e.target);if(!t)return;\n"
     " if(window.__JUCE__?.backend&&!window.__ARMIX_NATIVE_M2__?.enabled)return;\n"
     " if(window.__ARMIX_NATIVE_M2__?.consumeDoubleClick?.(e,t))return;\n"),
    # Native-only: prevent keyboard FX moves from mutating demo inserts in JUCE.
    ("function fxNativeTransactionRequired(){\n",
     "function fxNativeTransactionRequired(){\n"
     " if(window.__JUCE__?.backend)return true;\n"),
    # Source's pointerup clears faderGesture BEFORE the later deferred bridge
    # listener runs. Dispatch exactly once at the actual gesture commit point.
    (" const g=faderGesture;faderGesture=null;\n"
     " g.box.releasePointerCapture?.(ev.pointerId);\n",
     " const g=faderGesture;faderGesture=null;\n"
     " g.box.releasePointerCapture?.(ev.pointerId);\n"
     " if(window.__ARMIX_NATIVE_M2__?.consumeField?.({target:g.input,type:'change'}))return;\n"),
    # Native WebView2 only: expose the original Pages IIFE model to the thin
    # bridge. No second renderer/state reducer, no change to public /preview/.
    ("restorePluginPreferences();init();\n})();",
     "window.__ARMIX_NATIVE_SCOPE__=Object.freeze({\n"
     " get tracks(){return tracks;},get master(){return master;},\n"
     " make,render,findTrack,displayMeterDbfs,\n"
     " colors:COLORS,wireGraph:ArSonWireGraph,\n"
     " allocateWireId(){return nextWireId++;},\n"
     " installGraph(channels,main,edges){\n"
     "  tracks=channels;master=main;wireEdges=edges;\n"
     "  wireWsHistory=[];selectedWireEdgeId=null;\n"
     "  syncWireOutputProjection();render();\n"
     " },\n"
     " stopDemo(){if(preview)togglePreview();}\n"
     "});\nrestorePluginPreferences();init();\n})();"),
)


def native_html(approved: str | None = None) -> str:
    original = accepted_pages_html() if approved is None else approved
    newline = "\r\n" if "\r\n" in original else "\n"
    html = original.replace("\r\n", "\n")
    if html.count("</body>") != 1 or ADAPTER_MARKER in html:
        raise ValueError("Approved Pages must have exactly one body and no prior native injection")
    for before, after in NATIVE_EVENT_HOOKS:
        if html.count(before) != 1 or after in html:
            raise ValueError("Native-only M2 control hook anchor drift: " + before[:55])
        html = html.replace(before, after, 1)
    html = html.replace("</body>", ADAPTER_MARKER + "\n</body>")
    return html.replace("\n", newline)


def public_from_native(native: str) -> str:
    """Reconstruct original public Pages HTML byte-for-byte from the native source."""
    newline = "\r\n" if "\r\n" in native else "\n"
    html = native.replace("\r\n", "\n")
    if html.count(ADAPTER_MARKER + "\n") != 1:
        raise ValueError("Native adapter marker missing or duplicated")
    html = html.replace(ADAPTER_MARKER + "\n", "", 1)
    for before, after in reversed(NATIVE_EVENT_HOOKS):
        if html.count(after) != 1:
            raise ValueError("Native-only M2 control hook missing or duplicated")
        html = html.replace(after, before, 1)
    return html.replace("\n", newline)


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
        shutil.copy2(ROOT / "web" / "approved-native-contract.js",
                     out / "assets" / "approved-native-contract.js")
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
