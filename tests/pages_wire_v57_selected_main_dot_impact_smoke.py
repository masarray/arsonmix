#!/usr/bin/env python3
"""V57: focused FL-style WIRE, exact MAIN inner dot, custom OUT and full-span impact.

Browser control-plane fixture only; no claim of native PCM/audio routing.
"""
from math import hypot
from pathlib import Path
import re
from playwright.sync_api import sync_playwright

PAGE = Path(__file__).resolve().parents[1] / "site/preview-v2/index.html"
NUM = re.compile(r"[-+]?[0-9]+(?:[.][0-9]+)?")


def numbers(path):
    return [float(x) for x in NUM.findall(path)]


def displacement(jolt, cubic):
    """Normal displacement of the 56 impact samples from the original cubic."""
    x0, y0, cx0, cy0, cx1, cy1, x1, y1 = numbers(cubic)
    p = numbers(jolt)
    assert len(p) == 114, ("impact must reach the OUT endpoint", len(p))
    assert hypot(p[0] - x0, p[1] - y0) < .1
    assert hypot(p[-2] - x1, p[-1] - y1) < .1
    d = []
    for i in range(1, 57):
        t = i / 56
        u = 1 - t
        bx = u**3*x0 + 3*u*u*t*cx0 + 3*u*t*t*cx1 + t**3*x1
        by = u**3*y0 + 3*u*u*t*cy0 + 3*u*t*t*cy1 + t**3*y1
        d.append(hypot(p[2*i] - bx, p[2*i+1] - by))
    return d


def select(page, channel):
    page.locator(f'#tracks .strip[data-id="{channel}"] .strip-identity-top').click()


def inspect(page, channel):
    return page.locator(f'#tracks .strip[data-id="{channel}"] .wire-node-out').evaluate(
        """el => ({
          main:el.getAttribute('data-wire-main-edge'),
          display:el.getAttribute('data-wire-display-focus'),
          custom:el.getAttribute('data-wire-connected'),
          pin:getComputedStyle(el.querySelector('.wire-pin')).backgroundColor
        })""")


def run():
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        for width, height in ((1366, 768), (1680, 900), (1920, 1080)):
            page = browser.new_page(viewport={"width": width, "height": height})
            errors = []
            page.on("pageerror", lambda exc: errors.append(str(exc)))
            page.goto(PAGE.as_uri())
            body = page.locator(".console-body")
            # MAIN is a graph edge, NOT a permanent visual wire. Folded
            # source/MAIN summaries must never draw physical wire fragments.
            assert "collapsed-send" in (body.get_attribute("class") or ""), width
            for ch in (1, 5, 6):
                folded = page.locator(f'#tracks .strip[data-id="{ch}"] .wire-fold-summary')
                assert folded.get_attribute("data-wire-main-edge") is None, (width, ch, "ghost MAIN edge")
                assert folded.evaluate(
                    "el=>getComputedStyle(el,'::after').content") in ("none", "normal"), (
                    width, ch, "folded MAIN stem visible")
                assert "has-out" not in (folded.locator(".fold-link-icon").get_attribute("class") or "")
            master_fold = page.locator("#master .wire-fold-summary")
            assert master_fold.get_attribute("data-wire-main-edge") is None
            assert master_fold.evaluate(
                "el=>getComputedStyle(el,'::after').content") in ("none", "normal")
            select(page, 5)
            assert page.locator('#wireLayer path.wire-outgoing-line,#wireLayer path.wire-incoming-line').count() == 0
            assert page.locator('#tracks .strip[data-id="5"] .wire-fold-summary').evaluate(
                "el=>getComputedStyle(el,'::after').content") in ("none", "normal")
            for density in ("comfort", "standard", "compact"):
                page.locator("#densitySelect").select_option(density)
                if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
                    page.locator('.section-rail [data-section="send"]').click()
                # CH→MAIN graph edges stay even when their physical wires are hidden.
                assert inspect(page, 5)["main"] and inspect(page, 6)["main"]
                assert inspect(page, 5)["custom"] is None and inspect(page, 6)["custom"] is None
                select(page, 5)
                a = inspect(page, 5)
                assert a["pin"] == "rgb(75, 83, 91)", (
                    width, density, "default MAIN falsely illuminated OUT", a)
                assert a["display"] == "true", (width, density, a)
                assert inspect(page, 6)["display"] is None
                master = page.locator("#master .wire-node-in")
                assert master.get_attribute("data-wire-main-edge") == a["main"]
                dot = master.locator(".wire-arrow")
                anchor = dot.evaluate("""el => ({
                  content:getComputedStyle(el,'::after').content,
                  height:getComputedStyle(el,'::after').height,
                  position:getComputedStyle(el,'::after').position
                })""")
                assert anchor["content"] not in ("none", "normal"), (width, density, anchor)
                assert anchor["position"] == "absolute", (width, density, anchor)
                assert anchor["height"] == "15px", (width, density, anchor)
                outer_box, dot_box = master.bounding_box(), dot.bounding_box()
                assert outer_box and dot_box
                assert hypot((outer_box["x"]+outer_box["width"]/2)-(dot_box["x"]+dot_box["width"]/2),
                             (outer_box["y"]+outer_box["height"]/2)-(dot_box["y"]+dot_box["height"]/2)) < .4
                select(page, 6)
                assert inspect(page, 5)["display"] is None
                assert inspect(page, 6)["display"] == "true"
                assert master.get_attribute("data-wire-main-edge") == inspect(page, 6)["main"]
                assert page.locator("#wireLayer path.wire-outgoing-line,#wireLayer path.wire-incoming-line").count() == 0
            # The renderer only shows wires related to the selected channel.
            page.locator("#densitySelect").select_option("comfort")
            if "collapsed-send" in (page.locator(".console-body").get_attribute("class") or ""):
                page.locator('.section-rail [data-section="send"]').click()
            if width == 1680:
                # Pointerdown on destination changes selected channel to CH3;
                # the accepted 5->3 route is correctly displayed as INCOMING.
                # Observe that actual rendered SVG, never a detached OUT node.
                page.evaluate("""() => {
                  window.__v57Impact = [];
                  new MutationObserver(records => {
                    for (const r of records) {
                      const path = r.target;
                      if (r.type !== 'attributes' ||
                          !path.matches?.('path.wire-incoming-line[data-cable-key="5-3"]')) continue;
                      const d = path.getAttribute('d');
                      if (d?.includes(' L ') && window.__v57Impact.at(-1) !== d)
                        window.__v57Impact.push(d);
                    }
                  }).observe(document.querySelector('#wireLayer'), {
                    subtree:true, attributes:true, attributeFilter:['d']
                  });
                }""")
            page.locator('#tracks .strip[data-id="5"] .wire-node-out').click()
            page.locator('#tracks .strip[data-id="3"] .wire-node-in').click()
            if width == 1680:
                # A finite 1.6 s animation: inspect the real final DOM, not
                # an inferred timeline / unexposed JavaScript renderer symbol.
                page.wait_for_timeout(1950)
                captured = page.evaluate("""() => ({
                  frames:window.__v57Impact,
                  base:document.querySelector('#wireLayer path.wire-incoming-line[data-cable-key="5-3"]')?.getAttribute('d')
                })""")
                assert len(captured["frames"]) >= 8, (
                    "impact not repainted over multiple frames", width, len(captured["frames"]), captured["base"])
                assert captured["base"] and " C " in captured["base"], (
                    "animated curve did not settle to cubic", width, len(captured["frames"]), captured["base"])
                early = displacement(captured["frames"][0], captured["base"])
                late = displacement(captured["frames"][-1], captured["base"])
                assert max(early[42:53]) > 6, ("weak receiver impact", max(early))
                assert max(late[2:12]) > 1.25, ("wave stops before OUT", max(late[:12]))
                assert max(late[2:12]) < max(early[42:53])*.6, "wave fails to fade towards OUT"
            assert inspect(page, 5)["custom"] == "true"
            original_main = inspect(page, 5)["main"]
            select(page, 6)
            assert page.locator('#wireLayer path.wire-incoming-line,#wireLayer path.wire-outgoing-line,#wireLayer path.wire-context-line').count() == 0
            assert inspect(page, 5)["main"] == original_main
            assert inspect(page, 5)["custom"] == "true"
            assert inspect(page, 5)["pin"] != "rgb(75, 83, 91)", "real non-MAIN OUT should stay colored"
            select(page, 3)
            assert page.locator('#wireLayer path.wire-incoming-line[data-from="5"][data-to="3"]').count() == 1
            assert page.locator('#wireLayer path.wire-context-line').count() == 0
            page.locator("#wireConfigToggle").click()
            options = page.locator("#wireView option").evaluate_all("(els)=>els.map(el=>el.value)")
            assert options == ["focus", "out"], (width, options)
            page.locator("#wireView").select_option("out")
            assert page.locator('#wireLayer path.wire-incoming-line').count() == 0
            page.locator("#wireView").select_option("focus")
            assert page.locator('#wireLayer path.wire-incoming-line[data-from="5"][data-to="3"]').count() == 1
            page.locator("#wireConfigClose").click()
            # Custom route 5→3 lights only its own OUT and IN folded dots.
            # Default MAIN must never create a colored OUT or ghost line.
            page.locator('.section-rail [data-section="send"]').click()
            assert "collapsed-send" in (body.get_attribute("class") or ""), width
            for ch in (3, 5, 6):
                folded = page.locator(f'#tracks .strip[data-id="{ch}"] .wire-fold-summary')
                assert folded.get_attribute("data-wire-main-edge") is None
                assert folded.evaluate(
                    "el=>getComputedStyle(el,'::after').content") in ("none", "normal")
                active = folded.locator(".fold-link-icon").get_attribute("class") or ""
                assert ("has-out" in active) == (ch == 5), (width, ch, active)
            assert "has-in" in (page.locator('#tracks .strip[data-id="3"] .fold-link-icon').get_attribute("class") or "")
            dark_out = page.locator('#tracks .strip[data-id="3"] .fold-link-out').evaluate(
                "el=>getComputedStyle(el).fill")
            assert dark_out == "rgb(50, 73, 86)", (width, "receiver MAIN-only OUT lit", dark_out)
            master_fold = page.locator("#master .wire-fold-summary")
            assert master_fold.get_attribute("data-wire-main-edge") is None
            assert master_fold.evaluate(
                "el=>getComputedStyle(el,'::after').content") in ("none", "normal")
            assert page.locator('#wireLayer path.wire-incoming-line,#wireLayer path.wire-outgoing-line').count() == 0
            if width == 1680:
                page.screenshot(path=str(PAGE.parents[2] / "pages-wire-v58-collapsed-1680.png"))
            page.locator('.section-rail [data-section="send"]').click()
            assert page.locator('#wireLayer path.wire-incoming-line[data-from="5"][data-to="3"]').count() == 1
            assert not errors, (width, errors)
            if width == 1680:
                page.screenshot(path=str(PAGE.parents[2] / "pages-wire-v57-selected-1680.png"))
            page.close()
        browser.close()
    print("PASS V58: folded MAIN quiet, graphite MAIN-only OUT, focused wire tracing, custom LEDs and damped impact, 3 widths/3 densities")


if __name__ == "__main__":
    run()
