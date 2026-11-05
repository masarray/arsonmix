"""Stage C: compare compiled and original CSS in the EXACT approved /preview/ DOM.

The baseline is the byte-for-byte accepted CSS before optimization, with the
same approved PNG buttons, JS, font, layout and hardware-neutral demo.
This is a differential regression test, NOT physical Windows GPU FAT.
"""
import asyncio
from pathlib import Path
import shutil
import tempfile
from playwright.async_api import async_playwright
from approved_css_original_bundle_fixture import reconstructed_original_css_html

ROOT = Path(__file__).resolve().parents[1]
PRODUCTION = ROOT / "dist-pages" / "preview"
SHEETS = (
    "mixer-console-foundation.css", "mixer-channel-grid.css",
    "mixer-section-controls.css", "mixer-section-layout.css",
    "mixer-hardware-artwork.css", "mixer-v3-layout-contract.css",
    "mixer-chrome-material.css", "mixer-control-keys.css",
    "mixer-fx-rack.css", "mixer-plugin-browser.css",
    "mixer-pan-controls.css", "mixer-fader-assembly.css",
    "mixer-meter-display.css", "mixer-utility-controls.css",
    "inter-embedded.css", "mixer-wire-dots.css",
    "mixer-wire-out-align-v48.css", "mixer-contextual-buttons.css",
)
PROPERTIES = (
    "display", "position", "visibility", "opacity", "width", "height",
    "min-width", "min-height", "max-width", "max-height", "top", "left",
    "right", "bottom", "padding", "margin", "flex", "flex-basis",
    "grid-template-columns", "grid-template-rows", "gap", "overflow",
    "background", "background-image", "background-color", "color",
    "border", "border-color", "border-width", "border-radius",
    "box-shadow", "text-shadow", "filter", "transform", "transition",
    "clip-path", "font", "font-size", "font-weight", "line-height",
    "justify-content", "align-items", "pointer-events", "cursor",
    "--lit", "--peak-extent", "--meter-led-ramp", "--track-color",
    "--identity-h", "--meter-pitch", "--ch",
)
SAMPLE = (
    ".app", ".window-title", ".command-bar",
    ".console-body", ".section-rail", ".tracks-viewport",
    ".strip", ".strip *", ".wire-tray", ".wire-node-out",
    ".wire-node-in", "#wireWorkspace", ".more-tools-panel",
)


async def snapshot(page):
    return await page.evaluate("""({selectors, properties}) => {
        const elements = Array.from(document.querySelectorAll(selectors.join(',')));
        const rows = elements.map((el, index) => {
            const c = getComputedStyle(el);
            const rect = el.getBoundingClientRect();
            // Compare computed values, not deployment-specific file:// roots.
            // Preserve every filename, query string, color and dimension.
            const normalized = value => value.replace(
                /file:\/\/\/[^"')]*\/assets\//g,'assets/');
            const values = properties.map(p => normalized(c.getPropertyValue(p)));
            const pseudo = ['::before','::after'].map(part => {
                const s = getComputedStyle(el,part);
                return [s.content,s.display,s.opacity,s.filter,s.transform,
                        s.backgroundImage,s.backgroundColor,s.boxShadow].map(normalized);
            });
            return {
                tag:el.tagName,cls:typeof el.className==='string'?el.className:'',
                id:el.id||'',index,values,pseudo,
                rect:[rect.x,rect.y,rect.width,rect.height]
                    .map(x => Math.round(x*100)/100)
            };
        });
        return rows;
    }""", {"selectors": list(SAMPLE), "properties": list(PROPERTIES)})


async def inspect_page(browser, url, width, height):
    page = await browser.new_page(viewport={"width": width, "height": height})
    errors = []
    page.on("pageerror", lambda err: errors.append(str(err)))
    try:
        await page.goto(url)
        await page.wait_for_selector("#tracks .strip[data-id='1']")
        await page.evaluate("document.fonts.ready")
        samples = {}
        samples["idle"] = await snapshot(page)
        # Hover is not optional; passive hover previously caused user stutter.
        identity = page.locator("#tracks .strip[data-id='1'] .strip-identity-top")
        if await identity.count():
            await identity.hover()
            samples["identity-hover"] = await snapshot(page)
        key = page.locator("#tracks .strip[data-id='1'] .mx-hw-key[data-hw-kind='mute']")
        if await key.count():
            await key.hover()
            samples["hardware-key-hover"] = await snapshot(page)
            await key.click()
            samples["mute-selected"] = await snapshot(page)
        return samples, errors
    finally:
        await page.close()


async def main():
    assert PRODUCTION.is_dir(), "Stage C requires approved Pages assembled first"
    with tempfile.TemporaryDirectory(prefix="armix-css-c-baseline-") as tmp:
        before = Path(tmp) / "approved-original-css"
        shutil.copytree(PRODUCTION, before)
        original_html=reconstructed_original_css_html((before/'index.html').read_text(encoding='utf-8'))
        (before/'index.html').write_text(original_html,encoding='utf-8')
        for name in SHEETS:
            source = (ROOT / ("site/preview-buttons/assets"
                              if name == "mixer-control-keys.css"
                              else "site/preview-v2/assets") / name)
            shutil.copy2(source, before / "assets" / name)
        async with async_playwright() as playwright:
            browser = await playwright.chromium.launch(headless=True)
            try:
                for width, height in [(1366,768),(1680,900),(1920,1080)]:
                    original, e1 = await inspect_page(
                        browser, (before/"index.html").as_uri(), width, height)
                    compiled, e2 = await inspect_page(
                        browser, (PRODUCTION/"index.html").as_uri(), width, height)
                    assert not e1 and not e2, (e1,e2)
                    assert original.keys() == compiled.keys()
                    for state in original:
                        left, right = original[state], compiled[state]
                        assert len(left)==len(right), (
                            width,height,state,"DOM node count",len(left),len(right))
                        for i,(a,b) in enumerate(zip(left,right)):
                            assert a==b, (
                                "CSS COMPUTED-STYLE/GEOMETRY REGRESSION",
                                width,height,state,i,
                                {k:a[k] for k in ("tag","cls","id")},
                                {k:(a[k],b[k]) for k in a if a[k]!=b[k]})
                    print("PASS approved compiled CSS parity",width,height,
                          "states",len(original),"nodes",len(original["idle"]))
            finally:
                await browser.close()

if __name__ == "__main__":
    asyncio.run(main())
