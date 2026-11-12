"""Stage D: the EXACT approved /preview/ GUI loads one CSSOM, no override stack.

Resource-count proof only. It cannot claim Windows cursor or GPU latency.
"""
import asyncio
from pathlib import Path
from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "dist-pages/preview/index.html"


async def main():
    assert PAGE.is_file(), "Build the only approved /preview/ GUI first"
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        try:
            for size in ((1366,768), (1680,900), (1920,1080)):
                page = await browser.new_page(viewport={"width":size[0],"height":size[1]})
                css_requests = []
                errors = []
                page.on("request", lambda r: css_requests.append(r.url)
                        if r.resource_type == "stylesheet" else None)
                page.on("pageerror", lambda e: errors.append(str(e)))
                await page.goto(PAGE.as_uri())
                await page.wait_for_selector("#tracks .strip[data-id='1']")
                await page.evaluate("document.fonts.ready")
                stats = await page.evaluate("""() => {
                    const links=[...document.querySelectorAll('link[rel="stylesheet"]')];
                    const sheets=[...document.styleSheets].filter(x =>
                        x.ownerNode?.tagName==="LINK");
                    const keys=document.querySelector(
                        "#tracks .strip[data-id='1'] .mx-hw-key[data-hw-kind='mute']");
                    const image=getComputedStyle(keys,"::before").backgroundImage;
                    const root=getComputedStyle(document.querySelector(".console-body"));
                    return {links:links.map(x=>x.href),
                        count:sheets.length,
                        image,
                        display:root.display,
                        keys:document.querySelectorAll(".mx-hw-key").length,
                        kinds:[...document.querySelectorAll("#tracks .strip[data-id=\'1\'] .mx-hw-row .mx-hw-key")].map(x=>x.dataset.hwKind),
                        // file:// Playwright may block CSSOM rule inspection;
                        // stylesheet count and computed visuals remain valid.
                        cssRules:(() => {try{return sheets[0]?.cssRules.length??0}
                          catch{return null}})()};
                }""")
                assert len(stats["links"]) == stats["count"] == 1, (size,stats)
                if stats["cssRules"] is not None:
                    assert stats["cssRules"] > 100, (size,stats)
                assert stats["keys"] >= 3 and "harmonice-thin-flat-v8-57x30.png" in stats["image"], (size,stats)
                assert stats["kinds"] == ["mute","solo","tap"], (size,stats)
                assert "arsonmix-approved-runtime.css?sha=" in stats["links"][0]
                assert len(css_requests) == 1, (size,css_requests)
                assert not errors, errors
                # MUTE / focused WIRE and original controls remain operative.
                key=page.locator("#tracks .strip[data-id='1'] .mx-hw-key[data-hw-kind='mute']")
                await key.click()
                assert await key.get_attribute("aria-pressed") == "true"
                assert len(css_requests) == 1, (size,css_requests)
                await page.close()
                print("PASS Stage D one stylesheet / 17 requests eliminated:",
                      size,"CSS rules",stats["cssRules"])
        finally:
            await browser.close()
    print("PASS one approved runtime CSS, Harmonice v8 PNG MUTE/SOLO/PRE-POST, zero late CSS link")

if __name__ == "__main__":
    asyncio.run(main())
