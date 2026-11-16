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
        self.assertIn("s.renderMode=9",scene)
        for value in ("s.stereoSpreadDegrees=82.f","s.sourceDistanceMeters=1.6f",
                      "s.spacePreset=3","s.roomAmount=.78f","s.ambience=.33f",
                      "s.outputTrim=1.f"):
            self.assertIn(value,scene)
        self.assertIn("SonKuSpatialProcessor(imersipP92Factory(2))",p)
        self.assertIn("inline constexpr std::array<ImerSipP92Preset,6>",scene)
        self.assertIn('"Royal Concert Hall"',scene)
        self.assertIn('"Arena Thunder"',scene)
        self.assertIn('"Front Row Power"',scene)
        self.assertIn('"Symphonic Palace"',scene)
        self.assertIn('"Velvet Cinema"',scene)
        ui=source("engine/builtin_fx/ImerSipP92Editor.h")
        self.assertIn("ImerSipP92VisualSkin",ui)
        self.assertIn("imersipP92ConcertBoxes(",ui)
        self.assertIn("skin_.drawRoom(g)",ui)
        self.assertIn("skin_.drawPanel(g)",ui)
        self.assertIn("skin_.drawConcert(g,concert)",ui)
        skin=source("engine/builtin_fx/ImerSipP92VisualSkin.h")
        cmake=source("CMakeLists.txt")
        self.assertIn("ImageCache::getFromMemory",skin)
        self.assertIn("Graphics::highResamplingQuality",skin)
        self.assertIn("RectanglePlacement::centred",skin)
        self.assertNotIn("getClippedImage",skin)
        self.assertNotIn("Drawable::createFromImageData",skin)
        self.assertNotIn("juce::Drawable",skin)
        for name in ("line_array_left", "line_array_right",
                     "subwoofer_left", "subwoofer_right",
                     "front_fill_left", "front_fill_right",
                     "side_fill_left", "side_fill_right",
                     "slider_track", "slider_thumb", "knob_master_purple"):
            self.assertIn("BinaryData::"+name+"_png",skin)
        self.assertIn("scripts/generate_imersip_concert_png.py",cmake)
        self.assertIn("ARMIX_IMERSIP_PNG_SOURCES",cmake)
        self.assertNotIn("assets/imersip/vector/imersip_room.svg",cmake)
        self.assertIn("for(const auto& scene:imersipP92Presets)",ui)
        self.assertNotIn("P9.2 Neutral A/B",ui)
        self.assertNotIn("Warm Auditorium",ui)
        self.assertNotIn("Grand Arena",ui)
        self.assertIn("prepareNativeBuiltInFx<ImerSipP92Processor>",catalog)
        self.assertIn("ARMIX_SONKU_EXPERIMENTAL_P90_NATIVE_RT",catalog)
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
