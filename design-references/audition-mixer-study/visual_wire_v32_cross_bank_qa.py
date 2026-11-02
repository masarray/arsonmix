#!/usr/bin/env python3
"""V32 acceptance: isolated soft wire settling and any-bank source-first patching."""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE=Path(__file__).with_name("index.html")

def verify(ok,why):
    if not ok: raise AssertionError(why)

def main():
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page=browser.new_page(viewport={"width":width,"height":height})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            page.goto(PAGE.as_uri())
            page.locator('.section-rail [data-section="send"]').click()
            def ch(n):
                return page.locator(f'#tracks .strip[data-id="{n}"]')
            def cable(a,b):
                return page.locator(f'#wireLayer path.wire-outgoing-line[data-from="{a}"][data-to="{b}"]')
            def route_to(n):
                ch(n).locator('.wire-node-in').click()
            ch(1).locator('.wire-node-out').click()  # Explicit pin; never retarget from bank navigation
            route_to(3)
            verify(cable(1,3).count()==1,f"{width}: first wire missing")
            page.wait_for_timeout(360)
            before=cable(1,3).get_attribute('d')
            # Count actual SVG d-attribute writes during subsequent accepted route.
            # No d writes are allowed for CH01→CH03 while CH01→CH04 settles.
            page.evaluate("""() => {
              const seen={};
              window.__v32Seen=seen;
              window.__v32Observer=new MutationObserver(muts=>{
                for(const m of muts){
                  if(m.type!=='attributes'||m.attributeName!=='d')continue;
                  const key=m.target.getAttribute('data-cable-key');
                  if(key)seen[key]=(seen[key]||0)+1;
                }
              });
              window.__v32Observer.observe(document.getElementById('wireLayer'),
                {attributes:true,attributeFilter:['d'],subtree:true});
            }""")
            route_to(4)
            page.wait_for_timeout(360)
            seen=page.evaluate("""() => {
              window.__v32Observer.disconnect();
              return window.__v32Seen;
            }""")
            verify(cable(1,3).get_attribute('d')==before,
                   f"{width}: old cable changed shape when another cable was plugged")
            verify(seen.get("1-3",0)==0,
                   f"{width}: old cable received animation frames: {seen}")
            verify(seen.get("1-4",0)>0,
                   f"{width}: newly plugged wire did not settle independently: {seen}")
            # Leave source explicitly pinned, navigate away, and check the
            # status, existing legacy SEND and boundary portal for CH01→Bus A.
            page.locator('#bankNext').click()
            verify(ch(1).count()==0 and ch(11).count()==1,
                   f"{width}: CH11 is not in another bank")
            verify('FROM CH01' in page.locator('#wireLegend').inner_text() and
                   'Click any IN' in page.locator('#wireLegend').inner_text(),
                   f"{width}: no actionable cross-bank source context")
            verify(cable(1,11).count()==1 and
                   page.locator('#wireLayer .wire-offbank-entry').count()==1,
                   f"{width}: existing CH01→CH11 legacy SEND invisible across banks")
            verify(ch(11).locator('.wire-node-in').get_attribute('aria-pressed')=='true',
                   f"{width}: legacy Bus A send incorrectly shown unlinked")
            ch(11).locator('.strip-identity-top').click()
            verify('FROM CH01' in page.locator('#wireLegend').inner_text(),
                   f"{width}: ordinary mixer selection silently changed pinned source")
            route_to(11)
            verify('ALREADY CONNECTED' in page.locator('#info').inner_text(),
                   f"{width}: legacy Send duplication not explained or blocked")
            # Revisit source and choose CH03, which has no pre-existing Bus send.
            page.locator('#wireLegend .wire-offbank-source').click()
            verify(ch(1).count()==1,f"{width}: jump-to-source did not find CH01")
            ch(3).locator('.wire-node-out').click()
            page.locator('#bankNext').click()
            verify('FROM CH03' in page.locator('#wireLegend').inner_text(),
                   f"{width}: CH03 source not preserved across bank")
            route_to(11)
            verify(page.locator('#routeChoice').is_visible(),
                   f"{width}: Bus-only vs parallel decision bypassed cross-bank")
            page.locator('#routeParallel').click()
            verify(not page.locator('#routeChoice').is_visible() and cable(3,11).count()==1,
                   f"{width}: cross-bank CH03→CH11 was not committed")
            path=cable(3,11)
            parts=path.evaluate("""el => (el.getAttribute('d').match(/-?\\d+(?:\\.\\d+)?/g)||[]).map(Number)""")
            verify(len(parts)==8 and abs(parts[0]-12)<.2 and parts[3]>parts[1]+10,
                   f"{width}: cross-bank cable did not enter via labelled gravity portal: {parts}")
            verify(ch(11).locator('.wire-node-in').get_attribute('aria-pressed')=='true',
                   f"{width}: committed cross-bank receiver not marked linked")
            if width==1680:
                page.screenshot(path=str(PAGE.with_name("preview-v32-cross-bank-1680.png")))
            page.locator('#wireLegend .wire-offbank-source').click()
            verify(ch(3).count()==1,f"{width}: return-to-source failed after routing")
            # V33 universal native picker: target on a HIDDEN bank, no bank
            # navigation required; graph mutation still requires Bus choice.
            ch(5).locator('.wire-node-out').click()
            picker=page.locator('#wireLegend [data-wire-destination]')
            verify(picker.count()==1 and
                   'CH11' in picker.locator('option[value="11"]').inner_text(),
                   f"{width}: any-bank PATCH TO destination is missing")
            picker.select_option('11')
            verify(page.locator('#routeChoice').is_visible(),
                   f"{width}: cross-bank picker bypassed explicit Bus choice")
            page.locator('#routeParallel').click()
            page.locator('#bankNext').click()
            verify(cable(5,11).count()==1,
                   f"{width}: selected hidden-bank destination did not create route")
            verify('FROM CH05' in page.locator('#wireLegend').inner_text(),
                   f"{width}: picker lost pinned source on bank navigation")
            verify(not errors,f"{width}: page JS errors: {errors}")
            page.close()
        browser.close()
    print("PASS V32 per-edge no-bounce isolation, CH01→CH11 existing send, CH03→CH11 new patch, cross-bank UI at 1366/1680/1920")

if __name__=="__main__":main()
