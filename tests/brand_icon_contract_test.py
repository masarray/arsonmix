"""Small deterministic branding asset contract: no Pillow/Windows required."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class BrandIconContract(unittest.TestCase):
    def test_alpha_sizes_and_ico(self):
        with tempfile.TemporaryDirectory(prefix="arsonmix-brand-") as folder:
            out = Path(folder)
            subprocess.run([sys.executable, str(ROOT / "scripts/generate_brand_icons.py"),
                            "--source", str(ROOT / "assets/icon/brand-source-128.png"),
                            "--out", str(out)], check=True, capture_output=True)
            for size in (16, 24, 32, 48, 64, 128, 180, 192, 256, 512, 1024):
                data = (out / f"arsonmix-{size}.png").read_bytes()
                self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
                self.assertEqual(struct.unpack_from(">II", data, 16), (size, size))
                self.assertEqual(data[25], 6)  # RGBA, external alpha supported.
            ico = (out / "favicon.ico").read_bytes()
            self.assertEqual(struct.unpack_from("<HHH", ico, 0), (0, 1, 7))
            for index in range(7):
                width, height, _, _, _, _, length, offset = struct.unpack_from(
                    "<BBBBHHII", ico, 6 + index * 16)
                self.assertTrue(length > 100)
                self.assertEqual(ico[offset:offset + 8], b"\x89PNG\r\n\x1a\n")
            data = json.loads((out / "site.webmanifest").read_text("utf-8"))
            self.assertEqual([icon["sizes"] for icon in data["icons"]], ["192x192", "512x512"])
            self.assertTrue((out / "apple-touch-icon.png").exists())

if __name__ == "__main__":
    unittest.main()
