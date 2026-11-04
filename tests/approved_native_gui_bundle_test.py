"""Guard that Windows bundles the literal ONE approved Pages /preview/ GUI."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("approved_native_bundle", ROOT / "scripts/build_approved_native_gui.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class ApprovedNativeGuiBundleTests(unittest.TestCase):
    def test_native_is_exact_assembled_public_pages_plus_nonvisual_native_seam(self):
        with tempfile.TemporaryDirectory(prefix="armix-approved-test-") as tmp:
            out, published = Path(tmp) / "bundle", Path(tmp) / "published"
            source = bundle.build_public_pages(published)
            manifest = bundle.stage(out)
            self.assertEqual(manifest["authoritativePagesRoute"], "/preview/")
            self.assertEqual(manifest["authoritativePagesOutput"], "preview/index.html")
            self.assertEqual(manifest["adapterState"], "SCAFFOLD_ONLY__NOT_LIVE_PCM")
            approved = (source / "index.html").read_text("utf-8")
            native = (out / "index.html").read_text("utf-8")
            self.assertEqual(native.replace(bundle.ADAPTER_MARKER + "\n", ""), approved)
            self.assertEqual(manifest["approvedPagesHtmlSha256"], bundle.sha256((source / "index.html").read_bytes()))
            self.assertNotEqual(native, (ROOT / "web/index.html").read_text("utf-8"))
            self.assertIn("approved-native-bridge.js", native)
            self.assertIn("arMixGuiReady", (out / "assets/approved-native-bridge.js").read_text("utf-8"))
            for asset in (source / "assets").iterdir():
                self.assertEqual((out / "assets" / asset.name).read_bytes(), asset.read_bytes(),
                                 "Native /preview/ asset differs: " + asset.name)
            for name in ("mixer-control-keys.css", "mixer-control-keys.js", "mute-solo-pfl-56x37.png"):
                button = (ROOT / "site/preview-buttons/assets" / name).read_bytes()
                self.assertEqual((out / "assets" / name).read_bytes(), button)
            self.assertIn("assets/mixer-control-keys.css?rev=", native)
            self.assertNotIn("console-native.html", native)

    def test_manifest_reproducibility_and_byte_map(self):
        with tempfile.TemporaryDirectory(prefix="armix-approved-test-") as tmp:
            a, b = Path(tmp) / "a", Path(tmp) / "b"
            first = bundle.stage(a)
            second = bundle.stage(b)
            self.assertEqual(first, second)
            self.assertEqual(bundle.verify(a), first)
            header = (a / "ApprovedGuiResources.h").read_text("utf-8")
            self.assertIn("BinaryData::index_html", header)
            self.assertIn("BinaryData::mixercontrolkeys_css", header)
            self.assertIn("BinaryData::mixercontrolkeys_js", header)
            self.assertIn('"/assets/mute-solo-pfl-56x37.png"', header)
            for entry in first["files"]:
                self.assertEqual(bundle.sha256((a / entry["path"]).read_bytes()), entry["sha256"])
            (a / "index.html").write_text("WRONG GUI", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "STALE OR WRONG APPROVED GUI BUNDLE"):
                bundle.verify(a)

    def test_native_binary_never_routes_to_another_renderer(self):
        cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
        cpp = (ROOT / "src/Main.cpp").read_text("utf-8")
        self.assertIn("build_approved_native_gui.py", cmake)
        self.assertIn("ARMIX_APPROVED_GUI_DIR", cmake)
        self.assertNotIn("juce_add_binary_data(ARMixAssets SOURCES\n    web/index.html", cmake)
        self.assertNotIn("web/console-native.html web/console-native.css", cmake)
        self.assertIn("resolveApprovedGuiResource (path, data, size, mime)", cpp)
        self.assertNotIn("BinaryData::consolenative_html", cpp)
        self.assertNotIn("BinaryData::index_html", cpp)
        self.assertIn("withResourceProvider (assetFor)", cpp)


if __name__ == "__main__":
    unittest.main()
