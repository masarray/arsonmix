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
        # Mixer Button 180 must retain OFF/ON shadow *geometry*; only colors differ.
        def shared_shadow_shape(shadow):
            return re.sub(r'rgba?\([^)]*\)', '<color>', shadow)
        page.on("pageerror", lambda e: errors.append(str(e)))
        await page.set_content((project/"AR-MIX-Live-FX-Host-Standalone-v0.3.html").read_text())
        assert await page.locator(".strip").count()==11
        assert await page.locator("#channelList [data-choose]").count()==10
        assert await page.locator("[data-strip=\"10\"], [data-strip=\"11\"]").count()==0

        # P0 visual fix: mono gets ONE recessed LED slot, stereo gets TWO.
        # Existing 7px segment rendering and physical meter semantics are untouched.
        lcd=await page.evaluate("""()=>{
          const mono=document.querySelector('#mixer .strip:not(.master-strip) .vu-bank.is-mono');
          const stereo=document.querySelector('#mixer .strip:not(.master-strip) .vu-bank.is-stereo');
          const master=document.querySelector('#masterLeft .vu-bank');
          const bars=e=>e.querySelector('.vu-bars');
          const style=e=>getComputedStyle(e);
          const read=document.querySelector('#masterLeft .master-analysis');
          const labels=[...read.querySelectorAll('.master-info-row span')].map(x=>x.textContent);
          updateMasterAnalysisPreview(-11.5,-.5);
          const hot=[document.querySelector('#masterLufs').classList.contains('over-limit'),
                     document.querySelector('#masterDbtp').classList.contains('over-limit')];
          updateMasterAnalysisPreview(-12,-1);
          const edge=[document.querySelector('#masterLufs').classList.contains('over-limit'),
                      document.querySelector('#masterDbtp').classList.contains('over-limit')];
          return {monoBars:bars(mono).getBoundingClientRect().width,
            stereoBars:bars(stereo).getBoundingClientRect().width,
            monoBank:mono.getBoundingClientRect().width,
            stereoBank:stereo.getBoundingClientRect().width,
            masterBars:bars(master).getBoundingClientRect().width,
            physicalMono:mono.querySelectorAll('.meter').length,
            physicalStereo:stereo.querySelectorAll('.meter').length,
            innerShadow:style(bars(mono)).boxShadow,
            border:style(bars(mono)).borderTopWidth,
            lcdBackground:style(read).backgroundImage, lcdBorder:style(read).borderTopWidth,
            labels,kicker:!!read.querySelector('.master-info-kicker'),
            hot,edge};
        }""")
        assert lcd['monoBars']==14 and lcd['stereoBars']==30 and lcd['masterBars']==30,lcd
        assert lcd['monoBank']==35 and lcd['stereoBank']==51,lcd
        assert lcd['physicalMono']==1 and lcd['physicalStereo']==2,lcd
        assert 'inset' in lcd['innerShadow'] and lcd['border']=='0px',lcd
        assert lcd['labels']==['LUFS','dBTP'] and not lcd['kicker'],lcd
        assert lcd['hot']==[True,True] and lcd['edge']==[False,False],lcd
        assert 'gradient' in lcd['lcdBackground'] and lcd['lcdBorder']=='0px',lcd

        # Issue #56: defaults preserve geometry; mode CSS isolates hover/layers.
        assert await page.evaluate("""()=>document.documentElement.dataset.mixperf==='optimized'
            && !window.__arMixPerfProbe""")
        # Only one timer schedules each synthetic visual update; no polling rAF
        # at monitor refresh rate (60/120/144Hz) when meter target is ~25 Hz.
        scheduler=await page.evaluate("""()=>({
          stats:browserMeterStats,owner:typeof scheduleDemoMeter,
          idleRAF:meterRAF,paused:document.hidden,
          timerOwned:meterTimer!==0
        })""")
        assert scheduler['owner']=='function',scheduler
        assert scheduler['stats']['intervalMs'] in (40,60),scheduler
        assert scheduler['timerOwned'] or scheduler['idleRAF']!=0,scheduler

        # Pointer crossing 12 representative hardware surfaces must not alter
        # paint-bearing styles. Focus-visible, SELECTED/ON and press feedback
        # have independent checks later in this suite.
        async def passive_hover_is_inert(selector):
            target=page.locator(selector).first
            assert await target.count()==1,selector
            await page.mouse.move(1,1)
            static=await target.evaluate("""e=>{
                const s=getComputedStyle(e);
                return {background:s.background,border:s.borderColor,color:s.color,
                        shadow:s.boxShadow,filter:s.filter,opacity:s.opacity,
                        outline:s.outlineColor,transform:s.transform}
            }""")
            await target.hover()
            hovered=await target.evaluate("""e=>{
                const s=getComputedStyle(e);
                return {background:s.background,border:s.borderColor,color:s.color,
                        shadow:s.boxShadow,filter:s.filter,opacity:s.opacity,
                        outline:s.outlineColor,transform:s.transform}
            }""")
            assert static==hovered,(selector,static,hovered)

        for selector in (
            '[data-view="MIX"]', '[data-view="FX RACK"]',
            '.toolbar .spec-device', '[data-preset="Clean Vocal"]',
            '.channel-list .list-item', '[data-hpf="0"]',
            '#mixer .strip .rack .insert',
            '#mixer .strip .rack .insert.empty',
            '#masterLeft .master-insert', '[data-fxpower="0:0"]',
            '#mixer .strip .strip-name', '#masterMute'):
            await passive_hover_is_inert(selector)

        cap=page.locator('[data-fader="0"] .cap')
        await cap.hover()
        fast=await cap.evaluate("(e)=>getComputedStyle(e).filter")
        assert fast=='none',fast
        await page.evaluate("document.documentElement.dataset.mixperf='baseline'")
        original=await cap.evaluate("(e)=>getComputedStyle(e).filter")
        assert 'drop-shadow' in original,original
        assert await page.locator('.meter-fill').first.evaluate("(e)=>getComputedStyle(e).willChange")=='transform'
        await page.evaluate("document.documentElement.dataset.mixperf='layers-off'")
        assert await page.locator('.meter-fill').first.evaluate("(e)=>getComputedStyle(e).willChange")=='auto'
        await page.evaluate("document.documentElement.dataset.mixperf='hover-off'")
        assert await cap.evaluate("(e)=>getComputedStyle(e).filter")=='none'
        await page.evaluate("document.documentElement.dataset.mixperf='optimized'")
        # PAN at non-center has a separate legacy :hover cascade.
        pan=page.locator('[data-pan="0"]')
        await pan.hover()
        await pan.evaluate("(e)=>e.setAttribute('aria-valuenow','22')")
        pan_thumb=pan.locator('.pan-thumb')
        light_pan=await pan_thumb.evaluate("(e)=>getComputedStyle(e).filter")
        assert light_pan=='none',light_pan
        await page.evaluate("document.documentElement.dataset.mixperf='baseline'")
        # The baseline restores the historical PAN 95ms filter transition.
        # Inspect its final value, not the temporary interpolated frame.
        await page.wait_for_function("""()=>getComputedStyle(
            document.querySelector('[data-pan="0"] .pan-thumb')
        ).filter.includes('brightness(1.08)')""")
        old_pan=await pan_thumb.evaluate("(e)=>getComputedStyle(e).filter")
        assert 'brightness(1.08)' in old_pan and 'drop-shadow' in old_pan,old_pan
        await page.evaluate("document.documentElement.dataset.mixperf='optimized'")
        await pan.evaluate("(e)=>e.setAttribute('aria-valuenow','0')")
        # Toolbar/body fit: no excessive empty band between the hardware header and mixer.
        shell=await page.evaluate("""()=>{
          const bottom=document.querySelector('.toolbar').getBoundingClientRect().bottom;
          const main=document.querySelector('.main').getBoundingClientRect();
          const rail=document.querySelector('.left-rail').getBoundingClientRect();
          const mixer=document.querySelector('.workbench').getBoundingClientRect();
          return {topGap:rail.top-bottom,mixerGap:mixer.top-bottom,mainHeight:main.height};
        }""")
        assert abs(shell['topGap']-2)<=.2 and abs(shell['mixerGap']-2)<=.2,shell
        assert shell['mainHeight']>400,shell
        # Fixed-height, composited meters; peak only mutates when peak value changes.
        meter=await page.evaluate("""()=>{
          // Freeze synthetic preview ticks while probing deterministic peak-hold.
          // Otherwise the 25 Hz real-clock demo can interleave with fake test timestamps.
          if(meterTimer){clearTimeout(meterTimer);meterTimer=0}
          if(meterRAF){cancelAnimationFrame(meterRAF);meterRAF=0}
          const fill=document.querySelector('[data-meter="0"]');
          const peak=fill.closest('.meter').querySelector('.meter-peak');
          const slot=fill.closest('.meter');
          const heightBefore=getComputedStyle(fill).height;
          updateMeterVisual('0',27,10000);
          const first={ratio:fill.style.getPropertyValue('--level'),
            peak:peak.style.bottom,height:getComputedStyle(fill).height,
            lit:parseFloat(fill.style.getPropertyValue('--led-lit')),
            heightPx:fill.getBoundingClientRect().height,
            clip:getComputedStyle(fill).clipPath,palette:getComputedStyle(fill).backgroundImage,
            cached:meterNodes.get('0').fill===fill};
          updateMeterVisual('0',16,10016);
          const held=peak.style.bottom;
          updateMeterVisual('0',45,10032);
          const ratio=fill.style.getPropertyValue('--level');
          const clip=getComputedStyle(fill).clipPath;
          const palette=getComputedStyle(fill).backgroundImage;
          const levels=[10,30,60,85,98].map(v=>{
            setLedMeterLevel(fill,v);
            return {v,lit:parseFloat(fill.style.getPropertyValue('--led-lit')),
              height:fill.getBoundingClientRect().height,
              palette:getComputedStyle(fill).backgroundImage,
              clip:getComputedStyle(fill).clipPath};
          });
          return {first,held,latest:peak.style.bottom,ratio,
            heightBefore,heightAfter:getComputedStyle(fill).height,
            transform:getComputedStyle(fill).transform,clip,palette,levels,
            shape:slot.getBoundingClientRect().height};
        }""")
        assert meter['first']['cached'] and meter['first']['ratio']=='27%',meter
        assert meter['first']['peak']==meter['held'] and meter['latest']=='45%',meter
        assert meter['ratio']=='45%' and meter['heightBefore']==meter['heightAfter'],meter
        assert meter['transform']=='none' and meter['shape']>0,meter
        assert abs(meter['first']['lit']-7*int(meter['first']['heightPx']*.27/7))<.01,meter
        assert meter['first']['clip'].startswith('inset(') and meter['clip'].startswith('inset('),meter
        assert meter['first']['palette']==meter['palette'],meter
        levels=meter['levels']
        assert [x['v'] for x in levels]==[10,30,60,85,98],levels
        assert all(abs(x['lit']-7*int(x['height']*x['v']/100/7))<.01 for x in levels),levels
        assert all(x['palette']==meter['first']['palette'] for x in levels),levels
        assert all(x['lit']<=.9*x['height'] for x in levels[:4]),levels
        assert levels[-1]['lit']>.9*levels[-1]['height'],levels
        # The visible LED row must be the only normal-mode CSS mutation gate.
        # Two nearby raw levels with the same lit 7px row must not repaint.
        dirty=await page.evaluate("""()=>{
          const e=document.querySelector('[data-meter="0"]');
          setLedMeterLevel(e,1);
          const before=e.getAttribute('style'),lit=e.style.getPropertyValue('--led-lit');
          setLedMeterLevel(e,1.1);
          return {before,after:e.getAttribute('style'),lit,level:ledMeterCache.get(e).level};
        }""")
        assert dirty['before']==dirty['after'] and dirty['level']==1.1,dirty
        # Fast rise + aggressive early fall + smooth exponential landing.
        # Numerical envelope is frame-rate invariant, bounded and allocation-free.
        ballistic=await page.evaluate("""()=>{
          const fast=nativeMeterStep(8,96,.034);
          const down=nativeMeterStep(96,8,.034);
          const further=nativeMeterStep(down,8,.034);
          const initialDrop=96-down,nextDrop=down-further;
          const split=further; // two 34ms steps vs one 65ms plus 3ms
          const mixed=nativeMeterStep(nativeMeterStep(96,8,.065),8,.003);
          const zero=nativeMeterStep(8,8,.034);
          const landing=nativeMeterStep(8.25,8,.034);
          const noOvershoot=nativeMeterStep(2,0,.065);
          const retarget=nativeMeterStep(down,99,.034);
          const capped=nativeMeterStep(96,8,5);
          const bounded=nativeMeterStep(96,8,.065);
          return {fast,down,further,initialDrop,nextDrop,split,mixed,zero,
            landing,noOvershoot,retarget,capped,bounded,
            cap:NATIVE_METER_FRAME_MS,attack:NATIVE_METER_ATTACK_SECONDS,
            release:NATIVE_METER_RELEASE_SECONDS,
            nativeTarget:typeof window.__arMixSetNativeMeterTarget,
            stats:typeof window.__arMixMeterMotionStats};
        }""")
        assert ballistic['cap']>=34 and ballistic['nativeTarget']=='function',ballistic
        assert ballistic['attack']<ballistic['release']<.3,ballistic
        assert 80<ballistic['fast']<=96,ballistic
        assert 8<ballistic['further']<ballistic['down']<85,ballistic
        assert ballistic['initialDrop']>ballistic['nextDrop']>0,ballistic
        assert abs(ballistic['split']-ballistic['mixed'])<1e-6,ballistic
        assert ballistic['zero']==8 and ballistic['landing']==8,ballistic
        assert 0<=ballistic['noOvershoot']<2,ballistic
        assert ballistic['down']<ballistic['retarget']<99,ballistic
        assert abs(ballistic['capped']-ballistic['bounded'])<1e-6,ballistic
        # Full mixer rebuild must restore held peaks and clip state on fresh DOM nodes.
        restored=await page.evaluate("""()=>{
          clipLatch['0']=true;
          renderMixer();
          updateMeterVisual('0',5,10048);
          const slot=document.querySelector('[data-meter="0"]').closest('.meter');
          const result={peak:slot.querySelector('.meter-peak').style.bottom,
            clip:slot.querySelector('.clip-led').classList.contains('latched')};
          clipLatch['0']=false;
          slot.querySelector('.clip-led').classList.remove('latched');
          return result;
        }""")
        assert restored=={'peak':'45%','clip':True},restored
        await page.evaluate('scheduleDemoMeter()')
        # No per-event idle fader work: drag listener is attached only on pointerdown.
        assert await page.evaluate('drag===null')
        # All 10 source PAN knobs have been replaced by horizontal slider tracks.
        assert await page.locator('[data-pan]').count()==11
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
        for idx in range(10):
            p=page.locator(f'[data-pan="{idx}"]')
            assert await p.get_attribute('aria-orientation')=='horizontal'
            assert await p.locator('.pan-thumb').count()==1
        # Button 180: SOLID base and exact layered shadow (never PNG/gradient).
        for selector in ['[data-mute="0"]','[data-solo="0"]','[data-hpf="0"]']:
            v=await page.locator(selector).first.evaluate("""e=>{
                const s=getComputedStyle(e);return {
                    image:s.backgroundImage,shadow:s.boxShadow,
                    color:s.backgroundColor,transition:s.transitionDuration}
            }""")
            assert v['image']=='none' and v['color'].startswith('rgb('), (selector,v)
            assert v['shadow'].count('rgb')>=6,(selector,v)
            assert v['transition'].split(', ')[0]=='0.045s',(selector,v)
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
        assert [round(float(v.rstrip('s')),3) for v in style['dur'].split(', ')]==[.045,.045,0,0],style
        # Hover in production does not create a filter, while diagnostic
        # baseline restores brightness to permit same-PC performance A/B.
        utility=page.locator('[data-hpf="0"]')
        await utility.hover()
        assert await utility.evaluate("(e)=>getComputedStyle(e).filter")=='none'
        await page.evaluate("document.documentElement.dataset.mixperf='baseline'")
        assert 'brightness(1.32)' in await utility.evaluate("(e)=>getComputedStyle(e).filter")
        await page.evaluate("delete document.documentElement.dataset.mixperf")
        assert await utility.evaluate("(e)=>getComputedStyle(e).filter")=='none'
        # Visual-architecture regression: four intentionally different hardware
        # families, FX slots recessed rather than taking on button-cap artwork.
        # GUI target: MASTER is the 11th strip, not a floating right rail.
        assert await page.locator('#mixer > .strip').count()==11
        assert await page.locator('#mixer > .strip:last-child').get_attribute('id')=='masterLeft'
        assert await page.locator('[data-insert="0:5"]').count()==1
        assert await page.locator('[data-master-insert="5"]').count()==1
        assert await page.locator('.strip-controls .knob-label').filter(has_text="PAN").count()==0
        assert await page.locator('.pan-readline').count()==11
        assert await page.locator('#masterLeft .master-controls-zone .master-analysis').count()==1
        # Fluid ten-channel rail must fill the available width, without a dead right gutter.
        layout=await page.evaluate("""()=>{
          const area=document.querySelector('#mixer').getBoundingClientRect();
          const strips=[...document.querySelectorAll('#mixer > .strip')];
          const master=strips.at(-1).getBoundingClientRect();
          const prev=strips.at(-2).getBoundingClientRect();
          const controlPairs=[...document.querySelectorAll('#mixer > .strip:not(.master-strip)')].map(strip=>{
            const rect=e=>{const r=e.getBoundingClientRect();return {left:r.left,right:r.right,width:r.width}};
            const a=[...strip.querySelectorAll('.utility-row button')].map(rect);
            const b=[...strip.querySelectorAll('.channel-actions button')].map(rect);
            return {upper:a,lower:b};
          });
          return {areaRight:area.right,masterRight:master.right,gap:master.left-prev.right,
                  overflow:document.querySelector('#mixer').scrollWidth-document.querySelector('#mixer').clientWidth,
                  all:controlPairs};
        }""")
        assert abs(layout['masterRight']-layout['areaRight'])<=3,layout
        assert abs(layout['gap'])<=3,layout
        assert layout['overflow']<=2,layout
        for pair in layout['all']:
            top,bot=pair['upper'],pair['lower']
            assert len(top)==2 and len(bot)==2,pair
            assert all(b['width']>=42 for b in top+bot),pair
            assert abs(top[0]['width']-top[1]['width'])<=2,pair
            assert abs(bot[0]['width']-bot[1]['width'])<=2,pair
            assert abs(top[0]['left']-bot[0]['left'])<=2,pair
            assert abs(top[1]['right']-bot[1]['right'])<=2,pair
            assert 6<=top[1]['left']-top[0]['right']<=10,pair
            assert 6<=bot[1]['left']-bot[0]['right']<=10,pair
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
        assert await page.locator('.master-stereo-badge,.master-route-label,.fader-tools.master-tools').count()==0
        assert await page.locator('#masterMode.btn-180.is-stereo').count()==1
        assert await page.locator('#masterMode').inner_text()=='STEREO'
        assert await page.locator('#masterMode').get_attribute('aria-pressed')=='false'
        # Streaming output panel has only two useful readouts.
        assert await page.locator('#masterLeft .master-analysis .master-info-row').count()==2
        assert await page.locator('#masterPeakDb').count()==0
        assert await page.locator('#masterLufs').count()==1
        assert await page.locator('#masterDbtp').count()==1
        assert 'SIM' not in await page.locator('#masterLeft .master-analysis').inner_text()
        assert 'LUFS' in await page.locator('#masterLeft .master-analysis').inner_text()
        assert 'simulated' in (await page.locator('#masterLeft .master-analysis').get_attribute('title')).lower()
        # Buttons must describe and perform actual distinct actions: no "..." blank cap.
        assert await page.locator('.left-rail #addScene').inner_text()=='＋ SAVE SNAPSHOT'
        assert await page.locator('.left-rail #saveFxPreset').inner_text()=='SAVE FX'
        assert await page.locator('.left-rail #menuScene').inner_text()=='EXPORT'
        assert await page.locator('.left-rail .rail-bottom button').count()==3
        # Owner's global BTN-180 hardware recipe: 7 toolbar + 8 left rail.
        # Single solid face, identical resting 7-shadow model, no Button 19.
        assert await page.locator('.toolbar-actions .nav button.btn-180').count()==7
        assert await page.locator('.left-rail button.btn-180').count()==8
        assert await page.locator('.toolbar-actions .fx-nav,.left-rail .fx-nav').count()==0
        assert await page.locator('.left-rail .pushable,.left-rail .front,.left-rail .edge,.left-rail .shadow').count()==0
        style=await page.evaluate("""()=>{
          const grab=q=>{const e=document.querySelector(q),s=getComputedStyle(e),r=e.getBoundingClientRect();
            return {face:s.backgroundColor,image:s.backgroundImage,ink:s.color,shadow:s.boxShadow,
                    rect:r.toJSON(),transform:s.transform,radius:s.borderTopLeftRadius,
                    spanTransform:e.querySelector('span')?getComputedStyle(e.querySelector('span')).transform:null}};
          return {nav:grab('[data-view="FX RACK"]'),
                  selected:grab('[data-view="MIX"]'),
                  rail:grab('[data-preset="Clean Vocal"]'),
                  railOn:grab('[data-side="channels"]'),
                  mute:grab('[data-mute="0"]'),
                  utility:grab('[data-hpf="0"]')};
        }""")
        for key in ('rail','mute','utility'):
            assert style[key]['face']=='rgb(203, 209, 209)',(key,style)
            assert style[key]['image']=='none',(key,style)
            assert style[key]['shadow'].count('rgb')==7,(key,style)
            assert style[key]['radius']=='3px',(key,style)
        assert style['nav']['face']=='rgb(203, 209, 209)' and style['selected']['face']=='rgb(178, 232, 160)',style
        assert style['railOn']['face']=='rgb(237, 184, 223)',style
        # Audio Device shares the physical BTN-180 toolbar face and dark ink.
        # The OFF state is truthful in UI-only preview. Explicitly force ON
        # for a PALETTE-ONLY regression screenshot, then restore it immediately.
        device=page.locator('.toolbar .spec-device')
        devOff=await device.evaluate("""e=>{const s=getComputedStyle(e),t=getComputedStyle(e.querySelector('#headerDriver')),
          r=e.getBoundingClientRect();return {face:s.backgroundColor,ink:t.color,shadow:s.boxShadow,rect:r.toJSON(),
          pressed:e.classList.contains('on'),backgroundImage:s.backgroundImage}}""")
        assert devOff['face']==style['rail']['face']=='rgb(203, 209, 209)',devOff
        assert devOff['ink']=='rgb(23, 34, 41)' and devOff['backgroundImage']=='none',devOff
        assert not devOff['pressed'],devOff
        await device.evaluate("(e)=>e.classList.add('on')")
        await page.wait_for_function("""()=>getComputedStyle(document.querySelector('.spec-device')).backgroundColor==='rgb(145, 237, 172)'""")
        devOn=await device.evaluate("""e=>{const s=getComputedStyle(e),t=getComputedStyle(e.querySelector('#headerDriver'));
          return {face:s.backgroundColor,ink:t.color,shadow:s.boxShadow,rect:e.getBoundingClientRect().toJSON()}}""")
        assert devOn['face']=='rgb(145, 237, 172)' and devOn['ink']=='rgb(17, 51, 34)',devOn
        assert shared_shadow_shape(devOn['shadow'])==shared_shadow_shape(devOff['shadow']),(devOn,devOff)
        assert all(abs(devOff['rect'][k]-devOn['rect'][k])<.2 for k in ('x','y','width','height')),(devOff,devOn)
        await page.screenshot(path=str(project/'device-green-on-palette-review.png'),full_page=True)
        await device.evaluate("(e)=>e.classList.remove('on')")
        await page.wait_for_function("""()=>getComputedStyle(document.querySelector('.spec-device')).backgroundColor==='rgb(203, 209, 209)'""")
        assert all(style[key]['spanTransform']=='none' for key in ('nav','rail','selected')),style
        assert await page.locator('[data-hpf="0"]').evaluate(
            "(e)=>getComputedStyle(e).boxShadow.includes('5px 5px 2px -1px')"),style
        # Real 1px tactile travel; no text scaling or width/height changes.
        preset=page.locator('[data-preset="Clean Vocal"]')
        pre=await preset.evaluate("(e)=>e.getBoundingClientRect().toJSON()")
        await preset.hover()
        await page.mouse.down()
        await page.wait_for_timeout(120)
        pushed=await preset.evaluate("""e=>({transform:getComputedStyle(e).transform,
          shadow:getComputedStyle(e).boxShadow,rect:e.getBoundingClientRect().toJSON()})""")
        assert pushed['transform']=='matrix(1, 0, 0, 1, 0, 1)',pushed
        assert pushed['shadow'].startswith('rgba(0, 0, 0, 0.5)'),pushed
        assert abs(pushed['rect']['y']-pre['y']-1)<=.2,pushed
        assert all(abs(pushed['rect'][k]-pre[k])<=.2 for k in ('x','width','height')),pushed
        await page.mouse.up()
        await page.mouse.move(1,1)
        await page.wait_for_timeout(190)
        active=await preset.evaluate("""e=>({
          face:getComputedStyle(e).backgroundColor,shadow:getComputedStyle(e).boxShadow,
          rect:e.getBoundingClientRect().toJSON(),transform:getComputedStyle(e).transform})""")
        assert active['face']=='rgb(237, 184, 223)' and active['transform']=='matrix(1, 0, 0, 1, 0, 0)',active
        assert shared_shadow_shape(active['shadow'])==shared_shadow_shape(style['rail']['shadow']),active
        assert all(abs(active['rect'][k]-pre[k])<=.2 for k in ('x','y','width','height')),active
        # Utility ON is soft cyan, not the old green/purple polarity palettes.
        await page.locator('[data-hpf="0"]').click()
        await page.wait_for_timeout(190)
        cyan=await page.locator('[data-hpf="0"]').evaluate("(e)=>getComputedStyle(e).backgroundColor")
        assert cyan=='rgb(148, 232, 237)',cyan
        await page.locator('[data-hpf="0"]').click()
        # Navigation remains mutually exclusive; selected is only a color latch.
        await page.locator('[data-view="FX RACK"]').click()
        await page.wait_for_function("""()=>{
          const on=document.querySelector('[data-view="FX RACK"]');
          const off=document.querySelector('[data-view="MIX"]');
          return on.classList.contains('selected') && !off.classList.contains('selected')
            && getComputedStyle(on).backgroundColor==='rgb(178, 232, 160)'
            && getComputedStyle(off).backgroundColor==='rgb(203, 209, 209)';
        }""")
        nav=page.locator('[data-view="FX RACK"]')
        navBefore=await nav.evaluate("(e)=>e.getBoundingClientRect().toJSON()")
        await nav.hover()
        await page.mouse.down()
        await page.wait_for_timeout(120)
        navDown=await nav.evaluate("""e=>({shadow:getComputedStyle(e).boxShadow,
          rect:e.getBoundingClientRect().toJSON(),transform:getComputedStyle(e).transform})""")
        assert navDown['shadow'].startswith('rgba(0, 0, 0, 0.5)'),navDown
        assert navDown['transform']=='matrix(1, 0, 0, 1, 0, 1)',navDown
        assert abs(navDown['rect']['y']-navBefore['y']-1)<=.2,navDown
        await page.mouse.up()
        await page.locator('[data-view="MIX"]').click()
        await page.screenshot(path=str(project/'toolbar-global-btn180-pad.png'),full_page=True)
        # One-shot toolbar commands remain unlatched after release.
        await page.locator('#audioSetupBtn').click()
        assert await page.locator('#audioSetupBtn').evaluate(
            '(e)=>!e.classList.contains("active") && !e.classList.contains("selected")')
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
          const master=bounds('#masterMute'),stereo=bounds('#masterMode');
          return {master,stereo,rows:[...document.querySelectorAll('.strip:not(.master-strip) .channel-actions')].map(row=>{
            const mute=row.querySelector('.mute'),solo=row.querySelector('.solo');
            return {mute:bounds('[data-mute="'+mute.dataset.mute+'"]'),solo:bounds('[data-solo="'+solo.dataset.solo+'"]')};
          })};
        }""")
        assert abs(actions['master']['middle']-actions['stereo']['middle'])<=1,actions
        assert all(abs(row[key]['middle']-actions['master']['middle'])<=1 for row in actions['rows'] for key in ('mute','solo')),actions
        assert all(abs(row['mute']['middle']-row['solo']['middle'])<=1 for row in actions['rows']),actions
        assert actions['stereo']['height']==actions['master']['height']==29,actions
        # Without obsolete tools, MASTER fader rail must be as long as sources.
        header=await page.evaluate("""()=>{
          const btns=[...document.querySelectorAll('.toolbar .nav button')];
          return {buttons:btns.map(b=>{const r=b.getBoundingClientRect();return {width:r.width,mid:r.top+r.height/2,right:r.right}}),
            device:(()=>{const r=document.querySelector('.spec-device').getBoundingClientRect();return r.top+r.height/2})()};
        }""")
        assert all(b['width']>=77 and abs(b['mid']-header['device'])<=1 for b in header['buttons']),header

        assert await page.locator('header .nav button').count()==7
        assert await page.locator('footer.status button').count()==0
        assert await page.locator('[data-clear-all-clips]').count()==0
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
        assert abs(architecture['mainFader']['height']-architecture['masterFader']['height'])<=2,architecture
        assert architecture['nav']=='none',architecture
        assert architecture['long']=='none',architecture
        assert 'data:image/png;base64' not in architecture['fx']
        assert 'data:image/png;base64' not in architecture['chrome']
        assert not architecture['overflow']
        assert architecture['navBounds']['right']<=1680+1,architecture
        assert await page.locator('.toolbar-actions .nav button').count()==7
        assert await page.locator('#masterLeft').evaluate(
            '(m)=>Math.abs(m.getBoundingClientRect().width-document.querySelector("[data-strip=\\\"0\\\"]").getBoundingClientRect().width)<=1')
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
        assert await page.locator('.strip').count()==11
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
        await page.wait_for_timeout(65) # 45ms palette, immediate pressed displacement
        on=await switch_state('[data-mute="0"]')
        assert on['selected'] and shared_shadow_shape(on['shadow'])==shared_shadow_shape(off['shadow']),(off,on)
        assert on['color']!=off['color'],(off,on)
        assert on['t']==off['t'] and same_rect(off,on),(off,on)
        assert off['color']=='rgb(203, 209, 209)' and off['ink']=='rgb(23, 34, 41)',(off,on)
        assert on['ink']!=off['ink'],(off,on)
        assert on['shadow'].startswith('rgba(0, 0, 0, 0.18)'),on
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
        assert len(geometry)==11 and all(x['nameTop']>=x['faderBottom']-1 for x in geometry),geometry
        assert all(not x['hasTools'] for x in geometry if not x['master']),geometry
        assert not any(x['hasTools'] for x in geometry),geometry
        # Fader rail must be an actual smooth bent-metal recess (not old slit-only
        # or a bright white rectangular frame); checked for all 10 + MASTER
        # in the Browser before approving the Windows distribution.
        fader_recess=await page.evaluate("""() => [...document.querySelectorAll('#mixer [data-fader]')].map(f=>{
          const bay=getComputedStyle(f,'::before'),slit=getComputedStyle(f,'::after');
          const rail=f.getBoundingClientRect(),cap=f.querySelector('.cap');
          return {id:f.dataset.fader,rail:rail.width,bay:parseFloat(bay.width),
            fill:bay.backgroundColor,edge:bay.boxShadow,border:bay.borderTopWidth,
            top:bay.top,bottom:bay.bottom,slit:parseFloat(slit.width),
            slitColor:slit.backgroundColor,capWidth:cap.getBoundingClientRect().width};
        })""")
        assert len(fader_recess)==11,fader_recess
        assert all(x['bay']>=30 and x['bay']<=46.1 and x['bay']<=x['rail']+.1
            and x['fill']=='rgb(38, 52, 60)' and x['border']=='0px'
            and 'inset' in x['edge'] and x['top']=='0px'
            and x['bottom']=='0px' and x['slit']==5
            and x['slitColor']=='rgb(5, 9, 12)' and x['capWidth']==36
            for x in fader_recess),fader_recess

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
        assert during['shadow']!=before['shadow'] and '0.5' in during['shadow'],(before,during)
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
        assert '0.5' in pressed_on['shadow'],pressed_on
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
        assert widths['master']>=128 and abs(widths['master']-widths['source'])<=1, widths
        if not widths['visible']:
            # Smaller viewports scroll inside workbench; MASTER must remain
            # intact and reachable instead of becoming a distorted wide strip.
            master_reachable=await page.evaluate("""()=>{
              const mixer=document.querySelector('#mixer');mixer.scrollLeft=mixer.scrollWidth;
              const master=document.querySelector('#masterLeft').getBoundingClientRect();
              const view=mixer.getBoundingClientRect();
              return master.right<=view.right+1 && master.left>=view.left-1;
            }""")
            assert master_reachable,widths
            await page.evaluate("document.querySelector('#mixer').scrollLeft=0")
        lcd=await page.evaluate("""()=>[...document.querySelectorAll('#mixer .vu-bank .meter')].map(e=>{
          const s=getComputedStyle(e),glass=getComputedStyle(e,'::after');
          return {back:s.backgroundColor,glass:glass.content};
        })""")
        assert len(lcd)>=11 and all(x['back']=='rgb(1, 6, 10)' and x['glass']!='none' for x in lcd),lcd
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
        assert await p1.locator('.pan-thumb').evaluate('(e)=>getComputedStyle(e).filter')=='none'
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
        assert await page.locator('.strip .insert-slot').count()==60
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
        # A real two-state BTN-180 control: blue STEREO, red MONO, durable state.
        mode=page.locator('#masterMode')
        blue=await mode.evaluate("(e)=>getComputedStyle(e).getPropertyValue('--b180-color').trim()")
        assert blue=='#9dd2f0',blue
        await mode.click()
        red=await mode.evaluate("(e)=>getComputedStyle(e).getPropertyValue('--b180-color').trim()")
        assert await mode.inner_text()=='MONO' and await mode.get_attribute('aria-pressed')=='true'
        assert red=='#f99aa4' and red!=blue,red
        assert await page.evaluate('durableSession().masterMono') is True
        await mode.click()
        assert await mode.inner_text()=='STEREO' and await mode.get_attribute('aria-pressed')=='false'
        assert await page.evaluate('durableSession().masterMono') is False
        await page.locator('#masterMute').click()
        assert await page.locator('#masterMute').inner_text()=='MUTE'
        assert await page.locator('#masterMute').get_attribute('aria-pressed')=='true'
        await page.locator('#masterMute').click()
        assert await page.locator('#masterMute').inner_text()=='MUTE'
        # Hover and active feedback stays visual and does not change native fader geometry.
        cap0=page.locator('[data-fader="0"] .cap')
        await cap0.hover()
        fader_hover=await cap0.evaluate("(e)=>getComputedStyle(e).filter")
        assert fader_hover=='none',fader_hover
        button=page.locator('[data-mute="0"]')
        await button.hover()
        button_hover=await button.evaluate("(e)=>getComputedStyle(e).filter")
        assert button_hover=='none',button_hover  # No passive GPU filter in normal GUI
        # FX power and FX name remain separate inside the 104px channel strip.
        assert await page.evaluate("""() => [...document.querySelectorAll('.strip .insert-slot')].every(slot=>{
            const cell=slot.getBoundingClientRect(),strip=slot.closest('.strip').getBoundingClientRect();
            return cell.x>=strip.x-1&&cell.right<=strip.right+1&&cell.height===27
        })"""), 'FX power overflow or slot geometry changed'
        # Channel identities are real editable state, not only colored labels.
        await page.locator('[data-editstrip="0"]').click()
        assert await page.locator('#channelIdentity').is_hidden(), 'Single-click must select only'
        assert await page.locator('[data-strip="0"]').evaluate("(e)=>e.classList.contains('selected')")
        await page.locator('[data-editstrip="0"]').dblclick()
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
        await page.locator('[data-editstrip="0"]').dblclick()
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
        assert (master_fader_debug['rect']['width'],master_fader_debug['rect']['height'])==(36,77)
        assert master_fader_debug['background'].startswith('url("data:image/png;base64,')
        decoded=await page.locator('[data-fader="master"] .cap').evaluate("""async el => {
          const bg=getComputedStyle(el).backgroundImage;
          const image=new Image();image.src=bg.slice(5,-2);
          await image.decode();
          return {width:image.naturalWidth,height:image.naturalHeight};
        }""")
        assert decoded=={'width':36,'height':77}, 'Gold MASTER PNG must decode to original 36x77'
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
        assert await page.locator('[data-clip]').count()==16  # 6 mono + 4 stereo pairs + MASTER L/R
        assert await page.locator('[data-meter="masterL"]').count()==1
        assert await page.locator('[data-meter="masterR"]').count()==1
        # Each visible source has its configured mono/stereo topology.
        topology=await page.evaluate("""()=>{
          const channels=[...document.querySelectorAll('#mixer > [data-strip]')];
          const units=channels.map((strip,i)=>({stereo:channelsState(i),keys:[...strip.querySelectorAll('[data-meter]')].map(e=>e.dataset.meter)}));
          function channelsState(i){return channels[i].querySelectorAll('.meter-unit').length===2}
          return {units,master:[...document.querySelectorAll('#masterLeft .meter-unit [data-meter]')].map(e=>e.dataset.meter)};
        }""")
        assert [m['keys'] for m in topology['units']]==[[str(i)] for i in range(6)]+[[f'{i}L',f'{i}R'] for i in range(6,10)],topology
        assert topology['master']==['masterL','masterR'],topology

        # Calibration contract: +10 and -60 must align the PNG's black row 38.
        calibrated=await page.evaluate("""()=>{
          const selected=['0','6','master'];
          const ticks=[10,0,-10,-30,-60];
          const result=[];
          for(const key of selected){
            const f=document.querySelector('[data-fader="'+key+'"]'),strip=f.closest('.strip');
            const scale=strip.querySelector('.gain-scale');
            const old=f.dataset.fader==='master'?masterGain:channels[+key].fader;
            for(const db of ticks){
              updateFader(key,db);
              const cap=f.querySelector('.cap').getBoundingClientRect();
              const tick=scale.querySelector('[data-gain-tick="'+db+'"]').getBoundingClientRect();
              result.push({key,db,error:cap.top+38-tick.top,
                           inside:cap.top>=f.getBoundingClientRect().top-.5&&cap.bottom<=f.getBoundingClientRect().bottom+.5});
            }
            updateFader(key,old);
          }
          const vu=[];
          for(const strip of [...document.querySelectorAll('#mixer>.strip')]){
            const scale=strip.querySelector('.vu-scale-travel');
            const active=strip.querySelector('.meter-signal-area');
            const a=active.getBoundingClientRect(),b=scale.getBoundingClientRect();
            vu.push({top:a.top-b.top,bottom:a.bottom-b.bottom,
              ticks:[0,-12,-24,-60].map(db=>{
                const target=scale.querySelector('[data-vu-tick="'+db+'"]').getBoundingClientRect();
                return target.top-(a.top+(-db/60)*a.height);
              })});
          }
          return {result,vu};
        }""")
        assert len(calibrated['result'])==15,calibrated
        assert all(abs(x['error'])<=1 and x['inside'] for x in calibrated['result']),calibrated
        assert len(calibrated['vu'])==11 and all(abs(s['top'])<=2 and abs(s['bottom'])<=2 and all(abs(d)<=1.5 for d in s['ticks']) for s in calibrated['vu']),calibrated
        assert await page.evaluate('meterPercentToDbfs(0)')==-60
        assert await page.evaluate('meterPercentToDbfs(80)')==-12
        assert await page.evaluate('meterPercentToDbfs(100)')==0

        # Shared physical LED width: 14px case, equal inner fill and clip caps.
        vu_geometry=await page.evaluate("""()=>{
          const nodes=[...document.querySelectorAll('#mixer .meter-unit')];
          return nodes.map(unit=>{const m=unit.querySelector('.meter'),fill=m.querySelector('.meter-fill');
            const a=unit.getBoundingClientRect(),b=m.getBoundingClientRect(),c=fill.getBoundingClientRect();
            return {unit:a.width,meter:b.width,fill:c.width,gradient:getComputedStyle(fill).backgroundImage,
                    clip:m.querySelector('.clip-led')!==null};});
        }""")
        assert len(vu_geometry)==16,vu_geometry
        # Reference-inspired hierarchy MUST be [VU left] [rail middle] [scale right].
        module_layout=await page.evaluate("""()=>{
          return [...document.querySelectorAll('#mixer > .strip')].map((strip)=>{
            const z=strip.querySelector('.fader-zone'),bank=z.querySelector('.vu-bank'),rail=z.querySelector('.fader'),ruler=z.querySelector('.scale');
            const box=x=>{const r=x.getBoundingClientRect();return {l:r.left,r:r.right,t:r.top,b:r.bottom,w:r.width,h:r.height};};
            const meterItems=[...bank.querySelectorAll('.meter-unit')];
            return {master:strip.id==='masterLeft',dom:[...z.children].map(x=>x.classList.contains('vu-bank')?'VU':x.classList.contains('fader')?'FADER':x.classList.contains('scale')?'SCALE':'OTHER'),
                    strip:box(strip),zone:box(z),bank:box(bank),rail:box(rail),ruler:box(ruler),
                    meters:meterItems.map(box),count:meterItems.length};
          });
        }""")
        assert len(module_layout)==11,module_layout
        assert [s['count'] for s in module_layout]==[1]*6+[2]*5,module_layout
        for s in module_layout:
            assert s['dom']==['VU','FADER','SCALE'],s
            assert abs(s['bank']['w']-(35 if s['count']==1 else 51))<.3 and abs(s['ruler']['w']-26)<.3,s
            assert s['bank']['r']+2<s['rail']['l'] and s['rail']['r']+2<s['ruler']['l'],s
            assert s['bank']['l']>=s['zone']['l']-1 and s['ruler']['r']<=s['zone']['r']+1,s
            assert all(abs(m['w']-14)<.3 for m in s['meters']),s
            assert s['rail']['w']>=36 and s['rail']['h']>150,s
        # P0 screenshot regression: sleeping LCD glass, 10 CH + MASTER
        # must share identical neutral black material regardless of track color.
        lcd_glass=await page.evaluate("""()=>{
          return [...document.querySelectorAll('#mixer .fader-zone .vu-bank')].map(bank=>{
            const well=bank.querySelector('.vu-bars');
            const face=bank.querySelector('.meter-unit .meter');
            const wc=getComputedStyle(well),fc=getComputedStyle(face);
            return {wellColor:wc.backgroundColor,faceColor:fc.backgroundColor,
              width:Math.round(well.getBoundingClientRect().width),
              leds:bank.querySelectorAll('.meter-fill').length,
              inset:wc.boxShadow.includes('inset'),
              filter:wc.filter,blur:wc.backdropFilter||'none'};
          });
        }""")
        assert len(lcd_glass)==11,lcd_glass
        assert all(v['wellColor']=='rgb(2, 7, 11)' and
                   v['faceColor']=='rgb(1, 6, 10)' for v in lcd_glass),lcd_glass
        assert all(v['inset'] and v['filter']=='none' and
                   v['blur']=='none' for v in lcd_glass),lcd_glass
        assert all(v['width']==(30 if v['leds']==2 else 14) for v in lcd_glass),lcd_glass
        # Mono's narrower well intentionally releases 16px to its fader.
        # Same-format strips retain precisely aligned central rail offsets.
        mono_x=[s['rail']['l']-s['zone']['l'] for s in module_layout[:10] if s['count']==1]
        stereo_x=[s['rail']['l']-s['zone']['l'] for s in module_layout[:10] if s['count']==2]
        assert max(mono_x)-min(mono_x)<1 and max(stereo_x)-min(stereo_x)<1,(mono_x,stereo_x)
        assert abs((stereo_x[0]-mono_x[0])-16)<1,(mono_x,stereo_x)

        assert all(abs(v['unit']-14)<.3 and abs(v['meter']-14)<.3 and abs(v['fill']-10)<.6 and v['clip'] for v in vu_geometry),vu_geometry
        assert len(set(v['gradient'] for v in vu_geometry))==1, 'VU palette must match across all L/R/mono bars'
        await page.locator('#masterMode').click()
        assert await page.locator('#masterMode').inner_text()=='MONO'
        mono_text=await page.locator('#masterMode').evaluate('(e)=>({color:getComputedStyle(e).color,shadow:getComputedStyle(e).textShadow})')
        assert mono_text['color']=='rgb(20, 16, 20)' and mono_text['shadow']=='none',mono_text
        await page.locator('#masterMode').click()
        assert await page.locator('#masterMode').inner_text()=='STEREO'
        # Manual peak-hold reset must be deterministic: cancel the browser's
        # unrelated 25Hz demo producer before injecting explicit levels. Without
        # this, a live demo RAF can repopulate peakHold after RESET and make the
        # test flaky on slower CI hosts. Production native metering is untouched.
        await page.evaluate("""()=>{
          if(meterTimer){clearTimeout(meterTimer);meterTimer=0}
          if(meterRAF){cancelAnimationFrame(meterRAF);meterRAF=0}
        }""")
        await page.evaluate('updateMeterVisual("6L",99,performance.now())')
        assert await page.locator('[data-clip="6L"]').evaluate('(e)=>e.classList.contains("latched")')
        await page.locator('[data-clip="6L"]').click()
        assert not await page.locator('[data-clip="6L"]').evaluate('(e)=>e.classList.contains("latched")')
        assert await page.locator('[data-meter="6R"]').count()==1
        
        await page.evaluate('updateMeterVisual("masterL",99,performance.now())')
        assert await page.locator('[data-clip="masterL"]').evaluate('(e)=>e.classList.contains("latched")')
        assert await page.evaluate('Boolean(peakHold.masterL)') is True
        await page.locator('[data-clip="masterL"]').click()
        assert not await page.locator('[data-clip="masterL"]').evaluate('(e)=>e.classList.contains("latched")')
        assert await page.evaluate('Boolean(peakHold.masterL)') is False
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
        # BTN-180 color transition is 45ms, while tactile press is immediate.
        await page.wait_for_function("""()=>getComputedStyle(document.querySelector('[data-mute="0"]')).backgroundColor==='rgb(249, 154, 164)'""",timeout=2500)
        assert await image_for('[data-mute="0"]') != await image_for('[data-solo="0"]'), await page.evaluate("""()=>{
          return [...document.querySelectorAll('[data-mute="0"],[data-solo="0"]')].map(e=>({
            cls:e.className,background:getComputedStyle(e).backgroundColor,
            token:getComputedStyle(e).getPropertyValue('--b180-color'),
            bgImage:getComputedStyle(e).backgroundImage
          }))
        }""")
        await page.locator('[data-solo="0"]').click()
        assert await page.locator('[data-solo="0"]').get_attribute('aria-pressed') == 'true'
        await page.wait_for_function("""()=>getComputedStyle(document.querySelector('[data-solo="0"]')).backgroundColor==='rgb(244, 212, 121)'""",timeout=2500)
        assert await image_for('[data-solo="0"]') != await image_for('[data-mute="0"]')
        await page.locator('[data-mute="0"]').click()
        await page.locator('[data-solo="0"]').click()
        await page.locator('#masterMute').click()
        assert await page.locator('#masterMute').get_attribute('aria-pressed') == 'true'
        await page.locator('#masterMute').click()
        assert await page.locator('#dim,#talkBtn,[data-master-tab]').count()==0

        # Silver channels and gold MASTER use exact unscaled 36x77 PNGs, detent y=38.
        # Verify geometry, travel and distinct source rasters without CSS resampling.
        geometry=await page.evaluate("""async () => {
            const slots=[...document.querySelectorAll('[data-fader]')];
            const heads=slots.map(f => {
                const c=f.querySelector('.cap'),b=c.getBoundingClientRect(),r=f.getBoundingClientRect();
                const s=getComputedStyle(c);
                return {master:f.dataset.fader==='master',width:b.width,height:b.height,bodyInside:b.top>=r.top-1 && b.bottom<=r.bottom+1,
                        top:b.top,trackTop:r.top,radius:s.borderRadius,
                        bg:s.backgroundImage,transform:s.transform};
            });
            const uri=heads[0].bg.match(/data:image\\/png;base64,[a-zA-Z0-9+/=]+/)?.[0];
            if(!uri) throw new Error('Image data URI not found');
            const image=new Image();image.src=uri;await image.decode();
            return {heads,imgW:image.naturalWidth,imgH:image.naturalHeight};
        }""")
        assert len(geometry['heads'])==11
        assert (geometry['imgW'],geometry['imgH'])==(36,77),geometry
        assert sum(g['master'] for g in geometry['heads'])==1,geometry
        assert geometry['heads'][0]['bg'] != geometry['heads'][-1]['bg'], 'MASTER must use gold instead of channel silver'
        for g in geometry['heads']:
            assert (g['width'],g['height'])==(36,77),g
            assert g['bodyInside'],g
            assert g['radius']=='0px',g
            assert 'matrix(1, 0, 0, 1, -18, -38)' in g['transform'],g
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
        cy=rect['y']+38
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
        # Query-string run from actual standalone document, not a CSS-only class
        # flip: the explicit probe exists and meter-off is a browser-only feature.
        diag=await browser.new_page(viewport={"width":1680,"height":920})
        await diag.goto((project/"AR-MIX-Live-FX-Host-Standalone-v0.3.html").as_uri()+"?mixperf=meters-off")
        assert await diag.evaluate("""()=>document.documentElement.dataset.mixperf==='meters-off'
            && window.__arMixPerfProbe?.mode==='meters-off'""")
        capture=asyncio.create_task(diag.evaluate("()=>window.__arMixPerfProbe.run(1000)"))
        await diag.mouse.move(370,195)
        await diag.mouse.move(600,580,steps=16)
        await diag.mouse.move(900,625,steps=10)
        measured=await capture
        assert measured["mode"]=="meters-off" and measured["durationMs"]==1000,measured
        assert measured["mixer"]["samples"]>0 and measured["pointerMoves"]["mixer"]>0,measured
        await diag.close()
        # Native WebView bridge integration: real MASTER peaks must stay visible
        # while preview-only channels 2–10 remain dark. No synthetic DSP meter.
        native=await browser.new_page(viewport={"width":1680,"height":920})
        native_errors=[]
        native.on("pageerror",lambda e: native_errors.append(str(e)))
        await native.add_init_script("""(() => {
          window.__arMixMockEvents={};
          window.__JUCE__={backend:{
            emitEvent(){},
            addEventListener(name,fn){window.__arMixMockEvents[name]=fn;}
          }};
        })()""")
        await native.goto((project/"AR-MIX-Live-FX-Host-Standalone-v0.3.html").as_uri())
        await native.add_script_tag(path=str(project/"web/bridge.js"))
        native_static=await native.evaluate("""()=>{
          const master=document.querySelector('#masterLeft');
          const left=master.querySelector('[data-meter="masterL"]');
          const right=master.querySelector('[data-meter="masterR"]');
          const ch2=document.querySelector('[data-strip="1"]');
          const inactive=ch2.querySelector('[data-meter="1"]');
          return {native:document.documentElement.classList.contains('native-engine'),
            masterPreview:master.dataset.preview,fxPreview:master.dataset.fxPreview,
            leftOpacity:getComputedStyle(left).opacity,
            rightOpacity:getComputedStyle(right).opacity,
            ch2Preview:ch2.dataset.preview,
            ch2Opacity:getComputedStyle(inactive).opacity};
        }""")
        assert native_static["native"] and native_static["fxPreview"]=="1",native_static
        assert native_static["masterPreview"] is None,native_static
        assert native_static["leftOpacity"]=="1" and native_static["rightOpacity"]=="1",native_static
        assert native_static["ch2Preview"]=="1" and native_static["ch2Opacity"]=="0",native_static
        await native.evaluate("""()=>{
          window.__arMixMockEvents.arMixStatus({
            connected:true,error:'',driver:'Windows Audio',rate:48000,buffer:480,
            monitor:true,channelDb:0,masterDb:0,channelPan:0,
            inputPeak:.45,peak:.38,peakLeft:.38,peakRight:.24,
            peakOutput:.38,deviceOutputChannels:2,masterMute:false
          });
        }""")
        await native.wait_for_function("""()=>{
          const l=document.querySelector('[data-meter="masterL"]');
          const r=document.querySelector('[data-meter="masterR"]');
          return Number.parseFloat(l.style.getPropertyValue('--led-lit'))>7 &&
            Number.parseFloat(r.style.getPropertyValue('--led-lit'))>7;
        }""",timeout=3000)

        # R1: native-backed Waves-style INPUT PATCH, not an image or browser
        # mock-routing claim. Test real panel interaction against native ACKs.
        await native.evaluate("""()=>{
          window.__arMixSent=[];
          window.__JUCE__.backend.emitEvent=(name,payload)=>
            window.__arMixSent.push({name,payload});
        }""")
        await native.add_script_tag(path=str(project/"web/monitor.js"))
        await native.evaluate("""()=>{
          window.__arPatchRows=Array.from({length:10},()=>({
            mode:"NONE",left:-1,right:-1,gain:.25,pan:0,main:true,
            mute:false,solo:false,fxId:""
          }));
          window.__arPatchStatus={
            connected:true,driver:"ASIO",rate:48000,buffer:128,monitor:false,
            masterMute:false,channelMute:false,diagnosticRoute:"NORMAL",chainCount:0,
            dryRoutingLab:{active:false,ready:false,epoch:77,revision:0,
              ports:[{port:2,callback:0,label:"Input <Mic> & One"},
                     {port:5,callback:1,label:"USB Input Six"}],
              outputPorts:[0,1],channelControls:[],fxCount:0}
          };
          window.__arMixMockEvents.arMixStatus(window.__arPatchStatus);
        }""")
        await native.locator('[data-view="SIGNAL FLOW"]').click()
        assert await native.locator("#arRoutingLabOverlay").evaluate(
            "(e)=>e.classList.contains('open')")
        assert await native.locator("#arPatchGrid [data-patch-row][data-patch-port]").count()==20
        assert await native.locator("#arPatchGrid .arPatchCell").count()==80
        patch_visual=await native.evaluate("""()=>{
          const grid=document.querySelector('#arPatchGrid').getBoundingClientRect();
          const cols=[...document.querySelectorAll('#arPatchGrid .arPatchPort')]
            .map(x=>x.getBoundingClientRect().width);
          const panel=document.querySelector('#arRoutingLabPanel').getBoundingClientRect();
          return {grid:Math.round(grid.width),panel:Math.round(panel.width),
                  cols:cols.map(Math.round),rowCount:
                    document.querySelectorAll('#arPatchGrid .arPatchChan').length};
        }""")
        assert patch_visual['rowCount']==10 and len(patch_visual['cols'])==8,patch_visual
        assert max(patch_visual['cols'])-min(patch_visual['cols'])<=1,patch_visual
        assert all(58<=w<=140 for w in patch_visual['cols']),patch_visual
        await native.screenshot(path=str(project/"native-input-patch-review-1680.png"))
        await native.set_viewport_size({"width":1440,"height":820})
        await native.screenshot(path=str(project/"native-input-patch-review-1440.png"))
        await native.set_viewport_size({"width":1680,"height":920})
        assert await native.locator("#arPatchGrid .arPatchRow").first.evaluate(
            "(e)=>getComputedStyle(e).display")=="contents"
        assert await native.locator(".arPatchPort").first.get_attribute("title")=="Input <Mic> & One"
        cell=native.locator('[data-patch-row="0"][data-patch-port="2"].arPatchCell')
        assert await cell.get_attribute("data-patch-state")=="off"
        await cell.click()
        assert await cell.get_attribute("data-patch-state")=="draft"
        assert await cell.inner_text()=="M"
        assert await native.evaluate(
            "()=>window.__arMixSent.filter(x=>x.payload?.action==='routingDryApply').length")==0
        await native.locator('[data-patch-mode="1"]').select_option("STEREO")
        await native.locator('[data-patch-row="1"][data-patch-port="2"].arPatchCell').click()
        await native.locator('[data-patch-row="1"][data-patch-side="right"]').click()
        await native.locator('[data-patch-row="1"][data-patch-port="2"].arPatchCell').click()
        assert "cannot use the same" in await native.locator("#arRouteFeedback").inner_text()
        await native.locator('[data-patch-row="1"][data-patch-port="5"].arPatchCell').click()
        assert await native.locator('[data-patch-row="1"][data-patch-port="5"].arPatchCell').inner_text()=="R"
        await native.locator("#arRouteApply").click()
        action=await native.evaluate("""()=>{
          const actions=window.__arMixSent.filter(x=>x.payload?.action==='routingDryApply');
          return actions.at(-1)?.payload?.value;
        }""")
        assert action["deviceEpoch"]==77 and action["revision"]==0,action
        assert action["channels"][0]["left"]==2 and action["channels"][0]["mode"]=="MONO",action
        assert action["channels"][1]["left"]==2 and action["channels"][1]["right"]==5,action
        assert await native.locator("#arRouteApply").is_disabled()
        await native.evaluate("""()=>{
          const d=window.__arPatchStatus.dryRoutingLab;
          d.active=true;d.ready=true;d.revision=1;
          d.acceptedRoute={deviceEpoch:77,revision:0,
            channels:window.__arMixSent.filter(x=>x.payload?.action==='routingDryApply').at(-1).payload.value.channels,
            auxes:[],sends:[]};
          window.__arMixMockEvents.arMixRoutingLabAck({ok:true,state:d});
          window.__arMixMockEvents.arMixStatus(window.__arPatchStatus);
        }""")
        assert await cell.get_attribute("data-patch-state")=="live"
        assert await native.locator("#arRouteApply").is_enabled()
        await native.locator('[data-patch-row="0"][data-patch-port="5"].arPatchCell').click()
        assert await native.locator('[data-patch-row="0"][data-patch-port="5"].arPatchCell').get_attribute("data-patch-state")=="draft"
        await native.locator("#arPatchRevert").click()
        assert await cell.get_attribute("data-patch-state")=="live"
        await native.locator('[data-patch-tab="output"]').click()
        assert "READ-ONLY" in await native.locator("#arPatchOutputPanel").inner_text()
        assert await native.locator(".arPatchOutput").count()==2
        await native.locator('[data-patch-tab="input"]').click()
        await native.evaluate("""()=>{
          const d=window.__arPatchStatus.dryRoutingLab;
          d.epoch=78;d.revision=1;d.ready=false;d.active=false;
          d.ports=[{port:9,callback:0,label:"Other interface"}];
          d.acceptedRoute=null;
          window.__arMixMockEvents.arMixStatus(window.__arPatchStatus);
        }""")
        assert await native.locator('[data-patch-row][data-patch-port]').count()==10
        assert await native.locator('[data-patch-row="0"][data-patch-port="9"]').get_attribute("data-patch-state")=="off"
        assert "DEVICE CHANGED" in await native.locator("#arRouteFeedback").inner_text()

        assert not native_errors,native_errors
        await native.close()
        await browser.close()
    print("PASS: 10-channel premium mixer, rose MASTER, responsive layout, double-click rename, no JS errors")

if __name__=="__main__":asyncio.run(main())
