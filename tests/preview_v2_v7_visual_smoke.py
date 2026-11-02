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
SIZES = [(1366, 768), (1666, 888), (1711, 1005), (1920, 1080)]


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
        const wellRgb=(before.backgroundColor.match(/[0-9]+/g)||[]).slice(0,3).map(Number);
        if(before.backgroundImage!=='none'||wellRgb.length!==3||
           Math.max(...wellRgb)>46||Math.min(...wellRgb)<4)
            problems.push('deep solid graphite fader cavity absent');
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





def audit_p0_collapse(page, label):
    """Every ROUTE/FX/SEND combination preserves PAN/SWITCH/FADER/FOOTER."""
    result=page.evaluate("""() => {
      const body=document.querySelector('.console-body');
      const rail=document.querySelector('.section-rail');
      const tracks=[...document.querySelectorAll('#tracks .strip'),
                    document.querySelector('#master .strip')].filter(Boolean);
      const box=e=>{const r=e.getBoundingClientRect();return {top:r.top,bottom:r.bottom,h:r.height};};
      const read=()=>({
        state:[...['route','fx','send'].map(k=>Number(body.classList.contains('collapsed-'+k)))].join(''),
        items:tracks.map(t=>Object.fromEntries(
          ['route','fx','send','pan','switch','fader','footer'].map(k=>[
            k,{...box(t.querySelector(k==='footer'?'.bottom-section':'.'+k+'-section')),
               list:k==='fx'?t.querySelector('.fxslot-list').clientHeight:undefined}]))),
        rail:Object.fromEntries(['route','fx','send','pan','switch','fader','footer']
          .map(k=>[k,box(rail.querySelector('.rail-cell.'+k))])),
        style:getComputedStyle(tracks[0]).backgroundImage,
        faces:['route','fx','send','pan','switch','fader'].map(k=>
          getComputedStyle(tracks[0].querySelector('.'+k+'-section')).backgroundImage),
        routing:{
          deck:getComputedStyle(tracks[0].querySelector('.wire-deck')).display,
          tray:getComputedStyle(tracks[0].querySelector('.wire-tray')).display,
          svg:getComputedStyle(document.querySelector('#wireLayer')).display,
          sendToggle:rail.querySelector('[data-section="send"]').getAttribute('aria-expanded')
        }
      });
      const original=[...['route','fx','send'].map(k=>body.classList.contains('collapsed-'+k))];
      const click=k=>rail.querySelector('[data-section="'+k+'"]').click();
      const states={};
      for(let n=0;n<8;n++){
        for(let j=0;j<3;j++){
          const k=['route','fx','send'][j],want=Boolean(n&(1<<j));
          if(body.classList.contains('collapsed-'+k)!==want)click(k);
        }
        states[read().state]=read();
      }
      for(let j=0;j<3;j++){
        const k=['route','fx','send'][j];
        if(body.classList.contains('collapsed-'+k)!==original[j])click(k);
      }
      return {states,restored:read()};
    }""")
    states=result['states']
    assert len(states)==8,(label,'missing collapse permutations',list(states))
    baseline=states['000']
    expected=max(150,min(485,page.viewport_size['height']-630))
    first=baseline['items'][0]
    assert first['fx']['list']>=88,(label,'four FX rows not visible with all upper panels open',first)
    assert abs(first['fader']['h']-expected)<=2,(label,'fader height not viewport-proportional',expected,first)
    anchor=['pan','switch','fader','footer']
    for state,frame in states.items():
        for i,item in enumerate(frame['items']):
            for k in anchor:
                ref=first[k]
                if state in ('011','111') and k in ('pan','switch','fader'):
                    if k in ('pan','switch'):
                        assert abs(item[k]['h']-ref['h'])<=1.2,(
                            label,state,i,k,'PAN/SWITCH height must stay calibrated')
                    else:
                        assert item[k]['h']>ref['h']+75,(
                            label,state,i,k,'FX+SEND CLOSED must grow physical fader')
                    assert item[k]['top']<ref['top']-35,(
                        label,state,i,k,'FX+SEND CLOSED must raise lower controls')
                else:
                    assert abs(item[k]['top']-ref['top'])<=1.2,(
                        label,state,i,k,'lower section Y jumped',ref,item[k])
                    assert abs(item[k]['h']-ref['h'])<=1.2,(
                        label,state,i,k,'lower height changed',ref,item[k])
                assert abs(item[k]['top']-frame['rail'][k]['top'])<=1.2,(
                    label,state,i,k,'rail lower not aligned')
            for k in ('route','fx','send'):
                assert abs(item[k]['top']-frame['rail'][k]['top'])<=1.2,(
                    label,state,i,k,'upper rail mismatch')
                assert abs(item[k]['h']-frame['rail'][k]['h'])<=1.2,(
                    label,state,i,k,'upper rail height mismatch')
        assert frame['style']=='none',(label,state,'strip must have FLAT surface',frame['style'])
        assert all(bg=='none' for bg in frame['faces']),(
            label,state,'broad gradient still paints panel face',frame['faces'])
        if state.endswith('1'):
            assert frame['items'][0]['send']['h']<=24,(
                label,state,'collapsed WIRE must be a header only')
            assert frame['routing']['deck']=='none' and frame['routing']['tray']=='none',(
                label,state,'collapsed SEND still renders graphical patch bay')
            assert frame['routing']['svg']=='none',(
                label,state,'collapsed SEND leaves SVG wires visible')
            assert frame['routing']['sendToggle']=='false',(
                label,state,'folded route disclosure reports expanded')
        else:
            assert frame['items'][0]['send']['h']>=110,(
                label,state,'expanded WIRE bay is missing')
            assert frame['routing']['deck']!='none' and frame['routing']['tray']!='none',(
                label,state,'expanded patch graphic missing')
            assert frame['routing']['svg']!='none',(
                label,state,'SVG overlay not restored')
            assert frame['routing']['sendToggle']=='true',(
                label,state,'expanded route disclosure reports collapsed')
    assert states['100']['items'][0]['fx']['h']>first['fx']['h']+40,(
        label,'ROUTE collapse did not expand FX')
    # Either ROUTE state must give reclaimed FX+SEND room to fader.
    assert states['011']['items'][0]['fader']['h']>first['fader']['h']+75,(
        label,'ROUTE open, FX+SEND closed: fader must consume blank area')
    assert states['111']['items'][0]['fader']['h']>=states['011']['items'][0]['fader']['h']+35,(
        label,'collapsing ROUTE too must free additional fader height')
    assert states['001']['items'][0]['fx']['h']>first['fx']['h']+55,(
        label,'SEND collapse did not expand FX')
    assert states['001']['items'][0]['fx']['list']>first['fx']['list']+50,(
        label,'extra FX height is not usable visibility')
    assert result['restored']['state']=='001',(
        label,'initial SEND-collapsed preference not restored',result['restored'])
    return result


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



def audit_channel_rearrange(page,label):
    """Actually drag a header right/left; channel IDs and wire state stay put."""
    first=page.locator('#tracks .strip[data-id="1"] .strip-identity-top')
    target=page.locator('#tracks .strip[data-id="4"] .strip-identity-top')
    before=page.evaluate("""() => ({
        ids:[...document.querySelectorAll('#tracks .strip')].map(e=>Number(e.dataset.id)),
        source:document.querySelector('#tracks .strip[data-id="1"] .fxslot-list')?.textContent,
        wires:document.querySelector('#tracks .strip[data-id="1"] .split-count')?.textContent,
        master:document.querySelector('#master .strip')?.dataset.id
    })""")
    assert before['ids'][:4]==[1,2,3,4],(label,'unexpected starting bank order',before)
    bb=target.bounding_box()
    first.drag_to(target,target_position={"x":bb["width"]-5,"y":bb["height"]/2})
    after=page.evaluate("""() => ({
        ids:[...document.querySelectorAll('#tracks .strip')].map(e=>Number(e.dataset.id)),
        source:document.querySelector('#tracks .strip[data-id="1"] .fxslot-list')?.textContent,
        wires:document.querySelector('#tracks .strip[data-id="1"] .split-count')?.textContent,
        master:document.querySelector('#master .strip')?.dataset.id
    })""")
    assert after['ids'][:4]==[2,3,4,1],(label,'drag right did not rearrange channels',after)
    assert after['source']==before['source'] and after['wires']==before['wires'],(
        label,'reorder mutated FX / signal routing')
    assert after['master']==before['master'],(label,'pinned MAIN moved')
    if page.viewport_size['width']==1711:
        page.screenshot(path=str(ROOT / "preview-v16-reordered-1711.png"))
    # Accessible keyboard rearrangement uses same model operation.
    header=page.locator('#tracks .strip[data-id="1"] .strip-identity-top')
    header.focus()
    header.press('Alt+ArrowLeft')
    ids=page.locator('#tracks .strip').evaluate_all(
        "(xs)=>xs.map(x=>Number(x.dataset.id))")
    assert ids[:4]==[2,3,1,4],(label,'Alt+left did not move channel',ids)
    header=page.locator('#tracks .strip[data-id="1"] .strip-identity-top')
    header.drag_to(page.locator('#tracks .strip[data-id="2"] .strip-identity-top'),
        target_position={"x":5,"y":10})
    ids=page.locator('#tracks .strip').evaluate_all(
        "(xs)=>xs.map(x=>Number(x.dataset.id))")
    assert ids[:4]==[1,2,3,4],(label,'drag left failed to restore arrangement',ids)
    return True


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
                if width==1711:
                    # Owner screenshot's exact monitor size, ALL upper sections
                    # expanded. Compare against default SEND-collapsed screenshot.
                    page.locator('.section-rail [data-section="send"]').click()
                    assert not page.locator('.console-body').evaluate(
                        "(el)=>el.classList.contains('collapsed-send')")
                    audit_geometry(page,"1711x1005 all upper sections expanded");count+=1
                    page.screenshot(path=str(ROOT / "preview-v13-all-expanded-1711.png"))
                    page.locator('.section-rail [data-section="send"]').click()
                    page.locator('.section-rail [data-section="route"]').click()
                    page.locator('.section-rail [data-section="fx"]').click()
                    page.screenshot(path=str(ROOT / "preview-v14-all-folded-1711.png"))
                    page.locator('.section-rail [data-section="fx"]').click()
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
                audit_channel_rearrange(page,f"{width}x{height} arrangement");count+=1
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
