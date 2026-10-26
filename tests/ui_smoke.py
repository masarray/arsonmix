"""UI smoke test (optional): pip install playwright && playwright install chromium"""
import asyncio
import re
from pathlib import Path
from playwright.async_api import async_playwright

async def main():
    project=Path(__file__).resolve().parents[1]
    async with async_playwright() as tool:
        browser=await tool.chromium.launch(headless=True)
        page=await browser.new_page(viewport={"width":1680,"height":920})
        errors=[]
        page.on("pageerror", lambda e: errors.append(str(e)))
        await page.set_content((project/"AR-MIX-Live-FX-Host-Standalone-v0.3.html").read_text())
        assert await page.locator(".strip").count()==13
        # All 12 PAN knobs have been replaced by horizontal slider tracks.
        assert await page.locator('[data-pan]').count()==13
        assert await page.locator('[data-knob="pan"]').count()==0
        pan_z=await page.locator('[data-pan="0"]').evaluate("""e=>({
          center:getComputedStyle(e,'::after').zIndex,
          thumb:getComputedStyle(e.querySelector('.pan-thumb')).zIndex,
          fill:getComputedStyle(e.querySelector('.pan-fill')).zIndex,
          png:getComputedStyle(e.querySelector('.pan-thumb')).backgroundImage
        })""")
        assert int(pan_z['center'])<int(pan_z['thumb']) and int(pan_z['fill'])<int(pan_z['thumb']),pan_z
        pan_offset=await page.locator('[data-pan="0"]').evaluate("""e=>({
          groove:getComputedStyle(e,'::before').top,
          fill:getComputedStyle(e.querySelector('.pan-fill')).top,
          detent:getComputedStyle(e,'::after').top
        })""")
        assert pan_offset=={'groove':'13px','fill':'15px','detent':'12px'},pan_offset
        assert 'data:image/png;base64,' in pan_z['png'],pan_z
        assert ''.join((await page.locator('.brand').inner_text()).split())=='ArSonMix'
        for idx in range(12):
            p=page.locator(f'[data-pan="{idx}"]')
            assert await p.get_attribute('aria-orientation')=='horizontal'
            assert await p.locator('.pan-thumb').count()==1
        # Button 180: SOLID base and exact layered shadow (never PNG/gradient).
        for selector in ['.spec-device',
                         '[data-mute="0"]','[data-solo="0"]','[data-hpf="0"]']:
            v=await page.locator(selector).first.evaluate("""e=>{
                const s=getComputedStyle(e);return {
                    image:s.backgroundImage,shadow:s.boxShadow,
                    color:s.backgroundColor,transition:s.transitionDuration}
            }""")
            assert v['image']=='none' and v['color'].startswith('rgb('), (selector,v)
            assert v['shadow'].count('rgb')>=6,(selector,v)
            assert v['transition'].split(', ')[0]=='0.15s',(selector,v)
        style=await page.locator('[data-mute="0"]').evaluate("""e=>{
          const s=getComputedStyle(e);return {dur:s.transitionDuration,border:s.borderTopWidth,
            radius:s.borderTopLeftRadius,shadow:s.boxShadow};
        }""")
        assert style['border']=='1px' and style['radius']=='3px',style
        # Inter is part of the app payload; no network font or fallback is allowed.
        assert await page.evaluate("""async()=>{
            await document.fonts.load('600 12px "ArSonMix Inter"');
            return document.fonts.check('600 12px "ArSonMix Inter"')&&
                getComputedStyle(document.querySelector('.strip-name')).fontFamily.includes('ArSonMix Inter');
        }"""),'Embedded Inter font was not applied'
        assert [round(float(v.rstrip('s')),3) for v in style['dur'].split(', ')]==[.15,.15,.15,.09,.12,.075],style
        # Visual-architecture regression: four intentionally different hardware
        # families, FX slots recessed rather than taking on button-cap artwork.
        # GUI target: the master is the 13th sibling, not a floating right rail.
        assert await page.locator('#mixer > .strip').count()==13
        assert await page.locator('#mixer > .strip:last-child').get_attribute('id')=='masterLeft'
        assert await page.locator('[data-insert="0:5"]').count()==1
        assert await page.locator('[data-master-insert="5"]').count()==1
        assert await page.locator('.strip-controls .knob-label').filter(has_text="PAN").count()==0
        assert await page.locator('.pan-readline').count()==13
        assert await page.locator('#masterLeft .master-controls-zone .master-analysis').count()==1
        # Six real insert slots fit entirely within each FX rack (no cropping).
        rack_geometry=await page.evaluate("""() => [...document.querySelectorAll('.strip')].every(strip=>{
           const rack=strip.querySelector('.rack,.master-fx-rack');
           const cells=[...rack.querySelectorAll('.insert-slot,.master-fx-slot')];
           const r=rack.getBoundingClientRect();
           return cells.length===6&&cells.every(c=>{
             const b=c.getBoundingClientRect();return b.top>=r.top-.5 && b.bottom<=r.bottom+.5;
           });
        })""")
        assert rack_geometry, 'Some of the six FX slots are clipped'

        assert await page.locator('.right-rail,.master-tabs,.head').count()==0
        assert await page.locator('[data-master-output]').count()==0
        assert await page.locator('.master-stereo-badge,.master-route-label').count()==2
        # Streaming output panel has only two useful readouts.
        assert await page.locator('#masterLeft .master-analysis .master-info-row').count()==2
        assert await page.locator('#masterPeakDb').count()==0
        assert await page.locator('#masterLufs').count()==1
        assert await page.locator('#masterDbtp').count()==1
        assert 'SIM' in await page.locator('#masterLeft .master-analysis').inner_text()
        assert 'LUFS-I' in await page.locator('#masterLeft .master-analysis').inner_text()
        # Buttons must describe and perform actual distinct actions: no "..." blank cap.
        assert await page.locator('.left-rail #addScene').inner_text()=='＋ SAVE SNAPSHOT'
        assert await page.locator('.left-rail #saveFxPreset').inner_text()=='SAVE FX'
        assert await page.locator('.left-rail #menuScene').inner_text()=='EXPORT'
        assert await page.locator('.left-rail .rail-bottom button').count()==3
        # FX Power and nav/rail are the same component skin, with inverted ON semantics.
        assert await page.locator('.toolbar-actions button.fx-control').count()==7
        assert await page.locator('.left-rail button.fx-control').count()==8
        assert await page.locator('.toolbar-actions .btn-180,.left-rail .btn-180').count()==0
        assert await page.locator('.left-rail .pushable,.left-rail .front,.left-rail .edge,.left-rail .shadow').count()==0
        shared=await page.evaluate("""()=>{
          const snap=e=>{const s=getComputedStyle(e),r=e.getBoundingClientRect();return {
            image:s.backgroundImage,shadow:s.boxShadow,ink:s.color,edge:s.borderTopColor,
            radius:s.borderTopLeftRadius,rect:r.toJSON()
          }};
          const p=document.querySelector('[data-fxpower="0:0"]');
          const powered=snap(p);
          p.classList.add('is-bypassed');
          const bypassed=snap(p);
          p.classList.remove('is-bypassed');
          return {powered,bypassed,
            railOff:snap(document.querySelector('[data-preset="Clean Vocal"]')),
            navOff:snap(document.querySelector('[data-view="FX RACK"]')),
            navOn:snap(document.querySelector('[data-view="MIX"]')),
            channelOff:snap(document.querySelector('[data-mute="0"]'))};
        }""")
        for k in ('image','shadow','ink','edge'):
            assert shared['railOff'][k]==shared['navOff'][k]==shared['bypassed'][k],(k,shared)
            assert shared['navOn'][k]==shared['powered'][k],(k,shared)
        assert shared['railOff']['image'].startswith('linear-gradient('),shared
        assert shared['channelOff']['image']=='none',shared
        assert shared['channelOff']['edge']=='rgb(99, 99, 99)',shared
        assert shared['railOff']['radius']==shared['navOn']['radius']=='3px',shared
        preset=page.locator('[data-preset="Clean Vocal"]')
        before=await preset.evaluate("(e)=>e.getBoundingClientRect().toJSON()")
        await preset.hover()
        await page.wait_for_function("""()=>{
          return getComputedStyle(document.querySelector('[data-preset="Clean Vocal"]')).filter==='brightness(1.45)';
        }""",timeout=2500)
        hovered=await preset.evaluate("""e=>({
          border:getComputedStyle(e).borderTopColor,
          image:getComputedStyle(e).backgroundImage,
          box:e.getBoundingClientRect().toJSON()})""")
        assert hovered['border']==shared['railOff']['edge'] and hovered['image']==shared['railOff']['image'],hovered
        assert all(abs(before[k]-hovered['box'][k])<=.2 for k in ('x','y','width','height')),hovered
        await preset.click()
        assert await preset.evaluate("(e)=>e.classList.contains('active')")
        await page.wait_for_timeout(120)
        active=await preset.evaluate("""e=>({
          image:getComputedStyle(e).backgroundImage,
          edge:getComputedStyle(e).borderTopColor,
          ink:getComputedStyle(e).color,
          rect:e.getBoundingClientRect().toJSON()})""")
        for k in ('image','edge','ink'):
            assert active[k]==shared['powered'][k],(k,active,shared['powered'])
        assert all(abs(before[k]-active['rect'][k])<=.2 for k in ('x','y','width','height')),active
        await page.mouse.move(1,1)
        # FX Power transient press also applies to nav without reflow.
        nav=page.locator('[data-view="FX RACK"]')
        await nav.hover()
        normal=await nav.evaluate("(e)=>e.getBoundingClientRect().toJSON()")
        await page.mouse.down()
        await page.wait_for_timeout(130)
        down=await nav.evaluate("""e=>({
          t:getComputedStyle(e).transform,r:e.getBoundingClientRect().toJSON()})""")
        assert down['t']=='matrix(1, 0, 0, 1, 0, 1)',down
        assert abs(down['r']['y']-normal['y']-1)<=.2,down
        await page.mouse.up()
        await page.wait_for_timeout(125)
        assert await nav.evaluate("(e)=>getComputedStyle(e).transform")=='matrix(1, 0, 0, 1, 0, 0)'
        await page.mouse.move(1,1)
        # Restore factory FX chain so this visual-button test cannot change later FX tests.
        await page.evaluate("""()=>{
          channels[0].inserts=[...initialFactoryChannels[0].inserts];
          document.querySelectorAll('[data-preset]').forEach(b=>b.classList.remove('active'));
          renderMixer();
        }""")
        assert await page.locator('#saveFxPreset').count()==1
        assert await page.locator('.left-rail .rail-input-footer').count()==1
        railGeometry=await page.evaluate("""()=>{
          const rect=s=>{const r=document.querySelector(s).getBoundingClientRect();return {top:r.top,bottom:r.bottom,width:r.width,height:r.height}};
          const rail=rect('.left-rail'),channels=rect('#channelList');
          const presets=rect('#presetList'),snapshots=rect('#scenesList');
          const actions=rect('.rail-bottom'),input=rect('.rail-input-footer');
          const sections=[...document.querySelectorAll('.left-rail .rail-section-title')].map(e=>e.getBoundingClientRect());
          return {rail,channels,presets,snapshots,actions,input,sectionTops:sections.map(r=>r.top),
            emptyControls:[...document.querySelectorAll('.left-rail .rail-bottom button')].some(b=>!b.innerText.trim()),
            buttons:[...document.querySelectorAll('.left-rail .rail-bottom button')].map(b=>({width:b.getBoundingClientRect().width,height:b.getBoundingClientRect().height}))};
        }""")
        assert railGeometry['channels']['bottom']<=railGeometry['sectionTops'][0]+2,railGeometry
        assert railGeometry['actions']['bottom']<=railGeometry['rail']['bottom']+1,railGeometry
        assert railGeometry['input']['bottom']<=railGeometry['rail']['bottom']+1,railGeometry
        assert railGeometry['snapshots']['bottom']<=railGeometry['actions']['top']+1,railGeometry
        assert not railGeometry['emptyControls'],railGeometry
        assert all(btn['width']>=40 and btn['height']>=25 for btn in railGeometry['buttons']),railGeometry
        await page.screenshot(path=str(project/'mixer-master-dual-metric-left-rail.png'),full_page=True)
        off_mute=await page.locator('[data-mute="0"]').evaluate("(e)=>getComputedStyle(e).getPropertyValue('--b180-color').trim()")
        await page.locator('[data-mute="0"]').click()
        mute_on=await page.locator('[data-mute="0"]').evaluate("(e)=>getComputedStyle(e).getPropertyValue('--b180-color').trim()")
        assert mute_on!=off_mute and await page.locator('[data-mute="0"]').get_attribute('aria-pressed')=='true'
        await page.locator('[data-mute="0"]').click()
        await page.locator('[data-solo="0"]').click()
        solo_on=await page.locator('[data-solo="0"]').evaluate("(e)=>getComputedStyle(e).getPropertyValue('--b180-color').trim()")
        assert solo_on!=mute_on
        await page.locator('[data-solo="0"]').click()
        await page.locator('[data-hpf="0"]').click()
        hpf_on=await page.locator('[data-hpf="0"]').evaluate("(e)=>getComputedStyle(e).getPropertyValue('--b180-color').trim()")
        assert hpf_on not in (mute_on,solo_on,off_mute)
        await page.locator('[data-hpf="0"]').click()
        await page.screenshot(path=str(project/'mixer-crystal-candy-review.png'),full_page=True)
        await page.screenshot(path=str(project/'mixer-crystal-depth-glow-review.png'),full_page=True)
        await page.screenshot(path=str(project/'mixer-inter-recessed-toggle-review.png'),full_page=True)
        await page.screenshot(path=str(project/'mixer-fixed-buttons-3d-onoff.png'),full_page=True)
        # Verify true geometric alignment of each source MUTE/SOLO and master MUTE/STEREO.
        actions=await page.evaluate("""()=>{
          const bounds=s=>{const r=document.querySelector(s).getBoundingClientRect();
            return {top:r.top,height:r.height,middle:r.top+r.height/2,width:r.width}};
          const master=bounds('#masterMute'),stereo=bounds('.master-stereo-badge');
          return {master,stereo,rows:[...document.querySelectorAll('.strip:not(.master-strip) .channel-actions')].map(row=>{
            const mute=row.querySelector('.mute'),solo=row.querySelector('.solo');
            return {mute:bounds('[data-mute="'+mute.dataset.mute+'"]'),solo:bounds('[data-solo="'+solo.dataset.solo+'"]')};
          })};
        }""")
        assert abs(actions['master']['middle']-actions['stereo']['middle'])<=1,actions
        assert all(abs(row[key]['middle']-actions['master']['middle'])<=1 for row in actions['rows'] for key in ('mute','solo')),actions
        assert all(abs(row['mute']['middle']-row['solo']['middle'])<=1 for row in actions['rows']),actions
        assert actions['stereo']['height']==actions['master']['height']==29,actions
        header=await page.evaluate("""()=>{
          const btns=[...document.querySelectorAll('.toolbar .nav button')];
          return {buttons:btns.map(b=>{const r=b.getBoundingClientRect();return {width:r.width,mid:r.top+r.height/2,right:r.right}}),
            device:(()=>{const r=document.querySelector('.spec-device').getBoundingClientRect();return r.top+r.height/2})()};
        }""")
        assert all(b['width']>=77 and abs(b['mid']-header['device'])<=1 for b in header['buttons']),header

        assert await page.locator('header .nav button').count()==7
        assert await page.locator('footer.status button').count()==0
        assert await page.locator('[data-clear-all-clips]').count()==1
        architecture=await page.evaluate("""() => {
          const bg=s=>getComputedStyle(document.querySelector(s)).backgroundImage;
          const rect=s=>{const r=document.querySelector(s).getBoundingClientRect();return {x:r.x,y:r.y,right:r.right,bottom:r.bottom,height:r.height}};
          const main=document.querySelector('[data-strip="0"]'),master=document.querySelector('#masterLeft');
          const overflow=[...document.querySelectorAll('.strip .channel-actions button,.strip .utility-row button')].some(el=>{
            const b=el.getBoundingClientRect(),p=el.closest('.strip').getBoundingClientRect();
            return b.left<p.left-1||b.right>p.right+1;
          });
          return {nav:bg('.nav button.selected'),small:bg('[data-mute="0"]'),
            long:bg('.rail-presets button'),fx:bg('.strip .rack .insert'),chrome:bg('.spec-device'),
            navBounds:rect('.nav'),overflow,
            mainHeader:rect('[data-strip="0"] .ch-id'),masterHeader:rect('#masterLeft .ch-id'),
            mainBottom:rect('[data-strip="0"] .strip-name'),masterBottom:rect('#masterLeft .strip-name'),
            mainFader:rect('[data-strip="0"] .fader-zone'),masterFader:rect('#masterLeft .fader-zone'),
            selection:getComputedStyle(main,'::before').borderTopWidth,
            masterRows:[...master.children].map(x=>({cls:x.className,height:x.getBoundingClientRect().height}))};
        }""")
        assert architecture['small']=='none',architecture
        assert all(architecture[k].startswith('linear-gradient(') for k in ('nav','long')),architecture
        assert 'data:image/png;base64' not in architecture['fx']
        assert 'data:image/png;base64' not in architecture['chrome']
        assert not architecture['overflow']
        assert architecture['navBounds']['right']<=1680+1,architecture
        assert await page.locator('.toolbar-actions .nav button').count()==7
        assert await page.locator('#masterLeft').evaluate(
            '(m)=>m.getBoundingClientRect().width>document.querySelector("[data-strip=\\\"0\\\"]").getBoundingClientRect().width+30')
        for key in ('Header','Bottom','Fader'):
            assert abs(architecture['main'+key]['y']-architecture['master'+key]['y'])<=2,architecture
        assert abs(architecture['mainBottom']['bottom']-architecture['masterBottom']['bottom'])<=2,architecture
        assert architecture['selection']=='0px',architecture
        soft=await page.locator('[data-strip="0"]').evaluate("""e=>{
          const sel=getComputedStyle(e),a=getComputedStyle(e,'::before');
          return {shadow:sel.boxShadow,border:a.borderTopWidth,halo:a.boxShadow};
        }""")
        assert soft['border']=='0px' and soft['halo']!='none',soft
        await page.screenshot(path=str(project/'mixer-design-review.png'),full_page=True)
        await page.screenshot(path=str(project/'mixer-left-rail-fx-power-pan-131512.png'),full_page=True)
        # Medium desktop check; channel strips may scroll *inside* workbench.
        await page.set_viewport_size({"width":1440,"height":820})
        await page.screenshot(path=str(project/'mixer-design-review-1440.png'),full_page=True)
        smallRail=await page.evaluate("""()=>{
          const rail=document.querySelector('.left-rail').getBoundingClientRect();
          const footer=document.querySelector('.left-rail .rail-input-footer').getBoundingClientRect();
          const buttons=[...document.querySelectorAll('.left-rail .rail-bottom button')].map(e=>e.getBoundingClientRect());
          return {bottom:rail.bottom,footerBottom:footer.bottom,buttons:buttons.map(b=>({right:b.right,left:b.left,width:b.width}))};
        }""")
        assert smallRail['footerBottom']<=smallRail['bottom']+1,smallRail
        assert all(b['width']>35 for b in smallRail['buttons']),smallRail
        assert await page.locator('.strip').count()==13
        # The user's requirement: the SAME size/position before, during, and after toggle.
        # OFF/ON use the exact same shadow-layer geometry, differing only in palette.
        def switch_state(selector):
            return page.locator(selector).evaluate("""e=>{
              const s=getComputedStyle(e),r=e.getBoundingClientRect();
              return {t:s.transform,shadow:s.boxShadow,color:s.backgroundColor,
                ink:s.color,left:r.left,top:r.top,width:r.width,height:r.height,
                selected:e.classList.contains('active')||e.classList.contains('selected')};
            }""")
        def same_rect(a,b):
            return all(abs(a[k]-b[k])<=.2 for k in ('left','top','width','height'))
        off=await switch_state('[data-mute="0"]')
        assert off['t']=='matrix(1, 0, 0, 1, 0, 0)',off
        await page.locator('[data-mute="0"]').click()
        await page.wait_for_function("""()=>{
            const e=document.querySelector('[data-mute="0"]');
            return e.classList.contains('active')&&getComputedStyle(e).boxShadow.includes('inset');
        }""",timeout=2500)
        await page.wait_for_timeout(190) # let the original 150 ms shadow animation settle
        on=await switch_state('[data-mute="0"]')
        assert on['selected'] and shared_shadow_shape(on['shadow'])==shared_shadow_shape(off['shadow']),(off,on)
        assert on['color']!=off['color'],(off,on)
        assert on['t']==off['t'] and same_rect(off,on),(off,on)
        assert off['color']=='rgb(133, 130, 131)' and off['ink']=='rgb(43, 43, 43)',(off,on)
        assert on['ink']!=off['ink'],(off,on)
        assert on['shadow'].startswith('rgba(0, 0, 0, 0.72)') or '0.72' in on['shadow'],on
        await page.locator('[data-mute="0"]').click()
        await page.mouse.move(1,1)  # OFF after pointer leaves, not the intentional glossy hover state
        await page.wait_for_timeout(190)
        back=await switch_state('[data-mute="0"]')
        assert back['t']==off['t'] and same_rect(off,back),(off,back)
        assert back['shadow']==off['shadow'],(off,back)
        # Source A/B controls are gone; fader travel expands into their old space.
        assert await page.locator('[data-send]').count()==0
        assert await page.locator('#mixer > .strip:not(.master-strip) .fader-tools').count()==0
        geometry=await page.evaluate("""()=>[...document.querySelectorAll('#mixer > .strip')].map(s=>{
          const n=s.querySelector('.strip-name').getBoundingClientRect();
          const f=s.querySelector('.fader-zone').getBoundingClientRect();
          return {master:s.classList.contains('master-strip'),
            faderBottom:f.bottom,nameTop:n.top,nameBottom:n.bottom,
            hasTools:!!s.querySelector('.fader-tools')};
        })""")
        assert len(geometry)==13 and all(x['nameTop']>=x['faderBottom']-1 for x in geometry),geometry
        assert all(not x['hasTools'] for x in geometry if not x['master']),geometry
        assert sum(x['hasTools'] for x in geometry)==1,geometry
        # Both resting states share depth; while pointer is down only, cap sinks 1px
        # with a stronger inner shadow and unchanged width/height/layout slot.
        btn=page.locator('[data-mute="0"]')
        before=await btn.evaluate("""e=>({
            shadow:getComputedStyle(e).boxShadow,
            before:getComputedStyle(e,'::before').content,
            after:getComputedStyle(e,'::after').content})""")
        assert before['before']=='none' and before['after']=='none',before
        press_before=await btn.evaluate("(e)=>e.getBoundingClientRect().toJSON()")
        await btn.hover()
        await page.mouse.down()
        await page.wait_for_timeout(190)
        during=await btn.evaluate("""e=>({
            t:getComputedStyle(e).transform,rect:e.getBoundingClientRect().toJSON(),
            shadow:getComputedStyle(e).boxShadow})""")
        assert during['t']=='matrix(1, 0, 0, 1, 0, 1)',during
        assert abs(during['rect']['y']-press_before['y']-1)<=.2,during
        assert all(abs(during['rect'][k]-press_before[k])<=.2 for k in ('x','width','height')),during
        assert during['shadow']!=before['shadow'] and '0.82' in during['shadow'],(before,during)
        await page.mouse.up()
        await page.wait_for_timeout(190)
        assert await btn.get_attribute('aria-pressed')=='true'
        released_on=await btn.evaluate("""e=>({
            t:getComputedStyle(e).transform, shadow:getComputedStyle(e).boxShadow,
            rect:e.getBoundingClientRect().toJSON()
        })""")
        assert released_on['t']=='matrix(1, 0, 0, 1, 0, 0)',released_on
        assert abs(released_on['rect']['y']-press_before['y'])<=.2,released_on
        assert shared_shadow_shape(released_on['shadow'])==shared_shadow_shape(before['shadow']),(before,released_on)
        # Re-press while already ON; it must have the same 1px tactile travel.
        await btn.hover()
        await page.mouse.down()
        await page.wait_for_timeout(120)
        pressed_on=await btn.evaluate("""e=>({
            t:getComputedStyle(e).transform,shadow:getComputedStyle(e).boxShadow,
            rect:e.getBoundingClientRect().toJSON()
        })""")
        assert pressed_on['t']=='matrix(1, 0, 0, 1, 0, 1)',pressed_on
        assert abs(pressed_on['rect']['y']-press_before['y']-1)<=.2,pressed_on
        assert all(abs(pressed_on['rect'][k]-press_before[k])<=.2 for k in ('x','width','height')),pressed_on
        assert '0.82' in pressed_on['shadow'],pressed_on
        await page.mouse.up()
        await page.wait_for_timeout(190)
        assert await btn.get_attribute('aria-pressed')=='false'
        assert await btn.evaluate("(e)=>getComputedStyle(e).transform")=='matrix(1, 0, 0, 1, 0, 0)'
        released_off=await btn.evaluate("(e)=>e.getBoundingClientRect().toJSON()")
        assert abs(released_off['y']-press_before['y'])<=.2,released_off
        widths=await page.evaluate("""()=>{
          const mixer=document.querySelector('#mixer'),master=document.querySelector('#masterLeft');
          const source=document.querySelector('[data-strip="0"]');
          return {master:master.getBoundingClientRect().width,source:source.getBoundingClientRect().width,
             visible:master.getBoundingClientRect().right<=mixer.getBoundingClientRect().right+1,
             viewport:mixer.getBoundingClientRect().width};
        }""")
        assert widths['master']>=168 and widths['master']>widths['source']+25, widths
        assert widths['visible'],widths
        await page.set_viewport_size({"width":1680,"height":920})
        # Hardware-style PAN illumination must grow from center, both L and R.
        pan_fill=page.locator('[data-pan="1"] .pan-fill')
        assert await pan_fill.count()==1
        p1=page.locator('[data-pan="1"]')
        await p1.focus()
        await p1.press('Home')
        at_center=await pan_fill.bounding_box()
        assert at_center['width']<1,at_center
        await p1.press('End')
        assert 'drop-shadow' in await p1.locator('.pan-thumb').evaluate('(e)=>getComputedStyle(e).filter')
        right_fill=await pan_fill.bounding_box()
        assert right_fill['width']>10,right_fill
        await p1.press('Home')
        await p1.press('ArrowLeft')
        left_small=await pan_fill.bounding_box()
        assert left_small['width']>0,left_small
        for _ in range(20):
            await p1.press('Shift+ArrowLeft')
        left_fill=await pan_fill.bounding_box()
        assert left_fill['width']>10 and left_fill['x']<at_center['x'],left_fill
        await p1.press('Home')
        # Inline strip FX power and full FX Rack power reflect same saved state.
        assert await page.locator('.strip .insert-slot').count()==72
        inline=page.locator('[data-fxpower="0:0"]')
        assert await inline.get_attribute('aria-pressed')=='true'
        assert await page.locator('[data-insert="0:0"]').count()==1
        await inline.click()
        assert await inline.get_attribute('aria-pressed')=='false'
        assert await page.locator('[data-fxpower="0:0"]').locator('xpath=..').evaluate("(x)=>x.classList.contains('is-bypassed')")
        assert await page.evaluate('bypassed["0:0"]') is True
        assert await page.locator('#pluginModal').is_hidden()
        await page.locator('[data-view="FX RACK"]').click()
        rack_power=page.locator('[data-chainbypass="0"]')
        assert await rack_power.get_attribute('aria-pressed')=='false'
        await rack_power.click()
        assert await rack_power.get_attribute('aria-pressed')=='true'
        await page.locator('[data-view="MIX"]').click()
        assert await page.locator('[data-fxpower="0:0"]').get_attribute('aria-pressed')=='true'
        assert await page.evaluate('durableSession().bypassed["0:0"]') is False
        await page.locator('#masterMute').click()
        assert await page.locator('#masterMute').inner_text()=='MUTE'
        assert await page.locator('#masterMute').get_attribute('aria-pressed')=='true'
        await page.locator('#masterMute').click()
        assert await page.locator('#masterMute').inner_text()=='MUTE'
        # Hover and active feedback stays visual and does not change native fader geometry.
        cap0=page.locator('[data-fader="0"] .cap')
        await cap0.hover()
        fader_hover=await cap0.evaluate("(e)=>getComputedStyle(e).filter")
        assert 'brightness(1.19)' in fader_hover, fader_hover
        button=page.locator('[data-mute="0"]')
        await button.hover()
        await page.wait_for_timeout(150)  # Finish 90 ms physical-switch filter transition.
        button_hover=await button.evaluate("(e)=>getComputedStyle(e).filter")
        assert button_hover=='brightness(1.32)',button_hover
        # FX power and FX name remain separate inside the 104px channel strip.
        assert await page.evaluate("""() => [...document.querySelectorAll('.strip .insert-slot')].every(slot=>{
            const cell=slot.getBoundingClientRect(),strip=slot.closest('.strip').getBoundingClientRect();
            return cell.x>=strip.x-1&&cell.right<=strip.right+1&&cell.height===27
        })"""), 'FX power overflow or slot geometry changed'
        # Channel identities are real editable state, not only colored labels.
        await page.locator('[data-editstrip="0"]').click()
        assert await page.locator('#channelIdentity').is_visible()
        anchor=await page.evaluate("""()=>{
          const n=document.querySelector('[data-editstrip="0"]').getBoundingClientRect();
          const p=document.querySelector('.identity-panel').getBoundingClientRect();
          return {nameTop:n.top,nameCenter:n.left+n.width/2,popupBottom:p.bottom,popupLeft:p.left,
            popupRight:p.right,popupCenter:p.left+p.width/2,overlay:getComputedStyle(document.querySelector('.identity-overlay')).backdropFilter,
            active:document.querySelector('[data-strip="0"]').classList.contains('identity-editing')};
        }""")
        assert anchor['popupBottom']<=anchor['nameTop']-7,anchor
        assert anchor['popupLeft']>=0 and anchor['popupRight']<=1680,anchor
        assert anchor['active'] and anchor['overlay']=='none',anchor
        assert await page.locator('#modeToggle').count()==0
        await page.locator('#identityName').fill('Vocal Main')
        await page.locator('[data-icon-choice="🎙"]').click()
        await page.locator('[data-color-choice="#cf6b1e"]').click()
        await page.locator('#identitySave').click()
        assert await page.locator('#channelIdentity').is_hidden()
        assert await page.locator('[data-editstrip="0"] .strip-caption').inner_text()=='Vocal Main'
        assert await page.locator('[data-editstrip="0"] .track-icon').inner_text()=='🎙'
        assert await page.evaluate('durableSession().channels[0].color')=='#cf6b1e'
        # Channel name editor cancel must not overwrite committed changes.
        await page.locator('[data-editstrip="0"]').click()
        await page.locator('#identityName').fill('This Should Not Persist')
        await page.locator('#identityCancel').click()
        assert await page.locator('[data-editstrip="0"] .strip-caption').inner_text()=='Vocal Main'
        # Master FX chain has its own slots and independent quick power.
        assert await page.locator('.master-fx-slot').count()==6
        assert await page.locator('[data-fader="master"] .cap.master-cap').count()==1
        master_fader_debug=await page.locator('[data-fader="master"] .cap.master-cap').evaluate("""(el)=>{
            const c=getComputedStyle(el),p=getComputedStyle(el.parentElement),r=el.getBoundingClientRect();
            return {display:c.display,visibility:c.visibility,opacity:c.opacity,background:c.backgroundImage.slice(0,62),
              parent:{height:el.parentElement.getBoundingClientRect().height,width:el.parentElement.getBoundingClientRect().width},
              rect:{x:r.x,y:r.y,width:r.width,height:r.height},top:c.top,zIndex:c.zIndex,pos:el.style.getPropertyValue('--pos'),parentPosition:p.position};
        }""")
        assert master_fader_debug['display']!='none' and master_fader_debug['visibility']=='visible'
        assert (master_fader_debug['rect']['width'],master_fader_debug['rect']['height'])==(31,90)
        assert master_fader_debug['background'].startswith('url("data:image/png;base64,')
        decoded=await page.locator('[data-fader="master"] .cap').evaluate("""async el => {
          const bg=getComputedStyle(el).backgroundImage;
          const image=new Image();image.src=bg.slice(5,-2);
          await image.decode();
          return {width:image.naturalWidth,height:image.naturalHeight};
        }""")
        assert decoded=={'width':31,'height':90}, 'Master red PNG did not decode'
        a=await page.locator('[data-fader="0"]').bounding_box()
        m=await page.locator('[data-fader="master"]').bounding_box()
        assert abs(a['y']-m['y'])<=2,(a,m)
        assert await page.locator('#modeToggle').count()==0
        # All strips retain a single, optimized height; no PRO/EASY mode switch.
        assert abs(a['y']-m['y'])<=2,(a,m)



        masterPan=page.locator('[data-pan="master"]')
        assert await masterPan.get_attribute('aria-valuenow')=='0'
        await masterPan.focus()
        await masterPan.press('ArrowRight')
        assert await masterPan.get_attribute('aria-valuenow')=='1'
        await masterPan.press('Home')
        assert await masterPan.get_attribute('aria-valuenow')=='0'
        assert await page.evaluate('durableSession().masterPan')==0
        assert await page.locator('[data-master-insert="0"]').count()==1
        await page.locator('[data-master-power="0"]').click()
        assert await page.locator('[data-master-power="0"]').get_attribute('aria-pressed')=='false'
        assert await page.evaluate('durableSession().masterBypassed["0"]') is True
        await page.locator('[data-master-power="0"]').click()
        assert await page.locator('[data-master-power="0"]').get_attribute('aria-pressed')=='true'
        # Master insert selection can open an editor and replace the selected FX.
        await page.locator('[data-master-insert="0"]').click()
        assert await page.locator('#pluginModal').is_visible()
        await page.locator('#closeBrowser').click()
        assert await page.locator('#pluginModal').is_hidden()
        assert await page.locator('#masterLufs').count()==1
        assert await page.locator('#masterDbtp').count()==1
        assert await page.locator('[data-clip]').count()==14
        assert await page.locator('[data-meter="masterL"]').count()==1
        assert await page.locator('[data-meter="masterR"]').count()==1
        await page.evaluate('updateMeterVisual("masterL",99,performance.now())')
        assert await page.locator('[data-clip="masterL"]').evaluate('(e)=>e.classList.contains("latched")')
        await page.locator('[data-clip="masterL"]').click()
        assert not await page.locator('[data-clip="masterL"]').evaluate('(e)=>e.classList.contains("latched")')
        assert not await page.locator('.strip .insert-led').count()
        assert await page.locator('.strip .insert-name').first.inner_text() != ''
        assert await page.locator('.strip .insert-add-glyph').count()>0
        await page.screenshot(path=str(project/'mixer-master-clip-review.png'),full_page=True)
        # Click any passive part of master strip, it must gain the same full border.
        await page.locator('#masterLeft .master-ch-id').click()
        assert await page.locator('#masterLeft').evaluate('(e)=>e.classList.contains("selected")')
        assert await page.locator('[data-strip="0"]').evaluate('(e)=>!e.classList.contains("selected")')
        await page.locator('[data-strip="0"] .ch-id').click()
        assert await page.locator('#masterLeft').evaluate('(e)=>!e.classList.contains("selected")')

        pan=page.locator('[data-pan="0"]')
        await pan.focus()
        await pan.press('Home')
        assert await pan.get_attribute('aria-valuenow')=='0'
        await pan.press('ArrowRight')
        assert await pan.get_attribute('aria-valuenow')=='1'
        await pan.press('Shift+ArrowLeft')
        assert await pan.get_attribute('aria-valuenow')=='-9'
        await pan.press('End')
        assert await pan.get_attribute('aria-valuenow')=='100'
        await pan.press('Home')
        bbox=await pan.bounding_box()
        await page.mouse.click(bbox['x']+bbox['width']*.76,bbox['y']+20)
        assert int(await pan.get_attribute('aria-valuenow'))>0
        await pan.press('Home')
        thumb=await pan.locator('.pan-thumb').bounding_box()
        x=thumb['x']+thumb['width']/2
        y=thumb['y']+thumb['height']/2
        await page.mouse.move(x,y)
        await page.mouse.down()
        assert await pan.get_attribute('aria-valuenow')=='0','PAN jumped on grab'
        await page.mouse.move(x+14,y,steps=3)
        assert int(await pan.get_attribute('aria-valuenow'))>0
        await page.mouse.up()
        await pan.dblclick()
        assert await pan.get_attribute('aria-valuenow')=='0'
        assert not await page.locator('#editor').is_visible(),'PAN double click opened editor'

        # Same Button 180 construction, distinct solid color for each ON state.
        async def image_for(selector):
            return await page.locator(selector).first.evaluate(
                '(b) => getComputedStyle(b).backgroundColor')
        off_mute=await image_for('[data-mute="0"]')
        assert off_mute.startswith('rgb(')
        assert off_mute == await image_for('[data-solo="0"]')
        await page.locator('[data-mute="0"]').click()
        assert await page.locator('[data-mute="0"]').get_attribute('aria-pressed') == 'true'
        # btn-180 intentionally animates background for 150ms. Wait for actual final RGB.
        await page.wait_for_function("""()=>getComputedStyle(document.querySelector('[data-mute="0"]')).backgroundColor==='rgb(231, 111, 131)'""",timeout=2500)
        assert await image_for('[data-mute="0"]') != await image_for('[data-solo="0"]'), await page.evaluate("""()=>{
          return [...document.querySelectorAll('[data-mute="0"],[data-solo="0"]')].map(e=>({
            cls:e.className,background:getComputedStyle(e).backgroundColor,
            token:getComputedStyle(e).getPropertyValue('--b180-color'),
            bgImage:getComputedStyle(e).backgroundImage
          }))
        }""")
        await page.locator('[data-solo="0"]').click()
        assert await page.locator('[data-solo="0"]').get_attribute('aria-pressed') == 'true'
        await page.wait_for_function("""()=>getComputedStyle(document.querySelector('[data-solo="0"]')).backgroundColor==='rgb(219, 183, 92)'""",timeout=2500)
        assert await image_for('[data-solo="0"]') != await image_for('[data-mute="0"]')
        await page.locator('[data-mute="0"]').click()
        await page.locator('[data-solo="0"]').click()
        await page.locator('#masterMute').click()
        assert await page.locator('#masterMute').get_attribute('aria-pressed') == 'true'
        await page.locator('#masterMute').click()
        assert await page.locator('#dim,#talkBtn,[data-master-tab]').count()==0

        # The source image has a real metallic body and a long cast shadow.
        # The body must travel inside the track, centered on image y=35.
        geometry=await page.evaluate("""async () => {
            const slots=[...document.querySelectorAll('[data-fader]')];
            const heads=slots.map(f => {
                const c=f.querySelector('.cap'),b=c.getBoundingClientRect(),r=f.getBoundingClientRect();
                const s=getComputedStyle(c);
                return {width:b.width,height:b.height,bodyInside:b.top>=r.top-1 && b.top+66<=r.bottom+1,
                        top:b.top,trackTop:r.top,radius:s.borderRadius,
                        bg:s.backgroundImage,transform:s.transform};
            });
            const uri=heads[0].bg.match(/data:image\\/png;base64,[a-zA-Z0-9+/=]+/)?.[0];
            if(!uri) throw new Error('Image data URI not found');
            const image=new Image();image.src=uri;await image.decode();
            return {heads,imgW:image.naturalWidth,imgH:image.naturalHeight};
        }""")
        assert len(geometry['heads'])==13
        assert (geometry['imgW'],geometry['imgH'])==(31,90), geometry
        for g in geometry['heads']:
            assert (g['width'],g['height'])==(31,90),g
            assert g['bodyInside'],g
            assert g['radius']=='0px',g
            assert 'matrix(1, 0, 0, 1, -15.5, -35)' in g['transform'],g
        for fader in ('0','master'):
            ctrl=page.locator(f'[data-fader="{fader}"]')
            await ctrl.focus()
            await ctrl.press('Home')
            assert float(await ctrl.get_attribute('aria-valuenow'))==-60
            await ctrl.press('End')
            assert float(await ctrl.get_attribute('aria-valuenow'))==10
        # Repaint at 0 dB, then dragging the head starts without a value jump.
        channel=page.locator('[data-fader="0"]')
        await channel.focus()
        for _ in range(20):
            await channel.press('ArrowDown')
        original=float(await channel.get_attribute('aria-valuenow'))
        cap=channel.locator('.cap')
        rect=await cap.bounding_box()
        cx=rect['x']+rect['width']/2
        cy=rect['y']+35
        await page.mouse.move(cx,cy)
        await page.mouse.down()
        assert float(await channel.get_attribute('aria-valuenow'))==original
        await page.mouse.move(cx,cy+18,steps=3)
        assert float(await channel.get_attribute('aria-valuenow'))<original
        await page.mouse.up()
        # Rail click/drag uses a relative offset; unlike the old absolute-position
        # algorithm it never teleports the thumb under the initial pointer.
        for name in ('0','master'):
            rail=page.locator(f'[data-fader="{name}"]')
            await rail.focus()
            await rail.press('Home')
            await rail.press('PageUp')
            baseline=float(await rail.get_attribute('aria-valuenow'))
            box=await rail.bounding_box()
            await page.mouse.move(box['x']+box['width']/2,box['y']+box['height']*.75)
            await page.mouse.down()
            assert float(await rail.get_attribute('aria-valuenow'))==baseline,(name,baseline)
            await page.mouse.move(box['x']+box['width']/2,box['y']+box['height']*.75-18,steps=3)
            assert float(await rail.get_attribute('aria-valuenow'))>baseline,(name,baseline)
            await page.mouse.up()
        await page.screenshot(path=str(project/'fader-ui-smoke.png'),full_page=True)
        await page.locator('[data-view="FX RACK"]').click()
        await page.locator('[data-addplugin]').click()
        await page.locator("#pluginSearch").fill("de-esser")
        await page.locator('[data-pluginname="De-Esser"]').click()
        assert await page.locator(".chain-node").count() == 5
        await page.locator('[data-chainedit="0"]').click()
        await page.locator('[data-fxparam="mix"]').fill("72")
        assert await page.evaluate('fxParams["0:0"].mix')==72
        await page.locator('[data-chainmove="1"]').first.click()
        assert await page.evaluate('fxParams["0:1"].mix')==72
        await page.locator('[data-view="SIGNAL FLOW"]').click()
        assert await page.locator('.flow-node').count()==4
        await page.locator('#audioSetupBtn').click()
        assert await page.locator('#toast').inner_text()=='Audio Setup is available in the Windows desktop app'
        assert await page.locator('.setup-section').count()==0
        assert await page.locator('.editor').count()==0
        assert await page.locator('.status button').count()==0
        await page.locator('[data-view="MIX"]').click()
        assert await page.locator('#modeToggle').count()==0
        assert await page.locator('.app.easy').count()==1
        assert not errors, errors
        await browser.close()
    print("PASS: asset PNG for 13 faders, detent alignment, no-jump drag, endpoints, FX chain, no JS errors")

if __name__=="__main__":asyncio.run(main())
