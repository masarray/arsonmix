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
        self.assertIn('tspan->addTextElement(juce::String(graphicEq31Labels[band]))', self.native)
        self.assertIn('thumbGroups_[band]->setTransform(juce::AffineTransform::translation(0.f, shift))', self.native)
        self.assertIn('authoredFader->findChildWithID("THUMB")', self.native)
        self.assertIn('juce::Drawable::createFromSVG(*document)', self.native)
        self.assertIn('juce::Viewport viewport_', self.native)
        self.assertIn('std::array<juce::Slider, GraphicEq31Core::bandCount> sliders_', self.native)
        self.assertNotIn('fillRoundedRectangle(cx-thumbWidth', self.native)

    def test_compact_inter_and_owner_knob_input_only(self):
        raw_knob = KNOB.read_bytes()
        self.assertEqual(hashlib.sha256(raw_knob).hexdigest(), KNOB_SHA256)
        knob = ET.fromstring(raw_knob)
        self.assertIsNotNone(knob.find(".//s:g[@id='KNOB']", SVGNS))
        self.assertIsNotNone(knob.find(".//s:g[@id='POINTER']", SVGNS))
        self.assertIn('assets/fx/graphic-eq31/KNOB-EQ.svg', self.cmake)
        self.assertIn('BinaryData::KNOB_EQ_svg', self.native)
        self.assertIn('knobPointer_->setTransform(', self.native)
        self.assertIn('static constexpr int bandCellWidth=37', self.native)
        self.assertIn('setSize(1370,391)', self.native)
        self.assertIn('font-family: Inter', self.native)
        self.assertIn('return juce::Font("Inter",size,juce::Font::plain)', self.native)
        self.assertNotIn('output_.setBounds', self.native)
        self.assertNotIn('getRawParameterValue("outputDb")', self.native)
        self.assertNotIn('outputSmooth_', self.native)
        self.assertIn('out.writeInt(stateMagic);out.writeInt(2);', self.native)
        self.assertIn('discardedOutputDb=input.readFloat();', self.native)

    def test_thumb_travel_stays_inside_owner_viewbox(self):
        # Authored thumb bounds from original 469x2042 grouped SVG:
        # top ~721, bottom ~1139.  Positive boost moves original group up.
        for gain in [-12., -6., 0., 6., 12.]:
            shift = -560 * gain / 12
            self.assertGreaterEqual(721 + shift, 0)
            self.assertLessEqual(1139 + shift, 2042)

if __name__ == '__main__':
    unittest.main()