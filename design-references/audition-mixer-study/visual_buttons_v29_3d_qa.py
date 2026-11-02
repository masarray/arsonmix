#!/usr/bin/env python3
"""V30 corrected high-contrast satin raised buttons / real depressed chassis: live CSS + toggle QA."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE=Path(__file__).resolve()
ROOT=next(p for p in HERE.parents if (p/".github").is_dir())
PAGE=HERE.with_name("index.html")

def expect(ok,msg):
    if not ok: raise AssertionError(msg)

def css(element):
    return element.evaluate("""e=>{
        const c=getComputedStyle(e);
        return {bg:c.backgroundImage,shadow:c.boxShadow,top:c.borderTopColor,
                left:c.borderLeftColor,ink:c.color,transform:c.transform,
                height:e.getBoundingClientRect().height,
                width:e.getBoundingClientRect().width};
    }""")

def colors_match(style,first,second):
    return first in style["bg"] and second in style["bg"]

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            failures=[]
            page.on("pageerror",lambda exc:failures.append(str(exc)))
            page.goto(PAGE.as_uri())
            root=page.locator('.console-body')
            mute=page.locator('#tracks .strip[data-id="1"] .switch.mute')
            solo=page.locator('#tracks .strip[data-id="1"] .switch.solo')
            pfl=page.locator('#tracks .strip[data-id="1"] .switch.cue')
            switches=(mute,solo,pfl)
            before=[css(btn) for btn in switches]
            for name,button,state in zip(('MUTE','SOLO','PFL'),switches,before):
                expect(colors_match(state,'rgb(115, 134, 146)','rgb(53, 71, 80)'),
                       f'{width}: {name} raised face does not match raised satin graphite with enough chassis contrast: {state["bg"]}')
                expect(state['height']>=24 and state['height']<=34,
                       f'{width}: {name} key size was changed: {state["height"]}')
                expect(button.get_attribute('aria-pressed')=='false',
                       f'{width}: {name} should begin OFF')
                socket=button.locator('xpath=..')
                expect(socket.evaluate("e=>getComputedStyle(e,'::after').zIndex")=='4' and
                       socket.evaluate("e=>getComputedStyle(e).transform")=='none',
                       f'{width}: chassis lip must stay fixed over keycap')
            pan=page.locator('#tracks .strip[data-id="1"] .pan-section').bounding_box()
            fader=page.locator('#tracks .strip[data-id="1"] .fader-section').bounding_box()
            # Each key toggles independently, unlike the sample's jQuery radio buttons.
            mute.click();solo.click();pfl.click()
            for name,button,orig in zip(('MUTE','SOLO','PFL'),switches,before):
                current=css(button)
                expect(button.get_attribute('aria-pressed')=='true',
                       f'{width}: {name} key did not latch ON independently')
                expect(colors_match(current,'rgb(39, 53, 61)','rgb(21, 30, 37)'),
                       f'{width}: {name} ON is still raised instead of recessed: {current["bg"]}')
                expect('inset' in current['shadow'] and
                       current['transform'].endswith(', 1)'),
                       f'{width}: {name} must sink exactly 1px into its socket')
                expect(abs(current['height']-orig['height'])<=1 and
                       abs(current['width']-orig['width'])<=1,
                       f'{width}: clicking {name} reflowed keycap geometry')
                before_border=button.evaluate("e=>getComputedStyle(e,'::before').backgroundImage")
                after_border=button.evaluate("e=>getComputedStyle(e,'::after').backgroundImage")
                expect('linear-gradient' in before_border and 'linear-gradient' in after_border,
                       f'{width}: {name} selected indicator side edges missing')
            expect(pan['height']==page.locator('#tracks .strip[data-id="1"] .pan-section').bounding_box()['height'],
                   f'{width}: PAN rack resized when pressing button')
            expect(fader['height']==page.locator('#tracks .strip[data-id="1"] .fader-section').bounding_box()['height'],
                   f'{width}: FADER rack resized when pressing button')
            expect('collapsed-send' in root.get_attribute('class'),
                   f'{width}: buttons disturbed WIRE collapse')
            expect(not page.locator('#tracks .strip[data-id="1"] .wire-tray').is_visible(),
                   f'{width}: MUTE/SOLO material exposed collapsed jacks')
            # A second MUTE click releases only MUTE; SOLO/PFL remain ON.
            mute.click()
            expect(mute.get_attribute('aria-pressed')=='false' and
                   solo.get_attribute('aria-pressed')=='true' and
                   pfl.get_attribute('aria-pressed')=='true',
                   f'{width}: independent latched controls were replaced by sample radio toggle')
            expect(colors_match(css(mute),'rgb(130, 150, 160)','rgb(64, 83, 93)'),
                   f'{width}: MUTE hover should match V30 graphite hover face')
            page.mouse.move(2,2)
            expect(colors_match(css(mute),'rgb(115, 134, 146)','rgb(53, 71, 80)'),
                   f'{width}: MUTE OFF did not return to raised hardware face after hover exits')
            # Toolbar push keys reuse the same material but don't interfere with mixer.
            tool=page.locator('.command-bar button.tool').first
            expect(colors_match(css(tool),'rgb(115, 134, 146)','rgb(53, 71, 80)'),
                   f'{width}: toolbar push key not on common satin graphite material')
            # Unaffected input hardware and out/in wire nodes are still not keycaps.
            expect('linear-gradient(rgb(115, 134, 146) 0%, rgb(53, 71, 80) 100%)' not in
                   page.locator('#tracks .strip[data-id="1"] .center-pan').evaluate(
                       'e=>getComputedStyle(e).backgroundImage'),
                   f'{width}: 3D button style leaked into pan slider')
            expect(not failures,f'{width}: uncaught browser errors: {failures}')
            page.screenshot(path=str(ROOT/f"preview-v29-buttons-3d-{width}.png"))
            page.close()
        browser.close()
        print("PASS V30 Chromium 1366/1680/1920: graphite satin raised OFF, recessed ON, 3 independent keys, bezel and geometry stable")

if __name__=="__main__":main()
