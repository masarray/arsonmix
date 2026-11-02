#!/usr/bin/env python3
"""V34 operator-intent acceptance: no accidental routing, precise SEND and source pin.

Browser-only reference. This must never claim native ASIO/PCM routing validation.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE = Path(__file__).with_name("index.html")

def require(condition, message):
    if not condition:
        raise AssertionError(message)

def main():
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        for width, height in ((1366,768),(1680,920),(1910,912),(1920,1080)):
            page = browser.new_page(viewport={"width":width,"height":height})
            errors = []
            page.on("pageerror", lambda err: errors.append(str(err)))
            page.goto(PAGE.as_uri())
            page.locator('.section-rail [data-section="send"]').click()

            def channel(i):
                return page.locator(f'#tracks .strip[data-id="{i}"]')
            def routes():
                return page.locator('#wireLayer path.wire-outgoing-line')
            def gain():
                return channel(3).locator('.wire-gain')
            def knob():
                return channel(3).locator('.wire-pot:not(.is-idle)')

            # Mixer inspection is not patch mode.
            require(routes().count()==0, f"{width}: cables shown while idle")
            require(channel(3).locator('.wire-node-in').is_disabled(),
                    f"{width}: IN accepts a source that was never armed")
            require(page.locator('#wireLegend [data-wire-destination]').count()==0,
                    f"{width}: idle cross-bank patch picker unexpectedly exposed")
            channel(2).locator('.strip-identity-top').click()
            require(routes().count()==0 and channel(3).locator('.wire-node-in').is_disabled(),
                    f"{width}: ordinary channel selection armed patching")

            # One explicit OUT action is enough to make routing direct and obvious.
            channel(1).locator('.wire-node-out').click()
            require(not channel(3).locator('.wire-node-in').is_disabled(),
                    f"{width}: explicit OUT did not arm destination")
            channel(2).locator('.strip-identity-top').click()
            require('CH01' in page.locator('#wireLegend').inner_text(),
                    f"{width}: inspecting another channel silently retargeted source")
            channel(3).locator('.wire-node-in').click()
            require(channel(3).locator('.wire-node-in').get_attribute('aria-pressed')=='true',
                    f"{width}: accepted CH01 -> CH03 route missing")
            require(routes().count()==1, f"{width}: wrong number of focused cables")

            # Same 22x24px tactile knob, but long pointer-relative travel.
            knob().dblclick()  # physical unity reset
            require(float(gain().input_value())==0.0, f"{width}: knob unity reset failed")
            def drag(distance, fine=False):
                box = knob().bounding_box()
                x = box["x"] + box["width"]/2
                y = box["y"] + box["height"]/2
                page.mouse.move(x,y)
                if fine: page.keyboard.down("Shift")
                page.mouse.down()
                page.mouse.move(x,y+distance,steps=3)
                page.mouse.up()
                if fine: page.keyboard.up("Shift")
                return float(gain().input_value())
            normal=drag(30)
            require(-10.2<=normal<=-9.8,f"{width}: SEND gesture too short/wrong: {normal}")
            knob().dblclick()
            fine=drag(15,True)
            require(-1.2<=fine<=-1.0 and round(fine,1)==fine,
                    f"{width}: Shift does not provide 0.1 dB gain: {fine}")

            # Pointer cancellation is transactional: previous gain restored.
            before=float(gain().input_value())
            page.evaluate("""() => {
                const el=document.querySelector('#tracks .strip[data-id="3"] .wire-pot');
                el.addEventListener('pointerdown', e => window.__v34PointerId=e.pointerId, {once:true});
            }""")
            box=knob().bounding_box()
            x=box["x"]+box["width"]/2
            y=box["y"]+box["height"]/2
            page.mouse.move(x,y)
            page.mouse.down()
            page.mouse.move(x,y+20,steps=3)
            require(float(gain().input_value())!=before,
                    f"{width}: gain did not respond before cancel")
            page.evaluate("""() => {
                const el=document.querySelector('#tracks .strip[data-id="3"] .wire-pot');
                el.dispatchEvent(new PointerEvent('pointercancel',
                    {bubbles:true,pointerId:window.__v34PointerId}));
            }""")
            page.mouse.up()
            require(float(gain().input_value())==before,
                    f"{width}: canceled SEND gesture did not restore previous gain")

            # A deliberately pinned OUT survives bank navigation, not vice versa.
            page.locator('#bankNext').click()
            require(channel(11).count()==1 and 'FROM CH01' in page.locator('#wireLegend').inner_text(),
                    f"{width}: remote-bank source identity lost")
            channel(11).locator('.strip-identity-top').click()
            require('FROM CH01' in page.locator('#wireLegend').inner_text(),
                    f"{width}: selecting remote channel hijacked route source")
            page.keyboard.press('Escape')
            require(routes().count()==0 and channel(11).locator('.wire-node-in').is_disabled(),
                    f"{width}: Escape did not return to nonrouting mixer mode")
            require(page.locator('#wireLegend [data-wire-destination]').count()==0,
                    f"{width}: hidden armed source picker remains after Escape")
            page.locator('#wireView').select_option('all')
            require(routes().count()==0 and channel(11).locator('.wire-node-in').is_disabled(),
                    f"{width}: View changed patch state without explicit OUT")
            require(not errors, f"{width}: JS errors: {errors}")
            page.close()
        browser.close()
    print("PASS V34: idle safety, deliberate source, multi-bank pin, 0.1dB SEND gesture/cancel across 4 viewports")

if __name__ == "__main__":
    main()
