# SPDX-License-Identifier: AGPL-3.0-only
"""Regression: FAT must fail when a successful generic build omits ImmerSIB."""
import io
from pathlib import Path
import sys
import tempfile
import unittest
from contextlib import redirect_stdout, redirect_stderr

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from verify_windows_spatial_fx_packaging import (  # noqa: E402
    FLAGS, NATIVE_CATALOG_MARKERS, PNG_MAGIC, SCENES, main,
    require_spatial_products,
)


class WindowsSpatialFxArtifactGate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cache = ("\n".join(f"{flag}:BOOL=ON" for flag in FLAGS) + "\n").encode()
        cls.scenes = {
            name: PNG_MAGIC + (name + "::").encode() * 10
            for name in SCENES
        }
        cls.valid_exe = (
            b"MZ" + b"\0" * (4 * 1024 * 1024)
            + b"armix.builtin.imersip.v1"
            + b"".join(marker + b"\0" for marker in NATIVE_CATALOG_MARKERS)
            + b"".join(cls.scenes.values())
        )

    def test_catalog_flag_and_all_six_real_embedded_scenes_are_required(self):
        evidence = require_spatial_products(self.cache, self.valid_exe, self.scenes)
        self.assertEqual(evidence["catalogId"], "armix.builtin.imersip.v1")
        self.assertEqual(evidence["sceneCount"], "6")

    def test_original_fat_default_off_is_rejected_despite_generic_100_ctests(self):
        cache = self.cache.replace(
            b"ARMIX_IMMERSIB_RELEASE_CANDIDATE:BOOL=ON",
            b"ARMIX_IMMERSIB_RELEASE_CANDIDATE:BOOL=OFF",
        )
        with self.assertRaisesRegex(ValueError, "SPATIAL_FEATURE_DISABLED"):
            require_spatial_products(cache, self.valid_exe, self.scenes)

    def test_both_public_release_flags_must_match_exact_cache_values(self):
        cache = self.cache.replace(b"ARMIX_SONKU_APPROVED_P99_MAIN:BOOL=ON",
                                   b"ARMIX_SONKU_APPROVED_P99_MAIN:BOOL=OFF")
        with self.assertRaisesRegex(ValueError, "SPATIAL_FEATURE_DISABLED"):
            require_spatial_products(cache, self.valid_exe, self.scenes)
        with self.assertRaisesRegex(ValueError, "SPATIAL_FEATURE_DISABLED"):
            require_spatial_products(self.cache + b"ARMIX_IMMERSIB_RELEASE_CANDIDATE:BOOL=OFF\n",
                                     self.valid_exe, self.scenes)

    def test_stale_gui_without_native_descriptors_fails_even_if_flags_are_on(self):
        for marker in NATIVE_CATALOG_MARKERS:
            with self.subTest(marker=marker):
                damaged = self.valid_exe.replace(marker, b"REMOVED_FOR_TEST")
                with self.assertRaisesRegex(ValueError, "SPATIAL_NATIVE_CATALOG_NOT_EMBEDDED"):
                    require_spatial_products(self.cache, damaged, self.scenes)

    def test_missing_compiled_juce_venue_png_fails_even_if_catalog_marker_exists(self):
        missing = self.valid_exe.replace(self.scenes["scene_velvet_cinema"],
                                         b"REMOVED_FOR_TEST")
        with self.assertRaisesRegex(ValueError, "SPATIAL_SCENE_NOT_IN_COMPILED_GUI"):
            require_spatial_products(self.cache, missing, self.scenes)

    def test_invalid_or_missing_preset_asset_fails_closed(self):
        malformed = dict(self.scenes, scene_live_concert=b"not a PNG")
        with self.assertRaisesRegex(ValueError, "SPATIAL_SCENE_INVALID_OR_MISSING"):
            require_spatial_products(self.cache, self.valid_exe, malformed)
        missing = dict(self.scenes)
        del missing["scene_royal_concert_hall"]
        with self.assertRaisesRegex(ValueError, "SPATIAL_SCENE_INVALID_OR_MISSING"):
            require_spatial_products(self.cache, self.valid_exe, missing)

    def test_not_an_executable_cannot_become_a_release_by_adding_strings(self):
        with self.assertRaisesRegex(ValueError, "SPATIAL_NOT_ACTUAL_WIN64_GUI_EXE"):
            require_spatial_products(self.cache, self.valid_exe[3:], self.scenes)
        with self.assertRaisesRegex(ValueError, "SPATIAL_NOT_ACTUAL_WIN64_GUI_EXE"):
            require_spatial_products(self.cache, b"MZtest", self.scenes)

    def test_cli_error_is_nonzero_and_success_reports_approved_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "build").mkdir()
            (root / "scenes").mkdir()
            (root / "build" / "CMakeCache.txt").write_bytes(self.cache)
            (root / "app.exe").write_bytes(self.valid_exe)
            for name, data in self.scenes.items():
                (root / "scenes" / (name + ".png")).write_bytes(data)
            args = ["--build-dir", str(root / "build"),
                    "--exe", str(root / "app.exe"),
                    "--scene-dir", str(root / "scenes")]
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                self.assertEqual(main(args), 0)
            self.assertIn("WINDOWS_SPATIAL_PRODUCT_FAT_PASS", stdout.getvalue())
            (root / "build" / "CMakeCache.txt").write_text(
                "ARMIX_IMMERSIB_RELEASE_CANDIDATE:BOOL=OFF\n")
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                self.assertEqual(main(args), 1)
            self.assertIn("WINDOWS_SPATIAL_PRODUCT_FAT_REJECTED", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
