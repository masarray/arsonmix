#!/usr/bin/env python3
"""Run headless real-Chromium physical geometry and workflow audit for Pages V7.

This is an independent review of the published /preview-v2/ snapshot in main,
NOT a validation of native ASIO/VST3 audio. Screenshots are uploaded by UI smoke.
No external web request; local site/preview-v2/index.html only.
"""
from __future__ import annotations
from pathlib import Path
import subprocess, sys
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
PAGE = (ROOT / "site/preview-v2/index.html").resolve()
PUBLIC = (ROOT / "_preview-public-qa/preview/mixer.html").resolve()
SIZES = [(1366, 768), (1666, 888), (1920, 1080)]


def audit_geometry(page, title):
    problems = page.evaluate("""() => {
        const r=e=>e.getBoundingClientRect();
        const strips=[...document.querySelectorAll('#tracks .strip')];
        const m=document.querySelector('#master .strip');
        const viewport=document.getElementById('viewport');
        const rail=document.querySelector('.section-rail');
        const cells={identity:'.strip-identity-top',route:'.route-section',
            fx:'.fx-section',send:'.send-section',pan:'.pan-section',
            switch:'.switch-section',footer:'.bottom-section'};
        const problems=[];
        if(!strips.length||!m)return ['missing tracks/master'];
        const width=r(m).width;
        for(const t of [...strips,m]){
            const tag=t.getAttribute('data-id');
            if(Math.abs(r(t).width-width)>1.2)problems.push('Master/ch width mismatch '+tag);
            const top=t.querySelector('.strip-identity-top');
            const bottom=t.querySelector('.footer-label');
            if(!top||!bottom)problems.push('missing matched nameplates '+tag);
            else {
                const a=getComputedStyle(top).borderBottomColor;
                const b=getComputedStyle(bottom).borderBottomColor;
                if(a!==b)problems.push('top/bottom channel color mismatch '+tag);
            }
            const controls=[...t.querySelectorAll('.switch-section .switch')];
            if(controls.length!==3)problems.push('MUTE/SOLO/PFL count !=3 '+tag);
            else if(Math.abs(r(controls[0]).width-r(controls[1]).width)>1)
                problems.push('inconsistent button widths '+tag);
            const cap=t.querySelector('.fader-travel .cap');
            if(!cap||Math.abs(r(cap).width-26)>1.5||Math.abs(r(cap).height-55)>1.5)
                problems.push('scaled 26x55 fader cap missing '+tag);
            const route=t.querySelector('.route-section');
            if(route&&route.scrollHeight>route.clientHeight+3)
                problems.push('IN/OUT clipped '+tag);
            if(!t.querySelector('.meter-box .vu-bars'))problems.push('missing meter '+tag);
            if(t.querySelector('.eq-section,.trim-section,[data-field="sendPan"]'))
                problems.push('legacy DAW or TRIM UI leaked '+tag);
        }
        if(viewport.scrollWidth>viewport.clientWidth+2)
            problems.push('horizontal scrollbar overflow');
        const fx=r(strips[0].querySelector('.fx-section')).height;
        if(fx<104||fx>145)problems.push('FX rack not compact: '+fx);
        const before=getComputedStyle(strips[0].querySelector('.fader-box'),'::before');
        if(!before.backgroundImage.includes('rgb(5, 11, 16)'))
            problems.push('deep graphite fader well material absent');
        if(!getComputedStyle(strips[0]).boxShadow.includes('inset'))
            problems.push('missing metallic strip edge');
        for(const [name, selector] of Object.entries(cells)){
            const cell=rail.querySelector(name==='identity'?'.rail-cell.identity':'.rail-cell.'+name);
            const panel=strips[0].querySelector(selector);
            if(!cell||!panel)continue;
            const a=r(cell),b=r(panel);
            if(Math.abs(a.top-b.top)>1.5||Math.abs(a.bottom-b.bottom)>1.5)
                problems.push('rail/channel row misalign '+name+': '+a.top+'-'+a.bottom+' vs '+b.top+'-'+b.bottom);
        }
        return [...new Set(problems)];
    }""")
    assert not problems, title + ":\n" + "\n".join(problems)


def main():
    # Exercise the real production Pages builder; public URL cannot be
    # validated against a checked-in snapshot that may lag another thread.
    subprocess.run([sys.executable, str(ROOT / "scripts/build_pages.py"),
                    "--out", str(ROOT / "_preview-public-qa"), "--sha", "qa"],
                   check=True)
    assert PAGE.exists() and PUBLIC.exists(), (PAGE, PUBLIC)
    assert PUBLIC.read_bytes()==PAGE.read_bytes(), "Public /preview/mixer.html must be the PR278 reference, not old M4"
    count = 0
    with sync_playwright() as playwright:
        browser=playwright.chromium.launch(headless=True)
        try:
            for width,height in SIZES:
                page=browser.new_page(viewport={"width":width,"height":height},device_scale_factor=1)
                errors=[]
                page.on("pageerror",lambda error: errors.append(str(error)))
                page.goto(PAGE.as_uri())
                audit_geometry(page,f"{width}x{height} default");count+=1
                page.screenshot(path=str(ROOT / f"preview-v2-v7-{width}.png"))
                # Single click must SELECT, not open the name/color popover.
                page.locator('#tracks .strip-identity-top').first.click()
                assert not page.locator('#channelIdentity').is_visible()
                page.locator('#tracks .strip-identity-top').first.dblclick()
                assert page.locator('#channelIdentity').is_visible(), "double-click identity failed"
                page.locator('#identityCancel').click()
                count+=1
                page.locator('#pluginsButton').click()
                assert page.locator('#pluginBrowser').is_visible(), "modeless manager absent"
                # Mouse can interact with live strip while plugin window is open.
                page.locator('#tracks .strip-identity-top').first.click()
                assert page.locator('#pluginBrowser').is_visible(), "manager incorrectly modal"
                page.locator('#pluginMinimize').click()
                assert 'minimized' in page.locator('#pluginBrowser').get_attribute('class')
                page.locator('#pluginMinimize').click()
                count+=1
                # FX quick select does NOT terminate the modeless manager.
                page.locator('#tracks .fx-pick.empty').first.click()
                assert page.locator('#fastPicker').is_visible(), "Add FX quick picker absent"
                assert page.locator('#pluginBrowser').is_visible(), "Add FX closed manager"
                page.locator('#fastClose').click()
                page.locator('#pluginClose').click()
                count+=1
                # The last partially populated page must NOT become a giant bus fader.
                page.locator('#bankNext').click()
                audit_geometry(page,f"{width}x{height} next bank");count+=1
                page.locator('#bankPrev').click()
                assert not errors, f"{width}x{height}: JavaScript runtime errors {errors}"
                page.close()
            # The URL explicitly requested by the owner is an independent
            # acceptance surface. Verify its behavior, not just /preview-v2/.
            for width,height in SIZES:
                page=browser.new_page(viewport={"width":width,"height":height},device_scale_factor=1)
                errors=[]
                page.on("pageerror",lambda error: errors.append(str(error)))
                page.goto(PUBLIC.as_uri())
                audit_geometry(page,f"PUBLIC /preview/mixer.html {width}x{height}");count+=1
                page.screenshot(path=str(ROOT / f"preview-operator-{width}.png"))
                assert page.locator('#tracks .strip').count()>0
                assert page.get_by_text('SIMULATED METERS · NO REAL AUDIO').is_visible()
                page.locator('#tracks .strip-identity-top').first.dblclick()
                assert page.locator('#channelIdentity').is_visible()
                page.locator('#identityCancel').click()
                page.locator('#pluginsButton').click()
                assert page.locator('#pluginBrowser').is_visible()
                page.locator('#tracks .fx-pick.empty').first.click()
                assert page.locator('#fastPicker').is_visible()
                assert page.locator('#pluginBrowser').is_visible()
                page.locator('#fastClose').click()
                page.locator('#pluginClose').click()
                page.locator('#bankNext').click()
                audit_geometry(page,f"PUBLIC bank paging {width}x{height}");count+=1
                assert not errors, f"PUBLIC {width}x{height}: {errors}"
                page.close()
        finally:
            browser.close()
    print(f"PASS {count} true-Chromium V7 visual/interaction scenarios")


if __name__ == '__main__':
    main()
