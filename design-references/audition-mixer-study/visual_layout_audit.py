#!/usr/bin/env python3
"""Reference V4 real-Chromium visual acceptance (NOT audio/DSP/native QA).

Usage (optional one-time local review):
    pip install playwright
    python -m playwright install chromium
    python visual_layout_audit.py --screenshots

No network, font download, native VST3 or GitHub Actions runs are required.
A failure is a visual regression; do not weaken assertions to conceal it.
"""
from __future__ import annotations

import argparse
from pathlib import Path

from playwright.sync_api import sync_playwright

SIZES = ((1366, 768), (1680, 900), (1920, 1080))
SECTIONS = (
    ("route", ".route-section"),
    ("fx", ".fx-section"),
    ("send", ".send-section"),
    ("pan", ".pan-section"),
    ("trim", ".trim-section"),
    ("switch", ".switch-section"),
    ("footer", ".bottom-section"),
)


def verify(page, description: str, plugin_expected: bool = False):
    issues = page.evaluate(r"""() => {
      const rect = n => n?.getBoundingClientRect();
      const master = document.querySelector('#master .strip');
      const strips = [...document.querySelectorAll('#tracks .strip')];
      const rail = document.querySelector('.section-rail');
      const browser = document.getElementById('pluginBrowser');
      const viewport = document.getElementById('viewport');
      const body = document.querySelector('.console-body');
      const out = [];
      const near = (a,b,t=1.25) => Math.abs(a-b)<=t;
      if(!master || !rail || !strips.length) return ['missing mixer structure'];
      const m=rect(master),first=rect(strips[0]),frame=rect(viewport);
      const cols=Number(getComputedStyle(document.getElementById('tracks')).getPropertyValue('--bank-columns')) || 10;
      if(!near(first.width,m.width)) out.push('MASTER not same width as CHANNEL: '+m.width+' vs '+first.width);
      if(strips.length>cols) out.push('Rendered channels exceed fixed bank capacity');
      if(frame.right>m.left+1) out.push('viewport overlaps pinned MASTER');
      if(viewport.scrollWidth>viewport.clientWidth+2) out.push('hidden horizontal content exceeds track viewport');
      for(const [name,css] of [
        ['route','.route-section'],['fx','.fx-section'],['send','.send-section'],
        ['pan','.pan-section'],['trim','.trim-section'],['switch','.switch-section'],
        ['footer','.bottom-section']
      ]){
        const cell=rail.querySelector('.rail-cell.'+name);
        if(!cell) {out.push('missing section rail '+name);continue}
        const r=rect(cell);
        for(const strip of [...strips,master]){
          const section=strip.querySelector(css);
          if(!section){out.push('missing '+name+' on '+strip.dataset.id);continue}
          const actual=rect(section);
          if(!near(actual.top,r.top)||!near(actual.bottom,r.bottom))
            out.push(name+' misalign strip '+strip.dataset.id+' '+actual.top+'..'+actual.bottom+' vs '+r.top+'..'+r.bottom);
        }
      }
      if(document.querySelector('.eq-section') || document.querySelector('.rail-cell.eq'))out.push('unwanted EQ row');
      if(document.querySelector('[data-field="sendPan"]'))out.push('unwanted Send Stereo Pan');
      if(document.querySelector('[data-action="mon"]'))out.push('unwanted input I button');
      for(const strip of [...strips,master]){
        const keys=[...strip.querySelectorAll('.switch-section .switch')];
        if(keys.length!==3)out.push('not 3 buttons on '+strip.dataset.id);
        else if(keys.some(x=>!near(rect(x).width,rect(keys[0]).width,.5)))
          out.push('unequal BTN-180 widths on '+strip.dataset.id);
        if(!strip.querySelector('.fader-box .fader-travel .cap'))out.push('missing old fader CAP');
        if(!strip.querySelector('.meter-box .vu-scale'))out.push('missing old VU/scale');
        if(!strip.querySelector('.fader-scale .gain-scale-travel'))out.push('missing gain ruler');
        const cap=strip.querySelector('.fader-travel .cap');
        if(cap && !getComputedStyle(cap).backgroundImage.includes('data:image/png;base64'))
          out.push('missing original ARI fader PNG on '+strip.dataset.id);
        const trim=strip.querySelector('.trim-section input');
        if(trim && !trim.classList.contains('ari-horizontal'))out.push('missing old PAN slider on TRIM');
      }
      const panel=rect(browser);
      if(browser && !browser.hidden){
        if(panel.right>m.left+1)out.push('Plugin Manager covers Master');
        if(panel.left<frame.right-1)out.push('Plugin Manager covers visible channel bank');
      }
      return out;
    }""")
    assert not issues, description + "\n  " + "\n  ".join(issues)


def main(screenshots: bool):
    path = Path(__file__).with_name("index.html").resolve()
    assert path.exists(), path
    folder = path.parent / "_visual_audit"
    if screenshots:
        folder.mkdir(exist_ok=True)
    total=0
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        try:
            for width, height in SIZES:
                page = browser.new_page(viewport={"width":width,"height":height})
                errors=[]
                page.on("pageerror",lambda e:errors.append(str(e)))
                page.goto(path.as_uri())
                verify(page,f"{width}x{height}: initial V4")
                total+=1
                if screenshots:
                    page.screenshot(path=str(folder/f"mixer_v4_{width}x{height}_mix.png"))
                # Additional channels are paged. Never stretch a single last-bank strip.
                page.locator('#bankNext').click()
                verify(page,f"{width}x{height}: second bank must retain exactly same MASTER/channel width")
                total+=1
                page.locator('#bankPrev').click()
                for section in ('fx','send','route'):
                    page.locator(f'.section-rail [data-section="{section}"]').click()
                    verify(page,f"{width}x{height}: toggle {section}")
                    total+=1
                    page.locator(f'.section-rail [data-section="{section}"]').click()
                page.locator('#pluginsButton').click()
                verify(page,f"{width}x{height}: docked Plugin Manager",plugin_expected=True)
                total+=1
                if screenshots:
                    page.screenshot(path=str(folder/f"mixer_v4_{width}x{height}_plugins.png"))
                page.locator('#pluginClose').click()
                for density in ('standard','compact','comfort'):
                    page.locator('#densitySelect').select_option(density)
                    verify(page,f"{width}x{height}: density {density}")
                    total+=1
                assert not errors, "JavaScript page errors: "+repr(errors)
                page.close()
        finally:
            browser.close()
    print(f"PASS {total} real Chromium V4 geometry scenarios")
    if screenshots:
        print(f"Screenshots in {folder}")


if __name__=="__main__":
    ap=argparse.ArgumentParser()
    ap.add_argument("--screenshots",action="store_true")
    main(ap.parse_args().screenshots)
