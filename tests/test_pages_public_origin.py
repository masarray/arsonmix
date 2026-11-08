"""Cloudflare / GitHub Pages dual-base contract without another CI lane."""
import os
from pathlib import Path
import sys
import unittest
from urllib.parse import urljoin

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from pages_public_origin import (DEFAULT_PUBLIC_URL, OG_IMAGE, OG_URL,
                                 normalize_public_url, render_landing_metadata,
                                 sitemap_xml, robots_txt)

DIST = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "dist-pages"


class PagesPublicOriginTest(unittest.TestCase):
    def test_origin_sanitizer(self):
        self.assertEqual(normalize_public_url(None), DEFAULT_PUBLIC_URL)
        self.assertEqual(normalize_public_url("https://masarray.github.io/arsonmix"),
                         DEFAULT_PUBLIC_URL)
        self.assertEqual(normalize_public_url("https://mix.example.org"),
                         "https://mix.example.org/")
        for invalid in ("http://mix.example.org", "https://a.example/?x=1",
                        "https://user:a@mix.example.org/", "https://x.example:8443/",
                        "https://x.example/a/../b/", "https://x.example/#fragment"):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    normalize_public_url(invalid)

    def test_metadata_and_host_independent_links(self):
        original = (ROOT / "site/index.html").read_text(encoding="utf-8")
        self.assertIn(OG_IMAGE, original)
        self.assertIn(OG_URL, original)
        for base in (DEFAULT_PUBLIC_URL, "https://mix.example.org/"):
            with self.subTest(base=base):
                compiled = render_landing_metadata(original, base)
                self.assertIn('<link rel="canonical" href="' + base + '">', compiled)
                self.assertIn('<meta property="og:url" content="' + base + '">', compiled)
                self.assertIn('<meta property="og:image" content="' + base + 'brand-512.png">', compiled)
                for resource in ("./preview/", "./downloads.js", "./landing.css",
                                 "./release-catalog.json", "./media/console-overview.jpg"):
                    self.assertEqual(urljoin(base, resource), base + resource[2:])
                self.assertIn(base + "preview/", sitemap_xml(base))
                self.assertIn(base + "sitemap.xml", robots_txt(base))

    def test_actual_distribution(self):
        base = normalize_public_url(os.environ.get("ARSONMIX_PUBLIC_URL"))
        self.assertTrue((DIST / "index.html").is_file(), "Build dist-pages first")
        html = (DIST / "index.html").read_text(encoding="utf-8")
        self.assertIn('<link rel="canonical" href="' + base + '">', html)
        self.assertIn('<meta property="og:url" content="' + base + '">', html)
        self.assertIn('<meta property="og:image" content="' + base + 'brand-512.png">', html)
        self.assertEqual((DIST / "sitemap.xml").read_text(encoding="utf-8"), sitemap_xml(base))
        self.assertEqual((DIST / "robots.txt").read_text(encoding="utf-8"), robots_txt(base))
        self.assertTrue((DIST / "preview/index.html").is_file())
        for name in ("./preview/", "./downloads.js", "./landing.css"):
            self.assertIn(name, html)
        self.assertNotIn('href="/preview/"', html)
        self.assertNotIn('href="/arsonmix/preview/"', html)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
