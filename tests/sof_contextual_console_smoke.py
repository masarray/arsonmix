"""Contextual Gain/FX Rack and Sends-on-Faders public GUI smoke.

Runs against the single *assembled* /preview/ renderer. All control checks
below are browser-reference only, not claims of native ASIO/PCM admission.
No direct engine/graph mutation in this fixture.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]


def run() -> None:
    with tempfile.TemporaryDirectory(prefix="arsonmix-sof-context-") as tmp:
        dist = Path(tmp) / "pages"
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts/build_pages.py"),
                "--out", str(dist),
                "--sha", "sof-context-test",
                "--branch", "gui-test",
            ],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        html = dist / "preview" / "index.html"
        source = html.read_text(encoding="utf-8")
        assert 'id="gainView"' in source and 'id="fxView"' in source
        assert 'id="mixContext"' in source
        assert "Never auto-patch or remove a WIRE at minimum send level." in source
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            try:
                for width, height in ((1366, 768), (1680, 900), (1920, 1080)):
                    page = browser.new_page(viewport={"width": width, "height": height})
                    errors = []
                    page.on("pageerror", lambda err: errors.append(str(err)))
                    page.goto(html.as_uri())
                    first = page.locator('#tracks .strip[data-id="1"]')
                    first.wait_for(timeout=15000)
                    gain = page.locator("#gainView")
                    fx = page.locator("#fxView")
                    assert gain.get_attribute("aria-pressed") == "false"
                    assert fx.get_attribute("aria-pressed") == "true"
                    assert page.locator("#mixContext").inner_text() == "MAIN MIX"

                    # The same cell swaps only its presentation, not FX state.
                    gain.click()
                    assert gain.get_attribute("aria-pressed") == "true"
                    assert first.locator(".is-gain-view").count() == 1
                    knob = first.locator('[data-field="trim"]')
                    assert knob.count() == 1
                    knob.evaluate("""el => {
                      el.value = '6.5';
                      el.dispatchEvent(new Event('input', {bubbles:true}));
                    }""")
                    assert "+6.5 dB" in first.locator(".gain-readout").inner_text()
                    assert "6.5" in knob.get_attribute("value") or knob.input_value() == "6.5"

                    fx.click()
                    assert first.locator(".is-gain-view").count() == 0
                    assert first.locator(".fxslot-list").count() == 1

                    # Enter SENDS: the approved strip and pinned master survive.
                    page.locator("#sofToggle").click()
                    assert page.locator("#mixContext").inner_text().startswith("SEND MIX")
                    assert gain.is_disabled() and fx.is_disabled()
                    assert first.locator(".is-sof-view").count() == 1
                    assert first.locator(".mx-sof-tap-key").count() == 1
                    assert first.locator('button[data-hw-kind="tap"]').count() == 0
                    assert first.locator('button[data-hw-kind="mute"]').count() == 1
                    assert first.locator('button[data-hw-kind="solo"]').count() == 1
                    assert first.locator(".fader").is_disabled(), (
                        "A missing WIRE must not turn a fader gesture into auto-patch")
                    assert page.locator("#master .strip").count() == 1
                    assert page.locator("#wireLayer").count() == 1

                    # SENDS mode exposes approved raster, not a fake CSS key.
                    tap = first.locator(".mx-sof-tap-key")
                    sprite = tap.evaluate(
                        "el => getComputedStyle(el, '::before').backgroundImage")
                    assert "harmonice-thin-flat-v9-approved-off-shadow-57x30.png" in sprite, sprite
                    assert tap.is_disabled()

                    # Keep FX slots/trim values when returning from SENDS.
                    page.locator("#sofToggle").click()
                    assert page.locator("#mixContext").inner_text() == "MAIN MIX"
                    gain.click()
                    assert "+6.5 dB" in first.locator(".gain-readout").inner_text()

                    # Every visible cell remains in its strip bounds.
                    geometry = first.locator(".gain-view-body").evaluate("""el => {
                      const a=el.getBoundingClientRect(),
                            b=el.closest('.fx-section').getBoundingClientRect();
                      return {left:a.left, right:a.right, top:a.top, bottom:a.bottom,
                        sectionLeft:b.left, sectionRight:b.right,
                        sectionTop:b.top, sectionBottom:b.bottom};
                    }""")
                    assert geometry["left"] >= geometry["sectionLeft"] - 2
                    assert geometry["right"] <= geometry["sectionRight"] + 2
                    assert geometry["bottom"] <= geometry["sectionBottom"] + 2
                    assert not errors, (width, errors)
                    page.close()
            finally:
                browser.close()
    print("PASS SOF contextual strip view, PNG, WIRE guard, Gain/FX interlock at 3 resolutions")


if __name__ == "__main__":
    run()
