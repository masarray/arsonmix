"""Firefox functional coverage for Issue #56 Canvas vs DOM mixer meter mode.

Headless Linux verifies correctness, NOT FPS improvement on Windows hardware.
"""
from pathlib import Path
import asyncio
from playwright.async_api import async_playwright

ROOT=Path(__file__).resolve().parents[1]
PAGE=(ROOT/"AR-MIX-Live-FX-Host-Standalone-v0.3.html").as_uri()

async def main():
    async with async_playwright() as pw:
        browser=await pw.firefox.launch(headless=True)
        try:
            page=await browser.new_page(viewport={"width":1440,"height":820})
            errors=[]
            page.on("pageerror",lambda e:errors.append(str(e)))
            await page.goto(PAGE+"?mixperf=canvas")
            await page.wait_for_function("""()=>meterCanvasState.draws>=2 &&
                meterCanvasState.entries.length===14""",timeout=15000)
            data=await page.evaluate("""()=>{
                const c=meterCanvasState.canvas;
                let alpha=0,ctx=c.getContext('2d');
                const d=ctx.getImageData(0,0,c.width,c.height).data;
                for(let n=3;n<d.length;n+=4)if(d[n]){alpha++;if(alpha>8)break}
                return {mode:mixperfMode,meters:meterCanvasState.entries.length,
                    drawn:meterCanvasState.draws,alpha,clips:document.querySelectorAll('#mixer [data-clip]').length,
                    visible:getComputedStyle(document.querySelector('[data-meter="0"]')).visibility}
            }""")
            assert data["mode"]=="canvas" and data["meters"]==14 and data["alpha"]>0,data
            assert data["clips"]==14 and data["visible"]=="hidden",data
            assert await page.evaluate("document.documentElement.dataset.mixcanvas")=='active'
            await page.locator('[data-view="FX RACK"]').click()
            assert await page.locator(".mix-meter-canvas").is_hidden()
            await page.locator('[data-view="MIX"]').click()
            await page.wait_for_function("""()=>meterCanvasState.canvas &&
                !meterCanvasState.canvas.classList.contains('hidden')""")
            await page.close()
            ordinary=await browser.new_page(viewport={"width":1440,"height":820})
            await ordinary.goto(PAGE)
            await ordinary.wait_for_function("""()=>document.querySelectorAll('#mixer [data-meter]').length===14""")
            assert await ordinary.locator(".mix-meter-canvas").count()==0
            assert await ordinary.evaluate("mixperfMode")=="optimized"
            assert not errors,errors
            await ordinary.close()
        finally:
            await browser.close()
    print("PASS: Firefox Canvas2D renders 14 demo meters, preserves 14 clip targets and default DOM fallback")

if __name__=="__main__":
    asyncio.run(main())
