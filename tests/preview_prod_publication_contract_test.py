"""Fast archive contract: only explicit /preview-prod/ is frozen; /preview/ remains live."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from publish_preview_prod import NEW_SPRITE, OLD_SPRITE, PINNED_SHA, publish


def verify(pages_root: Path) -> None:
    pages_root = pages_root.resolve()
    latest = pages_root / "preview"
    archive = pages_root / "preview-prod"
    old = json.loads((archive / "build.json").read_text("utf-8"))
    new = json.loads((latest / "build.json").read_text("utf-8"))
    assert old["sha"] == old["archived_source_sha"] == PINNED_SHA
    assert old["kind"] == "FROZEN_PRE_V7_GUI"
    assert old["canonical_url"] == "/preview-prod/" and old["frozen"] is True
    assert old["live_audio"] is False
    assert new["kind"] == "APPROVED_SINGLE_GUI"
    assert new["canonical_url"] == "/preview/"
    assert new["sha"] != PINNED_SHA, "latest preview accidentally rewound"
    assert (archive / "assets" / OLD_SPRITE).is_file()
    assert not (archive / "assets" / NEW_SPRITE).exists()
    assert (latest / "assets" / NEW_SPRITE).is_file()
    historic_html = (archive / "index.html").read_text("utf-8")
    latest_html = (latest / "index.html").read_text("utf-8")
    assert 'data-hw-kind="cue"' in historic_html
    assert 'data-hw-kind="tap"' not in historic_html
    assert 'data-hw-kind="pfl"' in latest_html
    assert 'data-hw-kind="tap"' not in latest_html
    assert "const showPfl=t.kind!=='master'" in latest_html
    assert 'data-hw-kind="cue"' not in latest_html
    assert (archive / "assets" / "arsonmix-approved-runtime.css").is_file()
    assert (archive / "assets" / "mixer-control-keys.js").is_file()
    assert (archive / "assets" / "mixer-control-keys.css").is_file()
    assert (archive / "index.html").read_bytes() != (latest / "index.html").read_bytes()


class ArchivePublisherTests(unittest.TestCase):
    def test_pinned_old_preview_copied_without_mutating_current(self):
        with tempfile.TemporaryDirectory(prefix="armix-preview-prod-") as temp:
            root = Path(temp)
            old = root / "old" / "preview"
            current = root / "pages" / "preview"
            (old / "assets").mkdir(parents=True)
            (current / "assets").mkdir(parents=True)
            old_html = '<html><div class="console-body" data-hw-kind="cue">PFL</div></html>'
            (old / "index.html").write_text(old_html, "utf-8")
            (old / "assets" / OLD_SPRITE).write_bytes(b"original-old-png")
            (old / "assets" / "mixer-control-keys.css").write_text("old CSS", "utf-8")
            (old / "build.json").write_text(json.dumps({
                "sha": PINNED_SHA, "canonical_url": "/preview/",
                "kind": "APPROVED_SINGLE_GUI",
            }), "utf-8")
            (current / "index.html").write_text("new HTML", "utf-8")
            (current / "assets" / NEW_SPRITE).write_bytes(b"new-png")
            path = publish(old, root / "pages")
            self.assertEqual((path / "index.html").read_text("utf-8"), old_html)
            self.assertEqual((path / "assets" / OLD_SPRITE).read_bytes(),
                             b"original-old-png")
            self.assertEqual((current / "index.html").read_text("utf-8"), "new HTML")
            self.assertEqual((current / "assets" / NEW_SPRITE).read_bytes(),
                             b"new-png")
            manifest = json.loads((path / "build.json").read_text("utf-8"))
            self.assertEqual(manifest["sha"], PINNED_SHA)
            self.assertTrue(manifest["frozen"])
            self.assertEqual(manifest["canonical_url"], "/preview-prod/")

    def test_rejects_wrong_old_sha(self):
        with tempfile.TemporaryDirectory(prefix="armix-preview-prod-") as temp:
            root = Path(temp)
            old = root / "old" / "preview"
            current = root / "pages" / "preview"
            (old / "assets").mkdir(parents=True)
            (current / "assets").mkdir(parents=True)
            (old / "index.html").write_text('console-body data-hw-kind="cue"')
            (old / "assets" / OLD_SPRITE).write_bytes(b"snapshot")
            (old / "build.json").write_text(json.dumps({
                "sha": "not-our-pin", "canonical_url": "/preview/",
                "kind": "APPROVED_SINGLE_GUI",
            }))
            (current / "index.html").write_text("current")
            (current / "assets" / NEW_SPRITE).write_bytes(b"current")
            with self.assertRaises(ValueError):
                publish(old, root / "pages")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        verify(Path(sys.argv[1]))
        print(f"PASS: live /preview/ and frozen /preview-prod/ separated at {PINNED_SHA}")
    else:
        unittest.main()
