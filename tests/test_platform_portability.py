"""Cheap native port and sign-free release gates; no compiler or macOS runner."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.check_platform_portability import blockers


class PlatformReleaseContractTest(unittest.TestCase):
    def test_native_adapters_and_package_scripts(self):
        for platform in ("linux", "macos"):
            with self.subTest(platform=platform):
                self.assertEqual([], blockers(platform, ROOT))

    def test_release_comment_only_never_push_matrix(self):
        workflow = (ROOT / ".github/workflows/crossplatform-native-qualification.yml").read_text()
        controller = (ROOT / ".github/workflows/release-control.yml").read_text()
        self.assertIn("workflow_dispatch:", workflow)
        self.assertNotIn("\n  push:", workflow)
        self.assertNotIn("\n  pull_request:", workflow)
        self.assertIn("issue_comment:", controller)
        self.assertIn("Release tag/source SHA mismatch", workflow)
        self.assertIn("Actual platform JUCE compile", workflow)
        self.assertIn("ctest --test-dir build", workflow)
        self.assertIn("git ls-remote", workflow)
        self.assertNotIn("matrix:", workflow)

    def test_macos_tcc_microphone_permission_required(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        macos = (ROOT / "scripts/package_native_macos.sh").read_text()
        self.assertIn("MICROPHONE_PERMISSION_ENABLED TRUE", cmake)
        self.assertIn("MICROPHONE_PERMISSION_TEXT", cmake)
        self.assertIn("NSMicrophoneUsageDescription", macos)
        self.assertIn("Missing macOS microphone privacy prompt", macos)

    def test_free_mac_signing_and_real_linux_binary_gates(self):
        mac = (ROOT / "scripts/package_native_macos.sh").read_text()
        linux = (ROOT / "scripts/package_native_linux.sh").read_text()
        self.assertIn("codesign --force --deep --sign - --timestamp=none", mac)
        self.assertNotIn("notarytool", mac)
        self.assertIn("Mach-O", mac)
        self.assertIn("lipo -archs", mac)
        self.assertIn("arch=universal", mac)
        workflow=(ROOT / ".github/workflows/crossplatform-native-qualification.yml").read_text()
        self.assertIn("CMAKE_OSX_ARCHITECTURES=arm64;x86_64", workflow)
        self.assertIn("hdiutil verify", mac)
        self.assertIn("ELF 64-bit", linux)
        self.assertIn("readelf -h", linux)
        self.assertIn("ldd", linux)
        self.assertIn("sourceCommit", linux)
        self.assertIn("sourceCommit", mac)


if __name__ == "__main__":
    unittest.main()
