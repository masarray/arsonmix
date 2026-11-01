#!/usr/bin/env python3
"""Opt-in *real-browser* geometry audit of the isolated live mixer reference.

Run: python visual_layout_audit.py --screenshots
Requires: pip install playwright ; python -m playwright install chromium

The checks compare equal section row geometry across rail, tracks, and master.
They do NOT assert Adobe pixel parity or audio/DSP correctness.
"""
from __future__ import annotations

import argparse
from pathlib import Path
from playwright.sync_api import sync_playwright

SIZES = ((1366, 768), (1680, 900), (1920, 900))
SECTIONS = (
    ("route", ".route-section"),
    ("fx", ".fx-section"),
    ("send", ".send-section"),
    ("eq", ".eq-section"),
    ("pan", ".pan-section"),
    ("trim", ".trim-section"),
    ("switch", ".switch-section"),
    ("fader", ".fader-section"),
    ("footer", ".bottom-section"),
)
COMBINATIONS = (
    (),
    ("fx",),
    ("send",),
    ("eq",),
    ("fx", "send"),
    ("send", "eq"),
    ("route", "fx", "send", "eq"),
)


def assert_geometry(page, label: str):
    result = page.evaluate("""() => {
      const pick = (selector) => document.querySelector(selector);
      const bounds = el => {
        const {top, bottom, width, height} = el.getBoundingClientRect();
        return {top, bottom, width, height};
      };
      const sections = [
        ['route', '.route-section'], ['fx', '.fx-section'],
        ['send', '.send-section'], ['eq', '.eq-section'],
        ['pan', '.pan-section'], ['trim', '.trim-section'],
        ['switch', '.switch-section'], ['fader', '.fader-section'],
        ['footer', '.bottom-section']
      ];
      const issues = [];
      const rail = pick('.section-rail');
      const strips = [...document.querySelectorAll('.tracks .strip')].slice(0, 6);
      const master = pick('#master .strip');
      if (!rail || !strips.length || !master) issues.push('missing rail, track or master');
      else for (const [name, selector] of sections) {
        const railCell = pick('.section-rail .rail-cell.' + name);
        if (!railCell) { issues.push('no rail: ' + name); continue; }
        const baseline = bounds(railCell);
        for (const strip of [...strips, master]) {
          const content = strip.querySelector(selector);
          if (!content) { issues.push('no section: ' + name); continue; }
          const actual = bounds(content);
          if (Math.abs(actual.top - baseline.top) > 1.25 ||
              Math.abs(actual.bottom - baseline.bottom) > 1.25) {
            issues.push(name + ' misaligned: ' + strip.dataset.id +
              ' rail=' + baseline.top.toFixed(1) + '..' + baseline.bottom.toFixed(1) +
              ' strip=' + actual.top.toFixed(1) + '..' + actual.bottom.toFixed(1));
          }
        }
      }
      for (const strip of [...strips, master].filter(Boolean)) {
        const fx = strip.querySelector('.fx-section');
        const header = fx?.querySelector('.subhead');
        const rack = fx?.querySelector('.fxslot-list');
        if (header && rack && getComputedStyle(rack).display !== 'none') {
          if (bounds(header).bottom > bounds(rack).top + 1)
            issues.push('FX header overlaps rack: ' + strip.dataset.id);
          if (header.scrollHeight > header.clientHeight + 1)
            issues.push('FX header text wraps/clips vertically: ' + strip.dataset.id);
        }
        const send = strip.querySelector('.send-section .send-header');
        if (send && send.scrollWidth > send.clientWidth + 2)
          issues.push('Send header overflows: ' + strip.dataset.id);
      }
      const panel = pick('#pluginBrowser');
      const masterRect = bounds(master);
      if (panel && !panel.hidden && bounds(panel).bottom > innerHeight + 1)
        issues.push('Plugin overlay exceeds viewport');
      if (panel && !panel.hidden && bounds(panel).right > masterRect.top * 0 + masterRect.width + masterRect.left + 1)
        issues.push('Plugin overlay covers pinned Master');
      return issues;
    }""")
    if result:
        raise AssertionError(label + ":\n  " + "\n  ".join(result))


def run_test(screenshots: bool):
    html = Path(__file__).with_name("index.html").resolve()
    assert html.is_file(), f"Missing reference HTML: {html}"
    screenshot_dir = html.parent / "_visual_audit"
    if screenshots:
        screenshot_dir.mkdir(exist_ok=True)
    checks = 0
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        try:
            for width, height in SIZES:
                page = browser.new_page(viewport={"width": width, "height": height}, device_scale_factor=1)
                errors = []
                page.on("pageerror", lambda error: errors.append(str(error)))
                page.goto(html.as_uri())
                for collapsed in COMBINATIONS:
                    page.reload()
                    for section in collapsed:
                        page.locator(f'.section-rail [data-section="{section}"]').click()
                    label = f"{width}x{height} collapsed={','.join(collapsed) or 'none'}"
                    assert_geometry(page, label)
                    checks += 1
                    if screenshots and (not collapsed or collapsed == COMBINATIONS[-1]):
                        name = f"mixer_{width}x{height}_{'all_collapsed' if collapsed else 'expanded'}.png"
                        page.screenshot(path=str(screenshot_dir / name), full_page=True)
                page.reload()
                page.locator('#pluginsButton').click()
                assert_geometry(page, f"{width}x{height} plugin-manager")
                checks += 1
                if screenshots:
                    page.screenshot(path=str(screenshot_dir / f"mixer_{width}x{height}_plugins.png"), full_page=True)
                page.locator('#pluginClose').click()
                page.locator('#moreTools summary').click()
                page.locator('#mode').click()
                assert_geometry(page, f"{width}x{height} narrow-mode")
                checks += 1
                if errors:
                    raise AssertionError("JavaScript runtime errors: " + "; ".join(errors))
                page.close()
        finally:
            browser.close()
    print(f"PASS {checks} real Chromium geometry configurations")
    if screenshots:
        print(f"Saved screenshots: {screenshot_dir}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--screenshots", action="store_true", help="Save browser screenshots next to this file")
    opts = parser.parse_args()
    run_test(opts.screenshots)
