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
        # V6: three identical buttons, no legacy CodePen or PFL-only housing.
        keys=page.locator('#tracks .strip[data-id="1"] .mx-hw-row > button.mx-hw-key')
        assert_true(keys.count()==3,'V6 must render exactly 3 sibling hardware keys')
        rows=keys.evaluate_all("""items=>items.map(e=>({
          kind:e.dataset.hwKind,h:e.getBoundingClientRect().height,
          top:e.getBoundingClientRect().top,
          width:e.getBoundingClientRect().width
        }))""")
        assert_true([x['kind'] for x in rows]==['mute','solo','cue'],rows)
        assert_true(all(abs(x['h']-29)<.5 and x['width']>24 for x in rows),rows)
        assert_true(max(x['top'] for x in rows)-min(x['top'] for x in rows)<.5,rows)
        well=page.locator('#tracks .strip[data-id="1"] .mx-hw-row')
        key=keys.nth(0)
        well_y=well.bounding_box()['y']
        key.scroll_into_view_if_needed()
        rr=key.bounding_box()
        page.mouse.move(rr['x']+rr['width']/2,rr['y']+rr['height']/2)
        page.mouse.down()
        assert_true(abs(well.bounding_box()['y']-well_y)<.25,
                    'Static row must not move while button depresses')
        page.mouse.up()
        key.click() # reset MUTE after test press; WIRE fixture starts from OFF
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
        assert_true(capStyle['w']=='27px' and capStyle['h']=='54px' and
                    capStyle['filter']=='none','M2 cap must remain crisp and correctly scaled')
        track=page.locator('#tracks .strip[data-id="1"] .fader-travel')
        travel=track.evaluate("""el=>({
            top:getComputedStyle(el).top,bottom:getComputedStyle(el).bottom
        })""")
        assert_true(travel['top']=='27px' and travel['bottom']=='27px',
                    'Fader slot not expanded consistently')
        assert_true(not errors,'V22 browser errors: '+str(errors))
        # CSS MUTE follows the owner reference. SOLO/PFL keep SVG cutout keys.
        key=page.locator('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]')
        solo=page.locator('#tracks .strip[data-id="1"] button[data-hw-kind="solo"]')
        cue=page.locator('#tracks .strip[data-id="1"] button[data-hw-kind="cue"]')
        mute_off=key.evaluate("""el=>({
          led:getComputedStyle(el).getPropertyValue('--mx-hw-led').trim(),
          text:el.querySelector('.mx-mute-legend')?.textContent,
          svg:el.querySelectorAll('svg,img').length,
          silver:getComputedStyle(el.querySelector('.mx-mute-face'),'::before').backgroundImage,
          stripe:getComputedStyle(el.querySelector('.mx-mute-lamp')).display
        })""")
        assert_true(mute_off['led']=='255,78,90' and mute_off['text']=='MUTE'
          and mute_off['svg']==0 and 'linear-gradient' in mute_off['silver']
          and mute_off['stripe']!='none','CSS MUTE initial material '+str(mute_off))
        for button,legend,color in ((solo,'SOLO','255,207,68'),(cue,'PFL','101,210,236')):
            data=button.evaluate("""el=>({
              led:getComputedStyle(el).getPropertyValue('--mx-hw-led').trim(),
              text:el.querySelector('svg.mx-hw-face mask text')?.textContent,
              maskTexts:el.querySelectorAll('svg.mx-hw-face mask text').length,
              overlayTexts:el.querySelectorAll('svg.mx-hw-face > text').length,
              lamp:getComputedStyle(el.querySelector('.mx-hw-lamp')).opacity
            })""")
            assert_true(data=={'led':color,'text':legend,'maskTexts':1,
                               'overlayTexts':0,'lamp':'0'},data)
        meter=page.locator('#tracks .strip[data-id="1"] .meter-lane').first
        led=meter.evaluate("""el=>({
          width:getComputedStyle(el).width,
          litWidth:getComputedStyle(el.querySelector('.meter-fill')).width,
          cells:getComputedStyle(el,'::before').backgroundImage,
          peak:!!el.querySelector('.meter-peak-hold')
        })""")
        assert_true(led['width']=='10px' and led['litWidth']=='6px'
                    and '12px' in led['cells'] and '14px' in led['cells'] and led['peak'],
                    'V51 LED must be 6x12px portrait optics with 2px gap and independent peak hold')
        # Independent state: reference MUTE cap + original SOLO/PFL lamps.
        key.click();solo.click();cue.click()
        mute_on=key.evaluate("""el=>({
          pressed:el.getAttribute('aria-pressed'),
          led:getComputedStyle(el).getPropertyValue('--mx-hw-led').trim(),
          caption:getComputedStyle(el.querySelector('.mx-mute-legend')).color,
          stripe:getComputedStyle(el.querySelector('.mx-mute-lamp')).display,
          whiteEdge:getComputedStyle(el.querySelector('.mx-mute-face'),'::after').opacity
        })""")
        assert_true(mute_on=={'pressed':'true','led':'255,78,90',
                             'caption':'rgb(242, 231, 231)',
                             'stripe':'none','whiteEdge':'1'},
                    'MUTE code-rendered ON material failed: '+str(mute_on))
        for button,color in ((solo,'255,207,68'),(cue,'101,210,236')):
            state=button.evaluate("""el=>({
              pressed:el.getAttribute('aria-pressed'),
              led:getComputedStyle(el).getPropertyValue('--mx-hw-led').trim(),
              glow:getComputedStyle(el,'::before').opacity,
              lamp:getComputedStyle(el.querySelector('.mx-hw-lamp')).opacity,
              maskTexts:el.querySelectorAll('svg.mx-hw-face mask text').length
            })""")
            assert_true(state=={'pressed':'true','led':color,'glow':'1',
                               'lamp':'1','maskTexts':1},
                        'SOLO/PFL V6 material regressed '+str(state))
        key.click()
        assert_true(key.get_attribute('aria-pressed')=='false' and
                    solo.get_attribute('aria-pressed')=='true' and
                    cue.get_attribute('aria-pressed')=='true',
                    'Releasing MUTE may not reset SOLO/PFL')
        solo.click();cue.click()
        key.scroll_into_view_if_needed()
        rect=key.bounding_box()
        assert_true(bool(rect),'Button missing')
        rest=key.evaluate("""el=>new DOMMatrix(
          getComputedStyle(el.querySelector('.mx-mute-face')).transform).m42""")
        page.mouse.move(rect['x']+rect['width']/2,rect['y']+rect['height']/2)
        page.mouse.down()
        pressed=key.evaluate("""el=>({
          housing:getComputedStyle(el).transform,
          capY:new DOMMatrix(getComputedStyle(el.querySelector('.mx-mute-face')).transform).m42
        })""")
        assert_true(pressed['housing']=='none' and abs((pressed['capY']-rest)-1)<.6,
                    'MUTE cap must depress 1px in fixed housing: '+str(pressed))
        page.mouse.up()
        key.click()
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
        # 17px utility power retains the compact Material icon, not a cramped keycap.
        svg=page.locator('#tracks .strip[data-id="1"] .fx-section .mx-util-key[data-util-kind="fx-main"] .mi-svg').first
        assert_true(svg.count()==1 and svg.locator('path').count()==1,
                    'Compact FX power must keep readable SVG icon under the V7 utility owner')
        assert_true(page.locator('#tracks .strip[data-id="1"] .fx-section .mini.power').count()==0,
                    'Old FX power style ownership must be retired')
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
        # V36/P0: CSS-mask knob must actually PAINT, not just exist in DOM.
        # In headless Chromium a missing external SVG can leave a fully
        # transparent pseudo-element while the gain input still exists.
        idle=page.locator('#tracks .strip[data-id="1"] .wire-pot.is-idle')
        assert_true(idle.count()==1 and idle.is_visible(),"Idle knob missing from WIRE tray")
        idle_style=idle.evaluate("""el=>({
            mask:getComputedStyle(el,'::before').maskImage,
            mode:getComputedStyle(el,'::before').maskMode
        })""")
        assert_true('data:image/svg+xml;base64,' in idle_style['mask'] and
                    idle_style['mode']=='alpha',
                    'WIRE SVG knob must use an offline-safe, alpha-based inline mask')
        paint_idle=idle.screenshot()
        hide=page.add_style_tag(content="""
          .console-body.wire-routing-v15 .strip .wire-tray .wire-pot.is-idle::before,
          .console-body.wire-routing-v15 .strip .wire-tray .wire-pot.is-active::before{
            display:none!important
          }
        """)
        without_idle=idle.screenshot()
        hide.evaluate("(el)=>el.remove()")
        assert_true(paint_idle!=without_idle,
                    'Idle knob is transparent: CSS pseudo exists but SVG silhouette does not paint')
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
        # CH08 retains its canonical default MAIN edge, but the custom OUT
        # badge counts only the single CH08→CH03 SEND. No false MAIN activity.
        assert_true('⇢1' in page.locator('#tracks .strip[data-id="8"]').inner_text(),
                    "Custom OUT count must exclude the default MAIN route")
        assert_true(page.locator('#tracks .strip[data-id="3"] .wire-gain').count()==1,
                    "Receiver wire gain control missing")
        active=page.locator('#tracks .strip[data-id="3"] .wire-pot.is-active')
        assert_true(active.count()==1 and active.is_visible(),
                    'Connected wire gain knob is not a visible interactive control')
        # Let the cable finish its lightweight single-edge settle before
        # comparing two screenshots of the hardware knob alone.
        page.wait_for_timeout(350)
        paint_active=active.screenshot()
        hide=page.add_style_tag(content="""
          .console-body.wire-routing-v15 .strip .wire-tray .wire-pot.is-active::before{
            display:none!important
          }
        """)
        without_active=active.screenshot()
        hide.evaluate("(el)=>el.remove()")
        assert_true(paint_active!=without_active,
                    'Connected gain knob did not render any visible SVG silhouette')
        # V31: the OUT/IN jacks are on TOP of the patch bay; each independent
        # gravity cable must droop DOWN below BOTH endpoints, not arch upward.
        # No V27 common stem/ferrule; do not change any live routing semantics.
        assert_true(page.locator('#wireLayer .wire-origin-stem,#wireLayer .wire-origin-junction').count()==0,
                    'V27 rigid source ferrule must be undone')
        # Pointerdown on the receiving jack selects CH03; its newly accepted
        # leftward signal cable is therefore shown as INCOMING for CH03.
        incoming=page.locator('#wireLayer path.wire-incoming-line[data-from="8"][data-to="3"]')
        assert_true(incoming.count()==1,'Selected receiver incoming wire is missing')
        # V43: plug-in launches a finite 36-frame socket-originated wavy impact.
        # The old V31 gravity-curve contract concerns the SETTLED cable. Wait
        # until the impact returns to its canonical cubic (no sleeps, no
        # disabled animation, no weakening of either regression assertion).
        page.wait_for_function("""()=>{
          const path=document.querySelector('#wireLayer path.wire-incoming-line[data-from="8"][data-to="3"]');
          return !!path && (path.getAttribute('d')||'').includes(' C ');
        }""",timeout=4000)
        import re
        nums=[float(a) for a in re.findall(r'-?\d+(?:\.\d+)?',incoming.first.get_attribute('d'))]
        assert_true(len(nums)==8 and nums[3]>nums[1]+20 and nums[5]>nums[7]+20,
                    'V31 WIRE must hang below both top-mounted OUT/IN anchors')
        assert_true(abs(nums[2]-nums[0])<.1 and abs(nums[4]-nums[6])<.1,
                    'V31 suspended cable must leave OUT downward and arrive at IN upward')
        wire_panel=page.locator('#tracks .strip[data-id="8"] .wire-section').bounding_box()
        wire_tray=page.locator('#tracks .strip[data-id="8"] .wire-tray').bounding_box()
        wire_deck=page.locator('#tracks .strip[data-id="8"] .wire-deck').bounding_box()
        assert_true(wire_panel and wire_tray and wire_deck and wire_tray["y"]<wire_deck["y"],
                    'V31 physical OUT/IN connectors must be above suspended wire canvas')
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
