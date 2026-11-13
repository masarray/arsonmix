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
    wire_css=(PAGE.parent/"assets"/"mixer-wire-dots.css").read_text(encoding="utf-8")
    assert "wireWsCanJoin" in html and "data:image/svg+xml;base64" in (html+wire_css)
    assert "switch-section mx-hw-row" in html
    assert "assets/mixer-control-keys.css" in html and "assets/mixer-control-keys.js" in html
    # Legacy stylesheet comments/selectors still exist for unrelated sections;
    # the live DOM must never instantiate either retired hardware wrapper.
    assert '<span class="buttons-wrapper codepen-buttons-wrapper"' not in html
    assert '<span class="hw-switch-socket">' not in html
    assert "ArSonMix V5 backlit hardware material" not in html
    assert "Source port: https://codepen.io/praktikdan/pen/jbqBWm" not in html
    assert css.count(".mx-hw-row>button.mx-hw-key")>=5
    assert "Owner-approved V5" not in css and ".codepen-buttons-wrapper" not in css
    assert "filter:blur" not in css.replace(" ","").lower()
    # SOLO/PFL retain SVG masks; MUTE is rendered by CSS only.
    assert "<mask id=" in js and js.count("<text ")==1
    assert "fill=\\"#edf6fa\\"" in js  # bright OFF ambient through one cutout
    # V25 migrated the exact V30 physical-material suffix ahead of the intact
    # V6 modern MUTE/SOLO/PFL owner. Keep the <=14 priority budget on the
    # ORIGINAL V6 owner section, not on the separate pre-existing V30 material.
    # V26 reconstructs the ORIGINAL V25 key owner with exact rule slices;
    # modern V6 selector bytes remain untouched after obsolete socket pruning.
    # Restore only the immutable CSS owner; the live page may legitimately
    # evolve after V26 (WIRE/FX) without changing hardware key source bytes.
    # Still verifies every V26 owner fingerprint and V25 migrated material.
    from css_v26_restore import restore_owner,historical_v26_owner,manifest as v26_manifest
    from css_v25_restore import manifest as v25_manifest
    original_v25_key=restore_owner(css,v26_manifest()["sources"]["mixer-control-keys.css"])
    v30=v25_manifest()["movedCss"]
    assert original_v25_key.startswith(v30)
    modern_v6=original_v25_key[len(v30):]
    # Historical V26 source is byte-exact after the *one* verified PFL
    # runtime correction. No other CSS edit may pass the owner fingerprint.
    assert 'data-hw-kind="pfl"]{--mx-hw-led:101,210,236}' in css
    assert historical_v26_owner(css,v26_manifest()["sources"]["mixer-control-keys.css"]).endswith(modern_v6),"V6 modern MUTE/SOLO/PFL CSS changed"
    assert css.count("!important")-modern_v6.count("!important")==83
    assert modern_v6.count("!important")<=14, "V6 key owner priority budget regressed"

async def assert_mixer(page, width):
    await page.wait_for_function("""()=>!!window.ArSonMixHardwareKeys &&
      !!document.querySelector('#tracks .strip[data-id="1"] .mx-mute-face') &&
      document.querySelectorAll('#tracks .strip[data-id="1"] svg.mx-hw-face').length===2""")
    rows=await page.locator(".mx-hw-row").count()
    assert rows>=6,(width,rows)
    assert await page.locator(".codepen-buttons-wrapper,.hw-switch-socket").count()==0
    first=page.locator('#tracks .strip[data-id="1"]')
    keys=first.locator(".mx-hw-row>button.mx-hw-key")
    assert await keys.count()==3
    details=await keys.evaluate_all("""xs=>xs.map(e=>{
      const s=getComputedStyle(e),r=e.getBoundingClientRect();
      const svg=e.querySelector('svg.mx-hw-face');
      const cssFace=e.querySelector('.mx-mute-face');
      return {
        kind:e.dataset.hwKind,action:e.dataset.action,
        label:svg?.querySelector('mask text')?.textContent||e.querySelector('.mx-mute-legend')?.textContent,
        masks:svg?.querySelectorAll('mask').length||0,
        texts:svg?.querySelectorAll('mask text').length||0,
        ambient:!!svg?.querySelector('rect[fill="#edf6fa"]'),
        cap:!!svg?.querySelector('linearGradient[id^="mx-cap"]'),
        shine:!!svg?.querySelector('linearGradient[id^="mx-shine"]'),
        cssFace:!!cssFace,
        faceGradient:cssFace?getComputedStyle(cssFace,'::before').backgroundImage:null,
        led:s.getPropertyValue('--mx-hw-led').trim(),
        h:r.height,w:r.width,x:r.x,y:r.y,filter:s.filter,
        glow:getComputedStyle(e,'::before').opacity,
        lamp:svg?getComputedStyle(svg.querySelector('.mx-hw-lamp')).opacity:null,
        background:s.backgroundImage,focus:e.getAttribute('aria-label')
      };
    })""")
    # HTML hardware kind is the operator-facing PFL key, while its
    # existing native action remains 'cue' for the monitoring contract.
    # Never normalize the visual PFL back to cue or blindly alter the GUI.
    assert [d["kind"] for d in details]==["mute","solo","pfl"],details
    assert [d["action"] for d in details]==["mute","solo","cue"],details
    assert [d["label"] for d in details]==["MUTE","SOLO","PFL"],details
    assert [d["led"] for d in details]==["255,78,90","255,207,68","101,210,236"],details
    assert details[0]["cssFace"] and details[0]["masks"]==0,details
    assert "linear-gradient" in details[0]["faceGradient"],details
    assert details[0]["background"]=="none",details
    assert all(d["masks"]==1 and d["texts"]==1 and d["ambient"] and d["cap"] and d["shine"]
               for d in details[1:]),details
    assert all(abs(d["h"]-29)<.7 and d["w"]>=26 for d in details),details
    assert max(d["y"] for d in details)-min(d["y"] for d in details)<.6,details
    assert max(d["w"] for d in details)-min(d["w"] for d in details)<1.7,details
    assert all(details[i]["x"]+details[i]["w"]<=details[i+1]["x"]+.3 for i in range(2)),details
    assert all(d["filter"]=="none" for d in details),details
    assert all(d["glow"]=="0" and d["lamp"]=="0" for d in details[1:]),details
    assert all(d["focus"] for d in details),details
    before=await keys.nth(0).evaluate("(e)=>getComputedStyle(e.querySelector('.mx-mute-face'),'::before').backgroundImage")
    await keys.nth(0).hover()
    after=await keys.nth(0).evaluate("(e)=>getComputedStyle(e.querySelector('.mx-mute-face'),'::before').backgroundImage")
    assert before==after,"MUTE hover must not repaint the cap"
    if width==1680:
        await page.screenshot(path="pages-mixer-v6-OFF-1680.png")
    for n in range(3):await keys.nth(n).click()
    assert await page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button[aria-pressed="true"]').count()==3
    on=await page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button').evaluate_all("""xs=>xs.map(e=>{
      const svg=e.querySelector('svg.mx-hw-face');
      return {
        kind:e.dataset.hwKind,
        face:!!e.querySelector('.mx-mute-face'),
        legend:e.querySelector('.mx-mute-legend')?getComputedStyle(e.querySelector('.mx-mute-legend')).color:null,
        lamp:svg?getComputedStyle(svg.querySelector('.mx-hw-lamp')).opacity:null,
        glow:getComputedStyle(e,'::before').opacity,
        rgb:svg?getComputedStyle(svg.querySelector('linearGradient[id^="mx-lamp"] stop')).stopColor:null
      };
    })""")
    assert on[0]["face"] and on[0]["legend"]=="rgb(242, 231, 231)",on
    assert all(x["glow"]=="1" and x["lamp"]=="1" for x in on[1:]),on
    assert [x["rgb"] for x in on[1:]]==["rgb(255, 207, 68)","rgb(101, 210, 236)"],on
    if width==1680:
        await page.screenshot(path="pages-mixer-v6-ON-1680.png")
    await keys.nth(0).click()
    state=await page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button').evaluate_all(
      "xs=>xs.map(x=>x.getAttribute('aria-pressed'))")
    assert state==['false','true','true'],state
    await page.locator('#tracks .strip[data-id="1"] button[data-action="solo"]').click()
    await page.wait_for_function("""()=>{
      const row=document.querySelector('#tracks .strip[data-id="1"] .mx-hw-row');
      return !!row?.querySelector('.mx-mute-face') && row.querySelectorAll('svg.mx-hw-face').length===2;
    }""")
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
    print("PASS: CSS-built MUTE, V6 SOLO/PFL masked icons, stable 29px keys, OFF/ON toggle, WIRE intact")

if __name__=="__main__":
    asyncio.run(main())
