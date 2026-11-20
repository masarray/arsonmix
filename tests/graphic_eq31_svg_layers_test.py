# SPDX-License-Identifier: AGPL-3.0-only
"""Release-gate contract: GEQ31 must use the owner's ORIGINAL grouped SVG.

Not an audio/ASIO test. This deliberately prevents future visual rewrites that
silently flatten THUMB, paint JUCE substitutes or discard the embedded artwork.
"""
from pathlib import Path
import hashlib
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SVG = ROOT / 'assets/fx/graphic-eq31/slider_eq_grp.svg'
PROCESSOR = ROOT / 'engine/builtin_fx/GraphicEq31Processor.h'
KNOB = ROOT / 'assets/fx/graphic-eq31/KNOB-EQ.svg'
CMAKE = ROOT / 'CMakeLists.txt'
EXPECTED_SHA256 = 'aeaf02b8904a667724f34774a692da39a5a466bae302611f347fae4d9fd14dba'
KNOB_SHA256 = '754b3d525be1d4dcbe26fdca1d6bedc5b67e2d2436efec2aa9265ad86c5aad80'
SVGNS = {'s': 'http://www.w3.org/2000/svg'}

class OwnerOriginalSvgLayersTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = SVG.read_bytes()
        cls.svg = ET.fromstring(cls.raw)
        cls.native = PROCESSOR.read_text(encoding='utf-8')
        cls.cmake = CMAKE.read_text(encoding='utf-8')

    def test_exact_owner_artwork_and_named_groups(self):
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(), EXPECTED_SHA256)
        self.assertEqual(self.svg.attrib['viewBox'], '0 0 469 2042')
        fader = self.svg.find("./s:g[@id='Slider_EQ']", SVGNS)
        self.assertIsNotNone(fader)
        self.assertEqual([child.get('id') for child in fader], ['Track', 'Text', 'THUMB'])
        self.assertGreaterEqual(len(fader.find("./s:g[@id='Track']", SVGNS)), 6)
        self.assertIsNotNone(fader.find("./s:g[@id='Text']/s:text[@id='32']/s:tspan", SVGNS))
        self.assertIsNotNone(fader.find("./s:g[@id='Text']/s:g[@id='Hz']", SVGNS))
        self.assertIsNotNone(fader.find("./s:g[@id='THUMB']//s:use[@id='_Image3']", SVGNS))
        embedded = self.svg.find("./s:defs/s:image[@id='img1']", SVGNS)
        self.assertTrue(embedded.get('href', '').startswith('data:image/png;base64,'))

    def test_exact_svg_is_production_embedded_source(self):
        self.assertIn('assets/fx/graphic-eq31/slider_eq_grp.svg', self.cmake)
        self.assertNotIn('assets/fx/graphic-eq31/slider_eq_vector.svgz', self.cmake)
        self.assertIn('BinaryData::slider_eq_grp_svg', self.native)
        self.assertIn('getChildByAttribute("id", "Text")', self.native)
        self.assertIn('getChildByAttribute("id", "THUMB")', self.native)
        self.assertIn('tspan->addTextElement(number)', self.native)
        self.assertIn('unitSpan->addTextElement(isKhz?"kHz":"Hz")', self.native)
        self.assertIn('thumbGroups_[band]->setTransform(juce::AffineTransform::translation(0.f, shift))', self.native)
        self.assertIn('authoredFader->findChildWithID("THUMB")', self.native)
        self.assertIn('juce::Drawable::createFromSVG(*document)', self.native)
        self.assertIn('juce::Viewport viewport_', self.native)
        self.assertIn('std::array<juce::Slider, GraphicEq31Core::bandCount> sliders_', self.native)
        self.assertNotIn('fillRoundedRectangle(cx-thumbWidth', self.native)

    def test_readable_50px_zero_shadow_band_only_policy(self):
        # Original owner asset remains untouched, but the duplicated raster
        # shadow is omitted from the UI clone; vector THUMB stays authentic.
        self.assertIn('document->setAttribute("viewBox","60 0 352 2042")', self.native)
        self.assertIn('bitmapWrapper->getChildByAttribute("id","_Image3")', self.native)
        self.assertIn('thumbGroup->removeChildElement(bitmapWrapper,true)', self.native)
        self.assertNotIn('labelGroup->removeChildElement(hz,true)', self.native)
        self.assertIn('auto* unitGroup=labelGroup->getChildByAttribute("id","Hz")', self.native)
        self.assertIn('unitText->setAttribute("text-anchor","middle")', self.native)
        self.assertIn('static constexpr int bandCellWidth=50', self.native)
        self.assertIn('setSize(1610,391)', self.native)
        self.assertIn('font-family: Inter', self.native)
        self.assertIn('return juce::Font("Inter",size,juce::Font::plain)', self.native)
        self.assertNotIn('BinaryData::KNOB_EQ_svg', self.native)
        self.assertNotIn('drawRotarySlider(', self.native)
        self.assertNotIn('input_.setBounds', self.native)
        self.assertNotIn('getRawParameterValue("inputDb")', self.native)
        self.assertNotIn('getRawParameterValue("outputDb")', self.native)
        self.assertNotIn('inputSmooth_', self.native)
        self.assertNotIn('outputSmooth_', self.native)
        self.assertIn('out.writeInt(stateMagic);out.writeInt(3);', self.native)
        self.assertIn('const auto obsoleteTrim=input.readFloat();', self.native)
        self.assertNotIn('assets/fx/graphic-eq31/KNOB-EQ.svg', self.cmake)

    def test_full_thumb_travel_and_visual_presentation(self):
        self.assertIn('const float shift=-600.f*juce::jlimit(-12.f,12.f,gain)/12.f;', self.native)
        self.assertIn('juce::DropShadow(juce::Colours::black.withAlpha(.27f)', self.native)
        self.assertIn('getChildByAttribute("class","s5")', self.native)
        self.assertIn('getChildByAttribute("class","s6")', self.native)
        self.assertIn('getChildByAttribute("id","g2")', self.native)
        self.assertIn('juce::Colour::fromRGB(229,233,234)', self.native)
        self.assertIn('g.drawHorizontalLine(y,0.f,static_cast<float>(width))', self.native)
        self.assertIn('g.drawText("+12",5,84,36,16', self.native)
        self.assertIn('g.drawText("0",5,186,36,16', self.native)
        self.assertIn('g.drawText("-12",5,291,36,16', self.native)
        self.assertNotIn('smile_', self.native)
        self.assertNotIn('peakLabel_', self.native)
        self.assertNotIn('31 BANDS  /  +/-12 dB', self.native)
        self.assertNotIn('timerCallback()', self.native)
        # The authored vector thumb extends ~776..1055, while the slot
        # extends 183..1661. Neither +/-12 endpoint leaves 40px of empty slot.
        self.assertLessEqual(abs((776-600)-183), 10)
        self.assertLessEqual(abs((1055+600)-1661), 10)
        for gain in [-12., -6., 0., 6., 12.]:
            shift=-600*gain/12
            self.assertGreaterEqual(776+shift, 160)
            self.assertLessEqual(1055+shift, 1675)

if __name__ == '__main__':
    unittest.main()
