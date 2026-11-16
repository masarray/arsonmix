# SPDX-License-Identifier: AGPL-3.0-only
"""Fail on ambiguity between P83 historical evidence, P9 checkpoint and ImerSip."""
import hashlib
import json
import pathlib
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
def read(name):return (ROOT/name).read_text(encoding="utf-8")

class SpatialAuthorityContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.record=json.loads(read("config/spatial_audio_authority.json"))

    def test_one_selected_immutable_checkpoint_and_distinct_effect(self):
        record=self.record
        base=record["selectedSonKuBaseline"]
        im=record["imersip"]
        self.assertEqual(base["pr"],719)
        self.assertEqual(base["issue"],720)
        self.assertEqual(base["immutableCheckpointSha"],
                         "763706a15a2617a10d23b5e2e43eb5689e454ccb")
        self.assertEqual(base["researchRenderMode"],9)
        self.assertEqual(base["modeNameInCode"],"MusicSpatialP90")
        self.assertEqual(im["activeIntegrationPr"],722)
        self.assertEqual(im["basePr"],719)
        self.assertEqual(im["catalogIdentity"],"armix.builtin.imersip.v1")
        self.assertEqual(im["displayName"],"ImmerSIB")
        self.assertEqual(im["previousDisplayName"],"ImerSip")
        self.assertEqual(im["nativeRenderer"],base["nativeRenderer"])
        self.assertFalse(im["independentSpatialDsp"])
        self.assertTrue(im["researchOnly"])
        self.assertFalse(im["releaseQualified"])
        self.assertTrue(record["production"]["mainMustRemainUnchanged"])

    def test_checkpoint_audio_sources_have_not_silently_drifted(self):
        # Git's blob identity pins real acoustics, not merely marketing
        # render mode names. Any intentional DSP change requires a NEW
        # listening checkpoint and explicit ledger review, never a quiet
        # update to make this check green.
        # Immutable historical P9.2 checkpoint is still recorded above;
        # intentional owner-directed forward port pins the P9.8 source blobs.
        pinned=self.record["immersibP98Integration"]["acousticSourceBlobSha1"]
        self.assertGreaterEqual(len(pinned),7)
        for path,expected in pinned.items():
            raw=(ROOT/path).read_bytes()
            actual=hashlib.sha1(
                b"blob "+str(len(raw)).encode("ascii")+b"\x00"+raw
            ).hexdigest()
            self.assertEqual(actual,expected,
                f"Acoustic source changed without checkpoint review: {path}")

    def test_real_core_not_marketing_mode_or_shadow_dsp(self):
        core=read("engine/builtin_fx/sonku/SonKuSpatialCore.h")
        scene=read("engine/builtin_fx/ImerSipP92Scene.h")
        fx=read("engine/builtin_fx/ImerSipP92Processor.h")
        reg=read("src/NativeBuiltInFx.h")
        self.assertIn("p92Bass_.process(",core)
        self.assertIn("p90Decoder_.compose(",core)
        self.assertIn("p83::Mode::MusicSpatialP90",core)
        self.assertIn("s.renderMode=9",scene)
        self.assertNotIn("ImerSipDsp.h",fx)
        self.assertIn("public SonKuSpatialProcessor",fx)
        self.assertIn("ImerSipP92Processor::catalogIdentity",reg)
        self.assertIn("ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT",reg)

    def test_operator_scene_is_not_factory_default(self):
        scene=read("engine/builtin_fx/ImerSipP92Scene.h")
        factory=self.record["audioScenes"]["p92NeutralABReference"]
        candidate=self.record["audioScenes"]["imersipNewInsertDefault"]
        screenshot=self.record["audioScenes"]["ownerScreenshotObservation"]
        user=self.record["audioScenes"]["operatorCheckpointObservation"]
        self.assertTrue(user["notFactoryDefault"])
        self.assertEqual(factory["earlyRoom"],.78)
        self.assertEqual(factory["lateRoom"],.33)
        self.assertEqual(user["earlyRoom"],1)
        self.assertEqual(user["lateRoom"],.62)
        self.assertEqual(user["outputGainDb"],7)
        self.assertEqual(screenshot["speakerSpreadDeg"],150)
        self.assertEqual(screenshot["distanceMeters"],1.6)
        self.assertEqual(screenshot["lateRoom"],.91)
        self.assertEqual(candidate["presetId"],2)
        self.assertGreater(candidate["distanceMeters"],1.6)
        self.assertGreater(candidate["outputGainDb"],screenshot["outputGainDb"])
        self.assertIn("106.f,10.f, 1.f,1.00f,5.10545f,0",scene)
        self.assertIn("150.f,2.2f,1.f,.91f,4.8f",scene)
        self.assertIn('"Royal Concert Hall"',scene)
        self.assertNotIn("P9.2 Neutral A/B",read(
            "engine/builtin_fx/ImerSipP92Editor.h"))
        self.assertIn("s.roomAmount=.78f",scene)
        self.assertIn("s.ambience=.33f",scene)
        self.assertIn("s.outputTrim=1.f",scene)

    def test_immersib_p97_golden_and_p98_native_wire(self):
        self.assertEqual(self.record["immersibP98Integration"]["venueGolden"]["distanceMeters"],10)
        self.assertEqual(self.record["immersibP98Integration"]["venueGolden"]["sceneVoicing"],0)
        self.assertIn("SONKU_P98_SCENE_VOICING_RESEARCH",read(
            "engine/builtin_fx/sonku/SonKuSpatialCore.h"))
        self.assertIn("out.writeInt(12)",read(
            "engine/builtin_fx/SonKuSpatialProcessor.h"))
        self.assertIn("IMRS v7",read(
            "engine/builtin_fx/ImerSipP92Processor.h"))

    def test_legacy_negative_evidence_kept_and_research_scoped(self):
        historical=self.record["historicalP83"]
        yml=read(historical["workflow"])
        original=read("tests/sonku_p83_physical_speaker_source_test.cpp")
        self.assertTrue(historical["notP9OrImerSipReleaseGate"])
        self.assertTrue(historical["strictOriginalFidelityVetoRetained"])
        self.assertIn("github.event_name == 'workflow_dispatch'",yml)
        self.assertIn("contains(github.head_ref, 'p83')",yml)
        self.assertIn("tests/sonku_p83_physical_speaker_source_test.cpp",yml)
        self.assertIn("grep -q '^PASS P8.3 SOURCE GATES:'",yml)
        self.assertIn("REJECTED_LOW_ENERGY_OR_SPECTRAL_HOLE",original)
        self.assertIn("P8.3 MUSIC MONITORING REJECTED",original)
        current=read(".github/workflows/imersip-p92-single-engine.yml")
        self.assertIn("imersip_p92_*contract_test.py",current)
        self.assertIn("NativeImerSipP92ExactSonKuCorePCMStateAndACK",current)

if __name__=="__main__": unittest.main()
