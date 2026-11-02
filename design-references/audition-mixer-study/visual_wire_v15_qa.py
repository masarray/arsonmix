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
        # V17: socket is larger than the solid OUT pin. Knob exists before routing.
        pin=page.locator('#tracks .strip[data-id="8"] .wire-pin').bounding_box()
        assert_true(pin is not None and dest.bounding_box()["width"]>pin["width"]+10,
                    "IN socket must be visibly larger than solid OUT pin")
        idle=page.locator('#tracks .strip[data-id="3"] .wire-pot.is-idle')
        assert_true(idle.count()==1,"Dark unconnected gain knob must always occupy its position")
        initial_in=dest.bounding_box()
        initial_pot=idle.bounding_box()

        assert_true(x1<x0,"Fixture must prove RIGHT-to-LEFT sends, not reverse")
        page.mouse.move(x0,y0);page.mouse.down()
        page.mouse.move((x0+x1)/2,(y0+y1)/2,steps=9)
        assert_true(page.locator('#wireLayer .wire-drag-line').count()>=2,
                    "Drag wire is not visible while following live pointer")
        # Stop within the magnetic halo, NOT on the exact IN node center.
        page.mouse.move(x1+19,y1-15,steps=11)
        page.wait_for_function("""() => !!document.querySelector(
            '#tracks .strip[data-id="3"] .wire-node-in.is-snap-target')""")
        assert_true('stroke-dasharray' not in page.locator('#wireLayer').inner_html(),
                    "Drag wire must be smooth, solid, and semi-transparent")

        page.mouse.up()
        assert_true(page.locator('#tracks .strip[data-id="3"] .wire-node-in.is-linked').count()==1,
                    "CH8 → CH3 leftward drop failed to create link")
        assert_true('⇢1' in page.locator('#tracks .strip[data-id="8"]').inner_text(),
                    "Source count did not update after leftward drag")
        assert_true(page.locator('#tracks .strip[data-id="3"] .wire-gain').count()==1,
                    "Receiver wire gain control missing")
        linked_in=dest.bounding_box()
        active_pot=page.locator('#tracks .strip[data-id="3"] .wire-pot').bounding_box()
        assert_true(all(abs(initial_in[k]-linked_in[k])<1 for k in ("x","y","width","height")),
                    "IN jack moved when connection was committed")
        assert_true(all(abs(initial_pot[k]-active_pot[k])<1 for k in ("x","y","width","height")),
                    "Gain knob moved when connection was committed")
        assert_true(page.locator('#tracks .strip[data-id="4"] .wire-pot.is-idle').count()==1,
                    "Unconnected destination knob should remain dark and stationary")

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
        # Adaptive responsive audit: 1366 and 1920 should retain clean fixed
        # slots; the master is pinned and active WIRE is geometrically bounded.
        for w,h in ((1366,768),(1920,1080)):
            page.set_viewport_size({"width":w,"height":h})
            page.reload()
            page.locator('.section-rail [data-section="send"]').click()
            assert_true(page.locator('.console-body').evaluate(
                "(el)=>!el.classList.contains('collapsed-send')"),
                f"{w} WIRE compartment unexpectedly collapsed")
            source=page.locator('#tracks .strip[data-id="1"] .wire-node-out')
            receiver=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
            knob=page.locator('#tracks .strip[data-id="3"] .wire-pot')
            assert_true(source.is_visible() and receiver.is_visible() and knob.is_visible(),
                        f"{w}: jack or fixed knob hidden")
            strip=page.locator('#tracks .strip[data-id="3"]')
            sr=strip.bounding_box()
            inn=receiver.bounding_box()
            kr=knob.bounding_box()
            assert_true(sr is not None and inn is not None and kr is not None,
                        f"{w}: patch geometry missing")
            assert_true(inn["x"]>=sr["x"]-1 and inn["x"]+inn["width"]<=sr["x"]+sr["width"]+1,
                        f"{w}: IN spills outside channel")
            assert_true(kr["x"]>=sr["x"]-1 and kr["x"]+kr["width"]<=sr["x"]+sr["width"]+1,
                        f"{w}: gain spills outside channel")
            base=(inn["x"],inn["y"],kr["x"],kr["y"])
            x0,y0=center(source);x1,y1=center(receiver)
            page.mouse.move(x0,y0);page.mouse.down()
            page.mouse.move((x0+x1)/2,(y0+y1)/2,steps=6)
            page.mouse.move(x1+16,y1-12,steps=8);page.mouse.up()
            assert_true(receiver.get_attribute("aria-pressed")=="true",
                        f"{w}: off-center magnetic snap failed")
            linked=receiver.bounding_box()
            linked_knob=knob.bounding_box()
            assert_true(all(abs(a-b)<1 for a,b in zip(base,(linked["x"],linked["y"],linked_knob["x"],linked_knob["y"]))),
                        f"{w}: routing reflow displaced OUT/IN/knob")
            assert_true(page.locator('#wireLayer path').count()>=2,
                        f"{w}: committed signal line not visible")
            if w==1366: page.screenshot(path=str(ROOT/"preview-v17-wire-1366.png"))
            if w==1920: page.screenshot(path=str(ROOT/"preview-v17-wire-1920.png"))
        # Reset temporary tests without changing stored routing state.
        page.locator('#reset').click()
        # Drop over a fader intentionally fails closed and never creates a wire.
        source=page.locator('#tracks .strip[data-id="8"] .wire-node-out')
        fader=page.locator('#tracks .strip[data-id="3"] .fader-box')
        sx,sy=center(source);fx,fy=center(fader)
        page.mouse.move(sx,sy);page.mouse.down()
        page.mouse.move(fx,fy,steps=12);page.mouse.up()
        assert_true(page.locator('#tracks .strip[data-id="3"] .wire-node-in.is-linked').count()==0,
                    "Unsafe fader-area drop created a route")
        assert_true(not errors,"JavaScript errors: "+str(errors))
        browser.close()
        print("PASS V15 Chromium: real OUT→IN leftward pointer cable, gain, Bus Only vs Main, clean screenshots")

if __name__=="__main__":main()
