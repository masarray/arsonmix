"""V29 operator-console regression: one public/native GUI, clean source-owner evolution."""
from __future__ import annotations
from pathlib import Path
import sys
import shutil
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from compose_operator_ux import apply_operator_ux, CLOSE_ICON

class OperatorConsoleTests(unittest.TestCase):
    def test_single_composed_gui_stays_faithful_and_editable(self):
        source=ROOT/"site/preview-v2"
        html_before=(source/"index.html").read_text(encoding="utf-8")
        chrome_before=(source/"assets/mixer-chrome-material.css").read_bytes()
        with tempfile.TemporaryDirectory(prefix="armix-operator-") as tmp:
            assets=Path(tmp)
            for name in ("mixer-chrome-material.css","mixer-plugin-browser.css"):
                shutil.copy2(source/"assets"/name,assets/name)
            compiled=apply_operator_ux(html_before,assets)
            chrome=(assets/"mixer-chrome-material.css").read_text(encoding="utf-8")
            plugin=(assets/"mixer-plugin-browser.css").read_text(encoding="utf-8")
            self.assertEqual(compiled.count('id="trackCount"'),1)
            self.assertIn('<header class="window-title">',compiled)
            self.assertNotIn("OPERATOR-FIRST",compiled)
            self.assertNotIn('<div class="mixer-title">',compiled)
            self.assertNotIn('id="pluginMinimize"',compiled)
            self.assertIn("const density='comfort'",compiled)
            self.assertIn('id="densitySelect"',compiled) # stable legacy JS contract, hidden
            self.assertIn('id="densitySelect" class="density-select" hidden',compiled)
            self.assertEqual(compiled.count(CLOSE_ICON),6)
            self.assertIn("V29 adaptive 50/50 console",chrome)
            self.assertIn("--ux-fx-balanced-h",chrome)
            self.assertIn("calc(50%",chrome)
            self.assertIn(".console-body.collapsed-route{--route-h:23px",chrome)
            self.assertIn("position:fixed!important",plugin)
            self.assertNotIn(".plugin-browser.minimized",plugin)
            self.assertIn("calc(100vw - 28px)",plugin)
            self.assertIn("calc(100dvh - 90px)",plugin)
            # No extra CSS file, one canonical assembler, source history untouched.
            self.assertEqual((source/"index.html").read_text(encoding="utf-8"),html_before)
            self.assertEqual((source/"assets/mixer-chrome-material.css").read_bytes(),chrome_before)
            self.assertEqual(len(list(assets.iterdir())),2)
    def test_native_hooks_and_audio_input_default(self):
        build=(ROOT/"scripts/build_approved_native_gui.py").read_text(encoding="utf-8")
        bridge=(ROOT/"web/approved-native-bridge.js").read_text(encoding="utf-8")
        engine=(ROOT/"src/LiveAudioEngine.h").read_text(encoding="utf-8")
        self.assertIn("openChannelIdentity,closeChannelIdentity",build)
        self.assertIn("?.applyIdentity?.())return;",build)
        self.assertIn("scope?.openChannelIdentity?.(t,event.target)",bridge)
        self.assertIn("const identityStoreKey=",bridge)
        self.assertIn("channelPresentation.set(nativeId",bridge)
        self.assertIn("deviceManager_.initialise (2, 2, savedSettings, !savedRoute)",engine)
        self.assertIn("active.enabledInputs",bridge)
        self.assertIn("data-audio-asio4all-hint",bridge)
        self.assertIn("armix-floating-close",bridge)
        self.assertNotIn("A virtual cable does not automatically play through headphones",bridge)

if __name__=="__main__":
    unittest.main()
