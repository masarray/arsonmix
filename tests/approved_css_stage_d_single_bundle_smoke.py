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
                assert stats["keys"] >= 3 and "harmonice-thin-flat-v9-approved-off-shadow-57x30.png" in stats["image"], (size,stats)
                assert stats["kinds"] == ["mute","solo","tap"], (size,stats)
                tap=page.locator("#tracks .strip[data-id='1'] [data-hw-kind='tap']")
                assert await tap.get_attribute("data-tap-visual")=="idle", "No-send POST must be unlit"
                # Visual parity with the approved v9 HTML at physical 1x:
                # only raised OFF PNG faces receive lower alpha drop-shadow.
                off_filters=await page.evaluate("""() => {
                    const f=(selector)=>getComputedStyle(
                        document.querySelector(selector),"::before").filter;
                    const row="#tracks .strip[data-id='1'] .mx-hw-row ";
                    return {
                        mute:f(row+'[data-hw-kind="mute"]'),
                        solo:f(row+'[data-hw-kind="solo"]'),
                        idle:f(row+'[data-hw-kind="tap"]')
                    };
                }""")
                assert "drop-shadow(" in off_filters["mute"],off_filters
                assert "drop-shadow(" in off_filters["solo"],off_filters
                assert off_filters["idle"]=="none",off_filters
                await tap.click()
                assert await tap.get_attribute("data-tap-visual")=="pre"
                pre_filter=await tap.evaluate("(e)=>getComputedStyle(e,'::before').filter")
                assert "drop-shadow(" in pre_filter,pre_filter
                await tap.click()  # restore neutral no-SEND POST for WIRE tests
                assert await tap.get_attribute("data-tap-visual")=="idle"
                assert await page.locator("#master .mx-hw-row .mx-hw-key").count()==2
                assert await page.locator("#clearCue").is_hidden(), "PFL toolbar must stay hidden"
                if size[0]==1680:
                    # UI-only regression: no private closure APIs or test
                    # injection. A real source OUT and destination IN must
                    # determine the physical POST/MIX artwork.
                    body=page.locator(".console-body")
                    if "collapsed-send" in (await body.get_attribute("class") or ""):
                        await page.locator('.section-rail [data-section="send"]').click()
                    await page.locator('#tracks .strip[data-id="1"] .wire-node-out').click()
                    await page.locator('#tracks .strip[data-id="3"] .wire-node-in').click()
                    tap=page.locator("#tracks .strip[data-id='1'] [data-hw-kind='tap']")
                    assert await tap.get_attribute("data-tap-visual")=="post", "Real POST send must illuminate"
                    await page.locator("#wireConfigToggle").click()
                    await page.locator("#wireMode").select_option("split")
                    await page.locator("#wireConfigClose").click()
                    await page.locator('#tracks .strip[data-id="1"] .wire-node-out').click()
                    await page.locator('#tracks .strip[data-id="4"] .wire-node-in').click()
                    tap=page.locator("#tracks .strip[data-id='1'] [data-hw-kind='tap']")
                    assert await tap.get_attribute("data-tap-visual")=="mixed", "Mixed sends must have honest MIX artwork"
                    assert await tap.get_attribute("aria-pressed")=="mixed"
                    await tap.click()
                    assert await page.locator("#tracks .strip[data-id='1'] [data-hw-kind='tap']").get_attribute("data-tap-visual")=="post", "MIX click must retarget sends to POST"
                assert "arsonmix-approved-runtime.css?sha=" in stats["links"][0]
                assert len(css_requests) == 1, (size,css_requests)
                assert not errors, errors
                # MUTE / focused WIRE and original controls remain operative.
                key=page.locator("#tracks .strip[data-id='1'] .mx-hw-key[data-hw-kind='mute']")
                await key.click()
                assert await key.get_attribute("aria-pressed") == "true"
                on_filter=await key.evaluate("(e)=>getComputedStyle(e,'::before').filter")
                assert on_filter=="none","MUTE ON must not inherit raised OFF shadow: "+on_filter
                assert len(css_requests) == 1, (size,css_requests)
                await page.close()
                print("PASS Stage D one stylesheet / 17 requests eliminated:",
                      size,"CSS rules",stats["cssRules"])
        finally:
            await browser.close()
    print("PASS one approved runtime CSS, Harmonice v9 PNG MUTE/SOLO/PRE-POST, zero late CSS link")

if __name__ == "__main__":
    asyncio.run(main())
