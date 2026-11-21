"""Regression for literal embedded Pages/native GUI resource attestation."""
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "verify_embedded_gui_resources", ROOT / "scripts" / "verify_embedded_gui_resources.py"
)
assert spec and spec.loader
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class NativeGuiEmbeddedBytesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="armix-exe-byte-qa-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bundle = self.root / "approved_gui"
        (self.bundle / "assets").mkdir(parents=True)
        self.exe = self.root / "ARMixConsole.exe"
        self.manifest = self.bundle / "approved-gui-manifest.json"
        self.assets = {
            "index.html": "<!doctype html><title>ArSonMix — /preview/</title>".encode("utf-8"),
            "assets/mixer.css": b".gain{color:#81b8c6}",
            "assets/button.png": b"\x89PNG\r\n\x1a\n\0\xff\x90\x12APPROVED\0",
        }
        self.records = [
            {"path": name, "sha256": hashlib.sha256(payload).hexdigest()}
            for name, payload in self.assets.items()
        ]
        for name, payload in self.assets.items():
            (self.bundle / name).write_bytes(payload)
        self.write_manifest()
        self.exe.write_bytes(
            b"MZ" + b"\x00" * 128
            + b"\x00".join(self.assets.values())
            + b"\x00" * 64
        )

    def write_manifest(self):
        self.manifest.write_text(json.dumps({
            "schema": "arsonmix.approved-native-pages-bundle.v2",
            "files": self.records,
        }), encoding="utf-8")

    def test_attests_exact_original_utf8_and_binary_bytes(self):
        self.assertEqual(mod.verify(self.exe, self.bundle, self.manifest), 3)

    def stage_native_floating_fx(self, frame=None):
        # The additional HTML is only the approved FX child HWND,
        # not a second/public mixer GUI.
        if frame is None:
            frame = (b'<title>Native owned FX</title>'
                     b'<link href="assets/arsonmix-approved-runtime.css">'
                     b'<script src="assets/approved-floating-fx-bridge.js"></script>'
                     b'<output id="fxWindowStatus"></output>')
        self.assets["fx-window.html"] = frame
        self.assets["assets/approved-floating-fx-bridge.js"] = b"APPROVED_FX_BRIDGE"
        self.records = [
            {"path": name, "sha256": hashlib.sha256(payload).hexdigest()}
            for name, payload in self.assets.items()
        ]
        for name, payload in self.assets.items():
            (self.bundle / name).write_bytes(payload)
        self.write_manifest()
        self.exe.write_bytes(
            b"MZ" + b"\x00" * 128
            + b"\x00".join(self.assets.values()) + b"\x00" * 64
        )

    def test_attests_native_owned_fx_frame_plus_original_pages(self):
        self.stage_native_floating_fx()
        self.assertEqual(mod.verify(self.exe, self.bundle, self.manifest), 5)

    def test_rejects_unapproved_second_root_html(self):
        self.stage_native_floating_fx()
        payload = b"<title>Unauthorized second mixer</title>"
        (self.bundle / "second-mixer.html").write_bytes(payload)
        self.records.append({"path": "second-mixer.html",
                             "sha256": hashlib.sha256(payload).hexdigest()})
        self.write_manifest()
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "Unsafe"):
            mod.verify(self.exe, self.bundle, self.manifest)

    def test_rejects_fx_frame_missing_native_only_bridge_or_skin(self):
        self.stage_native_floating_fx(b"<title>Wrong FX editor</title>")
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "native-only FX editor"):
            mod.verify(self.exe, self.bundle, self.manifest)

    def test_rejects_fx_frame_without_manifest_attested_bridge(self):
        self.stage_native_floating_fx()
        self.records = [r for r in self.records
                        if r["path"] != "assets/approved-floating-fx-bridge.js"]
        self.write_manifest()
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "missing its approved control bridge"):
            mod.verify(self.exe, self.bundle, self.manifest)

    def test_rejects_fx_frame_missing_from_executable(self):
        self.stage_native_floating_fx()
        self.exe.write_bytes(b"MZ" + b"\x00".join(
            payload for path, payload in self.assets.items()
            if path != "fx-window.html"))
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "NOT IN WINDOWS EXE: fx-window.html"):
            mod.verify(self.exe, self.bundle, self.manifest)

    def test_rejects_staged_png_not_present_in_executable(self):
        self.exe.write_bytes(b"MZ" + self.assets["index.html"]
                             + self.assets["assets/mixer.css"])
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "NOT IN WINDOWS EXE: assets/button.png"):
            mod.verify(self.exe, self.bundle, self.manifest)

    def test_rejects_tampered_staged_bytes_even_when_exe_has_old_bytes(self):
        (self.bundle / "assets/mixer.css").write_bytes(b".gain{color:orange}")
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "SHA mismatch: assets/mixer.css"):
            mod.verify(self.exe, self.bundle, self.manifest)

    def test_rejects_duplicate_or_traversal_manifest_entries(self):
        self.records.append(self.records[0].copy())
        self.write_manifest()
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "duplicate"):
            mod.verify(self.exe, self.bundle, self.manifest)
        self.records[-1] = {"path": "assets/../index.html",
                            "sha256": self.records[0]["sha256"]}
        self.write_manifest()
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "Unsafe"):
            mod.verify(self.exe, self.bundle, self.manifest)

    def test_rejects_empty_exe_and_invalid_schema(self):
        self.exe.write_bytes(b"")
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "empty"):
            mod.verify(self.exe, self.bundle, self.manifest)
        self.exe.write_bytes(b"MZ" + b"resource")
        self.manifest.write_text('{"schema":"invalid","files":[]}', encoding="utf-8")
        with self.assertRaisesRegex(mod.EmbeddedGuiError, "schema"):
            mod.verify(self.exe, self.bundle, self.manifest)


if __name__ == "__main__":
    unittest.main()
