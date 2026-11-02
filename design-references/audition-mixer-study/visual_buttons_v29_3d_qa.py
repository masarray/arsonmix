#!/usr/bin/env python3
"""Chromium QA: CodePen jbqBWm MUTE/SOLO visual parity, independent latches, no reflow."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE=Path(__file__).resolve()
ROOT=next(p for p in HERE.parents if (p/".github").is_dir())
PAGE=HERE.with_name("index.html")

def expect(ok,msg):
    if not ok:
        raise AssertionError(msg)

def style(element):
    return element.evaluate("""e=>{
        const c=getComputedStyle(e),r=e.getBoundingClientRect();
        return {bg:c.backgroundImage,shadow:c.boxShadow,transform:c.transform,
                width:r.width,height:r.height};
    }""")

def has_gradient(state,a,b):
    return a in state["bg"] and b in state["bg"]

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            failures=[]
            page.on("pageerror",lambda exc:failures.append(str(exc)))
            page.goto(PAGE.as_uri())
            strip='#tracks .strip[data-id="1"]'
            mute=page.locator(strip+' .codepen-buttons-wrapper > .button.mute')
            solo=page.locator(strip+' .codepen-buttons-wrapper > .button.solo')
            pfl=page.locator(strip+' .hw-switch-socket > .switch.cue')
            group=page.locator(strip+' .codepen-buttons-wrapper')
            expect(group.count()==1 and mute.count()==1 and solo.count()==1,
                   f'{width}: two-button CodePen group not mounted')
            expect(group.evaluate("e=>getComputedStyle(e).backgroundColor")=='rgb(0, 0, 0)',
                   f'{width}: original black CodePen wrapper lost')
            before=[style(btn) for btn in (mute,solo,pfl)]
            for name,button,state in zip(('MUTE','SOLO'),(mute,solo),before[:2]):
                expect(has_gradient(state,'rgb(51, 51, 51)','rgb(34, 34, 34)'),
                       f'{width}: {name} OFF does not match original #333/#222: {state["bg"]}')
                expect('inset 0px 1px 0px' in state['shadow'],
                       f'{width}: {name} original CodePen OFF inset lost')
                expect(button.get_attribute('aria-pressed')=='false',f'{width}: {name} must start OFF')
                expect(24<=state['height']<=34,f'{width}: {name} height outside compact mixer slot')
            expect(has_gradient(before[2],'rgb(115, 134, 146)','rgb(53, 71, 80)'),
                   f'{width}: PFL unrelated graphite material was changed')
            pan=page.locator(strip+' .pan-section').bounding_box()
            fader=page.locator(strip+' .fader-section').bounding_box()
            page.mouse.move(2,2)
            mute.hover()
            expect(has_gradient(style(mute),'rgb(55, 55, 55)','rgb(38, 38, 38)'),
                   f'{width}: exact CodePen hover #373737/#262626 missing')
            page.mouse.move(2,2)
            mute.click()
            solo.click()
            pfl.click()
            for name,button,old in zip(('MUTE','SOLO'),(mute,solo),before):
                state=style(button)
                expect(button.get_attribute('aria-pressed')=='true',
                       f'{width}: {name} did not latch independently')
                expect(has_gradient(state,'rgb(32, 32, 32)','rgb(21, 21, 21)'),
                       f'{width}: original selected #202020/#151515 missing: {state["bg"]}')
                expect('inset' in state['shadow'] and '10px 20px' in state['shadow'],
                       f'{width}: selected inset+outer CodePen depth lost')
                label=button.locator('.fa')
                expect(label.evaluate("e=>getComputedStyle(e).color")=='rgb(255, 255, 255)',
                       f'{width}: {name} selected white label missing')
                expect('2, 165, 238' in label.evaluate("e=>getComputedStyle(e).textShadow"),
                       f'{width}: {name} CodePen blue caption glow missing')
                expect(abs(state['width']-old['width'])<=1 and abs(state['height']-old['height'])<=1,
                       f'{width}: clicking {name} reflowed its key')
            # CodePen hides the outer LED seams on the first/last selected key.
            mute_right=mute.evaluate("e=>getComputedStyle(e,'::after').backgroundImage")
            solo_left=solo.evaluate("e=>getComputedStyle(e,'::before').backgroundImage")
            expect('linear-gradient' in mute_right and 'linear-gradient' in solo_left,
                   f'{width}: original inner blue separator gradients missing')
            expect(mute.evaluate("e=>getComputedStyle(e,'::before').display")=='none' and
                   solo.evaluate("e=>getComputedStyle(e,'::after').display")=='none',
                   f'{width}: original first/last edge rules lost')
            expect(pfl.get_attribute('aria-pressed')=='true',f'{width}: PFL did not toggle')
            expect(pan['height']==page.locator(strip+' .pan-section').bounding_box()['height'],
                   f'{width}: PAN rack reflowed')
            expect(fader['height']==page.locator(strip+' .fader-section').bounding_box()['height'],
                   f'{width}: fader rack reflowed')
            expect('collapsed-send' in page.locator('.console-body').get_attribute('class'),
                   f'{width}: WIRE collapsed state changed')
            # Independent toggles; sample CodePen jQuery exclusivity must not be copied.
            mute.click()
            expect(mute.get_attribute('aria-pressed')=='false' and
                   solo.get_attribute('aria-pressed')=='true' and
                   pfl.get_attribute('aria-pressed')=='true',
                   f'{width}: MUTE OFF unintentionally changed SOLO/PFL')
            page.mouse.move(2,2)
            expect(has_gradient(style(mute),'rgb(51, 51, 51)','rgb(34, 34, 34)'),
                   f'{width}: MUTE OFF failed to restore original raised gradient')
            expect(not failures,f'{width}: browser runtime errors: {failures}')
            page.screenshot(path=str(ROOT/f"preview-v29-buttons-3d-{width}.png"))
            page.close()
        browser.close()
        print('PASS CodePen 3D MUTE/SOLO 1366/1680/1920: source CSS, hover, selected, seam, independent latch, PFL unaffected')

if __name__=="__main__":
    main()
