#!/usr/bin/env python3
"""V32 operator intent, confined WIRE paint, precision SEND knob and premium fader QA.

Browser-only reference: these assertions do NOT attest native PCM/ASIO/VST3.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

PAGE = Path(__file__).with_name("index.html")

def check(condition, description):
    if not condition:
        raise AssertionError(description)

def main():
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        for width, height in ((1366, 768), (1680, 900), (1920, 1080)):
            page = browser.new_page(viewport={"width": width, "height": height})
            errors = []
            page.on("pageerror", lambda exc: errors.append(str(exc)))
            page.goto(PAGE.as_uri())
            strip = lambda n: page.locator(f'#tracks .strip[data-id="{n}"]')
            paths = page.locator('#wireLayer path')
            tray = page.locator('.section-rail [data-section="send"]')
            # Channel selection is unrelated to SEND routing.
            strip(2).locator('.strip-identity-top').click()
            tray.click()
            check(paths.count() == 0, f"{width}: selecting a channel lit routing")
            check(not strip(2).locator('.wire-node-out').evaluate(
                "(el) => el.classList.contains('is-source')"),
                f"{width}: wrong source lit after plain channel selection")
            check("SEND idle" in page.locator('#wireLegend').inner_text(),
                  f"{width}: idle legend is missing")
            # Explicit source pin is the only way to engage focused route drawing.
            strip(1).locator('.wire-node-out').click()
            strip(3).locator('.wire-node-in').click()
            check(strip(3).locator('.wire-node-in').get_attribute('aria-pressed') == 'true',
                  f"{width}: explicit CH1 -> CH3 send failed")
            cable = page.locator('#wireLayer path.wire-outgoing-line[data-from="1"][data-to="3"]')
            check(cable.count() == 1, f"{width}: accepted cable not drawn")
            clip = page.locator('#wireLayer').evaluate("(el)=>getComputedStyle(el).clipPath")
            check(clip.startswith('inset('), f"{width}: wire is not clipped to WIRE bay: {clip}")
            wire_box = strip(1).locator('.wire-section').bounding_box()
            check(wire_box["height"] >= 60, f"{width}: expanded WIRE bay too short")
            # Knob stays compact, while a 90 px gesture must adjust roughly 30 dB.
            gain = strip(3).locator('.wire-gain')
            gain.fill('0')
            gain.dispatch_event('input')
            knob = strip(3).locator('.wire-pot')
            box = knob.bounding_box()
            check(20 <= box["width"] <= 27 and 20 <= box["height"] <= 28,
                  f"{width}: SEND knob overgrown: {box}")
            x, y = box["x"] + box["width"]/2, box["y"] + box["height"]/2
            page.mouse.move(x, y); page.mouse.down()
            page.mouse.move(x, y+90, steps=8); page.mouse.up()
            gain = strip(3).locator('.wire-gain')
            got = float(gain.input_value())
            check(-35 <= got <= -25,
                  f"{width}: full-distance knob drag expected ~-30 dB, got {got}")
            # Shift gives one third the coarse adjustment over identical motion.
            box = knob.bounding_box()
            x, y = box["x"] + box["width"]/2, box["y"] + box["height"]/2
            page.keyboard.down("Shift")
            page.mouse.move(x, y); page.mouse.down()
            page.mouse.move(x, y-30, steps=5); page.mouse.up()
            page.keyboard.up("Shift")
            fine = float(strip(3).locator('.wire-gain').input_value())
            check(2.0 <= fine-got <= 4.0,
                  f"{width}: shift precision knob incorrect: {got}->{fine}")
            check(strip(3).locator('.wire-pot-value').count() == 1,
                  f"{width}: readable SEND value overlay missing")
            # Ordinary channel selection immediately returns to neutral dot/zero cables.
            strip(2).locator('.strip-identity-top').click()
            check(paths.count() == 0, f"{width}: channel focus left routing cables visible")
            check(not strip(1).locator('.wire-node-out').evaluate(
                "(el) => el.classList.contains('is-source')"),
                f"{width}: click channel kept explicit SEND mode active")
            strip(1).locator('.wire-node-out').click()
            check(cable.count() == 1, f"{width}: previously accepted send was lost")
            check(abs(float(strip(3).locator('.wire-gain').input_value())-fine) <= 0.1,
                  f"{width}: channel selection mutated stored send gain")
            # Visible cap is smaller than V31 while physical channel control stays aligned.
            caps = page.locator('.strip .fader-travel .cap')
            for index in range(min(caps.count(), 4)):
                dims = caps.nth(index).bounding_box()
                check(20 <= dims["width"] <= 22 and 42 <= dims["height"] <= 44,
                      f"{width}: fader thumb asset sizing incorrect {dims}")
            check(not errors, f"{width}: browser JS errors: {errors}")
            page.screenshot(path=f"preview-v32-operator-{width}.png", full_page=True)
            page.close()
        browser.close()
    print("V32 operator intent, SEND gain gesture and premium layout: PASS")

if __name__ == "__main__":
    main()
