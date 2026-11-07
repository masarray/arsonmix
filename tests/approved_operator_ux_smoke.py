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
            self.assertIn("position:fixed;z-index:115;left:8px",chrome)
            self.assertIn("max-height:calc(100dvh - 16px);overflow-y:auto",chrome)
            self.assertEqual(compiled.count('id="channelIdentity"'),1)
            self.assertLess(
                compiled.index('</div>\n    <div id="channelIdentity"'),
                compiled.index('<script src="assets/wire-graph-model.js'))
            self.assertIn("focus({preventScroll:true})",compiled)
            self.assertIn("positionChannelIdentity(dialog,anchor)",compiled)
            self.assertIn("installConsoleSplit",compiled)
            self.assertIn("ResizeObserver",compiled)
            self.assertIn("MutationObserver",compiled)
            self.assertIn("--ux-fx-balanced-h',fx+'px'",compiled)
            self.assertIn("--ux-fx-balanced-h",chrome)
            self.assertIn("calc(50%",chrome)
            self.assertIn(".console-body.collapsed-route{--route-h:23px",chrome)
            self.assertIn("position:fixed!important",plugin)
            self.assertNotIn(".plugin-browser.minimized",plugin)
            self.assertIn("calc(100vw - 28px)",plugin)
            self.assertIn("calc(100dvh - 90px)",plugin)
            self.assertIn("#pluginBrowser #pluginClose:active",plugin)
            self.assertIn("background:#962f38!important",plugin)
            # No extra CSS file, one canonical assembler, source history untouched.
            self.assertEqual((source/"index.html").read_text(encoding="utf-8"),html_before)
            self.assertEqual((source/"assets/mixer-chrome-material.css").read_bytes(),chrome_before)
            self.assertEqual(len(list(assets.iterdir())),2)
    def test_native_peq_close_matches_mixer_floating_windows(self):
        peq=(ROOT/"site/preview-v2/assets/mixer-native-peq.js").read_text(encoding="utf-8")
        css=(ROOT/"site/preview-v2/assets/mixer-fx-rack.css").read_text(encoding="utf-8")
        self.assertIn('close.innerHTML=',peq)
        self.assertIn('M6.4,19L5,17.6L10.6,12',peq)
        self.assertIn('.mx-peq .mx-peq-close:active',css)
        self.assertIn('background:#962f38',css)
        self.assertIn('width:29px;height:29px',css)

    # Native identity/ASIO assertions remain owned by #418's Windows branch.
    # Public Pages qualification checks only GUI composition, never native DSP.

if __name__=="__main__":
    unittest.main()
