#!/usr/bin/env python3
"""V19 collapsed rack indicators and restrained 28px DAW section rail."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE=Path(__file__).resolve()
ROOT=next(p for p in HERE.parents if (p/".github").is_dir())
PAGE=HERE.with_name("index.html")

def ensure(value,message):
    if not value: raise AssertionError(message)

def center(locator):
    box=locator.bounding_box()
    ensure(box is not None,"Missing channel node")
    return box["x"]+box["width"]/2,box["y"]+box["height"]/2

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda err:errors.append(str(err)))
            page.goto(PAGE.as_uri())
            body=page.locator('.console-body')
            ensure("collapsed-send" in body.get_attribute("class"),f"{width}: WIRE not collapsed by default")
            rail=page.locator('.section-rail')
            sections=rail.locator('button.rail-cell[data-section]')
            ensure(sections.all_inner_texts()==["I/O","FX","W"],f"{width}: left rail has awkward old labels")
            r=rail.bounding_box()
            ensure(r is not None and r["width"]<=34,f"{width}: left rail became too wide")
            wire=rail.locator('button[data-section="send"]')
            chevron=wire.evaluate("(el)=>getComputedStyle(el,'::after').content")
            ensure(chevron in ('""',"''","none"),f"{width}: text OPEN/PATCH still occupies rail")
            fold1=page.locator('#tracks .strip[data-id="1"] .wire-fold-summary')
            fold3=page.locator('#tracks .strip[data-id="3"] .wire-fold-summary')
            ensure(fold1.is_visible() and fold3.is_visible(),f"{width}: folded state blank")
            ensure("→1" in fold1.inner_text(),f"{width}: legacy SEND no longer represented")
            ensure("—" in fold3.inner_text(),f"{width}: empty custom link marker missing")
            foldHeight=fold3.bounding_box()["height"]
            ensure(18<=foldHeight<=23,f"{width}: folded summary changes row height")
            sectionHeight=page.locator('#tracks .strip[data-id="3"] .send-section').bounding_box()["height"]
            ensure(abs(sectionHeight-23)<=1,f"{width}: collapsed WIRE no longer 23px")

            # Entire compact indicator is the disclosure target, not a dead zone.
            fold3.click()
            ensure("collapsed-send" not in body.get_attribute("class"),
                   f"{width}: summary click did not expand patch rack")
            ensure(not fold3.is_visible(),f"{width}: folded marker overlaps expanded jacks")
            # Create an actual custom edge and close the rack via its left rail.
            out=page.locator('#tracks .strip[data-id="1"] .wire-node-out')
            receiving=page.locator('#tracks .strip[data-id="3"] .wire-node-in')
            x0,y0=center(out);x1,y1=center(receiving)
            page.mouse.move(x0,y0);page.mouse.down()
            page.mouse.move(x1,y1,steps=12);page.mouse.up()
            ensure(receiving.get_attribute("aria-pressed")=="true",
                   f"{width}: ordinary OUT→IN routing regressed")
            pan=page.locator('#tracks .strip[data-id="3"] .pan-section').bounding_box()
            fader=page.locator('#tracks .strip[data-id="3"] .fader-section').bounding_box()
            wire.click()
            ensure(fold1.is_visible() and fold3.is_visible(),f"{width}: WIRE not collapsed")
            ensure("→2" in fold1.inner_text() and "←1" in fold3.inner_text(),
                   f"{width}: live IN/OUT summary failed to reflect graph edit")
            ensure(not page.locator('#wireLayer').is_visible(),
                   f"{width}: cable layer should hide in collapsed WIRE")
            ensure(abs(page.locator('#tracks .strip[data-id="3"] .pan-section').bounding_box()["height"]-pan["height"])<1,
                   f"{width}: PAN height shifted")
            ensure(abs(page.locator('#tracks .strip[data-id="3"] .fader-section').bounding_box()["height"]-fader["height"])<1,
                   f"{width}: physical fader height changed")
            page.screenshot(path=str(ROOT/f"preview-v19-collapsed-{width}.png"))
            ensure(not errors,f"{width}: JS exceptions {errors}")
            page.close()
        browser.close()
        print("PASS V19 Chromium: 28px icon rail, compact linked status, expand-on-click and fixed PAN/fader sizing at 1366/1680/1920")

if __name__=="__main__":main()
