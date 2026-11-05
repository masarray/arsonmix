"""Stage A: actual assembled /preview/ probe, zero default profiling, A/B activation.

This is a functional instrumentation test, not synthetic evidence that the
user's Windows mouse stutter has been fixed.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parents[1]
PREVIEW = ROOT / "dist-pages/preview/index.html"


async def main():
    assert PREVIEW.exists(), "Build the approved /preview/ bundle first"
    async with async_playwright() as engine:
        browser = await engine.chromium.launch(headless=True)
        try:
            page = await browser.new_page(viewport={"width": 1366, "height": 768})
            errors = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            await page.goto(PREVIEW.as_uri())
            await page.wait_for_selector("#tracks .strip[data-id='1']")
            assert await page.evaluate("typeof window.ArSonMixPerf") == "object"
            assert await page.evaluate("window.ArSonMixPerf.active") is False
            assert await page.evaluate("window.ArSonMixPerf.last") is None
            assert await page.locator(".mx-hw-key[data-hw-kind='mute']").count() > 0
            before = await page.locator(".mx-hw-key[data-hw-kind='mute']").first.evaluate(
                "node => getComputedStyle(node).backgroundImage"
            )

            # No CSS, DOM controls or demo audio state are modified by profiling.
            assert await page.evaluate(
                "window.ArSonMixPerf.start({durationMs: 2500, label: 'chromium-test'})"
            ) is True
            assert await page.evaluate("window.ArSonMixPerf.active") is True
            bounds = await page.locator("#tracks .strip[data-id='1']").bounding_box()
            assert bounds
            for step in range(6):
                await page.mouse.move(bounds["x"] + 18, bounds["y"] + 70 + step * 16)
                await asyncio.sleep(.03)
            await asyncio.sleep(.18)
            result = await page.evaluate("window.ArSonMixPerf.stop()")
            assert result["schema"] == "arsonmix.gui-perf-stage-a.v1"
            assert result["renderer"] == "browser-preview"
            assert result["zoneFrames"]["mixer"]["samples"] > 0, result
            assert result["pointerMoves"]["mixer"] > 0, result
            assert result["handledPointerToRafMs"]["samples"] > 0, result
            for label, stats in result["zoneFrames"].items():
                if stats["samples"]:
                    assert 0 <= stats["medianMs"] <= stats["p95Ms"] <= stats["p99Ms"] <= stats["maxMs"], label
            assert isinstance(result["longTaskAvailable"], bool)
            if not result["longTaskAvailable"]:
                assert result["longTasks"] is None and result["longTaskMs"] is None
            assert await page.evaluate("window.ArSonMixPerf.active") is False
            assert await page.evaluate("window.ArSonMixPerf.exportJSON()") is not None
            after = await page.locator(".mx-hw-key[data-hw-kind='mute']").first.evaluate(
                "node => getComputedStyle(node).backgroundImage"
            )
            assert before == after, "Profiling must not alter approved buttons"

            # Keyboard on/off works independently of the query-string test mode.
            await page.keyboard.press("Control+Alt+Shift+F9")
            assert await page.evaluate("window.ArSonMixPerf.active") is True
            await page.keyboard.press("Control+Alt+Shift+F9")
            assert await page.evaluate("window.ArSonMixPerf.active") is False

            query = await browser.new_page(viewport={"width": 1366, "height": 768})
            query.on("pageerror", lambda error: errors.append(str(error)))
            await query.goto(PREVIEW.as_uri() + "?armixperf=1")
            await query.wait_for_selector("#tracks .strip[data-id='1']")
            assert await query.evaluate("window.ArSonMixPerf.active") is True
            await query.evaluate("window.ArSonMixPerf.stop()")
            assert not errors, errors
            await query.close()
            await page.close()
        finally:
            await browser.close()
    print("PASS Stage A: dormant default; assembled approved PNG GUI, opt-in profiling, "
          "pointer/rAF evidence, keyboard/URL activation, no fake hardware FPS claim")


if __name__ == "__main__":
    asyncio.run(main())
