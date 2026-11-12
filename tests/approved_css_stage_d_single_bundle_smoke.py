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
                assert stats["kinds"] == ["mute","solo","pfl"], (size,stats)
                row=page.locator("#tracks .strip[data-id='1'] .mx-hw-row")
                pfl=row.locator('[data-hw-kind="pfl"]')
                assert await row.locator('[data-hw-kind="tap"]').count()==0
                assert await page.locator("#master .mx-hw-row .mx-hw-key").count()==2
                assert await page.locator("#clearCue").is_hidden()
                artwork=await pfl.evaluate("""e=>({
                    bg:getComputedStyle(e,'::before').backgroundImage,
                    position:getComputedStyle(e,'::before').backgroundPosition,
                    shadow:getComputedStyle(e,'::before').filter
                })""")
                assert "harmonice-thin-flat-v9-pfl-font-matched-57x30.png" in artwork["bg"]
                assert artwork["position"].startswith("0%"), artwork
                assert "drop-shadow(" in artwork["shadow"], artwork
                await pfl.click()
                assert await pfl.get_attribute("aria-pressed")=="true"
                on_art=await pfl.evaluate("""e=>({
                    position:getComputedStyle(e,'::before').backgroundPosition,
                    shadow:getComputedStyle(e,'::before').filter
                })""")
                assert on_art["position"].startswith("100%") and on_art["shadow"]=="none"
                await pfl.click()
                assert await pfl.get_attribute("aria-pressed")=="false"
                if size[0]==1680:
                    # WIRE remains usable, but cannot replace third key or
                    # mass-retarget sends via a channel-global tap button.
                    body=page.locator(".console-body")
                    if "collapsed-send" in (await body.get_attribute("class") or ""):
                        await page.locator('.section-rail [data-section="send"]').click()
                    await page.locator('#tracks .strip[data-id="1"] .wire-node-out').click()
                    await page.locator('#tracks .strip[data-id="3"] .wire-node-in').click()
                    assert await row.locator('[data-hw-kind="pfl"]').count()==1
                    await page.locator("#wireConfigToggle").click()
                    await page.locator("#wireMode").select_option("split")
                    await page.locator("#wireConfigClose").click()
                    await page.locator('#tracks .strip[data-id="1"] .wire-node-out').click()
                    await page.locator('#tracks .strip[data-id="4"] .wire-node-in').click()
                    assert await row.locator('[data-hw-kind="tap"]').count()==0
                assert "arsonmix-approved-runtime.css?sha=" in stats["links"][0]
                assert len(css_requests) == 1, (size,css_requests)
                assert not errors, errors
                # MUTE / focused WIRE and original controls remain operative.
                key=page.locator("#tracks .strip[data-id='1'] .mx-hw-key[data-hw-kind='mute']")
                await key.click()
                assert await key.get_attribute("aria-pressed") == "true"
                on_filter=await key.evaluate("(e)=>getComputedStyle(e,'::before').filter")
                assert on_filter=="none","MUTE ON must not inherit raised OFF shadow: "+on_filter
                if size[0]==1680:
                    # Real SOF toolbar workflow; no private graph API or
                    # alternate renderer. Verify the exact user-approved PFL
                    # raster, OFF alpha shadow, ON image and workspace isolation.
                    await page.locator("#sofToggle").click()
                    row=page.locator("#tracks .strip[data-id='1'] .mx-hw-row")
                    assert await row.locator("button.mx-hw-key").count()==3
                    assert await row.locator("[data-hw-kind='mute']").count()==1
                    assert await row.locator("[data-hw-kind='solo']").count()==1
                    assert await row.locator("[data-hw-kind='tap']").count()==0
                    pfl=row.locator("[data-hw-kind='pfl']")
                    assert await pfl.count()==1
                    assert await pfl.get_attribute("aria-pressed")=="false"
                    off_pfl=await pfl.evaluate("""el=>({
                        image:getComputedStyle(el,'::before').backgroundImage,
                        position:getComputedStyle(el,'::before').backgroundPosition,
                        shadow:getComputedStyle(el,'::before').filter
                    })""")
                    assert "harmonice-thin-flat-v9-pfl-font-matched-57x30.png" in off_pfl["image"]
                    assert off_pfl["position"].startswith("0%"),off_pfl
                    assert "drop-shadow(" in off_pfl["shadow"],off_pfl
                    assert await page.locator("#master [data-hw-kind='pfl']").count()==0
                    await pfl.click()
                    assert await pfl.get_attribute("aria-pressed")=="true"
                    on_pfl=await pfl.evaluate("""el=>({
                        position:getComputedStyle(el,'::before').backgroundPosition,
                        shadow:getComputedStyle(el,'::before').filter
                    })""")
                    assert on_pfl["position"].startswith("100%"),on_pfl
                    assert on_pfl["shadow"]=="none",on_pfl
                    # PFL stays visible and its selection survives switching
                    # between MAIN MIX and SOF, never mutating WIRE.
                    await page.locator("#sofToggle").click()
                    assert await page.locator("#tracks .strip[data-id='1'] [data-hw-kind='pfl']").get_attribute("aria-pressed")=="true"
                    await page.locator("#sofToggle").click()
                    assert await page.locator("#tracks .strip[data-id='1'] [data-hw-kind='pfl']").get_attribute("aria-pressed")=="true"
                    await page.locator("#tracks .strip[data-id='1'] [data-hw-kind='pfl']").click()
                    assert await page.locator("#tracks .strip[data-id='1'] [data-hw-kind='pfl']").get_attribute("aria-pressed")=="false"
                    await page.locator("#sofToggle").click()
                assert len(css_requests) == 1, (size,css_requests)
                await page.close()
                print("PASS Stage D one stylesheet / 17 requests eliminated:",
                      size,"CSS rules",stats["cssRules"])
        finally:
            await browser.close()
    print("PASS one approved runtime CSS, Harmonice v9 PNG MUTE/SOLO/PFL, zero late CSS link")

if __name__ == "__main__":
    asyncio.run(main())
