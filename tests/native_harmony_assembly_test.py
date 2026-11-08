#!/usr/bin/env python3
"""Harmony single-approved-GUI, native authority and editor-ACK contracts."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from compose_native_peq import apply_native_peq
from approved_fx_rack_power import apply_authoritative_fx_power

class NativeHarmonyAssemblyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original=(ROOT/"site/preview-v2/index.html").read_text("utf-8")
        cls.html=apply_native_peq(apply_authoritative_fx_power(cls.original))
        cls.ui=(ROOT/"site/preview-v2/assets/mixer-native-harmony.js").read_text("utf-8")
        cls.css=(ROOT/"site/preview-v2/assets/mixer-fx-rack.css").read_text("utf-8")
        cls.bridge=(ROOT/"web/approved-native-bridge.js").read_text("utf-8")

    def test_single_approved_renderer(self):
        self.assertEqual(self.html.count("function renderFastPicker(){"),1)
        self.assertEqual(self.html.count("function insertChosenNativeHarmony(){"),1)
        self.assertEqual(self.html.count('data-fast-native-harmony="1"'),1)
        self.assertEqual(self.html.count('src="assets/mixer-native-harmony.js?v=harmony1"'),1)
        self.assertNotIn('href="assets/mixer-native-harmony.css',self.html)
        self.assertIn(".mx-harm[hidden]{display:none}",self.css)
        self.assertNotIn("new AudioContext",self.ui)

    def test_native_catalog_bridge_and_editor(self):
        self.assertIn("armix.builtin.arson-harmony.v1",self.bridge)
        self.assertIn("window.ArSonMixNativeHarmony?.open(t,slot)",self.bridge)
        self.assertIn("window.ArSonMixNativeHarmony.insert(activeFxTarget)",self.html)
        self.assertIn("arMixBuiltinHarmonyQueue",self.ui)
        self.assertIn("arMixBuiltinHarmonyAck",self.ui)
        self.assertIn("arMixBuiltinHarmonySnapshot",self.ui)
        self.assertIn("if(pending&&(!pending.ack||snap.appliedRequestId!==pending.id))return;",self.ui)
        for value in ("key","scale","voices","correction","wet","space","color","gainDb"):
            self.assertIn(value,self.ui)

    def test_no_fabricated_native_pcm(self):
        self.assertIn("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO",self.ui)
        self.assertIn("if(!native())",self.ui)
        self.assertIn("if(native()&&n?.admitted",self.ui)
        self.assertIn("Native Harmony host not ready · audio unchanged",self.html)
        with self.assertRaises(ValueError):
            apply_native_peq(self.html)

if __name__=="__main__":
    unittest.main()
