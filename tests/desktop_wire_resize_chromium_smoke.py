#!/usr/bin/env python3
"""Physical WIRE endpoint accuracy across Windows-like viewport resize sequences.

The exact generated /preview/ page is exercised in real Chromium. No mocked
routing or PCM authority, no screenshot-only judgement and no idle polling.
"""
from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

from playwright.sync_api import sync_playwright
from approved_publication_fixture import published_console

ROOT = Path(__file__).resolve().parents[1]
NUMBER = re.compile(r"-?\d+(?:\.\d+)?")


def center(locator):
    b = locator.bounding_box()
    assert b and b["width"] > 0 and b["height"] > 0, ("missing socket", b)
    return b["x"] + b["width"] / 2, b["y"] + b["height"] / 2


def path_points(locator):
    d = locator.get_attribute("d") or ""
    numbers = [float(v) for v in NUMBER.findall(d)]
    assert len(numbers) >= 8 and " C" in d, ("no committed cubic WIRE path", d)
    return (numbers[0], numbers[1]), (numbers[-2], numbers[-1])


def assert_pinned(page, width):
    vp = page.locator("#viewport").bounding_box()
    assert vp and vp["width"] > 0, (width, "viewport missing", vp)
    src = page.locator('#tracks .strip[data-id="5"] .wire-node-out .wire-pin')
    dst = page.locator('#tracks .strip[data-id="3"] .wire-node-in .wire-arrow')
    cable = page.locator('#wireLayer path.wire-outgoing-line[data-from="5"][data-to="3"]')
    assert cable.count() == 1, (width, "committed WIRE disappeared", cable.count())
    source, dest = path_points(cable)
    pin = center(src)
    socket = center(dst)
    actual = ((pin[0] - vp["x"], pin[1] - vp["y"]),
              (socket[0] - vp["x"], socket[1] - vp["y"]))
    for label, got, expect in zip(("OUT", "IN"), (source, dest), actual):
        assert abs(got[0] - expect[0]) < .6 and abs(got[1] - expect[1]) < .6, (
            width, label, "stale cable endpoint after resize", got, expect)
    assert page.locator('#tracks .strip[data-id="3"] .wire-node-in').get_attribute(
        "data-wire-arrival") == "true", (width, "accepted WIRE arrival lost")

    # A MAIN fragment is a local SVG drawn in its own send-section coordinate
    # system. It must follow the real pin after height changes without
    # depending on an edge-ID change.
    main = page.locator('#tracks .strip[data-id="5"] .wire-main-fragment path.wire-main-jacket')
    if main.count():
        p0, _ = path_points(main)
        section = page.locator('#tracks .strip[data-id="5"] .send-section.wire-section')
        pin = center(section.locator('.wire-node-out .wire-pin'))
        rect = section.bounding_box()
        assert abs(p0[0] - (pin[0] - rect["x"])) < .65 and abs(
            p0[1] - (pin[1] - rect["y"])) < .65, (
                width, "MAIN mini cable detached from physical pin", p0, pin, rect)


def main():
    with tempfile.TemporaryDirectory(prefix="arsonmix-caption-wire-") as td:
        dist = Path(td) / "dist"
        subprocess.run([sys.executable, str(ROOT / "scripts/build_pages.py"),
                        "--out", str(dist), "--sha", "caption-wire-resize",
                        "--branch", "native-caption"], cwd=ROOT,
                       text=True, capture_output=True, check=True)
        public = published_console(dist)
        with sync_playwright() as pw:
            browser = pw.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": 1680, "height": 900})
            errors = []
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(public.as_uri())
            page.locator('#tracks .strip[data-id="5"] .strip-identity-top').click()
            body = page.locator(".console-body")
            if "collapsed-send" in (body.get_attribute("class") or ""):
                page.locator('.section-rail [data-section="send"]').click()

            src = page.locator('#tracks .strip[data-id="5"] .wire-node-out .wire-pin')
            dst = page.locator('#tracks .strip[data-id="3"] .wire-node-in .wire-arrow')
            x0, y0 = center(src)
            x1, y1 = center(dst)
            page.mouse.move(x0, y0)
            page.mouse.down()
            page.mouse.move((x0+x1)/2, y0+52, steps=7)
            page.mouse.move(x1, y1, steps=7)
            page.mouse.up()
            page.mouse.move(x0, y0-65)
            page.wait_for_function("""()=>!!document.querySelector(
                '#wireLayer path.wire-outgoing-line[data-from="5"][data-to="3"]')""")
            # Exercise both within-bank resize and actual bank-capacity change.
            for width, height in ((1680, 900), (1366, 900), (1920, 1080),
                                  (1280, 760), (1680, 820)):
                page.set_viewport_size({"width": width, "height": height})
                page.wait_for_function("""()=> {
                    const svg=document.querySelector('#wireLayer');
                    const vp=document.querySelector('#viewport');
                    return !!svg&&!!vp&&svg.viewBox.baseVal.width===vp.getBoundingClientRect().width;
                }""")
                assert_pinned(page, width)

            # Start an OUT gesture, then force a bank remount via narrow resize.
            # The pointer-capture owner gets detached; the gesture must be
            # cancelled fail-closed, not turned into an unintended route.
            before = page.locator('#tracks .strip[data-id="2"] .wire-node-in').get_attribute(
                "data-wire-arrival")
            loose = page.locator('#tracks .strip[data-id="6"] .wire-node-out .wire-pin')
            x, y = center(loose)
            page.mouse.move(x, y)
            page.mouse.down()
            page.mouse.move(x+35, y+32, steps=5)
            assert "wire-dragging" in (body.get_attribute("class") or ""), (
                "test never activated a cable drag")
            page.set_viewport_size({"width": 1100, "height": 760})
            page.wait_for_function("""()=>Number(document.querySelector('.console-body')
                ?.style.getPropertyValue('--bank-capacity')) < 6""")
            page.mouse.up()
            assert "wire-dragging" not in (body.get_attribute("class") or ""), (
                "orphaned WIRE drag after remount")
            assert page.locator('#wireLayer path.wire-drag-line').count() == 0, (
                "stale cable survived structural resize")
            after = page.locator('#tracks .strip[data-id="2"] .wire-node-in').get_attribute(
                "data-wire-arrival")
            assert before == after, ("resize unexpectedly connected CH06 to CH02", before, after)
            assert not errors, ("JavaScript errors during restore/resize", errors)
            page.close()
            browser.close()
    print("PASS desktop WIRE: pin-accurate resize, safe bank remount, no phantom route")


if __name__ == "__main__":
    main()
