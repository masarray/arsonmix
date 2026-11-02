#!/usr/bin/env python3
"""Run REAL Chromium screenshot/geometry review of Preview V5; not a DSP test.

Requires: pip install playwright && python -m playwright install chromium
Usage: python visual_layout_audit_v5.py --screenshots
"""
from __future__ import annotations

import argparse
from pathlib import Path
from playwright.sync_api import sync_playwright

SIZES = [(1366, 768), (1680, 900), (1920, 1080)]


def inspect(page, label: str):
    errors = page.evaluate("""() => {
      const r = e => e.getBoundingClientRect();
      const tracks = [...document.querySelectorAll('#tracks .strip')];
      const master = document.querySelector('#master .strip');
      const viewport = document.getElementById('viewport');
      const problems = [];
      if(!master || !tracks.length) return ['Missing mixer/master'];
      const width = r(master).width;
      tracks.forEach(el => {
        if(Math.abs(r(el).width-width)>1.1) problems.push('Master/source width mismatch');
        const buttons = [...el.querySelectorAll('.switch-section .switch')];
        if(buttons.length!==3) problems.push('Not MUTE SOLO PFL only');
        if(buttons.some(btn=>Math.abs(r(btn).width-r(buttons[0]).width)>1))
          problems.push('BTN-180 widths unequal');
        if(el.querySelector('.trim-section') || el.querySelector('.eq-section') ||
           el.querySelector('[data-field="sendPan"]'))
          problems.push('Prohibited TRIM/EQ/Send PAN row');
        const route = el.querySelector('.route-section');
        if(route && route.scrollHeight>route.clientHeight+2)
          problems.push('IN/OUT section clipped');
        if(!el.querySelector('.fader-travel .cap') ||
           !el.querySelector('.meter-box .vu-bars') ||
           !el.querySelector('.gain-scale-travel'))
          problems.push('Old FADER/VU/ruler anatomy missing');
      });
      if(viewport.scrollWidth>viewport.clientWidth+2)
        problems.push('Bottom horizontal scrollbar content still overflows');
      const fast = document.getElementById('fastPicker');
      const manager = document.getElementById('pluginBrowser');
      if(fast && !fast.hidden && manager && !manager.hidden)
        problems.push('Fast picker incorrectly opens Plugin Manager');
      return [...new Set(problems)];
    }""")
    assert not errors, label + ": " + "; ".join(errors)


def review(output: bool):
    html = Path(__file__).with_name("index.html").resolve()
    assert html.exists(), f"index.html unavailable: {html}"
    folder = html.parent / "_visual_audit_v5"
    if output:
        folder.mkdir(exist_ok=True)
    checks=0
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        try:
            for width, height in SIZES:
                page=browser.new_page(viewport={"width":width,"height":height},device_scale_factor=1)
                exceptions=[]
                page.on('pageerror',lambda e:exceptions.append(str(e)))
                page.goto(html.as_uri())
                inspect(page,f"{width}x{height} initial");checks+=1
                if output:
                    page.screenshot(path=str(folder/f"v5_{width}x{height}_mixer.png"))
                initial=page.locator('#tracks .strip').first.bounding_box()
                master=page.locator('#master .strip').bounding_box()
                assert initial and master and abs(initial['width']-master['width'])<1.1
                page.locator('#bankNext').click()
                inspect(page,f"{width}x{height} CH11");checks+=1
                page.locator('#bankPrev').click()
                page.locator('.tracks .fx-pick').first.click()
                assert page.locator('#fastPicker').is_visible(), "Add FX did not open fast picker"
                assert not page.locator('#pluginBrowser').is_visible(), "Add FX opened Manager"
                page.locator('#fastSearch').fill('reverb')
                assert page.locator('.fast-effect').count() > 0, "Fast search yielded no effects"
                if output:
                    page.screenshot(path=str(folder/f"v5_{width}x{height}_fast.png"))
                page.locator('#fastClose').click();checks+=1
                page.locator('#pluginsButton').click()
                assert page.locator('#pluginBrowser').is_visible(), "Manager must float"
                inspect(page,f"{width}x{height} floating Manager");checks+=1
                page.locator('#pluginClose').click()
                page.locator('.fader-box').first.click(position={"x":20,"y":55})
                page.locator('.fader-box').first.hover()
                assert page.locator('.fader-travel .cap').first.is_visible()
                inspect(page,f"{width}x{height} pointer fader");checks+=1
                for density in ('standard','compact','comfort'):
                    page.locator('#densitySelect').select_option(density)
                    inspect(page,f"{width}x{height} {density}");checks+=1
                assert not exceptions, f"JS runtime errors: {exceptions}"
                page.close()
        finally:
            browser.close()
    print(f"PASS {checks} rendered Chromium V5 scenarios")
    if output:
        print(f"Screenshots: {folder}")


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument('--screenshots',action='store_true')
    review(parser.parse_args().screenshots)
