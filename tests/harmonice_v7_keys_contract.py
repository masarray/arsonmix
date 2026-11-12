"""Approved v9 channel keys: MUTE / SOLO / PFL in MAIN MIX and SOF.

Checks user-signed PNG bytes and source/native safety contracts. Does not claim
native PFL monitoring, DSP admission, or Windows hardware FAT.
"""
from pathlib import Path
import hashlib
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / "site/preview-buttons/assets"
HTML = (ROOT / "site/preview-v2/index.html").read_text(encoding="utf-8")
CSS = (OWNER / "mixer-control-keys.css").read_text(encoding="utf-8")
BUILD = (ROOT / "scripts/build_pages.py").read_text(encoding="utf-8")
BRIDGE = (ROOT / "web/approved-native-bridge.js").read_text(encoding="utf-8")
OLD = "harmonice-thin-flat-v9-approved-off-shadow-57x30.png"
PFL = "harmonice-thin-flat-v9-pfl-font-matched-57x30.png"


class HarmoniceV9KeysTests(unittest.TestCase):
    def test_exact_approved_artwork(self):
        original = (OWNER / OLD).read_bytes()
        pfl = (OWNER / PFL).read_bytes()
        self.assertEqual(original[:8], bytes.fromhex("89504e470d0a1a0a"))
        self.assertEqual(struct.unpack(">II", original[16:24]), (228, 90))
        self.assertEqual(hashlib.sha256(original).hexdigest(),
            "3a7b179fd9cc028f988215ea7f3b9e4a73a68031a7051d6e670df01aea940a6a")
        self.assertEqual(struct.unpack(">II", pfl[16:24]), (114, 30))
        self.assertEqual(pfl[25], 6)
        self.assertEqual(hashlib.sha256(pfl).hexdigest(),
            "1ede18534fa678461ebcb1a100a61170700d75048b11607600cf09d9de8872fd")
        self.assertIn('background-image:url("' + OLD + '")', CSS)
        self.assertIn('background-image:url("' + PFL + '")', CSS)
        self.assertIn("background-size:400% 300%", CSS)
        self.assertIn("background-size:200% 100%", CSS)
        self.assertIn("drop-shadow(0 .8px 1.15px", CSS)
        self.assertIn('[data-hw-kind="pfl"][aria-pressed="true"]', CSS)
        self.assertIn("sprite='" + OLD + "'", BUILD)
        self.assertIn("pfl_sprite='" + PFL + "'", BUILD)

    def test_visible_main_and_sof_keys(self):
        self.assertIn("const showPfl=t.kind!=='master'", HTML)
        self.assertIn('data-hw-kind="mute"', HTML)
        self.assertIn('data-hw-kind="solo"', HTML)
        self.assertIn('data-hw-kind="pfl"', HTML)
        self.assertIn('if(showPfl)code+=', HTML)
        self.assertNotIn('data-hw-kind="tap"', HTML)
        self.assertIn('data-tap-applicable="false"', HTML)
        self.assertIn('id="clearCue" hidden disabled', HTML)
        self.assertIn("t.cue=!t.cue", HTML)
        self.assertNotIn("if(!sofEnabled){tracks.forEach(channel=>{channel.cue=false;});master.cue=false;}", HTML)
        self.assertIn("if(t.kind==='master'||window.__JUCE__?.backend)", HTML)

    def test_send_tap_kept_per_wire_not_on_channel_keys(self):
        self.assertIn("wireTapMode:'send'", HTML)
        self.assertIn('data-ws-tap=', HTML)
        self.assertIn("wireGraphCommit(", HTML)
        self.assertNotIn("if(a==='tapMode')", HTML)
        self.assertNotIn('data-action="tapMode"', HTML)
        self.assertIn('if(action==="tapMode"){', BRIDGE)
        self.assertIn('routing unchanged', BRIDGE)

    def test_native_cue_fails_closed(self):
        self.assertIn("PFL requires a separate MONITOR L/R pair", BRIDGE)
        self.assertIn('PFL unavailable · independent native MONITOR L/R not admitted', HTML)
        self.assertIn("(isNative?'disabled':'')", HTML)


if __name__ == "__main__":
    unittest.main()
