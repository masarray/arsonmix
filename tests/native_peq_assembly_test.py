#!/usr/bin/env python3
"""Native PEQ vertical-integration invariants in the one approved Pages GUI."""
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from compose_native_peq import apply_native_peq
from approved_fx_rack_power import apply_authoritative_fx_power

class NativeParametricEqIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original=(ROOT/"site/preview-v2/index.html").read_text("utf-8")
        cls.assembled=apply_native_peq(apply_authoritative_fx_power(cls.original))

    def test_one_authoritative_mixer(self):
        self.assertEqual(self.assembled.count("function sectionFx(t){"),1)
        self.assertEqual(self.assembled.count("function renderFastPicker(){"),1)
        self.assertEqual(self.assembled.count("function insertChosenNativeEq(){"),1)
        self.assertEqual(self.assembled.count('id="fastPicker"'),1)
        self.assertNotIn("preview-v3",self.assembled)
        self.assertEqual(self.assembled.count('src="assets/mixer-native-peq.js?v=eq-graph1"'),1)

    def test_native_rack_uses_regular_insert_slot(self):
        self.assertIn("bridge.insertPlugin('armix.builtin.channel-eq.v1',activeFxTarget)",self.assembled)
        self.assertIn("if(t.fx[index]==='Parametric EQ')",self.assembled)
        # Does not fabricate a second EQ rack row or a special before-VST bank.
        self.assertNotIn("builtinEq.slotMarkup",self.assembled)
        self.assertNotIn("PRE VST3",self.assembled)
        self.assertEqual(self.assembled.count('data-fast-native-eq="1"'),1)

    def test_single_style_owner(self):
        # The approved CSS compiler requires exactly 18 component-owner links.
        # Native EQ styles live within the existing FX rack owner, not a 19th link.
        self.assertNotIn('href="assets/mixer-native-peq.css',self.assembled)
        css=(ROOT/"site/preview-v2/assets/mixer-fx-rack.css").read_text("utf-8")
        self.assertIn(".mx-peq[hidden]{display:none}",css)
        self.assertIn("grid-template-columns:minmax(0,1fr) 143px",css)
        script=(ROOT/"site/preview-v2/assets/mixer-native-peq.js").read_text("utf-8")
        self.assertIn('const LOG=Math.log(1000)',script)
        self.assertIn('document.createElementNS(NS,"svg")',script)
        self.assertIn('arMixBuiltinEqAck',script)
        self.assertNotIn("new AudioContext",script)

    def test_fail_closed_on_repeat(self):
        with self.assertRaises(ValueError):
            apply_native_peq(self.assembled)
        with self.assertRaises(ValueError):
            apply_native_peq(self.original.replace("function renderFastPicker(){",
                "function renderFastPickerRenamed(){"))

if __name__=="__main__":
    unittest.main()
