"""Verify a genuine code-rendered MUTE hardware key on the shipped Pages GUI.

PNG screenshots are design references ONLY. The shipping material is HTML/CSS:
beveled grey OFF face, recessed red bar, red ON face, white lower reflection.
Run as part of the existing Chromium UI smoke workflow.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/"site/preview-v2/assets"
PAGE=ROOT/"site/preview-v2/index.html"


def assert_source():
    css=(ASSETS/"mixer-control-keys.css").read_text(encoding="utf-8")
    js=(ASSETS/"mixer-control-keys.js").read_text(encoding="utf-8")
    build=(ROOT/"scripts/build_pages.py").read_text(encoding="utf-8")
    for name in ("M-MUTE-OFF.png","M-MUTE-ON.png"):
        assert not (ASSETS/name).exists(), "Reference bitmap must not ship: "+name
        assert name not in css+js+build, "Runtime may not reference user screenshot: "+name
    section=css[css.index("/* MUTE is rendered entirely from CSS/HTML"):]
    assert "linear-gradient" in section and "box-shadow" in section
    assert "aspect-ratio:56 / 37" in section
    assert "25cqw" in section,"MUTE legend must scale with channel width"
    assert "url(" not in section and "data:image" not in section
    assert "semantic==='mute'" in js and "mx-mute-face" in js
    assert "aria-pressed" in section
    assert "resize?.observe(button);" in js # SOLO/PFL existing renderer retained


async def check_mute(page,width):
    await page.wait_for_function("""()=>!!document.querySelector(
      '#tracks .strip[data-id="1"] button[data-hw-kind="mute"] .mx-mute-face')""")
    mute=page.locator('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]')
    async def state():
        return await mute.evaluate("""el=>{
          const face=el.querySelector('.mx-mute-face');
          const caption=face?.querySelector('.mx-mute-legend');
          const lamp=face?.querySelector('.mx-mute-lamp');
          const r=el.getBoundingClientRect(),s=getComputedStyle(el);
          const p=getComputedStyle(face,'::before');
          return {
            pressed:el.getAttribute('aria-pressed'),
            action:el.dataset.action,
            width:r.width,height:r.height,
            caption:caption?.textContent,
            captionColor:getComputedStyle(caption).color,
            captionSize:parseFloat(getComputedStyle(caption).fontSize),
            faceBg:p.backgroundImage,faceShadow:p.boxShadow,
            lamp:getComputedStyle(lamp).display,
            lowerReflection:getComputedStyle(face,'::after').opacity,
            nativeImages:el.querySelectorAll('img,svg,canvas').length,
            buttonBg:s.backgroundImage,
            hoverState:s.transitionDuration
          };
        }""")
    off=await state()
    assert off["action"]=="mute" and off["pressed"]=="false",off
    assert off["caption"]=="MUTE" and off["nativeImages"]==0,off
    assert off["buttonBg"]=="none" and "linear-gradient" in off["faceBg"],off
    assert "inset" in off["faceShadow"] and off["lamp"]!="none",off
    assert off["lowerReflection"]=="0",off
    assert abs(off["height"]-29)<.8 and off["width"]>=26,off
    assert abs(off["captionSize"]-off["width"]*.25)<1.0,off # container-scaled 25cqw
    await mute.hover()
    assert (await state())["faceBg"]==off["faceBg"],"Passive hover changes material"
    await mute.click()
    on=await state()
    assert on["pressed"]=="true" and on["faceBg"]!=off["faceBg"],on
    assert on["captionColor"]!=off["captionColor"],on
    assert on["lamp"]=="none" and on["lowerReflection"]=="1",on
    assert on["nativeImages"]==0 and on["action"]=="mute",on
    if width==1680:
        await mute.screenshot(path="mute-css-on-1680.png")
    await mute.click()
    off2=await state()
    assert off2["pressed"]=="false" and off2["faceBg"]==off["faceBg"],off2
    assert off2["lamp"]!="none" and off2["lowerReflection"]=="0",off2
    # SOLO and PFL are independent from the MUTE material and state.
    siblings=await page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button').evaluate_all(
       "xs=>xs.map(x=>({action:x.dataset.action,hasSVG:!!x.querySelector('svg.mx-hw-face')}))")
    assert siblings==[
       {"action":"mute","hasSVG":False},
       {"action":"solo","hasSVG":True},
       {"action":"cue","hasSVG":True},
    ],siblings


async def main():
    assert_source()
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        for width in (1366,1680):
            page=await browser.new_page(viewport={"width":width,"height":900})
            errors=[]
            page.on("pageerror",lambda err:errors.append(str(err)))
            await page.goto(PAGE.as_uri())
            await check_mute(page,width)
            assert not errors,(width,errors)
            await page.close()
        await browser.close()
    print("PASS CSS-only MUTE, ON/OFF/OFF toggle, correct bevel and white underline; no PNG/SVG runtime; SOLO/PFL preserved")


if __name__=="__main__":
    asyncio.run(main())
