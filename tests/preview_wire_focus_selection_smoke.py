#!/usr/bin/env python3
"""V38 WIRE focus ownership: source proximity, transient patch mode and whole-strip selection.
Browser GUI reference only: does not certify native PCM or physical routing.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT=Path(__file__).resolve().parents[1]
PAGE=ROOT/"site/preview-v2/index.html"

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        page=browser.new_page(viewport={"width":1680,"height":900})
        errors=[]
        page.on("pageerror",lambda error:errors.append(str(error)))
        page.goto(PAGE.as_uri())
        out=page.locator('#tracks .strip[data-id="3"] .wire-node-out')
        out.scroll_into_view_if_needed()
        bb=out.bounding_box()
        assert bb is not None
        # Approach from the socket's left: proximity precedes button hover.
        page.mouse.move(bb["x"]-9,bb["y"]+bb["height"]/2)
        assert out.evaluate("el=>el.classList.contains('is-near')"),"OUT does not respond before hit"
        glow=out.evaluate("el=>getComputedStyle(el).borderTopColor")
        assert glow=="rgb(120, 216, 174)",glow
        page.mouse.move(bb["x"]-80,bb["y"]+bb["height"]/2)
        assert not out.evaluate("el=>el.classList.contains('is-near')"),"Proximity must clear"

        out.click()
        assert out.get_attribute("aria-pressed")=="true","OUT must be armed"
        ready=page.locator('#tracks .strip[data-id="8"] .wire-node-in')
        assert ready.is_enabled(),"IN must be ready after OUT selection"
        assert ready.evaluate("el=>getComputedStyle(el).borderTopColor")=="rgb(110, 189, 157)"
        assert page.locator('#tracks .strip[data-id="3"]').get_attribute("class").find("selected")>=0

        # Blank WIRE tray is a real channel surface. It selects the destination
        # and releases transient OUT focus without creating a route.
        tray=page.locator('#tracks .strip[data-id="8"] .wire-tray')
        box=tray.bounding_box()
        page.mouse.click(box["x"]+2,box["y"]+2)
        assert "selected" in (page.locator('#tracks .strip[data-id="8"]').get_attribute("class") or "")
        assert page.locator('#tracks .strip[data-id="3"] .wire-node-out').get_attribute("aria-pressed")=="false"
        assert not page.locator('#tracks .strip[data-id="8"] .wire-node-in').is_enabled()
        assert page.locator('#tracks .strip.selected').count()==1

        # IN remains a legitimate route target: clicking it selects that
        # channel and keeps the patch gesture valid, not an empty-tray click.
        page.locator('#tracks .strip[data-id="3"] .wire-node-out').click()
        page.locator('#tracks .strip[data-id="8"] .wire-node-in').click()
        assert "selected" in (page.locator('#tracks .strip[data-id="8"]').get_attribute("class") or "")
        page.locator('.mixer-title .panel-name').click()
        assert page.locator('#tracks .strip[data-id="3"] .wire-node-out').get_attribute("aria-pressed")=="false"

        # Focus loss clears the transient tool without unselecting the channel.
        page.locator('#tracks .strip[data-id="3"] .wire-node-out').click()
        page.evaluate("window.dispatchEvent(new Event('blur'))")
        assert page.locator('#tracks .strip[data-id="3"] .wire-node-out').get_attribute("aria-pressed")=="false"
        rim=page.locator('#tracks .strip[data-id="8"]').evaluate(
            "el=>({opacity:getComputedStyle(el,'::after').opacity,events:getComputedStyle(el,'::after').pointerEvents})")
        assert float(rim["opacity"])<=0.72 and rim["events"]=="none",rim
        assert not errors,errors
        browser.close()
    print("PASS V38: proximity OUT, transient IN readiness, full tray selection, click-away/blur, lightweight selected rim")

if __name__=="__main__": main()
