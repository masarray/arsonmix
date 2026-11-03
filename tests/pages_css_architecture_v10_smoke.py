"""V10: reconstruct exact cascade order from owned source, then inspect real Chromium."""
import asyncio
import re
from pathlib import Path
from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parents[1]
HTML = ROOT / "site/preview-v2/index.html"
OWNERS = (
    "mixer-console-foundation.css",
    "mixer-channel-grid.css",
    "mixer-hardware-artwork.css",
    "mixer-section-controls.css",
    "mixer-section-layout.css",
    "mixer-v3-layout-contract.css",
    "mixer-chrome-material.css",
)
EXPECTED_FNV = "4bd22884"
EXPECTED_CSS_LENGTH = 133992
EXPECTED_INLINE_LENGTH = 85191

def fingerprint(css):
    h = 2166136261
    for c in css:
        h = ((h ^ ord(c)) * 16777619) & 0xffffffff
    return f"{h:08x}"

def source_gate():
    html = HTML.read_text(encoding="utf-8")
    head = html.split("</head>", 1)[0]
    css_order = re.findall(
        r'<link[^>]+href="assets/([^"]+\.css)\?v=css-owner-v(10|11|14|15)"[^>]*>|(<style>(.*?)</style>)',
        head, re.S)
    names = [n for n, version, tag, body in css_order if n]
    expected = [
        "mixer-console-foundation.css",
        "mixer-channel-grid.css",
        "mixer-section-controls.css",
        "mixer-section-layout.css",
        "mixer-hardware-artwork.css",
        "mixer-v3-layout-contract.css",
        "mixer-chrome-material.css",
    ]
    assert names == expected, names
    styles = [body for n, version, tag, body in css_order if tag]
    assert len(styles) == 3, "V11 stylesheet order or inline structure drift"
    all_css = "".join(
        (HTML.parent / "assets" / n).read_text(encoding="utf-8") if n else body
        for n, version, tag, body in css_order
    )
    assert len(all_css) == EXPECTED_CSS_LENGTH, "CSS ownership source bytes missing"
    assert fingerprint(all_css) == EXPECTED_FNV, "CSS cascade/content drift"
    assert sum(map(len, styles)) == EXPECTED_INLINE_LENGTH
    assert all_css.count("!important") == 981
    assert sum(body.count("!important") for body in styles) == 763
    original = (HTML.parent / "assets" / OWNERS[0]).read_text(encoding="utf-8")
    grid = (HTML.parent / "assets" / OWNERS[1]).read_text(encoding="utf-8")
    artwork = (HTML.parent / "assets" / OWNERS[2]).read_text(encoding="utf-8")
    assert ":root{" in original and ".command-bar{" in original
    assert ".console-body{" in grid and ".section-rail{" in grid
    assert all("--ari-" + n + ":url(data:image/png" in artwork
               for n in ("silver", "rose", "pan"))
    assert "wireWsCanJoin" in html
    assert "assets/mixer-wire-dots.css?v=v47-css-v16" in html
    assert "assets/inter-embedded.css" in html
    for p in (ROOT / "scripts/build_pages.py", ROOT / ".github/workflows/pages-publish.yml"):
        source = p.read_text(encoding="utf-8")
        for name in (*OWNERS, "mixer-wire-dots.css"):
            assert name in source, (p.name, name, "missing Pages parity manifest")

async def visual(page, width, height):
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    await page.goto(HTML.as_uri())
    await page.wait_for_function(
        "() => document.querySelectorAll('#tracks .strip').length>=5 && !!document.querySelector('#master .strip')")
    data = await page.evaluate("""() => {
      const rect=e=>{const x=e.getBoundingClientRect();return {top:x.top,width:x.width,height:x.height}};
      const strip=document.querySelector('#tracks .strip'),master=document.querySelector('#master .strip');
      const rail=document.querySelector('.section-rail');
      const cap=strip.querySelector('.fader-travel .cap');
      const key=strip.querySelector('.mx-hw-key');
      return {
        links:[...document.styleSheets].filter(x=>x.href).map(x=>x.href),
        title:rect(document.querySelector('.window-title')),
        toolbar:rect(document.querySelector('.command-bar')),
        header:rect(document.querySelector('.mixer-title')),
        strip:rect(strip),master:rect(master),cap:rect(cap),key:rect(key),
        pan:rect(strip.querySelector('.pan-section')),
        railPan:rect(rail.querySelector('.rail-cell.pan')),
        fader:rect(strip.querySelector('.fader-section')),
        railFader:rect(rail.querySelector('.rail-cell.fader')),
        font:getComputedStyle(strip).fontFamily
      };
    }""")
    for name in (*OWNERS, "mixer-wire-dots.css"):
        assert any("/" + name + "?" in uri for uri in data["links"]), (width, name)
    assert 20 <= data["title"]["height"] <= 30, (width, "chrome title", data)
    assert 30 <= data["toolbar"]["height"] <= 45, (width, "toolbar", data)
    assert 20 <= data["header"]["height"] <= 30, (width, "mixer header", data)
    assert abs(data["strip"]["width"] - data["master"]["width"]) < 1.5, (width, "master geometry", data)
    assert abs(data["pan"]["top"] - data["railPan"]["top"]) < 1.6, (width, "PAN rail", data)
    assert abs(data["fader"]["top"] - data["railFader"]["top"]) < 1.6, (width, "fader rail", data)
    assert 21 <= data["cap"]["width"] <= 25 and 45 <= data["cap"]["height"] <= 50, (width, "fader face", data)
    assert abs(data["key"]["height"] - 29) < 1.2, (width, "MUTE key", data)
    assert "Inter" in data["font"], (width, "font", data)
    assert not errors, (width, errors)
    if width in (1366,1680):
        await page.screenshot(path=f"pages-v10-shell-{width}.png")

async def main():
    source_gate()
    async with async_playwright() as p:
        browser = await p.chromium.launch(headless=True)
        for width,height in ((1366,768),(1680,900),(1920,1080)):
            page = await browser.new_page(viewport={"width":width,"height":height})
            await visual(page,width,height)
            await page.close()
        await browser.close()
    print("PASS V10: exact CSS-order fingerprint, Pages manifests, Chromium geometry, Inter, WIRE and keys")

if __name__ == "__main__":
    asyncio.run(main())
