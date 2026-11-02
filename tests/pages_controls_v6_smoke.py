"""ArSonMix public preview V6: CSS ownership, small-key legibility, state and WIRE.
Test the SAME source shipped to /preview/ and /preview-v2/, never a mock.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
PAGE=ROOT/"site/preview-v2/index.html"
STYLE=ROOT/"site/preview-v2/assets/mixer-control-keys.css"
RUNTIME=ROOT/"site/preview-v2/assets/mixer-control-keys.js"

def assert_ownership():
    html=PAGE.read_text(encoding="utf-8")
    css=STYLE.read_text(encoding="utf-8")
    js=RUNTIME.read_text(encoding="utf-8")
    assert "wireWsCanJoin" in html and "data:image/svg+xml;base64" in html
    assert "switch-section mx-hw-row" in html
    assert "assets/mixer-control-keys.css" in html and "assets/mixer-control-keys.js" in html
    assert "codepen-buttons-wrapper" not in html and "hw-switch-socket" not in html
    assert "ArSonMix V5 backlit hardware material" not in html
    assert "Source port: https://codepen.io/praktikdan/pen/jbqBWm" not in html
    assert css.count(".mx-hw-row>button.mx-hw-key")>=5
    assert "Owner-approved V5" not in css and ".codepen-buttons-wrapper" not in css
    assert "filter:blur" not in css.replace(" ","").lower()
    assert js.count("mask id=")==0  # generated at runtime, one per control
    assert "<mask id=" in js and js.count("<text ")==1
    assert "fill=\\"#edf6fa\\"" in js  # bright OFF ambient through one cutout
    assert css.count("!important")<=14, "Owner CSS acquiring another override cascade"

async def assert_mixer(page, width):
    await page.wait_for_function("""()=>!!window.ArSonMixHardwareKeys &&
      document.querySelectorAll('#tracks .mx-hw-row>button.mx-hw-key svg.mx-hw-face').length>=3""")
    assert await page.locator(".mx-hw-row").count()>=11
    assert await page.locator(".codepen-buttons-wrapper,.hw-switch-socket").count()==0
    first=page.locator('#tracks .strip[data-id="1"]')
    keys=first.locator(".mx-hw-row>button.mx-hw-key")
    assert await keys.count()==3
    details=await keys.evaluate_all("""xs=>xs.map(e=>{
      const s=getComputedStyle(e),r=e.getBoundingClientRect();
      const svg=e.querySelector('svg.mx-hw-face');
      return {
        kind:e.dataset.hwKind,
        action:e.dataset.action,
        label:svg.querySelector('mask text')?.textContent,
        masks:svg.querySelectorAll('mask').length,
        texts:svg.querySelectorAll('mask text').length,
        overlaid:svg.querySelectorAll(':scope > text').length,
        ambient:svg.querySelector('rect[fill="#edf6fa"]')!==null,
        cap:svg.querySelector('linearGradient[id^="mx-cap"]')!==null,
        shine:svg.querySelector('linearGradient[id^="mx-shine"]')!==null,
        led:s.getPropertyValue('--mx-hw-led').trim(),
        h:r.height,w:r.width,x:r.x,y:r.y,
        filter:s.filter,
        glow:getComputedStyle(e,'::before').opacity,
        lamp:getComputedStyle(svg.querySelector('.mx-hw-lamp')).opacity,
        background:s.backgroundImage,
        focus:e.getAttribute('aria-label')
      };
    })""")
    assert [d["kind"] for d in details]==["mute","solo","cue"],details
    assert [d["action"] for d in details]==["mute","solo","cue"],details
    assert [d["label"] for d in details]==["MUTE","SOLO","PFL"],details
    assert [d["led"] for d in details]==["255,78,90","255,207,68","101,210,236"],details
    assert all(d["masks"]==1 and d["texts"]==1 and d["overlaid"]==0
               and d["ambient"] and d["cap"] and d["shine"] for d in details),details
    assert all(abs(d["h"]-29)<.7 and d["w"]>=26 for d in details),details
    assert max(d["y"] for d in details)-min(d["y"] for d in details)<.6,details
    assert max(d["w"] for d in details)-min(d["w"] for d in details)<1.7,details
    assert all(details[i]["x"]+details[i]["w"]<=details[i+1]["x"]+.3 for i in range(2)),details
    assert all(d["filter"]=="none" and d["glow"]=="0" and d["lamp"]=="0" for d in details),details
    assert all(d["focus"] for d in details),details
    before=await keys.nth(0).evaluate("(e)=>getComputedStyle(e).backgroundImage")
    await keys.nth(0).hover()
    after=await keys.nth(0).evaluate("(e)=>getComputedStyle(e).backgroundImage")
    assert before==after,"Hover must NOT repaint the cap"
    if width==1680:
        await page.screenshot(path="pages-mixer-v6-OFF-1680.png")
    for n in range(3):await keys.nth(n).click()
    assert await page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button[aria-pressed="true"]').count()==3
    on=await page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button').evaluate_all(
      """xs=>xs.map(e=>({
        glow:getComputedStyle(e,'::before').opacity,
        lamp:getComputedStyle(e.querySelector('.mx-hw-lamp')).opacity,
        rgb:getComputedStyle(e.querySelector('linearGradient[id^="mx-lamp"] stop')).stopColor,
        width:e.getBoundingClientRect().width
      }))""")
    assert all(x["glow"]=="1" and x["lamp"]=="1" for x in on),on
    assert [x["rgb"] for x in on]==[
      "rgb(255, 78, 90)","rgb(255, 207, 68)","rgb(101, 210, 236)"],on
    if width==1680:
        await page.screenshot(path="pages-mixer-v6-ON-1680.png")
    await keys.nth(0).click()
    state=await page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button').evaluate_all(
      "xs=>xs.map(x=>x.getAttribute('aria-pressed'))")
    assert state==['false','true','true'],state
    # The existing mixer render path replaces strip HTML: no duplicate labels
    # and no old class may reappear after re-render.
    await page.evaluate("render()")
    await page.wait_for_function("""()=>document.querySelectorAll(
      '#tracks .strip[data-id="1"] .mx-hw-row>button .mx-hw-face').length===3""")
    assert await page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button').count()==3

async def main():
    assert_ownership()
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        for width in (1366,1680):
            page=await browser.new_page(viewport={"width":width,"height":900})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            await page.goto(PAGE.as_uri())
            await assert_mixer(page,width)
            assert not errors,(width,errors)
            await page.close()
        await browser.close()
    print("PASS V6: single CSS ownership, 3 identical 29px keys, lit/unlit contrast, one masked text each, no hover, independent state, WIRE source intact")

if __name__=="__main__":
    asyncio.run(main())
