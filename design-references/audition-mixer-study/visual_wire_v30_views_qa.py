#!/usr/bin/env python3
"""V30: Focus / OUT / ALL are visual-only modes, preserving real pointer routes."""
from pathlib import Path
from playwright.sync_api import sync_playwright

HERE = Path(__file__).resolve()
PAGE = HERE.with_name("index.html")

def verify(ok, message):
    if not ok:
        raise AssertionError(message)

def main():
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        for width,height in [(1366,768),(1680,900),(1920,1080)]:
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.goto(PAGE.as_uri())
            rail=page.locator('.section-rail [data-section="send"]')
            rail.click()
            def strip(ch):
                return page.locator(f'#tracks .strip[data-id="{ch}"]')
            def select(ch):
                strip(ch).locator('.strip-identity-top').click()
            def route(src,dst):
                select(src)
                socket=strip(dst).locator('.wire-node-in')
                socket.click()
                verify(socket.get_attribute('aria-pressed')=='true',
                       f"{width}: CH{src:02d}->CH{dst:02d} fixture was rejected")
            route(1,3)
            route(2,3)
            route(3,4)
            route(6,7)
            select(3)
            layer=page.locator('#wireLayer')
            selector=page.locator('#wireView')
            verify(selector.input_value()=='focus',f"{width}: Focus not default")
            def counts():
                return (layer.locator('.wire-context-line').count(),
                        layer.locator('.wire-incoming-line').count(),
                        layer.locator('.wire-outgoing-line').count())
            verify(counts()==(0,2,1),f"{width}: Focus lines incorrect {counts()}")
            # Never confuse graph editing with visualization: capture linked sockets,
            # primary outputs and fader positions before view changes.
            state=page.evaluate("""() => Array.from(document.querySelectorAll('#tracks .strip')).map(s => ({
                id:s.dataset.id,linked:s.querySelector('.wire-node-in')?.getAttribute('aria-pressed'),
                level:s.querySelector('input.fader')?.value
            }))""")
            selector.select_option('out')
            verify(counts()==(0,0,1),f"{width}: OUT should show selected sends only: {counts()}")
            selector.select_option('all')
            verify(counts()==(1,2,1),f"{width}: ALL should include CH06->07 in background: {counts()}")
            context=layer.locator('.wire-context-line')
            verify(context.first.get_attribute('data-from')=='6' and
                   context.first.get_attribute('data-to')=='7',
                   f"{width}: unrelated route not clearly identified")
            order=layer.locator('path').evaluate_all("""els=>els.map(e =>
                e.classList.contains('wire-context-line')?'ctx':
                e.classList.contains('wire-incoming-line')?'in':
                e.classList.contains('wire-outgoing-line')?'out':'shadow')""")
            verify(order.index('ctx') < order.index('in') < order.index('out'),
                   f"{width}: ALL layering incorrect: {order}")
            after=page.evaluate("""() => Array.from(document.querySelectorAll('#tracks .strip')).map(s => ({
                id:s.dataset.id,linked:s.querySelector('.wire-node-in')?.getAttribute('aria-pressed'),
                level:s.querySelector('input.fader')?.value
            }))""")
            verify(after==state,f"{width}: changing view altered routing or fader state")
            rail.click()
            verify(layer.locator('path').count()==0,f"{width}: collapse leaks cables in ALL mode")
            verify(not strip(3).locator('.wire-tray').is_visible(),
                   f"{width}: socket leaked into PAN on collapse")
            rail.click()
            page.wait_for_function("""() => document.querySelectorAll('#wireLayer path.wire-context-line').length===1""")
            selector.select_option('focus')
            verify(counts()==(0,2,1),f"{width}: Focus did not restore normal view")
            verify(not errors,f"{width}: page errors: {errors}")
            page.close()
        browser.close()
    print("PASS V30 Focus, OUT, ALL visual-only routing at 1366 / 1680 / 1920")

if __name__=="__main__":
    main()
