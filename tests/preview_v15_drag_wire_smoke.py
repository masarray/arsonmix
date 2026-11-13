#!/usr/bin/env python3
"""Current physical WIRE OUT->IN drag and BUS-only routing in real Chromium.

This fixture owns WIRE interaction and routing only. Modern hardware-key,
PFL, fader, plugin-picker and floating-window contracts are independently
qualified by their current dedicated Chromium tests. Do not repeat the
obsolete V15 hardware DOM internals in this WIRE-only regression.
Browser reference only; not evidence of native PCM processing.
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
        # CH08 starts with a real CH→MAIN edge; the new CH08→CH03 SEND
        # adds a SECOND accepted outgoing edge, not a ghost or replacement.
        assert_true('⇢1' in page.locator('#tracks .strip[data-id="8"]').inner_text(),
                    "Custom OUT count must exclude default MAIN")
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
        outgoing=page.locator('#wireLayer path.wire-outgoing-line')
        assert_true(outgoing.count()==1,'Active source wire is missing')
        # V43: plug-in launches a finite 36-frame socket-originated wavy impact.
        # The old V31 gravity-curve contract concerns the SETTLED cable. Wait
        # until the impact returns to its canonical cubic (no sleeps, no
        # disabled animation, no weakening of either regression assertion).
        page.wait_for_function("""()=>{
          const path=document.querySelector('#wireLayer path.wire-outgoing-line');
          return !!path && (path.getAttribute('d')||'').includes(' C ');
        }""",timeout=4000)
        import re
        nums=[float(a) for a in re.findall(r'-?\d+(?:\.\d+)?',outgoing.first.get_attribute('d'))]
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
        print("PASS V15 WIRE: OUT->IN leftward pointer cable, gain and BUS ONLY selection")

if __name__=="__main__":main()
