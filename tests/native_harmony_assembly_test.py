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
        self.assertIn(".mx-harm[hidden],.mx-harm-source-picker[hidden]{display:none}",self.css)
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
        self.assertIn('id="mxHTracking" aria-live="off"',self.ui)
        self.assertIn('showText(el("mxHTracking"),tracking)',self.ui)
        self.assertIn('if(now-lastGuideRead>500)',self.ui)
        self.assertIn('b.classList.toggle("queued"',self.ui)
        self.assertIn('.mx-harm-note.queued:not(.active)',self.css)
        self.assertIn('role="status" aria-live="polite"',self.ui)
        self.assertIn(".mx-harm-mode button.active",self.css)
        self.assertIn(".mx-harm-source-picker[hidden]{display:none}",self.css)
        self.assertIn(".mx-harm-wheel-core strong{",self.css)
        self.assertNotIn("KEY SOURCE</span>",self.ui)
        self.assertNotIn("MANUAL · KEY LOCKED",self.ui)

    def test_reference_inspired_wide_harmony_and_exact_shared_fx_chrome(self):
        chrome=(ROOT/"site/preview-v2/assets/mixer-contextual-buttons.css").read_text("utf-8")
        self.assertIn('class="mx-harm-control-deck"',self.ui)
        self.assertIn('class="mx-harm-signature"',self.ui)
        self.assertIn('class="mx-harm-wheel"',self.ui)
        self.assertIn('class="mx-harm-dial-face"',self.ui)
        self.assertIn('class="mx-harm-dial-pointer"',self.ui)
        self.assertIn("grid-template-columns:150px minmax(0,1fr) 234px",self.css)
        self.assertIn("width:min(800px,calc(100vw - 14px))",self.css)
        self.assertIn('class="mx-harm-brand">Harmony <em>Effect</em>',self.ui)
        self.assertIn('root.setAttribute("aria-label","Harmony Effect")',self.ui)
        self.assertNotIn('class="mx-harm-brand">ArSon <em>Harmony</em>',self.ui)
        self.assertIn("grid-template-columns:112px minmax(0,1fr)",self.css)
        self.assertIn(".mx-harm-wheel-core strong",self.css)
        # Do not restore the decorative inner border: it intersects the
        # 12 note labels after the blue-annulus radius adjustment.
        self.assertNotIn(".mx-harm-wheel:before{",self.css)
        self.assertNotIn("border:1px solid #c0d3d445",self.css)
        self.assertIn(".mx-harm-wheel-core{",self.css)
        self.assertIn("syncPower();",self.ui)
        self.assertIn('power.dataset.fxState=fxState;',self.ui)
        self.assertIn('fxState==="unknown"||fxState==="rack-off"',self.ui)
        self.assertIn('ch.fxRackBypassed?"rack-off":insert.bypassed?"off":"on"',self.ui)
        self.assertIn("if(power.dataset.fxState===fxState)return;",self.ui)
        self.assertIn(".mx-peq-close,.mx-comp-close,.mx-harm-close",chrome)
        self.assertIn(".mx-peq-global,.mx-comp-power,.mx-harm-power",chrome)
        # Reference-svg visual direction: flat satin fill, short drop shadow,
        # fine highlight edge. The knob's static groove and data-bound light
        # ring share one --dial-sweep variable; don't add JS timers/canvas.
        self.assertIn(".mx-harm-note.active{",self.css)
        self.assertIn("border-radius:50%;font-size:14.5px",self.css)
        self.assertIn("transform:translate(-50%,-50%) scale(1.05)",self.css)
        self.assertIn("transition:transform .15s ease",self.css)
        self.assertIn(".mx-harm-dial-face::before,.mx-harm-dial-face::after",self.css)
        self.assertIn("conic-gradient(from 225deg",self.css)
        self.assertIn("x=50+36.5*Math.cos(a)",self.ui)
        self.assertIn('data-voices="5"',self.ui)
        self.assertIn("const voiceGlyph=",self.ui)
        self.assertIn("Number.isInteger(s.voices)&&s.voices>=2&&s.voices<=5",self.ui)
        engine=(ROOT/"engine/builtin_fx/HarmonyDsp.h").read_text("utf-8")
        self.assertIn("static constexpr int maxVoices=5",engine)
        self.assertIn("std::array<float,maxVoices+1> filtered_",engine)
        self.assertIn(".mx-harm-scale button.active{",self.css)
        self.assertIn("flex:1 1 0;min-width:0;height:27px",self.css)
        self.assertIn("var(--dial-sweep,0deg)",self.css)
        self.assertIn("const turn=(-135+ratio*270)+\"deg\",sweep=(ratio*270)+\"deg\"",self.ui)
        self.assertIn('surface.style.setProperty("--dial-sweep",sweep)',self.ui)
        self.assertIn('surface.style.setProperty("--dial-turn",turn)',self.ui)
        self.assertNotIn("new AudioContext",self.ui)
        self.assertNotIn("setInterval(",self.ui)
        self.assertNotIn(".mx-harm-power.on{",self.css)
        self.assertNotIn(".mx-harm-close:hover{",self.css)
        self.assertIn('id="mxHClose"',self.ui)

    def test_no_fabricated_native_pcm(self):
        self.assertIn("BROWSER PREVIEW · VISUAL ONLY · NO AUDIO",self.ui)
        self.assertIn("if(!native())",self.ui)
        # Accepted ChannelId/instance binding is re-established on the
        # current graph, not permanently pinned to the editor's open tick.
        self.assertIn("if(!current||!native()||!n?.admitted)return null;",self.ui)
        self.assertIn("if(n?.admitted&&insert?.catalogId===ID&&insert.id)",self.ui)
        self.assertIn("function reconcileBinding(){",self.ui)
        self.assertNotIn("REOPEN ACCEPTED FX",self.ui)
        self.assertIn("Native Harmony host not ready · audio unchanged",self.html)
        with self.assertRaises(ValueError):
            apply_native_peq(self.html)

if __name__=="__main__":
    unittest.main()
