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
        assert "const send=sofEnabled&&t.kind==='audio';" in source, (
            "Fader reset must inspect active SEND context instead of overwriting MAIN")
        assert "nativeGainAvailable===true" in source, (
            "Native input gain should be available only with installed capability")
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
                    assert page.locator(".console-body").evaluate(
                        "el => el.classList.contains('gain-view-active')")
                    rail_state = page.locator(".section-rail .rail-cell.fx").evaluate("""el => ({
                        text: getComputedStyle(el, '::after').content,
                        display: getComputedStyle(el, '::after').display,
                        className: el.className,
                        railParent: el.parentElement.className,
                        styles: [...document.styleSheets].map(s=>s.href || 'inline').slice(0,25)
                    })""")
                    page.screenshot(path=str(ROOT / f"arsonmix-gain-knob-debug-{width}x{height}.png"))
                    assert "GAIN" in rail_state["text"], (width,rail_state)
                    knob_visual = first.locator(".gain-dial")
                    assert knob_visual.count() == 1
                    body_size = knob_visual.bounding_box()
                    assert body_size and 54 <= body_size["width"] <= 78, (width, body_size)
                    cap = first.locator(".gain-dial-face")
                    cap_css = cap.evaluate("""el => ({
                        image: getComputedStyle(el).backgroundImage,
                        transform: getComputedStyle(el).transform,
                        shadow: getComputedStyle(el).boxShadow
                    })""")
                    assert "mixer-gain-knob-brushed-steel.png" in cap_css["image"]
                    assert cap_css["transform"] == "none", cap_css
                    assert cap_css["shadow"] != "none", cap_css
                    arc = first.locator(".gain-dial-arc")
                    assert arc.count() == 1
                    assert "conic-gradient" in arc.evaluate(
                        "el => getComputedStyle(el, '::after').backgroundImage")
                    pointer = first.locator(".gain-dial-pointer")
                    before_turn = pointer.evaluate("el => getComputedStyle(el).transform")
                    lcd_size = first.locator(".gain-readout").bounding_box()
                    assert lcd_size and lcd_size["height"] >= 24, (width, lcd_size)
                    knob = first.locator('[data-field="trim"]')
                    assert knob.count() == 1
                    knob.evaluate("""el => {
                      el.value = '6.5';
                      el.dispatchEvent(new Event('input', {bubbles:true}));
                    }""")
                    assert "+6.5 dB" in first.locator(".gain-readout").inner_text()
                    assert "6.5" in knob.get_attribute("value") or knob.input_value() == "6.5"
                    assert pointer.evaluate("el => getComputedStyle(el).transform") != before_turn
                    assert cap.evaluate("el => getComputedStyle(el).transform") == "none"
                    # Double-click must use the same gain parameter path and restore unity.
                    first.locator(".gain-dial").dblclick()
                    # The public formatter reserves '+' for strictly
                    # positive values; gain unity is displayed as 0.0 dB.
                    assert first.locator(".gain-readout").inner_text().strip() == "0.0 dB"
                    assert knob.input_value() == "0"
                    knob.evaluate("""el => {
                      el.value = '6.5';
                      el.dispatchEvent(new Event('input', {bubbles:true}));
                    }""")

                    fx.click()
                    assert not page.locator(".console-body").evaluate(
                        "el => el.classList.contains('gain-view-active')")
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

                    # SOF target is a stable strip ID, never a BUS display name.
                    target_select = page.locator("#sofTarget")
                    initial_id = target_select.input_value()
                    assert initial_id.isdigit(), (width, initial_id)
                    expected_name = target_select.locator("option:checked").inner_text()
                    assert page.locator("#mixContext").inner_text() == "SEND MIX → " + expected_name
                    assert first.locator(".mx-sof-on-key").get_attribute("aria-pressed") == "false", (
                        "An absent SEND is not secretly ON")

                    # Returning to MAIN must keep FX/GAIN selection and actual fader mix.
                    page.locator("#sofToggle").click()
                    assert page.locator("#mixContext").inner_text() == "MAIN MIX"
                    gain.click()
                    assert "+6.5 dB" in first.locator(".gain-readout").inner_text()

                    # Rename the selected BUS in the native-style identity card.
                    # SOF selection must follow its ID, not jump back to Bus A.
                    # VIEW & SELECTION was deliberately moved under the
                    # compact More menu. Exercise its visible operator path;
                    # do not force a hidden <select> via direct DOM mutation.
                    def select_visible_channel_filter(value):
                        menu = page.locator("#moreTools")
                        choice = page.locator("#viewFilter")
                        if not choice.is_visible():
                            menu.locator("summary").click()
                        assert choice.is_visible(), "More menu must reveal channel filter"
                        choice.select_option(value)
                        if menu.evaluate("el=>el.open"):
                            menu.locator("summary").click()

                    select_visible_channel_filter("bus")
                    selected_bus = page.locator('#tracks .strip[data-id="' + initial_id + '"]')
                    assert selected_bus.count() == 1, (width, initial_id)
                    selected_bus.locator(".strip-identity-top").dblclick()
                    page.locator("#identityName").fill("STAGE MONITOR A")
                    page.locator("#identitySave").click()
                    assert target_select.input_value() == initial_id
                    assert target_select.locator("option:checked").inner_text() == "STAGE MONITOR A"
                    select_visible_channel_filter("all")
                    page.locator("#sofToggle").click()
                    assert page.locator("#mixContext").inner_text() == "SEND MIX → STAGE MONITOR A"
                    assert first.locator(".is-sof-view").get_attribute("data-sof-target") == initial_id
                    page.locator("#sofToggle").click()
                    gain.click()

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
                    # Actual assembled /preview/ screenshot: one GAIN bank per viewport,
                    # with static steel caps, green needles/arcs and the left GAIN rail.
                    page.screenshot(
                        path=str(ROOT / f"arsonmix-gain-knob-{width}x{height}.png"),
                        full_page=False,
                    )
                    page.close()
            finally:
                browser.close()
    print("PASS SOF contextual strip view, PNG, WIRE guard, Gain/FX interlock at 3 resolutions")


if __name__ == "__main__":
    run()
