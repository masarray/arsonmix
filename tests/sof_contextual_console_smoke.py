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
        assert "window.__ARMIX_GAIN_UNITY_DETENT__=gainUnityDetent;" in source
        native_bridge = (ROOT / "web/approved-native-bridge.js").read_text(encoding="utf-8")
        assert "window.__ARMIX_GAIN_UNITY_DETENT__?.(el,event)" in native_bridge
        assert 'querySelector(".gain-lcd-digits")||lcd' in native_bridge
        assert 'querySelector(".gain-lcd-digits")||readout' in native_bridge
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
                    gain_rail = page.locator(".section-rail .rail-cell.fx .rail-gain-caption")
                    assert gain_rail.count() == 1 and gain_rail.is_visible()
                    assert gain_rail.inner_text() == "GAIN"
                    assert gain_rail.evaluate(
                        "el => getComputedStyle(el).writingMode") == "vertical-rl"
                    knob_visual = first.locator(".gain-dial")
                    assert knob_visual.count() == 1
                    body_size = knob_visual.bounding_box()
                    assert body_size and 48 <= body_size["width"] <= 69, (width, body_size)
                    cap = first.locator(".gain-dial-face")
                    cap_css = cap.evaluate("""el => ({
                        image: getComputedStyle(el).backgroundImage,
                        transform: getComputedStyle(el).transform,
                        shadow: getComputedStyle(el).boxShadow
                    })""")
                    assert "mixer-gain-knob-brushed-steel.png" in cap_css["image"]
                    assert cap_css["transform"] == "none", cap_css
                    assert cap_css["shadow"] != "none", cap_css
                    layers = knob_visual.evaluate("""el => ({
                        ring: getComputedStyle(el.querySelector('.gain-dial-arc')).zIndex,
                        cap: getComputedStyle(el.querySelector('.gain-dial-face')).zIndex,
                        needle: getComputedStyle(el.querySelector('.gain-dial-pointer')).zIndex
                    })""")
                    assert layers == {"ring": "0", "cap": "2", "needle": "3"}, layers
                    cap_box = cap.bounding_box()
                    assert cap_box and cap_box["width"] < body_size["width"] - 12, (
                        width, cap_box, body_size)
                    assert cap_box["width"] <= 55, "Steel face must be smaller than previous bezel"
                    arc = first.locator(".gain-dial-arc")
                    assert arc.count() == 1
                    leds = arc.evaluate("""el => ({
                        inactive: getComputedStyle(el, '::before').backgroundImage,
                        active: getComputedStyle(el, '::after').backgroundImage,
                        cap: getComputedStyle(el).maskImage,
                        inactiveMask: getComputedStyle(el, '::before').maskImage,
                        activeMask: getComputedStyle(el, '::after').maskImage
                    })""")
                    assert "repeating-conic-gradient" in leds["inactive"], leds
                    assert "repeating-conic-gradient" in leds["active"], leds
                    assert "radial-gradient" in leds["cap"], leds
                    assert "conic-gradient" in leds["inactiveMask"], leds
                    assert "conic-gradient" in leds["activeMask"], leds
                    assert "rgb(32, 55, 47)" in leds["inactive"], leds
                    pointer = first.locator(".gain-dial-pointer")
                    before_turn = pointer.evaluate("el => getComputedStyle(el).transform")
                    before_led_mask = arc.evaluate("el => getComputedStyle(el, '::after').maskImage")
                    lcd = first.locator(".gain-readout")
                    lcd_size = lcd.bounding_box()
                    assert lcd_size and lcd_size["height"] >= 24, (width, lcd_size)
                    lcd_skin = lcd.evaluate("""el => ({
                        background: getComputedStyle(el).backgroundColor,
                        text: getComputedStyle(el).color,
                        backgroundImage: getComputedStyle(el).backgroundImage,
                        border: getComputedStyle(el).borderTopWidth,
                        radius: getComputedStyle(el).borderRadius,
                        shadow: getComputedStyle(el).boxShadow
                    })""")
                    assert lcd_skin["background"] == "rgb(2, 5, 3)", (width, lcd_skin)
                    assert lcd_skin["backgroundImage"] == "none", (width, lcd_skin)
                    assert lcd_skin["text"] == "rgb(116, 253, 160)", (width, lcd_skin)
                    assert lcd_skin["border"] == "0px", (width, lcd_skin)
                    assert lcd_skin["radius"] == "6px", (width, lcd_skin)
                    assert "inset" in lcd_skin["shadow"], (width, lcd_skin)
                    digits = lcd.locator(".gain-lcd-digits")
                    assert digits.count() == 1
                    assert digits.evaluate("el => getComputedStyle(el).transform") == (
                        "matrix(1, 0, 0, 1, 0, 2)")
                    knob = first.locator('[data-field="trim"]')
                    assert knob.count() == 1
                    knob.evaluate("""el => {
                      el.value = '6.5';
                      el.dispatchEvent(new Event('input', {bubbles:true}));
                    }""")
                    assert "+6.5 dB" in first.locator(".gain-readout").inner_text()
                    assert "6.5" in knob.get_attribute("value") or knob.input_value() == "6.5"
                    assert pointer.evaluate("el => getComputedStyle(el).transform") != before_turn
                    assert arc.evaluate(
                        "el => getComputedStyle(el, '::after').maskImage"
                    ) != before_led_mask, "LED progress did not follow accepted gain gesture"
                    assert cap.evaluate("el => getComputedStyle(el).transform") == "none"
                    # Hardware unity detent: capture ±0.6, hold until ±1.1,
                    # release above that without jitter or modifying whole strip.
                    def set_gain(value, field_type="input"):
                        knob.evaluate("""(el, args) => {
                          const [val, kind]=args;
                          el.value=String(val);
                          el.dispatchEvent(new Event(kind,{bubbles:true}));
                        }""", [str(value), field_type])
                    set_gain("0.4")
                    assert knob.input_value() == "0"
                    assert lcd.inner_text().strip() == "0.0 dB"
                    set_gain("0.9")
                    assert knob.input_value() == "0", "Unity detent must hold"
                    set_gain("1.2", "change")
                    assert knob.input_value() == "1.2", "Escape from detent must work"
                    assert lcd.inner_text().strip() == "+1.2 dB"
                    # Keyboard precision must not be trapped at zero.
                    knob.evaluate("""el => {
                      el.dataset.gainKeyboard='true';
                      el.value='0.2';
                      el.dispatchEvent(new Event('input',{bubbles:true}));
                    }""")
                    assert knob.input_value() == "0.2"
                    knob.evaluate("el => delete el.dataset.gainKeyboard")
                    set_gain("0.3")
                    assert knob.input_value() == "0"
                    # Real wheel over the visible dial: increment out of unity
                    # even though pointer detent holds small drag values.
                    knob_visual.hover()
                    page.mouse.wheel(0, -120)
                    assert knob.input_value() == "0.5", "GAIN wheel cannot leave zero"
                    set_gain("6.5")
                    assert lcd.inner_text().strip() == "+6.5 dB"
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
                    assert not gain_rail.is_visible(), "Rail GAIN caption leaked into FX RACK"
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
