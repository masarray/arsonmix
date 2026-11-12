"""Harmonice v7 approved PNG button integration — fast source/asset guard.

Tests one owner, physical PNG, PFL hidden (not deleted), atomic reference tap
transactions and native fail-closed behavior. This is NOT hardware PCM proof.
"""
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / "site/preview-buttons/assets"
HTML = (ROOT / "site/preview-v2/index.html").read_text(encoding="utf-8")
CSS = (OWNER / "mixer-control-keys.css").read_text(encoding="utf-8")
BUILD = (ROOT / "scripts/build_pages.py").read_text(encoding="utf-8")
BRIDGE = (ROOT / "web/approved-native-bridge.js").read_text(encoding="utf-8")
ASSET = "harmonice-thin-flat-v7-57x30.png"


class HarmoniceV7KeysTests(unittest.TestCase):
    def test_one_png_owner_with_alpha_and_correct_frames(self):
        png = (OWNER / ASSET).read_bytes()
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack(">II", png[16:24]), (114, 90))
        self.assertIn(png[25], (3, 6))
        if png[25] == 3:
            self.assertIn(b"tRNS", png)
        self.assertIn('background-image:url("' + ASSET + '")', CSS)
        self.assertIn("aspect-ratio:57/30", CSS)
        self.assertIn("background-size:200% 300%", CSS)
        for kind, pos in (("tap", "0%"), ("mute", "50%"), ("solo", "100%")):
            self.assertIn('data-hw-kind="' + kind + '"]{--mx-native-y:' + pos + '}', CSS)
        self.assertIn("sprite='" + ASSET + "'", BUILD)

    def test_three_visible_keys_and_cue_state_kept_hidden(self):
        for action in ("mute", "solo", "tapMode"):
            self.assertIn('data-action="' + action + '"', HTML)
        self.assertNotIn('data-hw-kind="cue"', HTML)
        self.assertIn('id="clearCue" hidden', HTML)
        self.assertIn("master.cue=false", HTML)
        self.assertIn("cue:false", HTML)

    def test_reference_prepost_atomic_and_main_independent(self):
        self.assertIn("wireTapMode:'send'", HTML)
        self.assertIn("sends.map(edge=>({type:'MODE'", HTML)
        self.assertIn("edge.kind==='send'&&edge.destId!==master.id", HTML)
        self.assertIn("'primary MAIN unchanged", HTML)
        self.assertIn("t.kind==='master'?'MAIN is always post-fader", HTML)
        self.assertIn("const next=sends.length?(allPost?'split':'send')", HTML)

    def test_native_pcm_never_fakes_prepost_state(self):
        self.assertIn('if(action==="tapMode"){', BRIDGE)
        self.assertIn('routing unchanged', BRIDGE)
        self.assertIn("if(window.__JUCE__?.backend){", HTML)
        self.assertIn("PRE/POST is unavailable for this native graph", HTML)


if __name__ == "__main__":
    unittest.main()
