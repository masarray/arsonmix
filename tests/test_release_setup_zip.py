"""Offline byte-identity and archive-entry verification fixtures."""
import pathlib
import tempfile
import unittest
import zipfile
import importlib.util
ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_zip", ROOT / "scripts/verify_release_setup_zip.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
verify = module.verify


class VerifyInstallerZip(unittest.TestCase):
    def test_one_flat_file_and_exact_exe_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = pathlib.Path(temp)
            exe = folder / "ArSonMix-v0.5.0-alpha.5-Windows-x64-Setup.exe"
            archive = exe.with_suffix(".zip")
            exe.write_bytes(b"Verified-Inno-EXE-" * 150000)
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
                z.write(exe, exe.name)
            self.assertEqual(len(verify(exe, archive)), 64)
            exe.write_bytes(b"Corrupted-Inno-EXE" * 150000)
            with self.assertRaisesRegex(ValueError, "differs"):
                verify(exe, archive)

    def test_nested_or_additional_unrelated_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = pathlib.Path(temp)
            exe = folder / "ArSonMix-v0.5.0-alpha.5-Windows-x64-Setup.exe"
            archive = exe.with_suffix(".zip")
            exe.write_bytes(b"A" * 2200000)
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr("subfolder/" + exe.name, exe.read_bytes())
            with self.assertRaisesRegex(ValueError, "one flat"):
                verify(exe, archive)
            with zipfile.ZipFile(archive, "w") as z:
                z.write(exe, exe.name)
                z.writestr("unapproved.cmd", "echo hack")
            with self.assertRaisesRegex(ValueError, "one flat"):
                verify(exe, archive)

    def test_identity_and_invalid_zip_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = pathlib.Path(temp)
            exe = folder / "ArSonMix-v0.5.0-alpha.5-Windows-x64-Setup.exe"
            exe.write_bytes(b"A" * 2200000)
            (folder / "other.zip").write_bytes(b"x")
            with self.assertRaisesRegex(ValueError, "identity"):
                verify(exe, folder / "other.zip")
            bad = exe.with_suffix(".zip")
            bad.write_text("not a real zip")
            with self.assertRaises(zipfile.BadZipFile):
                verify(exe, bad)


if __name__ == "__main__":
    unittest.main()
