import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

SPEC = importlib.util.spec_from_file_location("trust", Path(__file__).resolve().parents[1] / "scripts/release_trust_manifest.py")
trust = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(trust)
TAG = "v0.5.0-alpha.99"
SHA = "a" * 40

class TrustTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, data):
        (self.root / name).write_text(json.dumps(data), encoding="utf-8")

    def fixture(self, platform, tag=TAG):
        binaries, meta, sums = trust.layout(platform, tag)
        if platform == "windows":
            (self.root / binaries[0]).write_bytes(b"synthetic installer")
            with zipfile.ZipFile(self.root / binaries[1], "w") as z:
                z.write(self.root / binaries[0], binaries[0])
            gui = b"synthetic gui"
            with zipfile.ZipFile(self.root / binaries[2], "w") as z:
                z.writestr("ArSonMix.exe", gui)
            h = hashlib.sha256(gui).hexdigest()
            self.write(meta[0], {"sourceCommit": SHA, "exeSha256": h, "executable": "ArSonMix.exe"})
            self.write(meta[1], {"sourceCommit": SHA, "guiSha256": h, "installerSha256": trust.file_hash(self.root / binaries[0])})
            self.write(meta[2], {"test": True})
        else:
            (self.root / binaries[0]).write_bytes(b"synthetic package")
            self.write(meta[0], {"sourceCommit": SHA, "tag": tag, "platform": platform, "asset": binaries[0],
                                 "assetSha256": trust.file_hash(self.root / binaries[0])})
        (self.root / sums).write_text("".join(f"{trust.file_hash(self.root / n)}  {n}\n" for n in binaries))
        return binaries, meta, sums

    def test_windows_manifest_and_transferred_bytes(self):
        self.fixture("windows")
        m = trust.calculate(self.root, "windows", TAG, SHA)
        self.assertEqual(len(m["assets"]), 7)
        self.assertEqual(m["portableFileInventory"][0]["path"], "ArSonMix.exe")
        self.write("RELEASE_TRUST-windows.json", m)
        self.assertEqual(trust.calculate(self.root, "windows", TAG, SHA, True), m)

    def test_beta_windows_manifest_preserves_alpha_trust_contract(self):
        tag = 'v0.5.0-beta.1'
        self.fixture('windows', tag)
        manifest = trust.calculate(self.root, 'windows', tag, SHA)
        self.assertEqual(len(manifest['assets']), 7)
        self.write('RELEASE_TRUST-windows.json', manifest)
        self.assertEqual(trust.calculate(self.root, 'windows', tag, SHA, True), manifest)
        with self.assertRaises(ValueError):
            trust.calculate(self.root, 'windows', 'v0.5.0-rc.1', SHA)

    def test_linux_and_macos(self):
        for platform in ("linux", "macos"):
            with self.subTest(platform=platform):
                self.fixture(platform)
                self.assertEqual(len(trust.calculate(self.root, platform, TAG, SHA)["assets"]), 3)
                for f in self.root.iterdir():
                    f.unlink()

    def test_tampered_bytes(self):
        binaries, _, _ = self.fixture("windows")
        (self.root / binaries[0]).write_bytes(b"altered")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            trust.calculate(self.root, "windows", TAG, SHA)

    def test_wrong_source(self):
        self.fixture("linux")
        with self.assertRaisesRegex(ValueError, "provenance mismatch"):
            trust.calculate(self.root, "linux", TAG, "b"*40)

    def test_extra_file_and_invalid_tag(self):
        self.fixture("linux")
        (self.root / "unknown.txt").write_text("extra")
        with self.assertRaisesRegex(ValueError, "Unexpected"):
            trust.calculate(self.root, "linux", TAG, SHA)
        with self.assertRaisesRegex(ValueError, "Invalid"):
            trust.calculate(self.root, "linux", "../tag", SHA)

    def test_duplicate_checksum(self):
        _, _, sums = self.fixture("macos")
        p = self.root / sums
        p.write_text(p.read_text()*2)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            trust.calculate(self.root, "macos", TAG, SHA)

    def test_zip_traversal_and_duplicate(self):
        for names in [("../escape",), ("App.exe", "app.exe")]:
            path = self.root / "bad.zip"
            with zipfile.ZipFile(path, "w") as z:
                for n in names:
                    z.writestr(n, b"test")
            with self.assertRaises(ValueError):
                trust.zip_inventory(path)

    def test_windows_aliases_and_file_directory_collisions(self):
        for names in [("CON.txt",), ("file. ",), ("dir/LPT1.bin",),
                      ("App", "app/child"), ("x?y",), ("x\u0001y",)]:
            with self.subTest(names=names):
                path = self.root / "bad.zip"
                with zipfile.ZipFile(path, "w") as z:
                    for name in names:
                        z.writestr(name, b"test")
                with self.assertRaises(ValueError):
                    trust.zip_inventory(path)

    def test_duplicate_metadata_keys(self):
        self.fixture("linux")
        path = self.root / "LINUX_PROVENANCE.json"
        path.write_text('{"sourceCommit":"a","sourceCommit":"b"}')
        with self.assertRaisesRegex(ValueError, "Duplicate JSON"):
            trust.calculate(self.root, "linux", TAG, SHA)

    def test_draft_uploaded_asset_identity(self):
        self.fixture("windows")
        m = trust.calculate(self.root, "windows", TAG, SHA)
        self.write("RELEASE_TRUST-windows.json", m)
        release = {"tag_name": TAG, "target_commitish": SHA, "draft": True,
                   "assets": [{"name": p.name, "size": p.stat().st_size, "state": "uploaded",
                               "digest": "sha256:" + trust.file_hash(p)} for p in self.root.iterdir()]}
        trust.verify_uploaded_draft(self.root, release, TAG, SHA)
        release["assets"][0]["digest"] = "sha256:" + "0"*64
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            trust.verify_uploaded_draft(self.root, release, TAG, SHA)

    def test_manifest_changes_when_metadata_changes(self):
        _, meta, _ = self.fixture("linux")
        old = trust.calculate(self.root, "linux", TAG, SHA)
        self.write("RELEASE_TRUST-linux.json", old)
        p = self.root / meta[0]
        d = json.loads(p.read_text()); d["new"] = "altered"; self.write(meta[0], d)
        self.assertNotEqual(trust.calculate(self.root, "linux", TAG, SHA, True), old)

if __name__ == "__main__":
    unittest.main()
