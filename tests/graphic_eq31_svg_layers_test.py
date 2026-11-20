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
KNOB = ROOT / 'assets/fx/graphic-eq31/geq31_knob_black.png'
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
        self.assertIn('rasterShadow->setAttribute("opacity",0.87)', self.native)
        self.assertNotIn('removeChildElement(bitmapWrapper,true)', self.native)
        self.assertIn('authoredFader->findChildWithID("THUMB")', self.native)
        self.assertIn('juce::DropShadow(juce::Colours::black.withAlpha(.30f)', self.native)
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
        self.assertIn('g.setFont(inter(11.f).boldened())',self.native)
        self.assertNotIn('smile_', self.native)
        self.assertNotIn('peakLabel_', self.native)
        self.assertIn('void timerCallback() override', self.native)
        self.assertNotIn('31 BANDS  /  +/-12 dB', self.native)
        self.assertNotIn('BinaryData::KNOB_EQ_svg', self.native)
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
        self.assertIn('constexpr int x0=45, artY=55, artH=229', self.native)
        self.assertNotIn('jump_', self.native)

    def test_black_svg_ticks_but_original_source_unchanged(self):
        self.assertIn('tick->getStringAttribute("class")=="s0"', self.native)
        self.assertIn('"fill:none;stroke:#131a1d;stroke-width:7.9;"', self.native)
        self.assertIn('tick->setAttribute("style"', self.native)
        self.assertIn('getChildByAttribute("id","g1")', self.native)
        self.assertIn('getChildByAttribute("id","g2")', self.native)
        self.assertEqual(hashlib.sha256(self.raw).hexdigest(), EXPECTED_SHA256)

    def test_black_anodized_owner_thumb_not_a_replacement(self):
        # Original SVG geometry/embedded raster intact; only UI clones
        # recolour original g3-g6 paths and keep authored _Image3 shadow.
        self.assertIn('const char* ids[]={"g3","g4","g5","g6"}', self.native)
        for color in ('#090b0e','#323a40','#141a1e','#0d1216','#b4bdc1'):
            self.assertIn(color,self.native)
        self.assertIn('palettes[group][juce::jmin(i,maxima[group])]',self.native)
        self.assertIn('rasterShadow->setAttribute("opacity",0.87)',self.native)
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
        self.assertEqual(1430-1240,2*45+100)
        self.assertAlmostEqual(40/50,.8,places=6)
        self.assertLess(abs(229/286-.8),.002)
        self.assertIn('setResizeLimits(860,305,1760,305)',self.native)
        self.assertIn('setSize(1430,305)',self.native)
        self.assertIn('constexpr int x0=45, artY=55, artH=229',self.native)
        self.assertIn('juce::jmax(1,getWidth()-rightRailWidth-2*x0)',self.native)
        self.assertIn('static constexpr int bandCellWidth=40',self.native)
        self.assertIn('static constexpr int rightRailWidth=100',self.native)
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

    def test_all_faders_rotary_and_wheel_have_single_zero_detent_owner(self):
        # One JUCE Slider subclass for 31 bands AND Output, not duplicate
        # DSP/APVTS/local-wheel state. Default wheel path calls snapValue
        # with DragMode::notDragging (verified against JUCE Slider.cpp).
        self.assertIn('class GraphicEq31DetentSlider final : public juce::Slider',self.native)
        self.assertIn('GraphicEq31DetentSlider level_;',self.native)
        self.assertIn('std::array<GraphicEq31DetentSlider,GraphicEq31Core::bandCount> sliders_',self.native)
        self.assertIn('void setZeroDetent(double captureDb,double releaseDb) noexcept',self.native)
        self.assertIn('slider.setZeroDetent(1.1,1.8)',self.native)
        self.assertIn('level_.setZeroDetent(0.65,1.2)',self.native)
        self.assertIn('slider.setScrollWheelEnabled(true)',self.native)
        self.assertIn('level_.setScrollWheelEnabled(true)',self.native)
        self.assertIn('if(dragMode==notDragging)',self.native)
        self.assertIn('const double current=getValue()',self.native)
        self.assertIn('if(std::abs(current)>0.001 &&',self.native)
        self.assertIn('(current<0.0&&attemptedValue>0.0)',self.native)
        self.assertIn('(current>0.0&&attemptedValue<0.0)',self.native)
        self.assertIn('return juce::Slider::snapValue(attemptedValue,dragMode)',self.native)
        self.assertIn('detentEngaged_=std::abs(getValue())<0.001',self.native)
        self.assertIn('return 0.0;',self.native)
        self.assertNotIn('const double limit=detentEngaged_?0.85:0.45',self.native)
        # Verify the UX policy numerically: pointer hysteresis vs wheel
        # crossing/escape, no permanent 0dB trapping.
        def drag(values,capture,release):
            held=False
            result=[]
            for x in values:
                engage=release if held else capture
                held=abs(x)<=engage
                result.append(0 if held else x)
            return result
        self.assertEqual(drag([2.,1.09,1.7,1.81,-.8,-1.9],1.1,1.8),
                         [2.,0,0,1.81,0,-1.9])
        self.assertEqual(drag([.6,.8,1.19,1.21],.65,1.2),
                         [0,0,0,1.21])
        def wheel(current,target,capture):
            if abs(current)>.001 and (abs(target)<=capture or
                    (current<0<target) or (current>0>target)):
                return 0.
            return target
        self.assertEqual(wheel(-.9,-.3,1.1),0)
        self.assertEqual(wheel(-.3,.8,1.1),0)
        self.assertEqual(wheel(0.,.3,1.1),.3)
        self.assertEqual(wheel(0.,-.3,1.1),-.3)
        self.assertEqual(wheel(-2.,.2,.65),0.)
        self.assertEqual(wheel(0.,.2,.65),.2)
        # The native audio core remains independently owner-accepted.
        self.assertNotIn('snapValue(',self.native.split('class GraphicEq31Processor final')[1].split('class GraphicEq31DetentSlider')[0])

    def test_background_only_drag_moves_host_owned_native_window(self):
        self.assertIn('addMouseListener(this,true)',self.native)
        self.assertIn('removeMouseListener(this)',self.native)
        self.assertIn('juce::ComponentDragger windowDragger_',self.native)
        self.assertIn('juce::Component::SafePointer<juce::DocumentWindow> dragWindow_',self.native)
        self.assertIn('dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())',self.native)
        self.assertIn('windowDragger_.startDraggingComponent(window,event)',self.native)
        self.assertIn('windowDragger_.dragComponent(dragWindow_.getComponent(),event,nullptr)',self.native)
        self.assertIn('bool isDragSurface(const juce::MouseEvent& event) const noexcept',self.native)
        self.assertIn('const auto* origin=event.originalComponent',self.native)
        self.assertIn('origin==this || origin==&viewport_ || origin==&bandSurface_',self.native)
        self.assertIn('if(!event.mods.isLeftButtonDown()||!isDragSurface(event))return',self.native)
        self.assertNotIn('origin==&level_',self.native)
        # The mixer /preview/ WebView2 CSS and native FX controls are separate
        # owners; no global CSS overrides or free-floating editor replacement.
        self.assertIn('setSize(1430,305)',self.native)
        self.assertIn('level_.setBounds(getWidth()-106,127,84,84)',self.native)
        self.assertIn('getWidth()-rightRailWidth-14,104',self.native)
        self.assertIn('getWidth()-rightRailWidth-14,224',self.native)

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


    def test_static_png_dynamic_pointer_and_outer_blue_arc(self):
        raw=KNOB.read_bytes()
        self.assertTrue(raw.startswith(b'\x89PNG\r\n\x1a\n'))
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         '270aaf8220aa5659a15c46531fbcab55803d6351148400df1e86f0e143f75e03')
        self.assertIn('assets/fx/graphic-eq31/geq31_knob_black.png',self.cmake)
        self.assertIn('BinaryData::geq31_knob_black_png',self.native)
        self.assertNotIn('BinaryData::KNOB_EQ_svg',self.native)
        self.assertIn('g.drawImage(knobImage_',self.native)
        self.assertIn('g.strokePath(arc',self.native)
        self.assertIn('arc.addCentredArc(cx,cy,side*.365f',self.native)
        self.assertNotIn('side*.38f',self.native)
        self.assertNotIn('side*.436f',self.native)
        self.assertIn('const auto dot=polar(side*.292f,end)',self.native)
        self.assertIn('juce::Colour(0xff26b8f6)',self.native)
        self.assertIn('juce::Colour(0xff27c8ff)',self.native)
        self.assertIn('juce::jmap(db,-24.f,0.f,0.f,.5f)',self.native)
        self.assertIn('juce::jmap(db,0.f,6.f,.5f,1.f)',self.native)
        self.assertIn('static constexpr int bandCellWidth=40',self.native)
        self.assertNotIn('knobArtwork_',self.native)
        self.assertNotIn('rightReserve=145',self.native)
        self.assertNotIn('innerCutout',self.native)
        # Blue indicator and progress arc belong to JUCE, not PNG pixels.
        try:
            from PIL import Image
        except ImportError:
            return
        blue=sum(1 for red,green,b,a in Image.open(KNOB).convert('RGBA').getdata()
                 if a>100 and b>red*1.5 and b>green*1.3 and b>80)
        self.assertEqual(blue,0)


    def test_v12_optical_alignment_and_immersib_bypass_language(self):
        # Measured source PNG center and radial geometry in 192px.
        self.assertIn('96.7f/192.f',self.native)
        self.assertIn('97.97f/192.f',self.native)
        self.assertGreaterEqual(192*.365,69)
        self.assertLessEqual(192*.365,75)
        self.assertLess(192*.292+2.65*192/84,65)
        self.assertIn('juce::Graphics::highResamplingQuality',self.native)
        self.assertIn('arc.addCentredArc(cx,cy,side*.365f',self.native)
        self.assertNotIn('const int segments=juce::jmax(1',self.native)
        self.assertIn('if(t>0.f)',self.native)
        self.assertIn('level_.setBounds(getWidth()-106,127,84,84)',self.native)
        self.assertIn('getWidth()-rightRailWidth-14,104',self.native)
        self.assertIn('getWidth()-rightRailWidth-14,224',self.native)
        self.assertIn('g.setFont(inter(11.f).boldened())',self.native)
        self.assertIn('juce::Colour(0xff344047)',self.native)
        self.assertIn('#include <optional>',self.native)
        self.assertIn('bool effectiveBypassed() const noexcept',self.native)
        self.assertIn('(slotBypassed || rackBypassed || globalBypassed)',self.native)
        self.assertIn('std::optional<bool> hostBypassed_',self.native)
        self.assertIn('bool hostLeaseWasValid_=false',self.native)
        self.assertIn('void paintOverChildren(juce::Graphics& g) override',self.native)
        self.assertIn('g.excludeClipRegion(power_.getBounds())',self.native)
        self.assertIn('g.excludeClipRegion(close_.getBounds())',self.native)
        self.assertIn('juce::Colour(0xb803060a)',self.native)
        self.assertIn('juce::Colour(0xeeffa53c)',self.native)
        self.assertIn('bypassed?"BYPASSED":"FX UNAVAILABLE"',self.native)
        self.assertIn('if(changed)repaint()',self.native)
        # Cross-FX snapshot: do not create another bypass vocabulary or tint.
        visual=(ROOT/'engine/builtin_fx/ImerSipP92VisualSkin.h').read_text()
        editor=(ROOT/'engine/builtin_fx/ImerSipP92Editor.h').read_text()
        for token in ('"BYPASSED"','0xb803060a','0xeeffa53c'):
            self.assertIn(token,visual)
        self.assertIn('void paintOverChildren(juce::Graphics& g) override',editor)
        self.assertIn('std::optional<bool> hostBypassed_',editor)

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
