"""Check committed icon package and landing-page references."""
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
        head = (ROOT / "site/index.html").read_text(encoding="utf-8").split("</head>", 1)[0]
        for name in ("favicon.ico", "favicon-16x16.png", "favicon-32x32.png",
                     "apple-touch-icon.png", "site.webmanifest"):
            self.assertIn(name, head)
if __name__ == "__main__":
    unittest.main()
