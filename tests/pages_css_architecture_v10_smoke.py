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
)
EXPECTED_FNV = "39885f8a"
EXPECTED_CSS_LENGTH = 186176
EXPECTED_INLINE_LENGTH = 168504

def fingerprint(css):
    h = 2166136261
    for c in css:
        h = ((h ^ ord(c)) * 16777619) & 0xffffffff
    return f"{h:08x}"

def source_gate():
    html = HTML.read_text(encoding="utf-8")
    head = html.split("</head>", 1)[0]
    styles = re.findall(r"<style(?:\s[^>]*)?>(.*?)</style>", head, re.S)
    assert len(styles) == 2, "source-order split was modified"
    links = re.findall(r'<link[^>]+href="assets/([^"]+)"[^>]*>', head)
    selected = [x.split("?")[0] for x in links if "css-owner-v10" in x]
    assert selected == list(OWNERS), selected
    for name in OWNERS:
        assert links.count(name + "?v=css-owner-v10") == 1, name
    first, second = [head.index("assets/" + n + "?v=css-owner-v10") for n in OWNERS[:2]]
    art = head.index("assets/" + OWNERS[2] + "?v=css-owner-v10")
    style_starts = [m.start() for m in re.finditer("<style>", head)]
    style_ends = [m.end() for m in re.finditer("</style>", head)]
    assert first < second < style_starts[0] < style_ends[0] < art < style_starts[1]
    sheets = [(HTML.parent / "assets" / n).read_text(encoding="utf-8") for n in OWNERS]
    assembled = sheets[0] + sheets[1] + styles[0] + sheets[2] + styles[1]
    assert len(assembled) == EXPECTED_CSS_LENGTH, "source CSS missing after split"
    assert fingerprint(assembled) == EXPECTED_FNV, "CSS cascade or declarations changed"
    assert sum(map(len, styles)) == EXPECTED_INLINE_LENGTH
    assert ":root{" in sheets[0] and ".command-bar{" in sheets[0]
    assert ".console-body{" in sheets[1] and ".section-rail{" in sheets[1]
    assert all("--ari-" + n + ":url(data:image/png" in sheets[2]
               for n in ("silver", "rose", "pan"))
    assert "wireWsCanJoin" in html
    assert "assets/mixer-wire-dots.css?v=v42" in html
    assert "assets/inter-embedded.css" in html
    assert assembled.count("!important") == 1449
    for path in (ROOT / "scripts/build_pages.py", ROOT / ".github/workflows/pages-publish.yml"):
        source = path.read_text(encoding="utf-8")
        for name in (*OWNERS, "mixer-wire-dots.css"):
            assert name in source, (path.name, name, "missing Pages parity manifest")

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
