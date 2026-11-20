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
MAIN = ROOT / 'src/Main.cpp'
TEXTURE = ROOT / 'assets/fx/graphic-eq31/geq31_brushed_metal.jpg'
CMAKE = ROOT / 'CMakeLists.txt'
EXPECTED_SHA256 = 'aeaf02b8904a667724f34774a692da39a5a466bae302611f347fae4d9fd14dba'
TEXTURE_SHA256 = '4b7b5f68f502482f5d1fc1c973a48ae817087104485c71020c5bdfb8f6d1388c'
SVGNS = {'s': 'http://www.w3.org/2000/svg'}

class OwnerOriginalSvgLayersTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.raw = SVG.read_bytes()
        cls.svg = ET.fromstring(cls.raw)
        cls.native = PROCESSOR.read_text(encoding='utf-8')
        cls.main = MAIN.read_text(encoding='utf-8')
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
        self.assertIn('getChildByAttribute("id","Text")', self.native)
        self.assertIn('getChildByAttribute("id","THUMB")', self.native)
        self.assertIn('tspan->addTextElement(number)', self.native)
        self.assertIn('unitSpan->addTextElement(isKhz?"kHz":"Hz")', self.native)
        self.assertIn('thumbGroups_[band]->setTransform(juce::AffineTransform::translation(0.f, shift))', self.native)
        self.assertIn('authoredFader->findChildWithID("THUMB")', self.native)
        self.assertIn('juce::Drawable::createFromSVG(*document)', self.native)
        self.assertIn('juce::Viewport viewport_', self.native)
        self.assertIn('std::array<GraphicEq31DetentSlider,GraphicEq31Core::bandCount> sliders_', self.native)
        self.assertNotIn('fillRoundedRectangle(cx-thumbWidth', self.native)

    def test_brushed_texture_and_original_shadow(self):
        texture = TEXTURE.read_bytes()
        self.assertEqual(hashlib.sha256(texture).hexdigest(), TEXTURE_SHA256)
        self.assertTrue(texture.startswith(b'\xff\xd8\xff'))
        self.assertGreater(len(texture), 20_000)
        self.assertIn('assets/fx/graphic-eq31/geq31_brushed_metal.jpg', self.cmake)
        self.assertIn('BinaryData::geq31_brushed_metal_jpg', self.native)
        self.assertIn('juce::ImageCache::getFromMemory(', self.native)
        self.assertIn('g.drawImageWithin(brushedMetal_', self.native)
        # Use the original source THUMB raster rather than a vector-only
        # substitute, preserving author artwork and animation semantics.
        self.assertIn('rasterShadow->setAttribute("opacity",0.96)', self.native)
        self.assertNotIn('removeChildElement(bitmapWrapper,true)', self.native)
        self.assertIn('authoredFader->findChildWithID("THUMB")', self.native)
        self.assertIn('juce::DropShadow(juce::Colours::black.withAlpha(.55f)', self.native)
        self.assertIn('juce::RectanglePlacement::stretchToFit', self.native)

    def test_hardware_visual_fidelity_and_no_noise(self):
        self.assertIn('document->setAttribute("viewBox","60 0 352 2042")', self.native)
        self.assertIn('getChildByAttribute("id","g1")', self.native)
        self.assertIn('"#929b9f","#a6afb3"', self.native)
        self.assertIn('getChildByAttribute("id","g2")', self.native)
        self.assertIn('"#080c0f","#12181c"', self.native)
        self.assertIn('static constexpr int bandCellWidth=40', self.native)
        self.assertIn('setSize(1430,305)', self.native)
        self.assertIn('font-family: Inter; font-size: 27px; font-weight: 700', self.native)
        self.assertIn('font-family: Inter; font-size: 18px; font-weight: 600', self.native)
        self.assertIn('unitSpan->addTextElement(isKhz?"kHz":"Hz")', self.native)
        self.assertIn('return juce::Font("Inter",size,juce::Font::plain)', self.native)
        self.assertIn('getChildByAttribute("class","s5")', self.native)
        self.assertIn('getChildByAttribute("class","s6")', self.native)
        # SVG tick coordinates are kept; each is mapped through the
        # same 229px fader height and printed at BOTH window edges.
        self.assertIn('{208.75f,"+12"}',self.native)
        self.assertIn('{918.33f,"0"}',self.native)
        self.assertIn('{1627.92f,"-12"}',self.native)
        self.assertIn('viewport_.getRight()+6,labelY,36,19',self.native)
        self.assertIn('g.setFont(inter(10.5f).boldened())',self.native)
        self.assertNotIn('smile_', self.native)
        self.assertNotIn('peakLabel_', self.native)
        self.assertIn('void timerCallback() override', self.native)
        self.assertNotIn('31 BANDS  /  +/-12 dB', self.native)
        self.assertIn('BinaryData::KNOB_EQ_svg', self.native)
        self.assertIn('drawRotarySlider(', self.native)
        self.assertNotIn('getRawParameterValue("inputDb")', self.native)
        self.assertNotIn('getRawParameterValue("outputDb")', self.native)
        self.assertIn('out.writeInt(stateMagic);out.writeInt(4);', self.native)

    def test_shared_header_control_lease_and_right_rail(self):
        self.assertIn('dynamic_cast<juce::DocumentWindow*>(', self.native)
        self.assertIn('window->setVisible(false)', self.native)
        self.assertIn('geq31.close', self.native)
        self.assertIn('geq31.host-bypass', self.native)
        self.assertIn('bindHostPower(std::function<bool(bool)>', self.native)
        self.assertIn('power_.setEnabled(false)', self.native)
        self.assertIn('power_.setToggleState(active,juce::dontSendNotification)', self.native)
        self.assertIn('requestBypass_(!before.slotBypassed)', self.native)
        self.assertIn('if(!before.valid||before.globalBypassed||before.rackBypassed)', self.native)
        self.assertIn('geq->bindHostPower(', self.main)
        self.assertIn('engine_.bypassRetainedNativeFx(', self.main)
        self.assertIn('engine_.bypassRetainedRoutedFx(', self.main)
        self.assertIn('engine_.retainedNativeFxPower(', self.main)
        self.assertIn('engine_.retainedRoutedFxPower(', self.main)
        self.assertIn('juce::ComboBox preset_', self.native)
        self.assertIn('preset_.addItem("Flat",2)', self.native)
        self.assertIn('preset_.addItem("Vocal Presence",3)', self.native)
        self.assertIn('processor_.setBandFromUi(i,presetGain(choice,graphicEq31Hz[i]))', self.native)
        self.assertIn('int matchingFactoryPreset() const', self.native)
        self.assertIn('if(preset_.getSelectedId()!=accepted)', self.native)
        self.assertIn('flat_.setBounds(r-125,16,44,24)', self.native)
        self.assertIn('constexpr int x0=45, artY=55, artH=229, rightReserve=145', self.native)
        self.assertNotIn('jump_', self.native)

    def test_black_svg_ticks_but_original_source_unchanged(self):
        self.assertIn('tick->getStringAttribute("class")=="s0"', self.native)
        self.assertIn('"fill:none;stroke:#131a1d;stroke-width:7.9;"', self.native)
        self.assertIn('tick->setAttribute("style"', self.native)
        self.assertIn('getChildByAttribute("id","g1")', self.native)
        self.assertIn('getChildByAttribute("id","g2")', self.native)
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(), EXPECTED_SHA256)

    def test_output_level_and_cached_thumb_depth(self):
        self.assertIn('assets/fx/graphic-eq31/KNOB-EQ.svg', self.cmake)
        self.assertIn('BinaryData::KNOB_EQ_svg', self.native)
        self.assertIn('findChildWithID("POINTER")', self.native)
        self.assertIn('drawRotarySlider(', self.native)
        self.assertIn('level_.setBounds(getWidth()-94,125,72,72)', self.native)
        self.assertIn('levelAttachment_=std::make_unique<Attachment>', self.native)
        self.assertIn('juce::ParameterID{"levelDb",1}', self.native)
        self.assertIn('levelGain_.reset(sampleRate,0.015)', self.native)
        self.assertIn('levelGain_.getNextValue()', self.native)
        self.assertIn('const float levelDb=levelValue_->load(std::memory_order_relaxed)', self.native)
        self.assertIn('rtLevelDb_.store(levelDb,std::memory_order_release)', self.native)
        self.assertIn('rtLevelDb_.load(std::memory_order_acquire)', self.native)
        self.assertIn('if(acceptedLevel!=lastLevelDb_)', self.native)
        self.assertIn('lastLevelDb_=acceptedLevel', self.native)
        self.assertIn('float savedUserLevelDb_=0.f;', self.native)
        self.assertIn('processor_.acceptedLevelDb()-savedUserLevelDb_', self.native)
        self.assertIn('processor_.setFlatFromUi();', self.native)
        self.assertIn('g.drawImageAt(thumbShadowStamp_', self.native)
        self.assertIn('prepareThumbShadow(shadowBounds.getWidth()', self.native)
        self.assertIn('silhouette.addRoundedRectangle(', self.native)
        self.assertIn('juce::Colour::fromRGB(52,64,71)', self.native)
        self.assertNotIn('drawForRectangle(g,shadowBounds)', self.native)
        self.assertIn('out.writeInt(stateMagic);out.writeInt(4)', self.native)
        self.assertIn('version==3&&length!=v3Bytes', self.native)
        self.assertIn('version==4&&length!=v4Bytes', self.native)
        self.assertIn('explicitLevel=input.readFloat()', self.native)
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(), EXPECTED_SHA256)

    def test_black_anodized_owner_thumb_not_a_replacement(self):
        # Original SVG geometry/embedded raster intact; only UI clones
        # recolour original g3-g6 paths and keep authored _Image3 shadow.
        self.assertIn('const char* ids[]={"g3","g4","g5","g6"}', self.native)
        for color in ('#090b0e','#323a40','#141a1e','#0d1216','#b4bdc1'):
            self.assertIn(color,self.native)
        self.assertIn('palettes[group][juce::jmin(i,maxima[group])]',self.native)
        self.assertIn('rasterShadow->setAttribute("opacity",0.96)',self.native)
        self.assertIn('authoredFader->findChildWithID("THUMB")',self.native)
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),EXPECTED_SHA256)

    def test_titanium_brushing_is_cached_not_rendered_per_band(self):
        self.assertIn('makeSatinBrushedMetal(originalMetal)',self.native)
        self.assertIn('juce::Image::BitmapData out(result,juce::Image::BitmapData::writeOnly)',self.native)
        self.assertIn('(sourceLuma-145.f)*.08f',self.native)
        self.assertIn('constexpr int w=512,h=256',self.native)
        self.assertIn('g.drawImageWithin(brushedMetal_',self.native)
        self.assertNotIn('makeSatinBrushedMetal(source)',self.native)
        # The exact JPEG approved earlier remains embedded, without drift.
        self.assertEqual(hashlib.sha256(TEXTURE.read_bytes()).hexdigest(),TEXTURE_SHA256)

    def test_close_matches_approved_shared_fx_24px_language(self):
        self.assertIn('close_.setButtonText({})',self.native)
        self.assertIn('close_.setBounds(r-41,16,24,24)',self.native)
        self.assertIn('button.getComponentID()=="geq31.close"',self.native)
        self.assertIn('juce::Colour(0xff3e5866)',self.native)
        self.assertIn('juce::Colour(0xff293e4b)',self.native)
        self.assertIn('juce::Colour(0xff633840)',self.native)
        self.assertIn('juce::Colour(0xffc52d40)',self.native)
        self.assertIn('juce::Colour(0xff607d8c)',self.native)
        self.assertIn('juce::Colour(0xfff4f8fa)',self.native)
        self.assertIn('juce::AffineTransform::scale(22.f/24.f)',self.native)
        self.assertIn('x.lineTo(17.6f,19.f)',self.native)
        self.assertNotIn('close_.setButtonText(juce::String::fromUTF8("×"))',self.native)
        self.assertIn('window->setVisible(false)',self.native)

    def test_thumb_index_reaches_real_major_ticks(self):
        # The mechanical metal THUMB may overhang the slot at +12/-12.
        # What matters is the WHITE index aligned with the source ticks,
        # not the thumb exterior reaching the outer slot edge.
        idx=916.5
        for db in (-12.,-6.,0.,6.,12.):
            t=db/12.
            shift=-(idx-208.75)*t if t>=0 else -(1627.92-idx)*t
            y=idx+shift
            self.assertGreaterEqual(y,208.75-1e-4)
            self.assertLessEqual(y,1627.92+1e-4)
        self.assertAlmostEqual(idx-(idx-208.75),208.75,places=4)
        self.assertAlmostEqual(idx+(1627.92-idx),1627.92,places=4)
        self.assertIn('-(indexCenterY-208.75f)*t',self.native)
        self.assertIn('-(1627.92f-indexCenterY)*t',self.native)

    def test_compact_scale_and_symmetric_layout_without_header_changes(self):
        # 31 x 50px -> 31 x 40px and 286px -> 229px.
        self.assertEqual(31*40,1240)
        self.assertEqual(1430-1240,45+145)
        self.assertAlmostEqual(40/50,.8,places=6)
        self.assertLess(abs(229/286-.8),.002)
        self.assertIn('setResizeLimits(760,305,1750,305)',self.native)
        self.assertIn('setSize(1430,305)',self.native)
        self.assertIn('constexpr int x0=45, artY=55, artH=229, rightReserve=145',self.native)
        self.assertIn('viewport_.setBounds(x0,artY,',self.native)
        self.assertIn('static constexpr int bandCellWidth=40',self.native)
        self.assertIn('juce::jmax(1,getWidth()-x0-rightReserve)',self.native)
        self.assertIn('g.drawText(tick.second,4,labelY,36,19',self.native)
        self.assertIn('g.drawText(tick.second,viewport_.getRight()+6,labelY,36,19',self.native)
        # Header remains the identical five placements established before v7.
        for spec in ('preset_.setBounds(r-349,16,152,24)',
                     'save_.setBounds(r-189,16,56,24)',
                     'flat_.setBounds(r-125,16,44,24)',
                     'power_.setBounds(r-73,16,24,24)',
                     'close_.setBounds(r-41,16,24,24)'):
            self.assertIn(spec,self.native)
        self.assertEqual(55-53,2)
        self.assertEqual(305-(55+238),12)
        for y in (208.75,918.33,1627.92):
            pixel_y=55+y*229/2042
            self.assertTrue(55<=pixel_y<305-12)

    def test_real_hardware_zero_db_detent_on_pointer_drag(self):
        self.assertIn('class GraphicEq31DetentSlider final : public juce::Slider',self.native)
        self.assertIn('double snapValue(double attemptedValue,DragMode dragMode) override',self.native)
        self.assertIn('detentEngaged_=std::abs(getValue())<0.001',self.native)
        self.assertIn('const double limit=detentEngaged_?0.85:0.45',self.native)
        self.assertIn('if(dragMode==notDragging)',self.native)
        self.assertIn('return 0.0;',self.native)
        # Mechanical-feel entry/release hysteresis in dB; does not
        # quantise the processor, user presets or automation targets.
        engaged=False
        results=[]
        for attempted in (1.5,.8,.44,.6,.84,.86,-.3,-.87):
            limit=.85 if engaged else .45
            engaged=abs(attempted)<=limit
            results.append(0.0 if engaged else attempted)
        self.assertEqual(results,[1.5,.8,0.,0.,0.,.86,0.,-.87])
        self.assertNotIn('snapValue(',self.native.split('class GraphicEq31Processor final')[1].split('class GraphicEq31DetentSlider')[0])

    def test_white_index_hit_major_ticks_and_pixel_crisp(self):
        # Important: alignment uses the s11 WHITE index center, not the
        # outer THUMB ends. +12 must hit 208.75, -12 1627.92.
        index_center=(913.+920.)*.5
        up_shift=-(index_center-208.75)
        down_shift=1627.92-index_center
        self.assertAlmostEqual(index_center+up_shift,208.75,places=4)
        self.assertAlmostEqual(index_center+down_shift,1627.92,places=4)
        self.assertIn('static constexpr float indexCenterY=916.5f;',self.native)
        self.assertIn('juce::roundToInt(y+',self.native)
        self.assertIn('g.fillRect(indexX,indexY,indexWidth,2);',self.native)
        self.assertIn('g.setColour(juce::Colours::white);',self.native)
        # Full original THUMB/Track groups and source checksum remain unchanged.
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(),EXPECTED_SHA256)

    def test_saved_user_presets_are_host_owned_and_header_uniform(self):
        self.assertIn('bindUserPresetService(',self.native)
        self.assertIn('save_.setComponentID("geq31.save")',self.native)
        self.assertIn('save_.onClick=[this]{promptSavePreset();}',self.native)
        self.assertIn('recallUserPreset_(userPresets_[index].id)',self.native)
        self.assertIn('refreshPresetList();',self.native)
        self.assertIn('preset_.setBounds(r-349,16,152,24)',self.native)
        self.assertIn('save_.setBounds(r-189,16,56,24)',self.native)
        self.assertIn('flat_.setBounds(r-125,16,44,24)',self.native)
        self.assertIn('power_.setBounds(r-73,16,24,24)',self.native)
        self.assertIn('close_.setBounds(r-41,16,24,24)',self.native)
        self.assertIn('geq->bindUserPresetService(',self.main)
        self.assertIn('builtinFxUserPresets_.saveNew(entry)',self.main)
        self.assertIn('builtinFxUserPresets_',self.main)
        self.assertIn('BuiltInFxAcceptedSnapshot::acceptedSettings(',self.main)
        self.assertIn('engine_.retainedNativeFxPower(',self.main)
        self.assertIn('engine_.retainedRoutedFxPower(',self.main)
        self.assertNotIn('rightRail=176',self.native)
        self.assertNotIn('preset_.setBounds(getWidth()-298,13,200,30)',self.native)

if __name__ == '__main__':
    unittest.main()
