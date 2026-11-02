#!/usr/bin/env python3
"""Chromium rendered-geometry acceptance for ArSonMix isolated preview V8.

Requires playwright with installed Chromium; does NOT test native audio.
  python -m pip install playwright
  python -m playwright install chromium
  python visual_layout_audit_v8.py --screenshots

Intentionally local/on-demand: avoid repeated GitHub CI for visual iterations.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from playwright.sync_api import sync_playwright

VIEWPORTS = [(1366, 768), (1680, 900), (1920, 1080)]
DENSITIES = {"comfort": 150, "standard": 125, "compact": 105}
SOURCE = Path(__file__).with_name("index.html")
MEASURE = r"""() => {
    const body = document.querySelector('.console-body');
    const rail = document.querySelector('.section-rail');
    const sources = [...document.querySelectorAll('#tracks > .strip')];
    const master = document.querySelector('#master > .strip');
    const viewport = document.getElementById('viewport');
    const rect = e => e.getBoundingClientRect();
    const problems = [];
    if (!rail || !sources.length || !master) return {problems: ['No strip/rail/master']};
    const width = rect(master).width;
    const minimum = {comfort: 150, standard: 125, compact: 105}[body.dataset.density] || 150;
    if (width < minimum - 0.6) problems.push('MAIN below density threshold: ' + width);
    if (sources.some(s => Math.abs(rect(s).width - width) > 1.25))
      problems.push('MAIN/source strip width mismatch');
    if (Math.abs(rect(viewport).right - rect(master).left) > 1.5)
      problems.push('Viewport does not meet pinned MAIN');
    if (viewport.scrollWidth > viewport.clientWidth + 2)
      problems.push('Unexpected horizontal mixer scrollbar');
    if (sources.some(s => Math.abs(rect(s).top - rect(master).top) > 1.5))
      problems.push('Strips do not align vertically with MAIN');
    const strip = sources[0];
    for (const [railSelector, stripSelector, name] of [
      ['.identity','.strip-identity-top','identity'],
      ['.route','.route-section','route'],
      ['.fx','.fx-section','FX'],
      ['.send','.send-section','send'],
      ['.pan','.pan-section','pan'],
      ['.switch','.switch-section','M/S/PFL'],
      ['.fader','.fader-section','fader'],
      ['.footer','.bottom-section','footer']]) {
      const a = rail.querySelector('.rail-cell' + railSelector);
      const b = strip.querySelector(stripSelector);
      const c = master.querySelector(stripSelector);
      if (!a || !b || !c) {problems.push('Missing ' + name + ' section'); continue;}
      if (Math.abs(rect(a).top - rect(b).top) > 1.6 ||
          Math.abs(rect(b).top - rect(c).top) > 1.6)
        problems.push(name + ' section top misalignment');
      if (Math.abs(rect(a).height - rect(b).height) > 1.6)
        problems.push(name + ' rail height mismatch');
    }
    for (const [cl, selector, name] of [
      ['collapsed-route','.route-section .route-field','Route'],
      ['collapsed-fx','.fx-section .fxslot-list','FX'],
      ['collapsed-send','.send-section .sendlevel','Send']]) {
      if (body.classList.contains(cl)) {
        const el = strip.querySelector(selector);
        if (el && getComputedStyle(el).display !== 'none')
          problems.push(name + ' contains live expanded controls when collapsed');
      }
    }
    const pane = rect(body);
    if (pane.bottom > innerHeight + 1) problems.push('Mixer below viewport');
    const visibleFader = strip.querySelector('.fader-travel');
    if (!visibleFader || rect(visibleFader).height < 55)
      problems.push('Fader travel too short');
    return {problems, density:body.dataset.density, capacity:sources.length,
      channelWidth:Number(width.toFixed(2)), viewport:[innerWidth, innerHeight],
      railWidth:Number(rect(rail).width.toFixed(2)),
      collapsed:['route','fx','send'].filter(n=>body.classList.contains('collapsed-'+n))};
}"""


def set_section(page, name: str, collapsed: bool) -> None:
    body = page.locator(".console-body")
    present = "collapsed-" + name in (body.get_attribute("class") or "").split()
    if present != collapsed:
        page.locator(f'.section-rail [data-section="{name}"]').click()


def verify(page, label: str, errors: list) -> dict:
    result = page.evaluate(MEASURE)
    for problem in result["problems"]:
        errors.append(f"{label}: {problem}")
    print(json.dumps({"scenario": label, **result}, ensure_ascii=False))
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--screenshots", action="store_true",
                    help="Save all 9 density-size baseline screenshots plus three 1680px states")
    ap.add_argument("--out", type=Path, default=Path(__file__).with_name("v8-screenshots"))
    opts = ap.parse_args()
    if not SOURCE.is_file():
        raise SystemExit(f"Missing reference HTML: {SOURCE}")
    if opts.screenshots:
        opts.out.mkdir(parents=True, exist_ok=True)
    errors = []
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(device_scale_factor=1, reduced_motion="reduce")
        page.on("pageerror", lambda err: errors.append(f"JS page error: {err}"))
        for width, height in VIEWPORTS:
            page.set_viewport_size({"width": width, "height": height})
            for density in DENSITIES:
                page.goto(SOURCE.resolve().as_uri(), wait_until="load")
                page.locator("#densitySelect").select_option(density)
                page.locator("#bankSelect").select_option("0")
                key = f"{width}x{height}-{density}"
                verify(page, key + "-default", errors)
                if opts.screenshots:
                    page.screenshot(path=str(opts.out / (key + ".png")), full_page=False)
                # Simultaneously expanded Route/FX/Send must fit without crossing sections.
                for name in ("route", "fx", "send"):
                    set_section(page, name, collapsed=False)
                verify(page, key + "-all-expanded", errors)
                # Three simultaneous collapses previously caused severe overlap.
                for name in ("route", "fx", "send"):
                    set_section(page, name, collapsed=True)
                verify(page, key + "-all-collapsed", errors)
                if width == 1680 and density == "comfort" and opts.screenshots:
                    page.screenshot(path=str(opts.out / (key + "-collapsed.png")),
                                    full_page=False)
                    for name in ("route", "fx", "send"):
                        set_section(page, name, collapsed=False)
                    page.screenshot(path=str(opts.out / (key + "-expanded.png")),
                                    full_page=False)
                    page.locator("#pluginsButton").click()
                    page.screenshot(path=str(opts.out / (key + "-manager.png")),
                                    full_page=False)
                # Last partial bank may contain only one source, must NEVER grow it.
                page.locator("#bankNext").click()
                verify(page, key + "-last-bank", errors)
        browser.close()
    if errors:
        print("\nFAIL: " + str(len(errors)) + " rendered Chromium problems")
        for e in errors:
            print(" - " + e)
        return 1
    print("\nPASS: rendered Chromium geometry; manual screenshot/visual review still required")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
