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
        self.assertIn("if(pending&&(!pending.ack||snap.appliedRequestId!==pending.id)){",self.ui)
        for value in ("key","scale","voices","correction","wet","space","color","gainDb"):
            self.assertIn(value,self.ui)

    def test_music_track_is_real_native_accepted_pcm_not_js_audio(self):
        self.assertIn('id="mxHGuideSource"',self.ui)
        self.assertIn('Manual Key',self.ui)
        self.assertIn('guideChannelId',self.ui)
        self.assertIn('guideDetectedKey',self.ui)
        self.assertIn('arMixConsoleSnapshot',self.ui)
        bank=(ROOT/"engine/core/DynamicChannelFxLeaseBank.h").read_text("utf-8")
        native=(ROOT/"src/Main.cpp").read_text("utf-8")
        dsp=(ROOT/"engine/builtin_fx/ChannelHarmonyProcessor.h").read_text("utf-8")
        self.assertIn("musicGuides_",bank)
        self.assertIn("observeMusicTrack(left,right,frames)",bank)
        self.assertIn("guide.processor->musicSourceChannel()==id.value",bank)
        self.assertIn('role=="INPUT"||role=="GROUP"',native)
        self.assertIn('guideDetectedScale',native)
        self.assertIn('guideConfidence',native)
        self.assertIn("MusicKeyDetector guide_",dsp)
        self.assertIn("guideAge_",dsp)
        self.assertNotIn("AudioContext",self.ui)
        self.assertNotIn("getUserMedia",self.ui)

    def test_effective_lcd_key_has_native_single_source_of_truth(self):
        native=(ROOT/"src/Main.cpp").read_text("utf-8")
        processor=(ROOT/"engine/builtin_fx/ChannelHarmonyProcessor.h").read_text("utf-8")
        self.assertIn('setProperty("effectiveKey",effective%12)',native)
        self.assertIn('setProperty("effectiveScale",effective/12)',native)
        self.assertIn("int effectiveKeyCode() const noexcept",processor)
        self.assertIn("std::atomic<int> effectiveKeyCode_",processor)
        self.assertIn("publishEffectiveKey();",processor)
        self.assertIn('nativeView?effectiveKey:v.key',self.ui)
        self.assertIn('nativeView?effectiveScale:v.scale',self.ui)
        self.assertIn("effectiveKey=snap.effectiveKey;effectiveScale=snap.effectiveScale",self.ui)
        self.assertIn("!Number.isInteger(snap.effectiveKey)",self.ui)
        self.assertIn("!Number.isInteger(snap.effectiveScale)",self.ui)
        self.assertIn("if(pending&&(!pending.ack||snap.appliedRequestId!==pending.id)){",self.ui)
        self.assertNotIn("displayKey=found?detectedKey",self.ui)
        self.assertNotIn("new AudioContext",self.ui)
        self.assertIn("if(!force&&readbackRequest&&now-readbackSentAt<2500)return;",self.ui)
        self.assertIn('snapshot(true);',self.ui)
        self.assertIn('now-pending.startedAt>5000',self.ui)
        self.assertIn('readbackRequest="";',self.ui)
        self.assertNotIn("setInterval(",self.ui)

    def test_quiet_manual_music_mode_and_selector(self):
        self.assertIn('data-guide-mode="0"',self.ui)
        self.assertIn('data-guide-mode="1"',self.ui)
        self.assertIn('aria-label="Music Key Detection"',self.ui)
        self.assertIn('id="mxHSourcePicker" hidden',self.ui)
        self.assertIn('el("mxHSourcePicker").hidden=!auto;',self.ui)
        self.assertIn('if(!channel){hint("ADD MUSIC INPUT TRACK");return;}',self.ui)
        self.assertIn('edit({guideMode:0,guideChannelId:"0"})',self.ui)
        self.assertIn('const status=!auto?"MANUAL":!inGraph?"SAFE":detected?"AUTO":"LISTEN";',self.ui)
        self.assertIn("showText(el(\"mxHKeyReadout\"),",self.ui)
        self.assertIn('role="status" aria-live="polite"',self.ui)
        self.assertIn(".mx-harm-mode button.active",self.css)
        self.assertIn(".mx-harm-source-picker[hidden]{display:none}",self.css)
        self.assertIn(".mx-harm-wheel-core strong{font-size:24px",self.css)
        self.assertNotIn("KEY SOURCE</span>",self.ui)
        self.assertNotIn("MANUAL · KEY LOCKED",self.ui)

    def test_no_fabricated_native_pcm(self):
        self.assertIn("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO",self.ui)
        self.assertIn("if(!native())",self.ui)
        self.assertIn("if(native()&&n?.admitted",self.ui)
        self.assertIn("Native Harmony host not ready · audio unchanged",self.html)
        with self.assertRaises(ValueError):
            apply_native_peq(self.html)

if __name__=="__main__":
    unittest.main()
