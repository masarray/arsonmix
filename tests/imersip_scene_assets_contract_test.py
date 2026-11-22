# SPDX-License-Identifier: AGPL-3.0-only
"""ImmerSIB scene asset FAT: accept six venues only; reject retired sprite pack."""
import pathlib
import struct
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
SCENES=("live_concert","royal_concert_hall","arena_thunder",
        "front_row_power","symphonic_palace","velvet_cinema")
ASSET=ROOT/"assets"/"imersip"

class ApprovedSceneAssets(unittest.TestCase):
    def test_public_sonku_profile_reconfigures_both_branded_flags(self):
        # Regression for the 2026-11-22 exact-SHA Windows red:
        # CMake option() caches both opt-ins after the RC build. Merely
        # setting RELEASE_CANDIDATE=OFF is insufficient.
        cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        workflow=(ROOT/".github/workflows/imersip-p92-single-engine.yml").read_text(encoding="utf-8")
        step=workflow.split(
            "- name: Recompile default public SonKu P9.9 profile with ImmerSIB RC OFF",
            1)[1].split("      - name:",1)[0]
        flags=("ARMIX_IMMERSIB_STAGE_MONITOR_RESEARCH",
               "ARMIX_IMMERSIB_MUSICAL_EQ_RESEARCH")
        self.assertIn("-DARMIX_IMMERSIB_RELEASE_CANDIDATE=OFF",step)
        self.assertIn("-DARMIX_SONKU_APPROVED_P99_MAIN=ON",step)
        self.assertIn("$cacheLines",step)
        for flag in flags:
            self.assertIn("option("+flag,cmake)
            self.assertIn("-D"+flag+"=OFF",step)
            self.assertIn(flag+":BOOL=OFF",step)
        self.assertIn("PUBLIC_PROFILE_CMAKE_CONFIGURE_FAILED",step)

    def test_p102_attribution_and_reopen_stress_are_audio_source_locked(self):
        # The profiler must measure real approved EARLY/FDN source, not a
        # parallel fake implementation, and cannot rewrite the 30 PCM files.
        profiler=(ROOT/"tools"/"immersib_p102_cpu_profile.cpp").read_text(encoding="utf-8")
        native=(ROOT/"tests"/"imersip_p92_native_test.cpp").read_text(encoding="utf-8")
        self.assertIn("IMMERSIB_P102_ROOM_ATTR",profiler)
        self.assertIn("--attribution",profiler)
        self.assertIn("SonKuEarlyReflections",profiler)
        self.assertIn("SonKuMusicalReverb",profiler)
        self.assertIn("ISOLATED_NOT_ADDITIVE",profiler)
        self.assertIn("editorReopenStress(44100)",native)
        self.assertIn("editorReopenStress(48000)",native)
        self.assertIn("iteration<100",native)
        self.assertIn("fx->pollCompletion(ack) && ack.applied",native)
        self.assertIn("fx->getStateInformation(snapshot)",native)
        self.assertIn("sceneId!=previouslySelected",native)
        self.assertIn("previouslySelected==8?2:previouslySelected+1",native)
        self.assertIn("pending_close_reopen=verified",native)
        self.assertIn("juce::Timer::callPendingTimersSynchronously()",native)
        editor=(ROOT/"engine"/"builtin_fx"/"ImerSipP92Editor.h").read_text(encoding="utf-8")
        self.assertIn("orphanApplied.has_value()",editor)
        self.assertIn('status_=*orphanApplied?"RECONCILED NATIVE AUDIO ACK":',editor)

    def test_idle_editor_timer_and_host_controls_are_change_driven(self):
        # Audio and the native host remain authoritative. Unchanged 25 Hz
        # polling must not repeatedly reconfigure JUCE controls or repaint
        # the footer; a rejected orphan ACK must not be reported as applied.
        editor=(ROOT/"engine/builtin_fx/ImerSipP92Editor.h").read_text(encoding="utf-8")
        self.assertIn("hostReadbackCached_ && cachedHasRequest_==hasRequest",editor)
        self.assertIn("lastHostReadback_.rackBypassed==state.rackBypassed",editor)
        self.assertIn("lastHostReadback_.globalBypassed==state.globalBypassed",editor)
        self.assertIn("if(status_!=previousStatus)",editor)
        self.assertIn("std::optional<bool> orphanApplied",editor)
        self.assertIn('status_=*orphanApplied?"RECONCILED NATIVE AUDIO ACK":',editor)
        self.assertIn('"NATIVE AUDIO EDIT REJECTED"',editor)

    def test_lazy_photo_decode_no_eager_constructor(self):
        skin=(ROOT/"engine/builtin_fx/ImerSipP92VisualSkin.h").read_text(encoding="utf-8")
        self.assertIn("mutable std::array<juce::Image,6> sceneImages_",skin)
        self.assertIn("const auto& art=sceneImage(sceneIndex)",skin)
        self.assertIn("allValid({&sceneImage(0)",skin)
        self.assertNotIn(": sceneLive_(load(",skin)
        self.assertIn("ImageCache::getFromMemory",skin)
        for index in range(6):
            self.assertIn(f"case {index}: image=load(BinaryData::scene_",skin)

    def test_six_photo_scene_pngs(self):
        expected=sorted(ASSET/"scenes"/f"scene_{name}.png" for name in SCENES)
        self.assertEqual(sorted((ASSET/"scenes").glob("*.png")),expected)
        for path in expected:
            with self.subTest(scene=path.name):
                with path.open("rb") as f:
                    header=f.read(33)
                self.assertEqual(header[:8],b"\x89PNG\r\n\x1a\n")
                self.assertEqual(header[12:16],b"IHDR")
                width,height,depth,colour,compression,filtering,interlace=struct.unpack(
                    ">IIBBBBB",header[16:29])
                # Exact two-times logical venue viewport: no side matte,
                # no independent X/Y stretching, no accidental 2:1 fallback.
                self.assertEqual((width,height),(2200,922))
                self.assertEqual(width*461,height*1100)
                self.assertEqual(depth,8)
                self.assertIn(colour,(2,6))
                self.assertEqual((compression,filtering,interlace),(0,0,0))

    def test_rejected_kit_and_generation_are_gone(self):
        for name in ("png","vector","asset_manifest.json",
                     "imersip_p92_skin_atlas.png","panel_flat_matte.svg"):
            self.assertFalse((ASSET/name).exists(),name)
        for path in (
            "scripts/generate_imersip_concert_png.py",
            "tests/imersip_p92_png_asset_test.py",
            ".github/workflows/imersip-p92-materialize-assets.yml",
            "docs/IMERSIP_P92_CONCERT_VECTOR_RENDER_QA.md"):
            self.assertFalse((ROOT/path).exists(),path)

    def test_no_compilation_or_ci_resurrection(self):
        cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        skin=(ROOT/"engine/builtin_fx/ImerSipP92VisualSkin.h").read_text(encoding="utf-8")
        editor=(ROOT/"engine/builtin_fx/ImerSipP92Editor.h").read_text(encoding="utf-8")
        native_test=(ROOT/"tests/imersip_p92_native_test.cpp").read_text(encoding="utf-8")
        workflow=(ROOT/".github/workflows/imersip-p92-single-engine.yml").read_text(encoding="utf-8")
        self.assertNotIn("imersipP92ConcertBoxes",native_test)
        self.assertIn("stageProbe->getMinimum()",native_test)
        self.assertIn("distanceProbe->getMaximum()",native_test)
        self.assertIn("ARMIX_IMERSIB_SCENE_PNG_SOURCES",cmake)
        self.assertIn("BinaryData::scene_live_concert_png",skin)
        self.assertIn("BinaryData::scene_velvet_cinema_png",skin)
        self.assertIn("getWidth()!=photoWidth",skin)
        self.assertIn("getHeight()!=photoHeight",skin)
        self.assertIn("photoWidth=2200",skin)
        self.assertIn("photoHeight=922",skin)
        self.assertIn("int matteCount=0",native_test)
        self.assertIn("assert(matteCount<3",native_test)
        self.assertIn("skin_.drawScene(g,visualSceneId_)",editor)
        self.assertIn("g.fillRect(0,461,1100,219)",skin)
        self.assertIn("drawRotarySlider(",skin)
        for forbidden in ("ARMIX_IMERSIP_PNG_SOURCES","assets/imersip/png",
                          "room_background_png","line_array_left_png",
                          "knob_master_purple_png","slider_thumb_png"):
            self.assertNotIn(forbidden,cmake+skin)
        for forbidden in ("19-RGBA-PNG-AssetPack","imersip-png-qa",
                          "all_speakers_composition","generate_imersip_concert_png",
                          "pillow>=11","test_19_independent_rgba"):
            self.assertNotIn(forbidden,workflow)

if __name__=="__main__":unittest.main()
