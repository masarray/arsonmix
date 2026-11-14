"""Physical mixer UX: PAN extremes, contextual GAIN/WIRE, global FX A/B.

Runs the one compiled public /preview/ renderer. Browser sound is simulated;
actual native PCM bypass must pass the separate JUCE/Windows contracts.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE = Path(__file__).resolve().parents[1] / "dist-pages/preview/index.html"


def pan(page, value):
    return page.locator('#tracks .strip[data-id="1"] .pan-section').evaluate(
        """(row,value)=>{
          const slider=row.querySelector('input.center-pan');
          slider.value=String(value);
          slider.dispatchEvent(new Event('input',{bubbles:true}));
          slider.dispatchEvent(new Event('change',{bubbles:true}));
          const text=row.querySelector('output'), r=text.getBoundingClientRect(),
                strip=row.closest('.strip').getBoundingClientRect();
          return {text:text.textContent, clipped:text.scrollWidth>text.clientWidth+1,
            overflow:r.right>strip.right+0.8, right:r.right, strip:strip.right};
        }""", value)


def rack(page, channel):
    return page.locator(f'#tracks .strip[data-id="{channel}"] .fx-section').evaluate(
        """el=>({rack:el.dataset.fxChainEnabled,global:el.dataset.globalFxEnabled,
          slots:[...el.querySelectorAll('.fxslot-row:not(.is-empty)')].map(row=>({
            id:row.dataset.insertId,individual:row.dataset.fxSlotEnabled,
            effective:row.dataset.fxEffective
          }))})""")


def run(page, width, height):
    errors=[]
    page.on("pageerror",lambda error:errors.append(str(error)))
    page.goto(PAGE.as_uri())
    page.locator('#tracks .strip[data-id="1"]').wait_for()
    page.evaluate("document.getElementById('densitySelect').value='comfort'")
    for value,label in [(-100,"L100"),(0,"C"),(100,"R100")]:
        seen=pan(page,value)
        assert seen["text"]==label,(width,label,seen)
        assert not seen["clipped"] and not seen["overflow"],(width,label,seen)
    # The operator's folded WIRE intent must be restored on FX mode.
    body=page.locator('.console-body')
    assert "collapsed-send" in (body.get_attribute("class") or "")
    page.locator('#gainView').click()
    assert "collapsed-send" not in (body.get_attribute("class") or "")
    assert page.locator('.section-rail [data-section="send"]').get_attribute(
        "aria-expanded")=="true"
    page.locator('#fxView').click()
    assert "collapsed-send" in (body.get_attribute("class") or "")
    # A manual WIRE rail choice in GAIN is a stronger operator intent.
    page.locator('#gainView').click()
    page.locator('.section-rail [data-section="send"]').click()
    page.locator('.section-rail [data-section="send"]').click()
    page.locator('#fxView').click()
    assert "collapsed-send" not in (body.get_attribute("class") or "")
    # Global A/B overrides effective FX only; insert IDs, individual rack
    # and slot flags remain preserved. Master is included, not just CH01.
    ch_before=rack(page,1)
    main_before=page.locator('.strip.master .fx-section').evaluate(
        "el=>el.dataset.fxChainEnabled")
    assert ch_before["slots"] and ch_before["slots"][0]["effective"]=="true"
    assert main_before=="true"
    page.locator('#globalFxToggle').click()
    assert page.locator('#globalFxToggle').get_attribute("aria-pressed")=="false"
    during=rack(page,1)
    assert during["global"]=="false"
    assert [v["id"] for v in during["slots"]]==[v["id"] for v in ch_before["slots"]]
    assert [v["individual"] for v in during["slots"]]==[
        v["individual"] for v in ch_before["slots"]]
    assert all(v["effective"]=="false" for v in during["slots"])
    master=page.locator('.strip.master .fx-section')
    assert master.get_attribute("data-global-fx-enabled")=="false"
    assert master.get_attribute("data-fx-chain-enabled")=="true"
    page.locator('#globalFxToggle').click()
    after=rack(page,1)
    assert after==ch_before | {"global":"true"},(ch_before,after)
    assert not errors,errors
    page.close()


def main():
    assert PAGE.is_file(),"Publish Pages into dist-pages/ before browser test"
    with sync_playwright() as pw:
        browser=pw.chromium.launch(headless=True)
        try:
            for width,height in [(390,844),(1366,768),(1680,900)]:
                page=browser.new_page(viewport={"width":width,"height":height})
                run(page,width,height)
        finally:
            browser.close()
    print("PASS PAN L100/R100 + GAIN/WIRE + non-destructive GLOBAL FX A/B")


if __name__=="__main__":
    main()
