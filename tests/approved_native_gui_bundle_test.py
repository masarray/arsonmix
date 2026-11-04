"""Fail closed when a native mixer bundles a second GUI instead of approved /preview/."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("approved_native_bundle", ROOT / "scripts/build_approved_native_gui.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class ApprovedNativeGuiBundleTests(unittest.TestCase):
    def test_exact_approved_png_button_skin_and_thin_adapter(self):
        with tempfile.TemporaryDirectory(prefix="armix-approved-test-") as tmp:
            out = Path(tmp) / "bundle"
            manifest = bundle.stage(out)
            self.assertEqual(manifest["authoritativePagesRoute"], "/preview/")
            self.assertEqual(manifest["authoritativePagesIframe"], "/preview-buttons/index.html")
            self.assertEqual(manifest["adapterState"], "SCAFFOLD_ONLY__NOT_LIVE_PCM")
            approved = bundle.accepted_pages_html()
            native = (out / "index.html").read_text("utf-8")
            self.assertEqual(native.replace(bundle.ADAPTER_MARKER + "\n", ""), approved)
            canonical = (ROOT / "site/preview-v2/index.html").read_text("utf-8")
            self.assertNotEqual(native, (ROOT / "web/index.html").read_text("utf-8"))
            for name in bundle.OVERRIDES:
                button = (ROOT / "site/preview-buttons/assets" / name).read_bytes()
                self.assertEqual((out / "assets" / name).read_bytes(), button)
                self.assertIn("assets/" + name + "?rev=" + bundle.sha256(button)[:16], native)
                self.assertIn("assets/" + name, canonical)
            self.assertEqual((out / "assets/mute-solo-pfl-56x37.png").read_bytes(),
                             (ROOT / "site/preview-buttons/assets/mute-solo-pfl-56x37.png").read_bytes())
            self.assertNotIn("console-native.html", native)
            self.assertNotIn('src="web/index.html"', native)
            self.assertIn("approved-native-bridge.js", native)
            self.assertIn('arMixGuiReady', (out / "assets/approved-native-bridge.js").read_text("utf-8"))

    def test_manifest_reproducibility_and_resource_mapping(self):
        with tempfile.TemporaryDirectory(prefix="armix-approved-test-") as tmp:
            a, b = Path(tmp) / "a", Path(tmp) / "b"
            first = bundle.stage(a)
            second = bundle.stage(b)
            self.assertEqual(first, second)
            self.assertEqual(bundle.verify(a), first)
            header = (a / "ApprovedGuiResources.h").read_text("utf-8")
            self.assertIn('BinaryData::index_html', header)
            self.assertIn('BinaryData::mixercontrolkeys_css', header)
            self.assertIn('BinaryData::mixercontrolkeys_js', header)
            self.assertIn('"/assets/mute-solo-pfl-56x37.png"', header)
            for entry in first["files"]:
                file = a / entry["path"]
                self.assertEqual(bundle.sha256(file.read_bytes()), entry["sha256"])
            (a / "index.html").write_text("INVALID GUI", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "STALE OR WRONG APPROVED GUI BUNDLE"):
                bundle.verify(a)

    def test_native_binary_routes_only_to_generated_gui(self):
        cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
        cpp = (ROOT / "src/Main.cpp").read_text("utf-8")
        self.assertIn("build_approved_native_gui.py", cmake)
        self.assertIn('ARMIX_APPROVED_GUI_DIR', cmake)
        self.assertNotIn("juce_add_binary_data(ARMixAssets SOURCES\n    web/index.html", cmake)
        self.assertNotIn("web/console-native.html web/console-native.css", cmake)
        self.assertIn('resolveApprovedGuiResource (path, data, size, mime)', cpp)
        self.assertNotIn('BinaryData::consolenative_html', cpp)
        self.assertNotIn('BinaryData::index_html', cpp)
        self.assertIn('withResourceProvider (assetFor)', cpp)


if __name__ == "__main__":
    unittest.main()
