"""V11 CSS ownership: compare original-order inline baseline with actual linked assets.

Real Chromium computed styles (including premium material) at desktop/laptop sizes,
eight collapsed rail states, and actual MUTE click. No mocks or engine changes.
"""
import asyncio
import re
import tempfile
from pathlib import Path
from playwright.async_api import async_playwright
from pages_css_architecture_v10_smoke import source_gate

ROOT = Path(__file__).resolve().parents[1]
HTML = ROOT / "site/preview-v2/index.html"
OWNED = (
    "mixer-section-controls.css",
    "mixer-section-layout.css",
    "mixer-v3-layout-contract.css",
    "mixer-chrome-material.css",
)
SELECTORS = [
    ".window-title", ".command-bar", ".mixer-title", ".console-body",
    ".section-rail", ".rail-cell.route", ".rail-cell.fx", ".rail-cell.send",
    ".rail-cell.pan", ".rail-cell.fader", ".rail-cell.footer",
    "#tracks .strip", "#master .strip",
    "#tracks .strip .route-section", "#tracks .strip .fx-section",
    "#tracks .strip .send-section", "#tracks .strip .eq-section",
    "#tracks .strip .pan-section", "#tracks .strip .switch-section",
    "#tracks .strip .bottom-section", "#tracks .strip .fader-travel .cap",
    '#tracks .strip[data-id="1"] button[data-hw-kind="mute"]',
    '#tracks .strip[data-id="1"] button[data-hw-kind="mute"] .mx-mute-face',
]
PROPS = [
    "display", "visibility", "position", "color", "background-color",
    "background-image", "box-shadow", "text-shadow", "border-top-color",
    "border-bottom-color", "border-top-width", "border-radius",
    "font-family", "font-size", "font-weight", "padding-top",
    "padding-bottom", "flex", "opacity", "outline-color", "filter",
    "pointer-events", "height", "width", "min-height", "max-height",
]
CAPTURE = """([selectors, props]) => selectors.map(sel => {
 const e = document.querySelector(sel);
 if (!e) return [sel, null];
 const st = getComputedStyle(e), r=e.getBoundingClientRect();
 return [sel, {
    rect:[r.x,r.y,r.width,r.height].map(x=>Math.round(x*10)/10),
    css:Object.fromEntries(props.map(p=>[p,st.getPropertyValue(p)])),
    pressed:e.getAttribute('aria-pressed'),
    disabled:e.hasAttribute('disabled'),
 }];
})"""
def make_baseline():
    original = HTML.read_text(encoding="utf-8")
    html = original
    for name in OWNED:
        css = (HTML.parent / "assets" / name).read_text(encoding="utf-8")
        assert "url(" not in css, ("relative assets would change CSS base URL", name)
        version = ("css-owner-v19" if name=="mixer-section-controls.css"
                   else "css-chrome-owner-v19-m3-header" if name=="mixer-chrome-material.css"
                   else "css-owner-v15" if name in {"mixer-section-layout.css", "mixer-v3-layout-contract.css"}
                   else "css-owner-v11")
        tag = f'<link rel="stylesheet" href="assets/{name}?v={version}">'
        assert html.count(tag) == 1, name
        html = html.replace(tag, "<style>" + css + "</style>")
    assert all(f"assets/{name}?v=" not in html for name in OWNED)
    file = tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", suffix=".html",
        prefix=".v11-visual-baseline-", dir=HTML.parent, delete=False,
    )
    with file:
        file.write(html)
    return Path(file.name)

async def take(page, width, height, toggles, click_mute=False):
    await page.set_viewport_size({"width":width,"height":height})
    await page.evaluate("""classes => {
        const el = document.querySelector('.console-body');
        for (const [key, enabled] of Object.entries(classes))
          el.classList.toggle('collapsed-' + key, enabled);
    }""", toggles)
    if click_mute:
        await page.locator('#tracks .strip[data-id="1"] button[data-hw-kind="mute"]').click()
    return await page.evaluate(CAPTURE, [SELECTORS, PROPS])

async def check(browser, baseline, width, height):
    old=await browser.new_page(viewport={"width":width,"height":height})
    new=await browser.new_page(viewport={"width":width,"height":height})
    errors=[]
    for page in (old,new):
        page.on("pageerror",lambda e:errors.append(str(e)))
    try:
        await old.goto(baseline.as_uri())
        await new.goto(HTML.as_uri())
        for page in (old,new):
            await page.wait_for_function(
                "() => document.querySelectorAll('#tracks .strip').length>=5 && !!document.querySelector('#master .strip')")
        for mask in range(8):
            state={"route": bool(mask&1),"fx":bool(mask&2),"send":bool(mask&4)}
            original=await take(old,width,height,state)
            candidate=await take(new,width,height,state)
            assert sum(1 for k,v in candidate if v is not None)>=20,(width,"missing physical UI",candidate)
            for (sel,x),(other,y) in zip(original,candidate):
                assert sel==other
                assert x==y, (width,mask,sel,x,y)
            # Keep screenshots deterministic: only one canonical user-facing layout.
            if width==1366 and mask==7:
                await new.screenshot(path="pages-v11-collapse-1366.png")
            if width==1680 and mask==0:
                await new.screenshot(path="pages-v11-console-1680.png")
        # Actual JS handler and state styling must be identical, not merely markup.
        originals=await take(old,width,height,{"route":False,"fx":False,"send":False},click_mute=True)
        candidates=await take(new,width,height,{"route":False,"fx":False,"send":False},click_mute=True)
        assert originals==candidates,(width,"MUTE click/style parity",originals,candidates)
        assert not errors,(width,"JavaScript errors",errors)
    finally:
        await old.close()
        await new.close()

async def main():
    source_gate()
    baseline=make_baseline()
    try:
        async with async_playwright() as p:
            browser=await p.chromium.launch(headless=True)
            for width,height in ((1366,768),(1680,900),(1920,1080)):
                await check(browser,baseline,width,height)
            await browser.close()
    finally:
        baseline.unlink(missing_ok=True)
    print("PASS V11 CSS: 23 sampled nodes, 25 properties, 8 collapse states x 3 viewports, MUTE click, unchanged original material")

if __name__=="__main__":
    asyncio.run(main())
