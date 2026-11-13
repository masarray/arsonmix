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
        self.assertEqual(self.assembled.count('src="assets/mixer-native-compressor.js?v=comp-1"'),1)
        self.assertEqual(self.assembled.count("function insertChosenNativeCompressor(){"),1)
        self.assertEqual(self.assembled.count("function insertChosenNativeThrow(){"),1)
        self.assertEqual(self.assembled.count('src="assets/mixer-native-vocal-throw.js?v=throw-1"'),1)
        self.assertEqual(self.assembled.count('data-fast-native-throw="1"'),1)
        self.assertEqual(self.assembled.count('data-fast-native-sonku="1"'),1)
        self.assertEqual(self.assembled.count('src="assets/mixer-native-sonku.js?v=sonku-1"'),1)
        self.assertEqual(self.assembled.count("function insertChosenNativeSonKu(){"),1)

    def test_native_rack_uses_regular_insert_slot(self):
        self.assertIn("host.insert(activeFxTarget)",self.assembled)
        self.assertNotIn("__ARMIX_NATIVE_M2__",self.assembled)
        self.assertIn("if(t.fx[index]==='Parametric EQ')",self.assembled)
        # Does not fabricate a second EQ rack row or a special before-VST bank.
        self.assertNotIn("builtinEq.slotMarkup",self.assembled)
        self.assertNotIn("PRE VST3",self.assembled)
        self.assertEqual(self.assembled.count('data-fast-native-eq="1"'),1)
        self.assertEqual(self.assembled.count('data-fast-native-comp="1"'),1)
        self.assertIn("if(t.fx[index]==='Compressor')",self.assembled)
        self.assertIn("if(t.fx[index]==='ArSon Vocal Throw')",self.assembled)

    def test_single_style_owner(self):
        # The approved CSS compiler requires exactly 18 component-owner links.
        # Native EQ styles live within the existing FX rack owner, not a 19th link.
        self.assertNotIn('href="assets/mixer-native-peq.css',self.assembled)
        css=(ROOT/"site/preview-v2/assets/mixer-fx-rack.css").read_text("utf-8")
        self.assertIn(".mx-peq[hidden]{display:none}",css)
        self.assertIn(".mx-sonku[hidden]{display:none!important}",css)
        self.assertIn(".mx-sonku .sk-listener-art",css)
        png=(ROOT/"site/preview-v2/assets/sonku-listener-top.png").read_bytes()
        self.assertEqual(png[:8],bytes([137,80,78,71,13,10,26,10]))
        self.assertLess(len(png),45000)
        self.assertNotIn("mixer-native-sonku.css",self.assembled)
        sonku=(ROOT/"site/preview-v2/assets/mixer-native-sonku.js").read_text("utf-8")
        self.assertIn("assets/sonku-listener-top.png",sonku)
        self.assertIn('arMixBuiltinSonKuAck',sonku)
        self.assertIn('nativeSonKuSet',sonku)
        self.assertNotIn("new AudioContext",sonku)
        self.assertIn("grid-template-columns:minmax(0,1fr) 143px",css)
        script=(ROOT/"site/preview-v2/assets/mixer-native-peq.js").read_text("utf-8")
        self.assertIn('const LOG=Math.log(1000)',script)
        self.assertIn('document.createElementNS(NS,"svg")',script)
        self.assertIn('arMixBuiltinEqAck',script)
        self.assertNotIn("new AudioContext",script)
        compressor=(ROOT/"site/preview-v2/assets/mixer-native-compressor.js").read_text("utf-8")
        self.assertIn('arMixBuiltinCompAck',compressor)
        self.assertIn('arMixBuiltinCompSnapshot',compressor)
        self.assertIn('addedLatencySamples!==0',compressor)
        self.assertNotIn("new AudioContext",compressor)
        self.assertIn(".mx-comp[hidden]{display:none}",css)
        throw=(ROOT/"site/preview-v2/assets/mixer-native-vocal-throw.js").read_text("utf-8")
        self.assertIn("arMixBuiltinThrowAck",throw)
        self.assertIn("arMixBuiltinThrowSnapshot",throw)
        self.assertIn("nativeThrowTrigger",throw)
        self.assertIn('id="vtPreset"',throw)
        self.assertIn("armix-floating-close",throw)
        self.assertNotIn("new AudioContext",throw)

    def test_sonku_native_space_v2_owned_by_single_pcm_graph(self):
        main=(ROOT/"src/Main.cpp").read_text("utf-8")
        processor=(ROOT/"engine/builtin_fx/SonKuSpatialProcessor.h").read_text("utf-8")
        core=(ROOT/"engine/builtin_fx/sonku/SonKuSpatialCore.h").read_text("utf-8")
        js=(ROOT/"site/preview-v2/assets/mixer-native-sonku.js").read_text("utf-8")
        snapshot=main.split("void emitNativeSonKuSnapshot(",1)[1].split(
            "void servicePendingNativeSonKu()",1)[0]
        for field in ("spacePreset","sourceDistanceMeters","roomAmount","ambience"):
            self.assertIn('values->setProperty("'+field+'"',snapshot)
            if field=="spacePreset":
                self.assertIn('values.getProperty("spacePreset"',main)
            else:
                self.assertIn('optionalNumber("'+field+'"',main)
            self.assertIn(field,js)
            self.assertIn(field,core)
        self.assertIn('reply->setProperty("schemaVersion",2)',snapshot)
        self.assertIn("queueSettings(settings,numeric)",main)
        self.assertIn("complete_.push(",processor)
        self.assertIn("out.writeInt(2)",processor)
        self.assertIn("version==1&&size!=30",processor)
        self.assertIn("version==2&&size!=46",processor)
        self.assertNotIn("new AudioContext",js)
        self.assertIn("[data-sk-room]",js)
        self.assertIn('arMixBuiltinSonKuAck',js)

    def test_native_tempo_readback_belongs_to_vocal_throw_not_eq(self):
        # The real C++ dispatcher must populate the exact VocalThrow
        # snapshot, not an unrelated EQ response. Browser-only mocks cannot
        # catch a payload sent on the wrong native event.
        main=(ROOT/"src/Main.cpp").read_text("utf-8")
        def body(start,finish):
            self.assertEqual(main.count(start),1)
            return main.split(start,1)[1].split(finish,1)[0]
        eq=body("void emitNativeEqSnapshot(",
                "void servicePendingNativeEq()")
        vocal=body("void emitNativeThrowSnapshot(",
                   "void servicePendingNativeThrow()")
        fields=("musicSourceChannelId","musicSourceActive",
                "musicLocked","musicBpm","musicConfidence",
                "musicGridAvailable","musicPhaseConfidence",
                "musicBeatAnchorFrame","musicDroppedFrames")
        for field in fields:
            self.assertIn('reply->setProperty("'+field+'"',vocal,
                          "missing accepted Vocal Throw BPM field: "+field)
            self.assertNotIn('reply->setProperty("'+field+'"',eq,
                             "cross-FX snapshot pollution: "+field)
        # After device restart or graph change the last control snapshot
        # is stale. No stale source/phase can be advertised as locked.
        self.assertIn("const bool currentMusicGraph=lastMusicTempo_.active",vocal)
        self.assertIn("lastMusicTempo_.epoch",vocal)
        self.assertIn("lastMusicTempo_.revision",vocal)
        self.assertIn("currentMusicGraph&&",vocal)
        self.assertIn("musicBpm",vocal)
        self.assertIn('juce::String("0")',vocal)
        self.assertIn('"arMixBuiltinThrowSnapshot"',vocal)
        self.assertNotIn('"arMixBuiltinThrowSnapshot"',eq)
        throw_js=(ROOT/"site/preview-v2/assets/mixer-native-vocal-throw.js").read_text("utf-8")
        for field in ("musicLocked","musicBpm","musicGridAvailable",
                      "musicSourceChannelId"):
            self.assertIn(field,throw_js)
        self.assertNotIn("new AudioContext",throw_js)

    def test_fail_closed_on_repeat(self):
        with self.assertRaises(ValueError):
            apply_native_peq(self.assembled)
        with self.assertRaises(ValueError):
            apply_native_peq(self.original.replace("function renderFastPicker(){",
                "function renderFastPickerRenamed(){"))

if __name__=="__main__":
    unittest.main()
