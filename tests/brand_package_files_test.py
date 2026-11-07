"""Check committed icon package and landing-page references."""
import hashlib
import struct
import subprocess
import sys
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
class BrandPackageFiles(unittest.TestCase):
    def test_committed_package_matches_approved_source(self):
        subprocess.run([sys.executable, str(ROOT / "scripts/sync_brand_assets.py"),
                        "--check"], check=True, capture_output=True)
    def test_physical_ico_and_site_favicons(self):
        ico = (ROOT / "assets/icon/arsonmix-app.ico").read_bytes()
        self.assertEqual(struct.unpack_from("<HHH", ico, 0), (0, 1, 7))
        for i in range(7):
            w, h, _, _, _, _, n, offset = struct.unpack_from("<BBBBHHII", ico, 6 + 16 * i)
            self.assertTrue(n > 100)
            self.assertEqual(ico[offset:offset+8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(ico, (ROOT / "site/favicon.ico").read_bytes())
        self.assertEqual(ico, (ROOT / "assets/icon/favicon.ico").read_bytes())
        master = ROOT / "assets/icon/brand-master-1254.png"
        self.assertEqual(hashlib.sha256(master.read_bytes()).hexdigest(),
                         "04d3be499e404d72518871f7d5062e6a2e57d2a5a12b83190d05d51c940a22da")
        hero = ROOT / "assets/icon/brand-512.png"
        self.assertEqual(hero.read_bytes(), (ROOT / "site/brand-512.png").read_bytes())
        self.assertEqual(struct.unpack_from(">II", hero.read_bytes(), 16), (512, 512))
        self.assertEqual(struct.unpack_from(">II", (ROOT / "site/arsonmix-128.png").read_bytes(), 16), (128, 128))
        head = (ROOT / "site/index.html").read_text(encoding="utf-8").split("</head>", 1)[0]
        self.assertIn('src="./arsonmix-128.png"', (ROOT / "site/index.html").read_text(encoding="utf-8"))
        self.assertIn('src="./brand-512.png"', (ROOT / "site/index.html").read_text(encoding="utf-8"))
        self.assertIn('src="assets/icon/brand-512.png"', (ROOT / "README.md").read_text(encoding="utf-8"))
        for name in ("favicon.ico", "favicon-16x16.png", "favicon-32x32.png",
                     "apple-touch-icon.png", "site.webmanifest"):
            self.assertIn(name, head)
if __name__ == "__main__":
    unittest.main()
