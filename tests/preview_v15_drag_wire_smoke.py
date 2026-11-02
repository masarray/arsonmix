#!/usr/bin/env python3
"""V15 physical OUT→IN drag and Bus-only routing smoke (real Chromium).
GUI-only: verifies pointer hit targets and visual state, NOT native PCM.
Run: python tests/preview_v15_drag_wire_smoke.py  (on main)
Or: python design-references/audition-mixer-study/visual_wire_v15_qa.py
"""
from __future__ import annotations
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE=Path(__file__).resolve()
ROOT=next(p for p in HERE.parents if (p/".github").is_dir())
PAGE=HERE.with_name("index.html") if HERE.parent.name=="audition-mixer-study" else ROOT/"site/preview-v2/index.html"

def assert_true(value,message):
    if not value: raise AssertionError(message)

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        page=browser.new_page(viewport={"width":1680,"height":900},device_scale_factor=1)
        errors=[]
        page.on("pageerror",lambda err:errors.append(str(err)))
        page.goto(PAGE.as_uri())
        # V18 hardware key socket / shallow click / portrait meter smoke.
        key=page.locator('#tracks .strip[data-id="1"] button.switch.mute')
        # V22 static overlaid metal socket and channel front-layer rim.
        well=key.locator('xpath=..')
        assert_true(well.evaluate("el=>el.classList.contains('hw-switch-socket')"),
                    'Button must have an independently mounted stationary socket')
        bezel=well.evaluate("""el=>({
            z:getComputedStyle(el,'::after').zIndex,
            cap:getComputedStyle(el.querySelector('button')).zIndex,
            events:getComputedStyle(el,'::after').pointerEvents
        })""")
        assert_true(bezel['z']=='4' and bezel['cap']=='2' and bezel['events']=='none',
                    'Outer hole lip must be layered above the moving cap')
        well_y=well.bounding_box()['y']
        key.scroll_into_view_if_needed()
        rr=key.bounding_box()
        page.mouse.move(rr['x']+rr['width']/2,rr['y']+rr['height']/2)
        page.mouse.down()
        assert_true(abs(well.bounding_box()['y']-well_y)<.25,
                    'Static hole must not move while button depresses')
        page.mouse.up()
        key.click() # reset MUTE after test press; state contract starts from OFF
        selected=page.locator('#tracks .strip.selected').first
        assert_true(selected.count()==1,'No selected channel to verify')
        rim=selected.evaluate("""el=>({
            z:getComputedStyle(el,'::after').zIndex,
            border:getComputedStyle(el,'::after').borderTopWidth,
            events:getComputedStyle(el,'::after').pointerEvents
        })""")
        assert_true(rim['z']=='60' and rim['border']=='1px' and rim['events']=='none',
                    'Selected channel rim is buried behind mixer sections')
        cap=page.locator('#tracks .strip[data-id="1"] .fader-travel .cap')
        capStyle=cap.evaluate("""el=>({
            w:getComputedStyle(el).width,h:getComputedStyle(el).height,
            filter:getComputedStyle(el).filter
        })""")
        assert_true(capStyle['w']=='23px' and capStyle['h']=='48px' and
                    capStyle['filter']=='none','Fader thumb is not compact/crisp')
        track=page.locator('#tracks .strip[data-id="1"] .fader-travel')
        travel=track.evaluate("""el=>({
            top:getComputedStyle(el).top,bottom:getComputedStyle(el).bottom
        })""")
        assert_true(travel['top']=='24px' and travel['bottom']=='24px',
                    'Fader slot not expanded consistently')
        assert_true(not errors,'V22 browser errors: '+str(errors))
        key=page.locator('#tracks .strip[data-id="1"] button.switch.mute')
        initial=key.evaluate("""el=>({
          radius:getComputedStyle(el).borderRadius,
          filter:getComputedStyle(el).filter,
          bay:getComputedStyle(el.parentElement).backgroundColor
        })""")
        assert_true(initial['radius']=='2px','Hardware key must remain sharp-edged')
        assert_true(initial['filter']=='none','No GUI blur filter allowed')
        assert_true(initial['bay']=='rgb(8, 20, 27)','Button socket cavity missing')
        meter=page.locator('#tracks .strip[data-id="1"] .meter-lane').first
        led=meter.evaluate("""el=>({
          width:getComputedStyle(el).width,
          cells:getComputedStyle(el,'::before').backgroundImage
        })""")
        assert_true(led['width']=='6px' and '9px' in led['cells'] and '11px' in led['cells'],
                    'LED must be portrait (6x9px with 2px spacing)')
        # V19 owner-approved modern console material: same socket and cap,
        # graphite OFF, ruby MUTE, amber SOLO, cyan PFL; mechanical depth unchanged.
        def material(locator):
            return locator.evaluate("""el=>({
                face:getComputedStyle(el).getPropertyValue('--am-hw-key-face').trim(),
                radius:getComputedStyle(el).borderRadius,
                filter:getComputedStyle(el).filter,
                press:getComputedStyle(el).getPropertyValue('--am-hw-key-press-y').trim()
            })""")
        assert_true(material(key)['face']=='#405360','V19 graphite resting key missing')
        assert_true(material(key)['press']=='1px','V19 must retain 1px hardware press')
        # V20 true cube extrusion + internal lens, without GPU glow.
        cube=key.evaluate("""el=>({
            bright:getComputedStyle(el).getPropertyValue('--am-hw-key-bright').trim(),
            deep:getComputedStyle(el).getPropertyValue('--am-hw-key-deep').trim(),
            wall:getComputedStyle(el).getPropertyValue('--am-hw-key-wall').trim(),
            shadow:getComputedStyle(el).boxShadow,
            background:getComputedStyle(el).backgroundImage,
            filter:getComputedStyle(el).filter
        })""")
        assert_true(cube['bright']=='#586c79' and cube['deep']=='#202f39',
                    'V20 distinct light/dark lens materials missing')
        assert_true('0px 2px 0px' in cube['shadow'],
                    'V22 restrained HARD sidewall missing')
        assert_true('linear-gradient' in cube['background'] and
                    'color(srgb' not in cube['background'],
                    'V22 smooth three-stop lens gradient missing')
        assert_true(cube['filter']=='none','V20 must avoid GPU blurring effects')
        key.click()
        assert_true(material(key)['face']=='#ae3346','V19 jewel ruby MUTE missing')
        key.click()
        solo=page.locator('#tracks .strip[data-id="1"] button.switch.solo')
        solo.click()
        assert_true(material(solo)['face']=='#bc8f2e','V19 amber SOLO missing')
        solo.click()
        cue=page.locator('#tracks .strip[data-id="1"] button.switch.cue')
        cue.click()
        assert_true(material(cue)['face']=='#2a879e','V19 cyan PFL missing')
        cue.click()
        key.scroll_into_view_if_needed()
        rect=key.bounding_box()
        assert_true(bool(rect),'Button missing')
        page.mouse.move(rect['x']+rect['width']/2,rect['y']+rect['height']/2)
        page.mouse.down()
        pressed=key.evaluate('el=>getComputedStyle(el).transform')
        assert_true(pressed.endswith(', 1)'),'Hardware press depth must be 1px: '+pressed)
        page.mouse.up()
        key.click() # undo MUTE so routing tests see same fixture
        page.screenshot(path=str(ROOT/'preview-v18-hardware-1680.png'))
        # V25 quick VST picker must dismiss on external click, focus or window blur.
        quick_open=page.locator('#fastPicker')
        fx_pick=page.locator('#tracks .strip[data-id="1"] .fx-pick').first
        fx_pick.click()
        assert_true(quick_open.is_visible(),'Fast VST picker should open from FX rack')
        page.locator('#tracks .strip[data-id="2"] .strip-identity-top').click()
        assert_true(quick_open.is_hidden(),'Click outside VST picker must close it')
        fx_pick.click()
        assert_true(quick_open.is_visible(),'Quick picker failed to reopen')
        page.locator('#bankSelect').focus()
        assert_true(quick_open.is_hidden(),'Changing keyboard focus must close VST picker')
        fx_pick.click()
        assert_true(quick_open.is_visible(),'Picker failed third opening')
        page.evaluate("window.dispatchEvent(new Event('blur'))")
        assert_true(quick_open.is_hidden(),'Window blur must dismiss quick VST picker')
        # Manager remains nonmodal inside, and dismisses when click leaves it.
        page.locator('#pluginsButton').click()
        assert_true(page.locator('#pluginBrowser').is_visible(),
                    'Plugin manager button did not open')
        page.locator('#pluginSearch').fill('reverb')
        assert_true(page.locator('#pluginBrowser').is_visible(),
                    'Focus inside manager must not dismiss it')
        page.locator('.mixer-title .panel-name').click()
        assert_true(page.locator('#pluginBrowser').is_visible(),
                    'Plugin Manager must stay modeless while mixing')
        page.locator('#pluginClose').click()
        # Material icon uses SVG path, no text-glyph substitution.
        svg=page.locator('#tracks .strip[data-id="1"] .fx-section .mini.power .mi-svg').first
        assert_true(svg.count()==1 and svg.locator('path').count()==1,
                    'FX power must be offline Material SVG, not ⏻ font glyph')
        assert_true(page.locator('.mixer-title .panel-sub svg.mi-svg').count()==1,
                    'Mixer panel icon must be SVG not fallback text symbol')
        selected=page.locator('#tracks .strip.selected').first
        thin=selected.evaluate("el=>getComputedStyle(el,'::after').borderTopWidth")
        assert_true(thin=='1px','V25 selected channel border must be exactly 1px')
        body=page.locator('.console-body')
        assert_true(body.evaluate("el=>getComputedStyle(el).getPropertyValue('--v13-fader-h').includes('clamp')"),
                    'Adaptive fader sizing contract must exist')
        # V16 owner P0 intentionally folds WIRE to a 23px disclosure by default.
        # Expand the WIRE workspace explicitly before patching.
        page.locator('.section-rail [data-section="send"]').click()
        assert_true(not page.locator('.console-body').evaluate(
                    "(el)=>el.classList.contains('collapsed-send')"),"WIRE did not expand")
        src=page.locator('#tracks .strip[data-id="8"] .wire-node-out')
        dest=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
        assert_true(src.is_visible() and dest.is_visible(),
                    "Filled OUT and hollow IN jacks must be visible in compact patch")
        def center(locator):
            box=locator.bounding_box()
            assert_true(bool(box),"Missing patch jack geometry")
            return box["x"]+box["width"]/2,box["y"]+box["height"]/2
        x0,y0=center(src);x1,y1=center(dest)
        assert_true(x1<x0,"Fixture must prove RIGHT-to-LEFT sends, not reverse")
        page.mouse.move(x0,y0);page.mouse.down()
        page.mouse.move((x0+x1)/2,(y0+y1)/2,steps=9)
        assert_true(page.locator('#wireLayer .wire-drag-line').count()>=2,
                    "Drag wire is not visible while following live pointer")
        page.mouse.move(x1,y1,steps=11)
        page.mouse.up()
        assert_true(page.locator('#tracks .strip[data-id="3"] .wire-node-in.is-linked').count()==1,
                    "CH8 → CH3 leftward drop failed to create link")
        assert_true('⇢1' in page.locator('#tracks .strip[data-id="8"]').inner_text(),
                    "Source count did not update after leftward drag")
        assert_true(page.locator('#tracks .strip[data-id="3"] .wire-gain').count()==1,
                    "Receiver wire gain control missing")
        # V27 cable geometry uses one source ferrule; branch and stem are
        # different SVG layers (no overlapping color lines at the OUT jack).
        # V28 original flexible 1:1 cable: no artificial ferrule or
        # shared vertical stem. The long wire must arc well above the jack.
        assert_true(page.locator('#wireLayer .wire-origin-stem,#wireLayer .wire-origin-junction').count()==0,
                    'V27 rigid source ferrule must be undone')
        outgoing=page.locator('#wireLayer path.wire-outgoing-line')
        assert_true(outgoing.count()==1,'Active source wire is missing')
        import re
        nums=[float(a) for a in re.findall(r'-?\d+(?:\.\d+)?',outgoing.first.get_attribute('d'))]
        assert_true(len(nums)==8 and nums[3]<nums[1]-85,
                    'Long WIRE cable should rise into smooth high arch')
        page.screenshot(path=str(ROOT/"preview-v15-wire-left-1680.png"))
        # New source CH3 -> Bus A, explicitly choose serial VIA BUS ONLY.
        page.locator('#tracks .strip[data-id="3"] .wire-node-out').click()
        page.locator('#bankSelect').select_option('1')
        page.locator('#tracks .strip[data-id="11"] .wire-node-in').click()
        assert_true(page.locator('#routeChoice').is_visible(),
                    "Bus needs an explicit Route Only vs Parallel choice")
        assert_true(page.locator('#routeChoiceTitle').inner_text().startswith('ROUTE '),
                    "Pending Bus dialog must name actual source and target")
        # Before choosing, no graph mutation.
        page.locator('#routeOnly').click()
        assert_true(not page.locator('#routeChoice').is_visible(),
                    "Route choice did not close after commit")
        page.locator('#bankSelect').select_option('0')
        source_out=page.locator('#tracks .strip[data-id="3"] select[data-field="output"]')
        assert_true(source_out.input_value()=='Bus A',
                    "VIA BUS ONLY did not replace direct Main with serial Bus primary route")
        assert_true(page.locator('#wireMaster').get_attribute('aria-pressed')=='false',
                    "Master button wrongly claims direct Main still live")
        page.screenshot(path=str(ROOT/"preview-v15-wire-bus-only-1680.png"))
        assert_true(not errors,"JavaScript errors: "+str(errors))
        browser.close()
        print("PASS V15 Chromium: real OUT→IN leftward pointer cable, gain, Bus Only vs Main, clean screenshots")

if __name__=="__main__":main()
