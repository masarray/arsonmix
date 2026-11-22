#!/usr/bin/env python3
"""Capture six *actual approved native-WebView FX editor modules* in Chromium.

The exact native-only fx-window.html and assets are produced by
scripts/build_approved_native_gui.py, the JUCE WebView2 resource assembler.
The test transport is logical/offline and NOT a screenshot of Windows PCM.
No CSS recreation, mock product art, separate mixer or audio manipulation.
"""
from __future__ import annotations

from functools import partial
from hashlib import sha256
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from importlib import util
from pathlib import Path
from threading import Thread
import json
import os
import subprocess
import sys
import tempfile

from PIL import Image

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "site/media"
CASES = (
    ("channel-eq", "armix.builtin.channel-eq.v1", "#mxNativePeq"),
    ("compressor", "armix.builtin.channel-compressor.v1", "#mxNativeComp"),
    ("graphic-eq31", "armix.builtin.graphic-eq31.v1", "#mxNativeGeq31"),
    ("harmonice", "armix.builtin.arson-harmony.v1", "#mxNativeHarmony"),
    ("vocal-delay", "armix.builtin.vocal-throw.v1", "#mxNativeVocalThrow"),
    ("8d-sonku", "armix.builtin.8d-sonku.v1", "#mxNativeSonKu"),
)


def load(path: Path, name: str):
    spec = util.spec_from_file_location(name, path)
    module = util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def capture():
    bundle = load(ROOT / "scripts/build_approved_native_gui.py", "armix_native_bundle")
    fixture = load(ROOT / "tests/approved_floating_fx_window_smoke.py", "armix_fx_fixture")
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="armix-fx-showcase-") as directory:
        approved = Path(directory) / "approved"
        manifest = bundle.stage(approved)
        assert (approved / "fx-window.html").is_file()
        assert manifest["adapterState"] == "SCAFFOLD_ONLY__NOT_LIVE_PCM"

        class Handler(SimpleHTTPRequestHandler):
            def log_message(self, *args):
                pass

        server = ThreadingHTTPServer(
            ("127.0.0.1", 0), partial(Handler, directory=str(approved))
        )
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        records = []
        try:
            with sync_playwright() as pw:
                browser = pw.chromium.launch(headless=True)
                try:
                    url = f"http://127.0.0.1:{server.server_port}/fx-window.html"
                    for slug, catalog, selector in CASES:
                        page = browser.new_page(
                            viewport={"width": 1600, "height": 1000},
                            device_scale_factor=1,
                            reduced_motion="reduce",
                        )
                        errors = []
                        page.on("pageerror", lambda e: errors.append(str(e)))
                        try:
                            page.add_init_script(fixture.INIT)
                            page.goto(url, wait_until="networkidle")
                            page.wait_for_function(
                                "() => window.__fxOutbound?.some(x=>x.name==='arMixFloatingReady')"
                            )
                            payload = {
                                "snapshot": fixture.document(catalog),
                                "channelId": fixture.CHANNEL,
                                "slot": 0,
                                "catalogId": catalog,
                                "instanceId": fixture.INSTANCE,
                            }
                            page.evaluate(
                                "(p) => window.__fxDispatch('arMixFloatingOpen',p)",
                                payload,
                            )
                            editor = page.locator(selector)
                            editor.wait_for(state="visible", timeout=12000)
                            assert editor.get_attribute("role") == "dialog"
                            assert editor.get_attribute("aria-modal") == "false"
                            assert page.locator("#fxWindowAbort").is_hidden()
                            assert page.evaluate(
                                "() => window.__ARMIX_NATIVE_M2__?.fxEditorState()?.logical===true"
                            )
                            page.evaluate("() => document.fonts.ready")
                            bounds = editor.bounding_box()
                            assert bounds and bounds["width"] >= 440 and bounds["height"] >= 140, (
                                slug, bounds
                            )
                            filename = f"fx-editor-{slug}.jpg"
                            dest = OUT / filename
                            editor.screenshot(
                                path=str(dest),
                                type="jpeg",
                                quality=86,
                                animations="disabled",
                            )
                            assert dest.stat().st_size > 15000, slug
                            assert not errors, (slug, errors)
                            records.append({
                                "file": filename,
                                "catalogId": catalog,
                                "editorSelector": selector,
                                "sizeBytes": dest.stat().st_size,
                                "sha256": sha256(dest.read_bytes()).hexdigest(),
                                "source": "approved JUCE WebView2 editor resource rendered in Chromium",
                                "actualWindowsScreenshot": False,
                                "audioOrHardwareVerifiedByImage": False,
                            })
                            print("CAPTURED_PUBLIC_FX_EDITOR", slug, dest.stat().st_size)
                        finally:
                            page.close()
                finally:
                    browser.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)
    assert len(records) == len(CASES)
    # Harmonice's separate HWND shell starts with a control-only fixture;
    # capture instead from the approved native Harmony browser regression,
    # after its accepted snapshot paints the complete original editor.
    subprocess.run(
        [sys.executable, str(ROOT / "tests/approved_native_harmony_smoke.py")],
        check=True,
        cwd=ROOT,
    )
    harmonice_png = ROOT / "arsonmix-native-harmony-editor-pitch-tune-on.png"
    assert harmonice_png.is_file(), "Accepted Harmonice editor capture missing"
    with Image.open(harmonice_png) as original:
        assert original.width >= 790 and original.height >= 330
        original.convert("RGB").save(
            OUT / "fx-editor-harmonice.jpg", "JPEG", quality=88, optimize=True
        )
    for record in records:
        if record["file"] == "fx-editor-harmonice.jpg":
            img = OUT / record["file"]
            record["sizeBytes"] = img.stat().st_size
            record["sha256"] = sha256(img.read_bytes()).hexdigest()
            record["source"] = (
                "approved JUCE WebView2 Harmonice editor rendered with "
                "accepted emulator readback in Chromium"
            )
    for leftover in ROOT.glob("arsonmix-native-harmony-editor-*.png"):
        leftover.unlink()
    provenance = {
        "schemaVersion": 1,
        "sourceCommit": os.environ.get("GITHUB_SHA", "local"),
        "nativeBundleApprovedPagesSha256": manifest["approvedPagesHtmlSha256"],
        "captureMode": "approved-native-webview-editor-chromium-offline-fixture",
        "notes": "Actual native editor HTML/CSS/JS pixels, not a Windows HWND or real DSP readback. No simulated JUCE controls beyond the documented test transport.",
        "images": records,
    }
    (OUT / "fx-gallery-provenance.json").write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print("ARSONMIX_FX_GALLERY_CAPTURE_OK", len(records))


if __name__ == "__main__":
    capture()
