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
        if(fx<60||fx>620)problems.push('FX rack not compact: '+fx);
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




def audit_p0_collapse(page, name):
    """Expanded-route rack has four visible rows, proportional fader; no ROUTE jump."""
    data=page.evaluate("""() => {
        const body=document.querySelector('.console-body');
        const rail=document.querySelector('.section-rail');
        const strips=[...document.querySelectorAll('#tracks .strip'),
          document.querySelector('#master .strip')].filter(Boolean);
        const read=()=>({
          route:body.classList.contains('collapsed-route'),
          send:body.classList.contains('collapsed-send'),
          fx:body.classList.contains('collapsed-fx'),
          cells:strips.map(s=>{
            const box=selector=>s.querySelector(selector).getBoundingClientRect();
            const fx=box('.fx-section'),fader=box('.fader-section');
            return {fx:fx.height,fader:fader.height,top:fader.top,
              route:box('.route-section').height,send:box('.send-section').height,
              list:s.querySelector('.fxslot-list').clientHeight};
          }),
          railFx:rail.querySelector('.rail-cell.fx').getBoundingClientRect().height,
          railFader:rail.querySelector('.rail-cell.fader').getBoundingClientRect().height});
        const click=id=>rail.querySelector('[data-section="'+id+'"]').click();
        const before=read();
        click('route');const route=read();
        click('route');const restoreRoute=read();
        click('send');const sendOpen=read();
        click('route');const openSendFoldRoute=read();
        click('route');
        click('send');const restoreSend=read();
        click('route');const bothHidden=read();
        click('route');
        return {before,route,restoreRoute,sendOpen,openSendFoldRoute,restoreSend,bothHidden};
    }""")
    base=data['before']
    assert not base['route'] and base['send'] and not base['fx'],(name,'initial fold',data)
    expected=min(600,max(215,page.viewport_size['height']-510))
    if page.viewport_size['height']<=680:
        expected=min(300,max(155,page.viewport_size['height']-505))
    for i,cell in enumerate(base['cells']):
        assert abs(cell['fader']-expected)<2.0,(name,'fader not proportional',i,cell,expected)
        assert 120<=cell['fx']<=145,(name,'FX rack not approximately four slots',i,cell)
        assert cell['list']>=88,(name,'FX viewport cannot show four 22px rows',i,cell)
        assert cell['fader']>cell['fx']+45,(name,'fader less usable than rack',i,cell)
    for key in ('route','restoreRoute','sendOpen','openSendFoldRoute','restoreSend','bothHidden'):
        now=data[key]
        assert len(now['cells'])==len(base['cells']),(name,key,'roster changed')
        for i,(a,b) in enumerate(zip(base['cells'],now['cells'])):
            assert abs(now['railFx']-b['fx'])<1.5,(name,key,i,'FX rail misaligned')
            assert abs(now['railFader']-b['fader'])<1.5,(name,key,i,'fader rail misaligned')
            if key in ('route','restoreRoute','restoreSend','bothHidden'):
                assert abs(a['fader']-b['fader'])<1.5,(name,key,i,'fader changed on ROUTE collapse')
                assert abs(a['top']-b['top'])<1.5,(name,key,i,'fader jumped on ROUTE collapse')
            if key in ('sendOpen','openSendFoldRoute'):
                assert b['list']>=88,(name,key,i,'four FX slots cropped with SEND expanded',b)
                assert b['fader']>=150,(name,key,i,'SEND open fader unsafe short',b)
    for i,(a,b) in enumerate(zip(base['cells'],data['route']['cells'])):
        assert b['route']<a['route']-35,(name,i,'ROUTE did not collapse',a,b)
        assert b['fx']>a['fx']+35,(name,i,'FX did not gain ROUTE pixels',a,b)
        assert b['list']>a['list']+35,(name,i,'FX visibility not improved',a,b)
        assert data['bothHidden']['cells'][i]['fx']>a['fx']+35,(name,i,'combined fold no gain')
        assert data['sendOpen']['cells'][i]['send']>a['send']+35,(name,i,'SEND not expanded')
        assert abs(data['sendOpen']['cells'][i]['fx']-a['fx'])<=4,(
            name,i,'SEND open should keep 4 FX rows; borrow fader only as needed',a,data)
        assert data['openSendFoldRoute']['cells'][i]['fx']>data['sendOpen']['cells'][i]['fx']+35,(
            name,i,'ROUTE collapse failed when SEND expanded',data)
    for key in ('restoreRoute','restoreSend'):
        assert abs(data[key]['cells'][0]['fx']-base['cells'][0]['fx'])<1.5,(
            name,key,'FX geometry did not restore',data)
    return data


def audit_p0_pan_center(page, name):
    """Physical center detent with hysteresis, sign, keyboard fine adjustment."""
    result=page.evaluate("""() => {
        const input=document.querySelector('#tracks .strip .center-pan');
        const read=()=>({value:Number(input.value),text:input.getAttribute('aria-valuetext'),
          label:input.closest('.pan-control').nextElementSibling.textContent,
          centered:input.closest('.pan-control').classList.contains('is-centered')});
        const send=(v,type='input')=>{
          input.value=String(v);input.dispatchEvent(new Event(type,{bubbles:true}));
          return read();
        };
        const initial=read();
        const farLeft=send(-35),capture=send(-6),held=send(10),
          released=send(18),nearRelease=send(11,'change');
        input.dataset.panAltPointer='true';
        const bypass=send(5);
        delete input.dataset.panAltPointer;
        const recenter=send(-4,'change');
        const nodeBefore=input;
        return {initial,farLeft,capture,held,released,nearRelease,bypass,recenter,
          unchanged:nodeBefore===document.querySelector('#tracks .strip .center-pan')};
    }""")
    assert result['farLeft']['value']==-35,(name,'PAN left sign',result)
    for key in ('capture','held','nearRelease','recenter'):
        p=result[key]
        assert p['value']==0 and p['centered'] and p['label']=='C' and p['text']=='Center',(
            name,key,'PAN center detent failed',result)
    assert result['released']['value']==18 and not result['released']['centered'],(
        name,'PAN release threshold failed',result)
    assert result['bypass']['value']==5,(name,'Alt-pointer center bypass failed',result)
    assert result['unchanged'],(name,'PAN remounted during gesture')
    pan=page.locator('#tracks .strip .center-pan').first
    pan.focus()
    pan.press('ArrowRight')
    assert pan.input_value()=='1',(name,'Keyboard cannot adjust off center')
    pan.press('ArrowLeft')
    assert pan.input_value()=='0',(name,'Keyboard cannot return center')



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
                audit_p0_collapse(page,f"{width}x{height} P0");count+=1
                audit_p0_pan_center(page,f"{width}x{height} PAN");count+=1
                if width==1366:
                    page.locator('.section-rail [data-section="route"]').click()
                    page.screenshot(path=str(ROOT / "preview-p0-fx-expanded-1366.png"))
                    page.locator('.section-rail [data-section="route"]').click()
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
                audit_p0_collapse(page,f"PUBLIC {width}x{height} P0");count+=1
                audit_p0_pan_center(page,f"PUBLIC {width}x{height} PAN");count+=1
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
