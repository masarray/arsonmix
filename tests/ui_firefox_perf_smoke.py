"""Firefox functional smoke for Issue #56; no synthetic FPS pass/fail gates.

Validates opt-in diagnostics and 11-strip native-size artwork on Gecko.
Real Windows frame pacing/compositor still requires owner-side Firefox Profiler.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parents[1]
PAGE = (ROOT / "AR-MIX-Live-FX-Host-Standalone-v0.3.html").as_uri()


async def main():
    async with async_playwright() as pw:
        browser = await pw.firefox.launch(headless=True)
        try:
            page = await browser.new_page(viewport={"width": 1440, "height": 820})
            errors = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            await page.goto(PAGE)
            await page.wait_for_selector('[data-fader="master"] .cap')
            assert await page.locator('#mixer [data-fader]').count() == 11
            assert await page.evaluate('window.__arMixPerfProbe === undefined'), "No profiler in normal mode"
            await page.close()

            for mode in ("optimized", "meters-off", "all-off"):
                page = await browser.new_page(viewport={"width": 1440, "height": 820})
                page.on("pageerror", lambda error: errors.append(str(error)))
                await page.goto(PAGE + "?mixperf=" + mode)
                await page.wait_for_selector('[data-fader="master"] .cap')
                assert await page.locator('#mixer [data-fader]').count() == 11
                assert await page.locator('#mixer [data-meter]').count() == 12
                assert await page.locator('#mixer [data-clip]').count() == 12

                heads = await page.evaluate("""() => {
                    const c = document.querySelector('[data-fader="0"] .cap');
                    const m = document.querySelector('[data-fader="master"] .cap');
                    const rect = e => {
                        const r = e.getBoundingClientRect();
                        return [r.width, r.height];
                    };
                    return {channel:rect(c),master:rect(m),
                            distinct:getComputedStyle(c).backgroundImage !== getComputedStyle(m).backgroundImage};
                }""")
                assert heads == {"channel": [36,77], "master": [36,77], "distinct": True}, heads

                fader = page.locator('[data-fader="0"]')
                await fader.focus()
                await fader.press("Home")
                assert float(await fader.get_attribute("aria-valuenow")) == -60.0
                await fader.press("End")
                assert float(await fader.get_attribute("aria-valuenow")) == 10.0

                capture = asyncio.create_task(page.evaluate("() => window.__arMixPerfProbe.run(1000)"))
                await asyncio.sleep(.06)
                bounds = await page.locator('#mixer [data-fader="0"]').bounding_box()
                assert bounds
                await page.mouse.move(bounds["x"]+bounds["width"]/2, bounds["y"]+15, steps=5)
                await page.mouse.move(bounds["x"]+bounds["width"]/2, bounds["y"]+40, steps=5)
                result = await capture
                assert result["mode"] == mode and result["environment"]["iframe"] is False, result
                assert result["mixer"]["samples"] > 0, result
                assert result["pointerMoves"]["mixer"] > 0, result
                for part in ("mixer", "outside"):
                    stat = result[part]
                    assert set(("medianMs","p95Ms","p99Ms","maxMs","over33Pct")).issubset(stat), stat
                    if stat["samples"]:
                        assert 0 <= stat["medianMs"] <= stat["p95Ms"] <= stat["p99Ms"] <= stat["maxMs"]
                        assert 0 <= stat["over33Pct"] <= 100
                assert isinstance(result["longTaskAvailable"], bool), result
                if not result["longTaskAvailable"]:
                    assert result["longTasks"] is None and result["longTaskMs"] is None, result
                if mode in ("meters-off", "all-off"):
                    assert await page.evaluate('browserMeterStats.ticks') == 0

                assert not errors, errors
                await page.close()
        finally:
            await browser.close()
    print("PASS: Firefox 11 faders, fixed LEDs, opt-in probe and honest long-task support")


if __name__ == "__main__":
    asyncio.run(main())
