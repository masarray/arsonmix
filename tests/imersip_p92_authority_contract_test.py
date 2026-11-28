# SPDX-License-Identifier: AGPL-3.0-only
"""Fail on ambiguity between P83 historical evidence, P9 checkpoint and ImerSip."""
import hashlib
import json
import pathlib
import sys
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from verify_immersib_approved_pcm_sources import (
    APPROVED_FILE_COUNT, APPROVED_ORIGINAL_BLOB, OPTIMIZED_BLOB, OPTIMIZED_PATH,
    github_blob_hash, validate, verify_exact_optimized_provenance,
    SONKU_CONTROL_PATH, SONKU_CONTROL_ORIGINAL, SONKU_CONTROL_VARIANT,
    SONKU_FIELD_PINS, verify_sonku_field_provenance,
)
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
        self.assertEqual(im["activeIntegrationPr"],734)
        self.assertEqual(im["sourceResearchPrs"],[722,732,731])
        self.assertEqual(im["approvedSonKuMainPr"],733)
        self.assertEqual(im["basePr"],719)
        self.assertEqual(im["catalogIdentity"],"armix.builtin.imersip.v1")
        self.assertEqual(im["displayName"],"ImmerSIB")
        self.assertEqual(im["previousDisplayName"],"ImerSip")
        self.assertEqual(im["nativeRenderer"],base["nativeRenderer"])
        self.assertFalse(im["independentSpatialDsp"])
        self.assertFalse(im["researchOnly"])
        self.assertTrue(im["releaseQualified"])
        self.assertFalse(record["production"]["mainMustRemainUnchanged"])
        self.assertTrue(record["production"]["noAutomaticMerge"])
        self.assertTrue(record["production"]["releaseCandidateIntegratedOnMain"])
        self.assertTrue(record["production"]["publicReleaseAllowed"])
        self.assertFalse(record["production"]["physicalHeadphoneAbStillRequired"])
        self.assertTrue(record["production"]["operatorHeadphoneApprovalSatisfied"])
        self.assertEqual(record["production"]["ownerListeningStatus"],
                         "approved_by_owner_2026-11-17")
        self.assertEqual(im["publicAlphaRelease"],"v0.5.0-alpha.26")
        self.assertIn("Windows x64 public alpha",im["releaseQualificationScope"])

    def test_checkpoint_audio_sources_have_not_silently_drifted(self):
        # Git's blob identity pins real acoustics, not merely marketing
        # render mode names. Any intentional DSP change requires a NEW
        # listening checkpoint and explicit ledger review, never a quiet
        # update to make this check green.
        # Immutable historical P9.2 checkpoint is still recorded above;
        # intentional owner-directed forward port pins the P9.8 source blobs.
        historical=self.record["immersibP98Integration"]["acousticSourceBlobSha1"]
        self.assertEqual(historical["engine/builtin_fx/sonku/SonKuSpatialCore.h"],
                         "f9fdcdec933bc62710bdb84fa44c0830bdc16868")
        accepted=self.record["immersibP1023Integration"]
        self.assertEqual(accepted["approvedAuditionSha"],"3095af08b817d59a3e14874dab404bf6e243f38a")
        self.assertEqual(accepted["ownerListeningStatus"],"approved_by_owner_2026-11-22_for_main_integration")
        pinned=accepted["acousticSourceBlobSha1"]
        lock=json.loads(read("config/immersib_approved_pcm_sources.json"))
        self.assertEqual(pinned,{x["path"]:x["blob"] for x in lock["files"]})
        self.assertGreaterEqual(len(pinned),7)
        # A deliberate, separately versioned implementation variant may be
        # admitted ONLY when exact PCM parity to this original owner-approved
        # source has been evidenced and the independent pinned manifest is
        # intact. The historic approved blob is NEVER re-sealed.
        self.assertEqual(validate(ROOT, lock), APPROVED_FILE_COUNT)
        for path,expected in pinned.items():
            raw=(ROOT/path).read_bytes().replace(b"\r\n",b"\n")
            actual=github_blob_hash(raw)
            if path in SONKU_FIELD_PINS and actual==SONKU_FIELD_PINS[path][1]:
                # Exact FIELD implementation only, no change to the
                # independent owner-approved authority ledger above.
                # validate() already requires the entire four-blob set.
                self.assertEqual(expected, SONKU_FIELD_PINS[path][0])
                verify_sonku_field_provenance(ROOT)
                continue
            if path==OPTIMIZED_PATH and actual==OPTIMIZED_BLOB:
                self.assertEqual(expected,APPROVED_ORIGINAL_BLOB)
                verify_exact_optimized_provenance(ROOT)
                continue
            if path==SONKU_CONTROL_PATH and actual==SONKU_CONTROL_VARIANT:
                # Explicit SonKu control-only variant; original ledger stays frozen.
                self.assertEqual(expected,SONKU_CONTROL_ORIGINAL)
                continue
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
        self.assertIn("ARMIX_IMMERSIB_RELEASE_CANDIDATE",reg)

    def test_regular_sonku_product_admission_is_shared_by_all_entrypoints(self):
        gate=read("engine/builtin_fx/sonku/SonKuProductAdmission.h")
        processor=read("engine/builtin_fx/SonKuSpatialProcessor.h")
        bridge=read("src/SonKuNativeBridgeContract.h")
        self.assertIn("next.sourceDistanceMeters<1.6f",gate)
        self.assertIn("next.roomAmount>1.f",gate)
        self.assertIn("next.sceneVoicing>p98::SceneVoicing::Cinema",gate)
        self.assertIn("valid8DSonKuInsertSettings(next)",processor)
        self.assertIn("!admitQueuedScene(next)",processor)
        self.assertIn("valid8DSonKuInsertSettings(proposed)",bridge)
        self.assertIn("sonkuProductIsolation(44100)",read(
            "tests/imersip_p92_native_test.cpp"))

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
        self.assertIn("s.stereoSpreadDegrees=106.f;s.sourceDistanceMeters=10.f",scene)
        self.assertIn("112.f,2.0f,1.36f,.84f,5.10545f,4",scene)
        self.assertIn("180.,2.5,180.,100.",read("engine/builtin_fx/ImerSipP92Editor.h"))
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

    def test_rc_is_flagged_without_importing_rejected_research_workflows(self):
        historical=self.record["historicalP83"]
        self.assertTrue(historical["notP9OrImerSipReleaseGate"])
        self.assertTrue(historical["strictOriginalFidelityVetoRetained"])
        cmake=read("CMakeLists.txt")
        self.assertIn("ARMIX_IMMERSIB_RELEASE_CANDIDATE",cmake)
        self.assertIn("NativeImmerSIBP99VocalRoomAndVenueGolden",cmake)
        self.assertIn("NativeImmerSIBJucePresetACKStateAndBypass",cmake)
        self.assertIn("NativeImmerSIBFixedPaPlusMusicalEq",cmake)
        self.assertIn('Branded ImmerSIB requires owner-approved P102.3',cmake)
        self.assertGreaterEqual(cmake.count(' ${ARMIX_IMMERSIB_RELEASE_CANDIDATE})'),2)
        self.assertIn("ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH",cmake)
        self.assertIn("ARMIX_SONKU_APPROVED_P99_MAIN",cmake)
        self.assertIn('"mainP99Preserved": true',
                      read("config/spatial_audio_authority.json"))
        self.assertTrue(self.record["production"]["releaseRequiresOperatorHeadphoneApproval"])
        self.assertTrue(self.record["imersip"]["releaseQualified"])

if __name__=="__main__": unittest.main()
