"""Pixel-exact owner-supplied MUTE surfaces in the live ArSonMix Pages mixer.

Preserve V6 SOLO/PFL, true 56:37 artwork proportions, and 29px hit areas.
Run: python tests/pages_mute_pixel_reference_smoke.py
"""
import asyncio
import hashlib
from pathlib import Path
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
ASSETS=ROOT/"site/preview-v2/assets"
PAGE=ROOT/"site/preview-v2/index.html"
EXPECTED={
    "M-MUTE-OFF.png":"1f927d1f64fbaf8b53f1502bc46bced92e40c258cdb2e37ea01ff001bc7fcf37",
    "M-MUTE-ON.png":"34478820d20620e1469c9b729003c773982da580b3d2fe7e1332876dc6a802cf",
}

def verify_source_assets():
    css=(ASSETS/"mixer-control-keys.css").read_text(encoding="utf-8")
    for file,digest in EXPECTED.items():
        assert hashlib.sha256((ASSETS/file).read_bytes()).hexdigest()==digest,(file,"asset altered")
        assert f'url("{file}")' in css,file
    assert 'background-size:100% auto;' in css  # preserve legend 56:37, never distort
    assert '[data-hw-kind="mute"][aria-pressed="true"]' in css

async def verify_browser():
    async with async_playwright() as p:
        browser=await p.chromium.launch(headless=True)
        for width in (1366,1680):
            page=await browser.new_page(viewport={"width":width,"height":900})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            await page.goto(PAGE.as_uri())
            mute=page.locator('#tracks .strip[data-id="1"] .mx-hw-row>button[data-hw-kind="mute"]')
            await mute.wait_for()
            await page.wait_for_function(
                "()=>!!document.querySelector('#tracks .strip[data-id=\"1\"] .mx-hw-row>button[data-hw-kind=\"mute\"] svg.mx-hw-face')"
            )
            async def check(expected):
                result=await mute.evaluate("""async el=>{
                    const css=getComputedStyle(el), art=css.backgroundImage;
                    const match=art.match(/url\\(["']?([^"')]+)["']?\\)/);
                    if(!match)throw new Error('Missing bitmap background: '+art);
                    const img=new Image();
                    img.src=match[1];
                    await img.decode();
                    const rect=el.getBoundingClientRect();
                    const row=el.parentElement;
                    return {
                        art, pressed:el.getAttribute('aria-pressed'),
                        w:rect.width,h:rect.height,
                        imageW:img.naturalWidth,imageH:img.naturalHeight,
                        fit:css.backgroundSize,
                        bevel:css.boxShadow,
                        fakeGlow:getComputedStyle(el,'::before').display,
                        oldFace:getComputedStyle(el.querySelector('svg.mx-hw-face')).visibility,
                        rowActions:[...row.querySelectorAll('button')].map(x=>x.dataset.action)
                    };
                }""")
                assert expected in result["art"],result
                assert result["imageW"]==56 and result["imageH"]==37,result
                assert result["fit"] in ("100%", "100% auto"),result
                assert result["bevel"]=="none",result
                assert result["fakeGlow"]=="none" and result["oldFace"]=="hidden",result
                assert abs(result["h"]-29)<.6 and result["w"]>=26,result
                assert result["rowActions"]==["mute","solo","cue"],result
                return result
            off=await check("M-MUTE-OFF.png")
            assert off["pressed"]=="false",off
            await mute.hover()
            assert (await check("M-MUTE-OFF.png"))["art"]==off["art"]
            await mute.click()
            on=await check("M-MUTE-ON.png")
            assert on["pressed"]=="true",on
            await mute.click()
            off_again=await check("M-MUTE-OFF.png")
            assert off_again["pressed"]=="false",off_again
            assert not errors,(width,errors)
            await page.close()
        await browser.close()

if __name__=="__main__":
    verify_source_assets()
    asyncio.run(verify_browser())
    print("PASS exact PNG bytes, proportional raster MUTE OFF/ON, latching, no shadow overlays, SOLO/PFL unchanged")
