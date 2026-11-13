#!/usr/bin/env python3
"""Run headless real-Chromium physical geometry and workflow audit for Pages V7.

Review both the internal authoring source and the sole published /preview/.
This does not validate native ASIO/VST3 audio. Screenshots are uploaded by UI
smoke. All browser pages are local deterministic build outputs.
"""
from __future__ import annotations
from pathlib import Path
import subprocess, sys
from playwright.sync_api import sync_playwright
from approved_publication_fixture import published_console

ROOT = Path(__file__).resolve().parents[1]
PAGE = (ROOT / "site/preview-v2/index.html").resolve()
PUBLIC = (ROOT / "_preview-public-qa/preview/index.html").resolve()
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
            const controls=[...t.querySelectorAll('.switch-section.mx-hw-row > button.mx-hw-key')];
            if(controls.length!==3)problems.push('MUTE/SOLO/PFL count !=3 '+tag);
            else {
                if(Math.max(...controls.map(c=>r(c).width))-Math.min(...controls.map(c=>r(c).width))>1.8)
                    problems.push('inconsistent V6 button widths '+tag);
                if(Math.max(...controls.map(c=>r(c).top))-Math.min(...controls.map(c=>r(c).top))>1)
                    problems.push('inconsistent V6 button baselines '+tag);
                if(controls.some(c=>Math.abs(r(c).height-29)>1))
                    problems.push('29px physical key height regression '+tag);
            }
            const cap=t.querySelector('.fader-travel .cap');
            const density=document.querySelector('.console-body')?.dataset.density;
            const [cw,ch]=density==='compact'?[25,50]:[27,54];
            if(!cap||Math.abs(r(cap).width-cw)>.75||Math.abs(r(cap).height-ch)>.75)
                problems.push('M2 '+cw+'x'+ch+' physical fader cap missing '+tag);
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
        const well=strips[0].querySelector('.fader-box');
        const before=getComputedStyle(well,'::before');
        const wellRgb=(before.backgroundColor.match(/[0-9]+/g)||[]).slice(0,3).map(Number);
        const historicalFlat=before.backgroundImage==='none'&&wellRgb.length===3&&
           Math.max(...wellRgb)<=46&&Math.min(...wellRgb)>=4;
        // Owner-directed rollback: only the original V51 graphite cavity,
        // exactly the surface accepted before V52/V54/V55. V52 bevel is
        // explicitly disallowed; preserving old mechanics is intentional.
        if(!historicalFlat || !before.boxShadow.includes('inset'))
            problems.push('owner-restored flat graphite fader cavity missing');
        if(Math.abs(parseFloat(getComputedStyle(well,'::after').width)-5)>.7)
            problems.push('original 5px fader tail aperture missing');
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






def audit_v29_operator_collapse(page, label):
    """Real published V60: one comfort layout, bounded fader-priority split.

    Keep all eight actual operator fold combinations, not the obsolete V7
    fixed-viewport fader-height law. Legacy V7 geometry remains independently
    tested by audit_p0_collapse on the unassembled authoring source.
    """
    body=page.locator(".console-body")
    assert page.locator("#densitySelect").is_hidden(), label
    assert body.get_attribute("data-density")=="comfort", label
    keys=("route","fx","send")
    rail={key:page.locator(f'.section-rail [data-section="{key}"]') for key in keys}
    original=tuple("collapsed-"+key in (body.get_attribute("class") or "") for key in keys)
    initial_edge=page.locator('#tracks .strip[data-id="5"] .wire-node-out').get_attribute("data-wire-main-edge")
    assert initial_edge, (label,"missing original MAIN EdgeId")
    seen=set()
    try:
        for n in range(8):
            target=tuple(bool(n & (1<<i)) for i in range(3))
            for i,key in enumerate(keys):
                if ("collapsed-"+key in (body.get_attribute("class") or ""))!=target[i]:
                    rail[key].click()
            # V29 measures the actual console on a ResizeObserver/Microtask
            # paint boundary; reading all changes in one synchronous JS loop
            # would sample a stale pre-fold --v13-fader-h value.
            page.wait_for_timeout(60)
            status=tuple("collapsed-"+key in (body.get_attribute("class") or "") for key in keys)
            assert status==target,(label,"incomplete fold state",target,status)
            seen.add(status)
            data=page.evaluate("""() => {
                const root=document.querySelector('.console-body'),
                      strip=document.querySelector('#tracks .strip[data-id="1"]'),
                      master=document.querySelector('#master .strip'),
                      rail=document.querySelector('.section-rail');
                const box=node=>{const b=node.getBoundingClientRect();return {top:b.top,bottom:b.bottom,height:b.height};};
                const cells=['identity','route','fx','send','pan','switch','fader','footer'];
                const slot=k=>k==='identity'?'.strip-identity-top':k==='footer'?'.bottom-section':'.'+k+'-section';
                const rows=Object.fromEntries(cells.map(k=>[
                    k,{strip:box(strip.querySelector(slot(k))),rail:box(rail.querySelector('.rail-cell.'+k))}]));
                return {root:box(root),rows,master:box(master.querySelector('.bottom-section')),
                        fxList:strip.querySelector('.fxslot-list').getBoundingClientRect().height,
                        wireDeck:getComputedStyle(strip.querySelector('.wire-deck')).display,
                        wireTray:getComputedStyle(strip.querySelector('.wire-tray')).display,
                        wireSvg:getComputedStyle(document.querySelector('#wireLayer')).display,
                        sendExpanded:rail.querySelector('[data-section="send"]').getAttribute('aria-expanded'),
                        overflow:strip.getBoundingClientRect().bottom-box(strip.querySelector('.bottom-section')).bottom};
            }""")
            rows=data["rows"]
            for key,part in rows.items():
                actual,expected=part["strip"],part["rail"]
                assert abs(actual["top"]-expected["top"])<=1.5 and abs(actual["bottom"]-expected["bottom"])<=1.5,(
                    label,status,key,"rail/channel misaligned",actual,expected)
            assert rows["fader"]["strip"]["height"]>=120,(label,status,"fader below usable limit",data)
            assert rows["footer"]["strip"]["height"]>=32,(label,status,"missing channel labels",data)
            assert abs(data["overflow"])<=3,(label,status,"strip footer truncated",data)
            assert abs(data["master"]["bottom"]-rows["footer"]["strip"]["bottom"])<=2,(
                label,status,"pinned MAIN footer not aligned",data)
            if target[1]:
                assert rows["fx"]["strip"]["height"]>=20,(label,status,"FX fold lost rack header",data)
            else:
                assert data["fxList"]>=88,(label,status,"expanded FX rack cannot show inserts",data)
            if target[2]:
                assert rows["send"]["strip"]["height"]<=29,(label,status,"WIRE fold not compact",data)
                assert data["wireDeck"]=="none" and data["wireTray"]=="none" and data["wireSvg"]=="none",(
                    label,status,"WIRE fold leaves painted cables",data)
                assert data["sendExpanded"]=="false",(label,status,"WIRE rail fold ARIA drift")
            else:
                assert rows["send"]["strip"]["height"]>=100,(label,status,"expanded WIRE bay missing",data)
                assert data["wireDeck"]!="none" and data["wireTray"]!="none",(
                    label,status,"WIRE dock unexpectedly hidden",data)
                assert data["sendExpanded"]=="true",(label,status,"WIRE rail expand ARIA drift")
            # V60 genuinely lengthens the fader only from spare FX area;
            # preserve bounded geometry even with every bay expanded.
            if not any(target):
                upper=rows["pan"]["strip"]["top"]-data["root"]["top"]
                lower=rows["footer"]["strip"]["bottom"]-rows["pan"]["strip"]["top"]
                assert -3<=lower-upper<=100,(label,"V60 tactile fader priority must be bounded",upper,lower,data)
            assert page.locator('#tracks .strip[data-id="5"] .wire-node-out').get_attribute("data-wire-main-edge")==initial_edge,(
                label,status,"fold changed accepted MAIN EdgeId")
    finally:
        for i,key in enumerate(keys):
            if ("collapsed-"+key in (body.get_attribute("class") or ""))!=original[i]:
                rail[key].click()
        page.wait_for_timeout(60)
    assert len(seen)==8,(label,"missing actual operator fold states",seen)
    assert tuple("collapsed-"+key in (body.get_attribute("class") or "") for key in keys)==original,(
        label,"initial fold state was not restored")
    return {"states":seen}


def audit_p0_collapse(page, label):
    """Every ROUTE/FX/SEND combination preserves PAN/SWITCH/FADER/FOOTER."""
    if page.locator("#densitySelect").is_hidden():
        return audit_v29_operator_collapse(page,label)
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
    expected=max(160,min(530,page.viewport_size['height']-(598 if page.viewport_size['height']>=801 else 584)))
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
                    label,state,i,k,'rail lower not aligned',item[k],frame['rail'][k])
            for k in ('route','fx','send'):
                assert abs(item[k]['top']-frame['rail'][k]['top'])<=1.2,(
                    label,state,i,k,'upper rail mismatch')
                assert abs(item[k]['h']-frame['rail'][k]['h'])<=1.2,(
                    label,state,i,k,'upper rail height mismatch')
        assert frame['style']=='none',(label,state,'strip must have FLAT surface',frame['style'])
        assert all(bg=='none' for bg in frame['faces']),(
            label,state,'broad gradient still paints panel face',frame['faces'])
        if state.endswith('1'):
            assert abs(frame['items'][0]['send']['h']-27)<=1.2,(
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
    # Verify the exact current assembler (approved PNG, FX semantics and
    # compiled CSS), not a comparison against the unassembled authoring HTML.
    assert published_console(PUBLIC.parent.parent)==PUBLIC
    assert 'http-equiv="refresh"' in (PUBLIC.parent/"mixer.html").read_text(encoding="utf-8")
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
                # The modeless manager never blocks mixer interactions. New
                # shared transient utility policy dismisses on an outside
                # mixer click, without stealing selection or track control.
                page.locator('#tracks .strip-identity-top').first.click()
                assert page.locator('#pluginBrowser').is_hidden(), (
                    "outside mixer click must dismiss transient Manager")
                # The authoring HTML still carries a historical Minimize
                # element. The approved assembled/public GUI removes it;
                # assert absence only on the shipped surface below.
                page.locator('#pluginsButton').click()
                assert page.locator('#pluginBrowser').is_visible(), (
                    "Manager must reopen after outside dismissal")
                count+=1
                # Add FX is a different transient utility: it supersedes the
                # Manager after an actual mixer click, without modal blocking.
                page.locator('#tracks .fx-pick.empty').first.click()
                assert page.locator('#fastPicker').is_visible(), "Add FX quick picker absent"
                assert page.locator('#pluginBrowser').is_hidden(), (
                    "outside Add FX click must dismiss previous Manager")
                page.locator('#fastClose').click()
                page.locator('#pluginsButton').click()
                assert page.locator('#pluginBrowser').is_visible(), (
                    "Manager cannot reopen after quick FX close")
                page.locator('#pluginClose').click()
                assert page.locator('#pluginBrowser').is_hidden()
                count+=1
                # The last partially populated page must NOT become a giant bus fader.
                page.locator('#bankNext').click()
                audit_geometry(page,f"{width}x{height} next bank");count+=1
                page.locator('#bankPrev').click()
                audit_channel_rearrange(page,f"{width}x{height} arrangement");count+=1
                assert not errors, f"{width}x{height}: JavaScript runtime errors {errors}"
                page.close()
            # The user-approved /preview/ is the sole public acceptance surface.
            # Verify its real behavior as well as the internal source.
            for width,height in SIZES:
                page=browser.new_page(viewport={"width":width,"height":height},device_scale_factor=1)
                errors=[]
                page.on("pageerror",lambda error: errors.append(str(error)))
                page.goto(PUBLIC.as_uri())
                assert page.locator('#pluginMinimize').count()==0, (
                    "approved public /preview/ must omit obsolete Minimize")
                audit_geometry(page,f"PUBLIC /preview/ {width}x{height}");count+=1
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
                assert page.locator('#pluginBrowser').is_hidden(), (
                    "public preview outside-click must dismiss prior Manager")
                page.locator('#fastClose').click()
                page.locator('#pluginsButton').click()
                assert page.locator('#pluginBrowser').is_visible()
                page.locator('#pluginClose').click()
                assert page.locator('#pluginBrowser').is_hidden()
                page.locator('#bankNext').click()
                audit_geometry(page,f"PUBLIC bank paging {width}x{height}");count+=1
                assert not errors, f"PUBLIC {width}x{height}: {errors}"
                page.close()
        finally:
            browser.close()
    print(f"PASS {count} true-Chromium V7 visual/interaction scenarios")


if __name__ == '__main__':
    main()
