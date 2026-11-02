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
