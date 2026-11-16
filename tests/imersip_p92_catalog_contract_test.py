# SPDX-License-Identifier: AGPL-3.0-only
"""Guard a real P9.2 ImerSip insert, never a GUI-only catalog item."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
def source(path): return (ROOT/path).read_text(encoding="utf-8")

class ImerSipP92Contract(unittest.TestCase):
    def test_single_pcm_authority_and_stable_catalog(self):
        p=source("engine/builtin_fx/ImerSipP92Processor.h")
        scene=source("engine/builtin_fx/ImerSipP92Scene.h")
        catalog=source("src/NativeBuiltInFx.h")
        self.assertIn("class ImerSipP92Processor final : public SonKuSpatialProcessor",p)
        self.assertNotIn("ImerSipDsp",p.split("#include",1)[0])
        self.assertIn('armix.builtin.imersip.v1',p)
        self.assertIn('productDisplayName="ImmerSIB"',p)
        self.assertIn('return productDisplayName;',p)
        self.assertIn("s.renderMode=9",scene)
        for value in ("s.stereoSpreadDegrees=82.f","s.sourceDistanceMeters=1.6f",
                      "s.spacePreset=3","s.roomAmount=.78f","s.ambience=.33f",
                      "s.outputTrim=1.f"):
            self.assertIn(value,scene)
        self.assertIn("SonKuSpatialProcessor(imersipP92Factory(2))",p)
        self.assertIn("imersipP97VenueGoldenScene()",scene)
        self.assertIn("next.sourceDistanceMeters=std::clamp(",p)
        self.assertIn("const double upper[]={180.,2.5,180.",source(
            "engine/builtin_fx/ImerSipP92Editor.h"))
        self.assertIn("sidePresence2_.run",source(
            "engine/builtin_fx/sonku/SonKuP98SceneVoicing.h"))
        self.assertIn("inline constexpr std::array<ImerSipP92Preset,6>",scene) # legacy
        self.assertIn("inline constexpr std::array<ImerSipP92Preset,7>",scene) # P9.8
        self.assertIn("imersipP92MinDistance()",scene)
        self.assertIn("134.f,1.5f,1.70f,.94f,4.40f,4",scene)
        self.assertIn('"Concert Front Row XL"',scene)
        # Named scenes bypass quantized JUCE slider roundtrip.
        ui=source("engine/builtin_fx/ImerSipP92Editor.h")
        self.assertIn("submit(factory);",ui)
        self.assertIn("uiScene_=s;",ui)
        self.assertIn("submit(uiScene_);",ui)
        self.assertIn("isCustomIntent_=true;",ui)
        # Brand opt-in only; factory IDs, GUI, IMRS and SonKu P9.9
        # approved instance must never be renamed or silently retuned.
        processor=source("engine/builtin_fx/ImerSipP92Processor.h")
        native=source("engine/builtin_fx/SonKuSpatialProcessor.h")
        core=source("engine/builtin_fx/sonku/SonKuSpatialCore.h")
        wrapper=source("engine/builtin_fx/sonku/SonKuImmerSIBVocalRoom.h")
        cmake=source("CMakeLists.txt")
        self.assertIn("optInImmerSIBVocalRoom();",processor)
        self.assertIn("core_.optInImmerSIBVocalRoom();",native)
        self.assertIn("immersibP99Enabled_ && p90World",core)
        self.assertIn("const float p98RoomSourceL",core)
        self.assertIn("p98RoomSourceL-x0",core)
        self.assertIn("p99::ImmerSIBVocalRoom",core)
        self.assertIn("MusicalVocalDepth donor_",wrapper)
        self.assertIn("ARMIX_IMMERSIB_RELEASE_CANDIDATE",cmake)
        self.assertIn("publicly shipped before headphone FAT",cmake)
        self.assertNotIn("readControls()",ui)
        self.assertIn('"Royal Concert Hall"',scene)
        self.assertIn('"Arena Thunder"',scene)
        self.assertIn('"Front Row Power"',scene)
        self.assertIn('"Symphonic Palace"',scene)
        self.assertIn('"Velvet Cinema"',scene)
        ui=source("engine/builtin_fx/ImerSipP92Editor.h")
        self.assertIn("ImerSipP92VisualSkin",ui)
        self.assertNotIn("imersipP92ConcertBoxes(",ui)
        self.assertNotIn("skin_.drawRoom(g)",ui)
        self.assertIn("skin_.drawPanel(g)",ui)
        self.assertIn("skin_.drawScene(g,visualSceneId_)",ui)
        self.assertIn("if(preset!=1)visualSceneId_=preset",ui)
        self.assertIn("visualSceneId_=id",ui)
        self.assertNotIn("L  /  WORLD PA",ui)
        self.assertNotIn("LIVE CONCERT WORLD",ui)
        self.assertNotIn("skin_.drawHeader(g)",ui)
        for label in ("PRESET","ROOM","DISTANCE","MASTER GAIN"):
            self.assertIn(label,ui)
        # Header/footer close chrome is owned by the native plugin window.
        self.assertNotIn("SPATIAL CONCERT MONITOR",ui)
        self.assertNotIn('g.fillRect(0,0,1100,68)',ui)
        self.assertNotIn('close_.setButtonText',ui)
        self.assertNotIn("close_",ui)
        # Host state is the only authority for the native power glyph.
        self.assertIn('power_.setButtonText({})',ui)
        self.assertIn('power_.setToggleState(valid && !bypassed',ui)
        # The bypass UI is strictly host-readback derived, never click
        # optimistic. JUCE paints the veil *after* child controls and
        # cuts a hole around the native power icon, leaving it actionable.
        self.assertIn("std::optional<bool> hostBypassed_",ui)
        self.assertIn("hostBypassed_=bypassed",ui)
        self.assertIn("if(changed)repaint()",ui)
        self.assertIn("paintOverChildren(juce::Graphics& g) override",ui)
        self.assertIn("g.excludeClipRegion(power_.getBounds())",ui)
        self.assertIn("skin_.drawBypassedVenueOverlay(g)",ui)
        skin=source("engine/builtin_fx/ImerSipP92VisualSkin.h")
        native=source("tests/imersip_p92_native_test.cpp")
        self.assertIn('g.drawText("BYPASSED"',skin)
        self.assertIn("juce::FontOptions(76.f,juce::Font::bold)",skin)
        self.assertIn("font.setExtraKerningFactor(0.055f)",skin)
        self.assertIn("juce::Colour(0xb803060a)",skin)
        self.assertIn("juce::Colour(0x7603060a)",ui)
        self.assertIn("sceneMutedEnergy*100<sceneOnEnergy*52",native)
        self.assertIn("assert(powerHighlightPixels>=15)",native)
        self.assertIn("assert(stageDarker>=24)",native)
        self.assertIn("assert(amberLetters>=380 && amberRows>=36)",native)
        self.assertIn("assert(panelMutedEnergy*100<panelOnEnergy*73)",native)
        self.assertIn('place(power_,1032,483,44,44)',ui)
        self.assertIn('place(knobs_[5],908,510,98,94)',ui)
        self.assertIn('place(labels_[5],884,484,139,22)',ui)
        self.assertIn('label.setComponentID(',ui)
        self.assertIn("g.fillRect(0,461,1100,219)",source(
            "engine/builtin_fx/ImerSipP92VisualSkin.h"))
        self.assertIn("juce::Colour(0xff151a20)",source(
            "engine/builtin_fx/ImerSipP92VisualSkin.h"))
        self.assertNotIn("draw(g,panel_",source(
            "engine/builtin_fx/ImerSipP92VisualSkin.h"))
        self.assertNotIn("g.fillRect(251,468,2,204)",ui)
        self.assertNotIn("g.fillRect(879,468,2,204)",ui)
        self.assertNotIn('g.drawText("SCENE SETUP"',ui)
        self.assertNotIn('g.drawText("SPATIAL FIELD"',ui)
        self.assertNotIn('g.drawText("OUTPUT"',ui)
        self.assertNotIn("skin_.drawConcert(g,concert)",ui)
        skin=source("engine/builtin_fx/ImerSipP92VisualSkin.h")
        cmake=source("CMakeLists.txt")
        self.assertIn("ImageCache::getFromMemory",skin)
        self.assertIn("Graphics::highResamplingQuality",skin)
        self.assertIn("RectanglePlacement::centred",skin)
        self.assertNotIn("getClippedImage",skin)
        self.assertNotIn("Drawable::createFromImageData",skin)
        self.assertNotIn("juce::Drawable",skin)
        self.assertNotIn("ARMIX_IMERSIP_PNG_SOURCES",cmake)
        self.assertNotIn("assets/imersip/png",cmake)
        for obsolete in ("room_background_png","line_array_left_png",
                         "slider_thumb_png","knob_master_purple_png",
                         "drawConcert(","drawRoom("):
            self.assertNotIn(obsolete,skin)
        self.assertIn("ARMIX_IMERSIB_SCENE_PNG_SOURCES",cmake)
        for scene in ("live_concert","royal_concert_hall","arena_thunder",
                      "front_row_power","symphonic_palace","velvet_cinema"):
            name="scene_"+scene+".png"
            self.assertTrue((ROOT/"assets/imersip/scenes"/name).exists())
            self.assertIn("BinaryData::scene_"+scene+"_png",skin)
        # Keep scene photo's aspect; do not stretch concert cabinets.
        self.assertIn("g.drawImageWithin(*art",skin)
        self.assertNotIn("RectanglePlacement::fillDestination",skin)
        self.assertIn("const int x=252+317*col",ui)
        self.assertIn('button.getComponentID()=="imersip.power"',skin)
        self.assertIn("powerRing.addCentredArc",skin)
        self.assertNotIn("BinaryData::button_normal_png",skin)

        for old_path in (
            "scripts/generate_imersip_concert_png.py",
            "tests/imersip_p92_png_asset_test.py",
            "assets/imersip/png","assets/imersip/vector",
            "assets/imersip/asset_manifest.json",
            "assets/imersip/imersip_p92_skin_atlas.png",
            ".github/workflows/imersip-p92-materialize-assets.yml"):
            self.assertFalse((ROOT/old_path).exists(),old_path)
        self.assertIn("for(const auto& scene:imersipP92Presets)",ui)
        self.assertNotIn("P9.2 Neutral A/B",ui)
        self.assertNotIn("Warm Auditorium",ui)
        self.assertNotIn("Grand Arena",ui)
        self.assertIn("ImerSipP92Processor::productDisplayName",catalog)
        self.assertIn("Saved ImmerSIB IMRS state rejected",catalog)
        self.assertNotIn("ImerSipP92Processor::productDisplayName",ui)
        self.assertIn("prepareNativeBuiltInFx<ImerSipP92Processor>",catalog)
        self.assertIn("ARMIX_IMMERSIB_RELEASE_CANDIDATE",catalog)
    def test_legacy_and_host_power_are_realtime_owned(self):
        p=source("engine/builtin_fx/ImerSipP92Processor.h")
        ui=source("engine/builtin_fx/ImerSipP92Editor.h")
        host=source("src/Main.cpp")
        self.assertIn("restoreUnpublishedState",p)
        self.assertIn("out.writeInt(6)",p)
        self.assertIn("SonKuSpatialProcessor::getStateInformation",p)
        self.assertIn("SonKuSpatialProcessor::queueSettings",p)
        self.assertIn("ImerSipP92Editor*>(editor)",host)
        self.assertIn("ImerSipP92Processor*>(p)",host)
        self.assertIn("admitQueuedScene",source(
            "engine/builtin_fx/SonKuSpatialProcessor.h"))
        self.assertIn("engine_.bypassNativeVst3(",host)
        self.assertIn("engine_.setDryRoutingLabFxBypass(",host)
        self.assertIn("power_.setEnabled(false)",ui)
        self.assertIn("fx_.lastCompletedRequestId()",ui)
    def test_no_shadow_fx_in_public_build(self):
        cmake=source("CMakeLists.txt")
        self.assertIn("ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT",cmake)
        self.assertIn("ARMixImerSipP92NativeTests",cmake)

if __name__=="__main__": unittest.main()
